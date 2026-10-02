/*
 * 纯白铃 - QQ 机器人管理平台
 * 进程内音频解码层实现（不启动 ffmpeg）
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "audiodecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QStringList>
#include <QtEndian>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// 第三方解码库
//   · minimp4   : MP4/MOV/M4A 解封装（CC0-1.0）
//   · faad2     : AAC 解码（GPL-2.0-or-later，Nero AG）
//   · stb_vorbis: OGG Vorbis 解码（MIT / 公共领域）
//   · dr_mp3 / dr_flac / dr_wav : MP3 / FLAC / WAV 解码（MIT-0 / 公共领域）
// 实现统一在 libs/decoders/decoder_impl.c 与各自 .c 里生成，这里只取声明。
// ---------------------------------------------------------------------------
#include "minimp4.h"
#include "neaacdec.h"
// STB_VORBIS_HEADER_ONLY：只取声明，实现由 CMake 单独编译的 stb_vorbis.c 提供，
// 不这么写就会在两个目标文件里各生成一份实现 → LNK2005 重复定义
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"

namespace {

// --------------------------------------------------------------------------
// 小工具
// --------------------------------------------------------------------------
void setErr(QString *err, const QString &msg)
{
    if (err) *err = msg;
}

#ifdef _WIN32
qint64 qaFseek(FILE *f, qint64 off, int whence = SEEK_SET)
{
    return _fseeki64(f, off, whence);
}
qint64 qaFtell(FILE *f)
{
    return _ftelli64(f);
}
#else
qint64 qaFseek(FILE *f, qint64 off, int whence = SEEK_SET)
{
    return fseeko(f, off_t(off), whence);
}
qint64 qaFtell(FILE *f)
{
    return qint64(ftello(f));
}
#endif

enum class Fmt {
    None,
    Wav,
    Mp3,
    Flac,
    OggVorbis,
    OggOpus,   // 已经是 Opus：本层不处理（要保留就整文件直传，这里交回上层兜底）
    Mp4,
    Adts
};

Fmt fromExtension(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "wav" || ext == "wave" || ext == "bwf")          return Fmt::Wav;
    if (ext == "mp3" || ext == "mp2" || ext == "mp1" || ext == "mpga") return Fmt::Mp3;
    if (ext == "flac")                                          return Fmt::Flac;
    if (ext == "ogg" || ext == "oga")                           return Fmt::OggVorbis;
    if (ext == "opus")                                          return Fmt::OggOpus;
    if (ext == "mp4" || ext == "m4a" || ext == "m4v" || ext == "mov" ||
        ext == "3gp" || ext == "3g2" || ext == "f4v" || ext == "m4b") return Fmt::Mp4;
    if (ext == "aac" || ext == "adts")                          return Fmt::Adts;
    return Fmt::None;
}

// 按文件头魔数嗅探格式；魔数不认识时退回扩展名
Fmt sniffFormat(const QString &path, QString *err = nullptr)
{
    Fmt byExt = fromExtension(path);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setErr(err, QStringLiteral("无法打开文件"));
        return Fmt::None;
    }
    // 读 4KB：Ogg 首页里的 OpusHead/vorbis 标识都在开头
    const QByteArray head = f.read(4096);
    f.close();
    const int n = int(head.size());
    if (n < 4) {
        setErr(err, QStringLiteral("文件太小"));
        return Fmt::None;
    }
    const unsigned char *d = reinterpret_cast<const unsigned char *>(head.constData());
    auto at = [&](int off, const char *sig) {
        const int len = int(std::strlen(sig));
        return n >= off + len && std::memcmp(d + off, sig, size_t(len)) == 0;
    };

    if (at(0, "RIFF") && n >= 12 && at(8, "WAVE"))      return Fmt::Wav;
    if (at(0, "fLaC"))                                  return Fmt::Flac;
    if (at(0, "OggS")) {
        return head.indexOf("OpusHead") >= 0 ? Fmt::OggOpus : Fmt::OggVorbis;
    }
    if (n >= 8 && at(4, "ftyp"))                        return Fmt::Mp4;
    if (at(0, "ID3"))                                   return Fmt::Mp3;
    if (d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) {
        // 同步字 0xFFEx：layer 位（bit1-2）为 00 是 ADTS，为 01 是 MPEG 音频（MP3）
        if (((d[1] >> 1) & 0x03) == 0)                  return Fmt::Adts;
        return Fmt::Mp3;
    }
    return byExt;
}

// 解码输出统一按 4096 帧一块回调
constexpr int kChunkFrames = 4096;

// --------------------------------------------------------------------------
// WAV（dr_wav：PCM / ADPCM / G.711 / float 都覆盖）
// --------------------------------------------------------------------------
bool decodeWav(const QString &path, const PcmSink &sink, int *rate, int *ch, QString *err)
{
    drwav wav;
    if (!drwav_init_file(&wav, QFile::encodeName(path).constData(), nullptr)) {
        setErr(err, QStringLiteral("WAV 打开失败"));
        return false;
    }
    if (rate) *rate = int(wav.sampleRate);
    if (ch)   *ch   = int(wav.channels);
    if (wav.channels == 0) {
        drwav_uninit(&wav);
        setErr(err, QStringLiteral("WAV 声道数为 0"));
        return false;
    }

    std::vector<float> buf(size_t(kChunkFrames) * wav.channels);
    bool ok = true;
    for (;;) {
        drwav_uint64 got = drwav_read_pcm_frames_f32(&wav, kChunkFrames, buf.data());
        if (got == 0) break;
        if (!sink(buf.data(), int(got), int(wav.channels))) { ok = false; break; }
    }
    drwav_uninit(&wav);
    return ok;
}

// --------------------------------------------------------------------------
// MP3（dr_mp3）
// --------------------------------------------------------------------------
bool decodeMp3(const QString &path, const PcmSink &sink, int *rate, int *ch, QString *err)
{
    drmp3 mp3;
    if (!drmp3_init_file(&mp3, QFile::encodeName(path).constData(), nullptr)) {
        setErr(err, QStringLiteral("MP3 打开失败"));
        return false;
    }
    if (rate) *rate = int(mp3.sampleRate);
    if (ch)   *ch   = int(mp3.channels);
    if (mp3.channels == 0) {
        drmp3_uninit(&mp3);
        setErr(err, QStringLiteral("MP3 声道数为 0"));
        return false;
    }

    std::vector<float> buf(size_t(kChunkFrames) * mp3.channels);
    bool ok = true;
    for (;;) {
        drmp3_uint64 got = drmp3_read_pcm_frames_f32(&mp3, kChunkFrames, buf.data());
        if (got == 0) break;
        if (!sink(buf.data(), int(got), int(mp3.channels))) { ok = false; break; }
    }
    drmp3_uninit(&mp3);
    return ok;
}

// --------------------------------------------------------------------------
// FLAC（dr_flac）
// --------------------------------------------------------------------------
bool decodeFlac(const QString &path, const PcmSink &sink, int *rate, int *ch, QString *err)
{
    drflac *flac = drflac_open_file(QFile::encodeName(path).constData(), nullptr);
    if (!flac) {
        setErr(err, QStringLiteral("FLAC 打开失败"));
        return false;
    }
    if (rate) *rate = int(flac->sampleRate);
    if (ch)   *ch   = int(flac->channels);
    if (flac->channels == 0) {
        drflac_close(flac);
        setErr(err, QStringLiteral("FLAC 声道数为 0"));
        return false;
    }

    std::vector<float> buf(size_t(kChunkFrames) * flac->channels);
    bool ok = true;
    for (;;) {
        drflac_uint64 got = drflac_read_pcm_frames_f32(flac, kChunkFrames, buf.data());
        if (got == 0) break;
        if (!sink(buf.data(), int(got), int(flac->channels))) { ok = false; break; }
    }
    drflac_close(flac);
    return ok;
}

// --------------------------------------------------------------------------
// OGG Vorbis（stb_vorbis；不支持 Opus / Ogg-FLAC，由上层兜底）
// --------------------------------------------------------------------------
bool decodeOggVorbis(const QString &path, const PcmSink &sink, int *rate, int *ch, QString *err)
{
    int vErr = 0;
    stb_vorbis *v = stb_vorbis_open_filename(QFile::encodeName(path).constData(), &vErr, nullptr);
    if (!v) {
        setErr(err, QStringLiteral("OGG Vorbis 打开失败(错误码 %1)").arg(vErr));
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    if (rate) *rate = int(info.sample_rate);
    if (ch)   *ch   = int(info.channels);
    if (info.channels == 0) {
        stb_vorbis_close(v);
        setErr(err, QStringLiteral("OGG 声道数为 0"));
        return false;
    }

    std::vector<float> buf(size_t(kChunkFrames) * info.channels);
    bool ok = true;
    for (;;) {
        const int frames = stb_vorbis_get_samples_float_interleaved(
                    v, info.channels, buf.data(), int(buf.size()));
        if (frames <= 0) break;
        if (!sink(buf.data(), frames, int(info.channels))) { ok = false; break; }
    }
    stb_vorbis_close(v);
    return ok;
}

// --------------------------------------------------------------------------
// MP4 / MOV / M4A 里的 AAC 音轨（minimp4 解封装 + faad2 解码）
// --------------------------------------------------------------------------
// minimp4 的读回调约定很反直觉：返回 **0 表示成功**，非 0 表示失败
// （见 minimp4.h 的 minimp4_fgets：`if (read_callback(...)) return -1;`）
int mp4ReadCb(int64_t offset, void *buffer, size_t size, void *token)
{
    FILE *f = static_cast<FILE *>(token);
    if (qaFseek(f, offset) != 0) return 1;
    return std::fread(buffer, 1, size, f) == size ? 0 : 1;
}

bool decodeMp4Aac(const QString &path, const PcmSink &sink, int *rate, int *ch, QString *err)
{
    // 第三方库都只吃 const char* 文件名，Windows 下 fopen 认的是「本地代码页」而不是 UTF-8，
    // 所以统一走 QFile::encodeName（Windows 转本地 8bit，Linux 转 UTF-8），中文路径才不会挂
    const QByteArray u8 = QFile::encodeName(path);
    FILE *f = std::fopen(u8.constData(), "rb");
    if (!f) {
        setErr(err, QStringLiteral("MP4 打开失败"));
        return false;
    }
    if (qaFseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        setErr(err, QStringLiteral("MP4 定位失败"));
        return false;
    }
    const qint64 fileSize = qaFtell(f);
    if (qaFseek(f, 0, SEEK_SET) != 0 || fileSize <= 0) {
        std::fclose(f);
        setErr(err, QStringLiteral("MP4 定位失败"));
        return false;
    }

    MP4D_demux_t mp4;
    std::memset(&mp4, 0, sizeof(mp4));
    if (!MP4D_open(&mp4, mp4ReadCb, f, fileSize)) {
        std::fclose(f);
        setErr(err, QStringLiteral("MP4 解析失败"));
        return false;
    }

    // 找第一条「AAC 音轨」（视频轨自动跳过：只认 handler_type == 'soun'）
    int track = -1;
    for (unsigned i = 0; i < mp4.track_count; ++i) {
        const MP4D_track_t &t = mp4.track[i];
        if (t.handler_type != MP4D_HANDLER_TYPE_SOUN) continue;
        if (t.object_type_indication != MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3) continue;
        if (!t.dsi || t.dsi_bytes == 0) continue;
        track = int(i);
        break;
    }
    if (track < 0) {
        MP4D_close(&mp4);
        std::fclose(f);
        setErr(err, QStringLiteral("MP4 里没有可解码的 AAC 音轨"));
        return false;
    }
    const MP4D_track_t &tr = mp4.track[track];

    NeAACDecHandle dec = NeAACDecOpen();
    if (!dec) {
        MP4D_close(&mp4);
        std::fclose(f);
        setErr(err, QStringLiteral("AAC 解码器创建失败"));
        return false;
    }
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(dec);
    if (cfg) {
        cfg->outputFormat = FAAD_FMT_16BIT;   // 16bit 最稳（float 输出在部分 SBR 流上有坑），后面自己转 float
        cfg->downMatrix   = 1;                // 多声道先降成双声道
        NeAACDecSetConfiguration(dec, cfg);
    }
    unsigned long srcRate = 0;
    unsigned char srcCh = 0;
    if (NeAACDecInit2(dec, tr.dsi, tr.dsi_bytes, &srcRate, &srcCh) != 0) {
        NeAACDecClose(dec);
        MP4D_close(&mp4);
        std::fclose(f);
        setErr(err, QStringLiteral("AAC 初始化失败(AudioSpecificConfig 不合法)"));
        return false;
    }
    if (rate) *rate = int(srcRate);
    if (ch)   *ch   = int(srcCh);

    std::vector<unsigned char> pkt;
    std::vector<float> out;
    bool ok = true;
    for (unsigned s = 0; s < tr.sample_count && ok; ++s) {
        unsigned bytes = 0, ts = 0, dur = 0;
        const MP4D_file_offset_t off = MP4D_frame_offset(&mp4, unsigned(track), s, &bytes, &ts, &dur);
        if (bytes == 0 || off == 0) continue;          // 空样本/坏索引：跳过这一帧
        if (size_t(bytes) > pkt.size()) pkt.resize(bytes);
        if (mp4ReadCb(int64_t(off), pkt.data(), bytes, f) != 0) continue;

        NeAACDecFrameInfo fi;
        std::memset(&fi, 0, sizeof(fi));
        void *pcm = NeAACDecDecode(dec, &fi, pkt.data(), bytes);
        if (fi.error || !pcm || fi.samples == 0 || fi.channels == 0) continue;   // 单帧坏 → 跳过

        const int frames = int(fi.samples / fi.channels);
        if (frames <= 0) continue;
        const short *sp = static_cast<const short *>(pcm);
        const size_t total = size_t(frames) * fi.channels;
        if (out.size() < total) out.resize(total);
        for (size_t i = 0; i < total; ++i) out[i] = float(sp[i]) / 32768.0f;
        if (!sink(out.data(), frames, int(fi.channels))) ok = false;
        // SBR/HE-AAC 会让实际输出采样率与初始值不同，随时校正
        if (fi.samplerate) { if (rate) *rate = int(fi.samplerate); }
    }

    NeAACDecClose(dec);
    MP4D_close(&mp4);
    std::fclose(f);
    if (ok && rate && *rate == 0) {
        setErr(err, QStringLiteral("MP4 音轨没有任何可解码帧"));
        return false;
    }
    return ok;
}

// --------------------------------------------------------------------------
// 裸 ADTS AAC（faad2；先把 ADTS 头解析成 AudioSpecificConfig，再逐帧喂裸负载）
// --------------------------------------------------------------------------
bool decodeAdtsAac(const QString &path, const PcmSink &sink, int *rate, int *ch, QString *err)
{
    static const int kSampleRates[16] = {96000, 88200, 64000, 48000, 44100, 32000,
                                         24000, 22050, 16000, 12000, 11025, 8000,
                                         7350, 0, 0, 0};

    // 第三方库都只吃 const char* 文件名，Windows 下 fopen 认的是「本地代码页」而不是 UTF-8，
    // 所以统一走 QFile::encodeName（Windows 转本地 8bit，Linux 转 UTF-8），中文路径才不会挂
    const QByteArray u8 = QFile::encodeName(path);
    FILE *f = std::fopen(u8.constData(), "rb");
    if (!f) {
        setErr(err, QStringLiteral("AAC 打开失败"));
        return false;
    }
    std::vector<unsigned char> all;
    {
        unsigned char tmp[65536];
        size_t got;
        while ((got = std::fread(tmp, 1, sizeof(tmp), f)) > 0)
            all.insert(all.end(), tmp, tmp + got);
    }
    std::fclose(f);

    // 跳过可能的 ID3v2
    size_t pos = 0;
    if (all.size() > 10 && std::memcmp(all.data(), "ID3", 3) == 0) {
        size_t sz = (size_t(all[6] & 0x7F) << 21) | (size_t(all[7] & 0x7F) << 14) |
                    (size_t(all[8] & 0x7F) << 7) | size_t(all[9] & 0x7F);
        pos = 10 + sz;
    }

    // 找第一个 ADTS 帧头（同步字 FFF + layer==00）
    size_t first = size_t(-1);
    for (size_t i = pos; i + 7 <= all.size(); ++i) {
        if (all[i] == 0xFF && (all[i + 1] & 0xF6) == 0xF0) { first = i; break; }
    }
    if (first == size_t(-1)) {
        setErr(err, QStringLiteral("找不到 ADTS 帧头"));
        return false;
    }

    // 由 ADTS 头拼出 AudioSpecificConfig（2 字节，无显式 SBR 信令）
    const unsigned char b2 = all[first + 2];
    const int profile = (b2 >> 6) & 0x03;      // MPEG-4 Audio Object Type - 1
    const int sfIndex = (b2 >> 2) & 0x0F;
    const int chanCfg = ((b2 & 0x01) << 2) | ((all[first + 3] >> 6) & 0x03);
    if (kSampleRates[sfIndex] == 0) {
        setErr(err, QStringLiteral("ADTS 采样率非法"));
        return false;
    }
    unsigned char asc[2];
    asc[0] = (unsigned char)(((profile + 1) << 3) | (sfIndex >> 1));
    asc[1] = (unsigned char)(((sfIndex & 0x01) << 7) | (chanCfg << 3));

    NeAACDecHandle dec = NeAACDecOpen();
    if (!dec) {
        setErr(err, QStringLiteral("AAC 解码器创建失败"));
        return false;
    }
    NeAACDecConfigurationPtr cfg = NeAACDecGetCurrentConfiguration(dec);
    if (cfg) {
        cfg->outputFormat = FAAD_FMT_16BIT;
        cfg->downMatrix   = 1;
        NeAACDecSetConfiguration(dec, cfg);
    }
    unsigned long srcRate = 0;
    unsigned char srcCh = 0;
    if (NeAACDecInit2(dec, asc, 2, &srcRate, &srcCh) != 0) {
        NeAACDecClose(dec);
        setErr(err, QStringLiteral("AAC 初始化失败"));
        return false;
    }
    if (rate) *rate = int(srcRate);
    if (ch)   *ch   = int(srcCh);

    std::vector<float> out;
    bool ok = true;
    size_t p = first;
    while (ok && p + 7 <= all.size()) {
        if (all[p] != 0xFF || (all[p + 1] & 0xF6) != 0xF0) {   // 失去同步：重新找帧头
            size_t q = p + 1;
            while (q + 7 <= all.size() && !(all[q] == 0xFF && (all[q + 1] & 0xF6) == 0xF0)) ++q;
            if (q + 7 > all.size()) break;
            p = q;
        }
        const int protAbsent = all[p + 1] & 0x01;
        const int hdrLen = protAbsent ? 7 : 9;
        const int frameLen = ((all[p + 3] & 0x03) << 11) | (all[p + 4] << 3) | (all[p + 5] >> 5);
        if (frameLen < hdrLen || p + size_t(frameLen) > all.size()) break;   // 尾巴不完整：正常结束

        NeAACDecFrameInfo fi;
        std::memset(&fi, 0, sizeof(fi));
        void *pcm = NeAACDecDecode(dec, &fi, all.data() + p + hdrLen, unsigned long(frameLen - hdrLen));
        if (fi.error || !pcm || fi.samples == 0 || fi.channels == 0) {
            p += size_t(frameLen);
            continue;
        }
        const int frames = int(fi.samples / fi.channels);
        const short *sp = static_cast<const short *>(pcm);
        const size_t total = size_t(frames) * fi.channels;
        if (frames > 0) {
            if (out.size() < total) out.resize(total);
            for (size_t i = 0; i < total; ++i) out[i] = float(sp[i]) / 32768.0f;
            if (!sink(out.data(), frames, int(fi.channels))) ok = false;
        }
        p += size_t(frameLen);
    }

    NeAACDecClose(dec);
    return ok;
}

// --------------------------------------------------------------------------
// 时长探测（只读元数据，不解码）
//   「打开 → 读几个字段 → 关闭」，任何一步拿不到就返回 false，交给调用方兜底。
// --------------------------------------------------------------------------

// WAV：总帧数 / 采样率（dr_wav 打开时已从 fmt+data 算好）
bool probeWavMs(const char *path, qint64 *outMs)
{
    drwav wav;
    if (!drwav_init_file(&wav, path, nullptr)) return false;
    const drwav_uint64 frames = wav.totalPCMFrameCount;
    const drwav_uint32 rate   = wav.sampleRate;
    drwav_uninit(&wav);
    if (rate == 0 || frames == 0) return false;
    *outMs = qint64(double(frames) * 1000.0 / double(rate));
    return true;
}

// MP3：帧数 / 采样率。CBR 时 dr_mp3 从文件长度直接算；VBR 要扫一遍帧头（只读头，不解码）
bool probeMp3Ms(const char *path, qint64 *outMs)
{
    drmp3 mp3;
    if (!drmp3_init_file(&mp3, path, nullptr)) return false;
    drmp3_uint64 frames = mp3.totalPCMFrameCount;
    if (frames == 0 || frames == DRMP3_UINT64_MAX)
        frames = drmp3_get_pcm_frame_count(&mp3);       // VBR：逐帧扫头
    const drmp3_uint32 rate = mp3.sampleRate;
    drmp3_uninit(&mp3);
    if (rate == 0 || frames == 0 || frames == DRMP3_UINT64_MAX) return false;
    *outMs = qint64(double(frames) * 1000.0 / double(rate));
    return true;
}

// FLAC：STREAMINFO 里的总样本数 / 采样率（dr_flac 打开时就读了，不解码）
bool probeFlacMs(const char *path, qint64 *outMs)
{
    drflac *flac = drflac_open_file(path, nullptr);
    if (!flac) return false;
    const drflac_uint64 frames = flac->totalPCMFrameCount;
    const drflac_uint32 rate   = flac->sampleRate;
    drflac_close(flac);
    if (rate == 0 || frames == 0) return false;
    *outMs = qint64(double(frames) * 1000.0 / double(rate));
    return true;
}

// Ogg（Vorbis / Opus）：读文件**最后一个页**的 granulepos。
// 只 seek 到尾部读 ≤64KB，不解码、不逐帧扫 —— 几百 MB 的 Ogg 也是微秒级。
//   Opus  : 时长 = (granulepos - preSkip) / 48000   （preSkip 在首页 body 的 OpusHead 里）
//   Vorbis: 时长 = granulepos / 采样率              （采样率在首页 body 的 \x01vorbis 里）
bool probeOggMs(const char *path, qint64 *outMs)
{
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;

    bool ok = false;
    do {
        if (qaFseek(f, 0, SEEK_END) != 0) break;
        const qint64 fileSize = qaFtell(f);
        if (fileSize < 27) break;

        const qint64 scan = fileSize < 65536 ? fileSize : 65536;
        // ⚠ 别写 `std::vector<unsigned char> tail(size_t(scan));` —— 那是 most vexing parse：
        //    「size_t(scan)」会被当成「形参声明：类型 size_t、名字 scan」，
        //    整个语句于是变成「声明函数 tail」，之后 tail.data() / tail[i] 全报 C2109/C2228。
        const size_t tailBytes = static_cast<size_t>(scan);
        std::vector<unsigned char> tail(tailBytes);
        if (qaFseek(f, fileSize - scan) != 0) break;
        if (std::fread(tail.data(), 1, size_t(scan), f) != size_t(scan)) break;

        // 从尾部往前找「最后一个完整页」：要求页长恰好走到文件尾，
        // 页体里偶然出现的伪 "OggS" 因此被排除掉。
        const qint64 bufStart = fileSize - scan;
        qint64 granule = -1;
        for (qint64 i = scan - 27; i >= 0; --i) {
            if (tail[size_t(i)]     != 'O' || tail[size_t(i) + 1] != 'g' ||
                tail[size_t(i) + 2] != 'g' || tail[size_t(i) + 3] != 'S') continue;
            const int nseg = int(tail[size_t(i) + 26]);
            if (i + 27 + nseg > scan) continue;
            qint64 body = 0;
            for (int s = 0; s < nseg; ++s) body += tail[size_t(i) + 27 + s];
            if (bufStart + i + 27 + nseg + body != fileSize) continue;
            granule = qint64(qFromLittleEndian<quint64>(tail.data() + i + 6));
            break;
        }
        if (granule < 0) break;

        // 从头取 preSkip(Opus) 或 采样率(Vorbis)
        unsigned char hdr[256] = {0};
        if (qaFseek(f, 0, SEEK_SET) != 0) break;
        const size_t hn = std::fread(hdr, 1, sizeof(hdr), f);

        qint64 ms = -1;
        for (size_t i = 0; i + 8 <= hn; ++i) {
            if (i + 19 <= hn && std::memcmp(hdr + i, "OpusHead", 8) == 0) {
                const unsigned preSkip = unsigned(hdr[i + 10]) | (unsigned(hdr[i + 11]) << 8);
                const qint64 real = granule > qint64(preSkip) ? granule - qint64(preSkip) : granule;
                ms = real * 1000 / 48000;
                break;
            }
            if (i + 16 <= hn && std::memcmp(hdr + i, "\x01vorbis", 7) == 0) {
                const quint32 rate = qFromLittleEndian<quint32>(hdr + i + 12);
                if (rate > 0) ms = granule * 1000 / qint64(rate);
                break;
            }
        }
        if (ms < 0) break;
        *outMs = ms;
        ok = true;
    } while (false);

    std::fclose(f);
    return ok;
}

// MP4 / M4A / MOV：取音轨的 duration / timescale（minimp4 解析 moov 时已经填好）。
// 优先用音轨而不是 movie —— 视频容器的 movie 时长含视频轨，会比音轨长。
bool probeMp4Ms(const char *path, qint64 *outMs)
{
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;

    bool ok = false;
    do {
        if (qaFseek(f, 0, SEEK_END) != 0) break;
        const qint64 fileSize = qaFtell(f);
        if (fileSize <= 0) break;
        if (qaFseek(f, 0, SEEK_SET) != 0) break;

        MP4D_demux_t mp4;
        std::memset(&mp4, 0, sizeof(mp4));
        if (!MP4D_open(&mp4, mp4ReadCb, f, fileSize)) break;

        for (unsigned i = 0; i < mp4.track_count && !ok; ++i) {
            const MP4D_track_t &t = mp4.track[i];
            if (t.handler_type != MP4D_HANDLER_TYPE_SOUN) continue;
            if (t.timescale == 0) continue;
            const quint64 dur = (quint64(t.duration_hi) << 32) | quint64(t.duration_lo);
            if (dur == 0) continue;
            *outMs = qint64(double(dur) * 1000.0 / double(t.timescale));
            ok = true;
        }
        if (!ok && mp4.timescale != 0) {          // 退一步用 movie 级时长
            const quint64 dur = (quint64(mp4.duration_hi) << 32) | quint64(mp4.duration_lo);
            if (dur > 0) { *outMs = qint64(double(dur) * 1000.0 / double(mp4.timescale)); ok = true; }
        }
        MP4D_close(&mp4);
    } while (false);

    std::fclose(f);
    return ok;
}

// 裸 ADTS AAC：没有时长字段，只能逐帧跳（帧长写在头里），每帧 1024 样本。
// 文件太大就不扫了（回退 ffmpeg）—— 裸流基本不会大到那去。
bool probeAdtsMs(const char *path, qint64 *outMs)
{
    static const quint32 kRates[16] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                        22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0 };
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;

    bool ok = false;
    do {
        if (qaFseek(f, 0, SEEK_END) != 0) break;
        const qint64 fileSize = qaFtell(f);
        if (fileSize < 7 || fileSize > 64LL * 1024 * 1024) break;
        if (qaFseek(f, 0, SEEK_SET) != 0) break;

        const size_t bufBytes = static_cast<size_t>(fileSize);   // 同上：别写成 buf(size_t(fileSize))
        std::vector<unsigned char> buf(bufBytes);
        if (std::fread(buf.data(), 1, size_t(fileSize), f) != size_t(fileSize)) break;

        size_t p = 0;
        if (fileSize >= 10 && std::memcmp(buf.data(), "ID3", 3) == 0) {   // 跳过 ID3v2
            const size_t tagSize = (size_t(buf[6] & 0x7F) << 21) | (size_t(buf[7] & 0x7F) << 14)
                                 | (size_t(buf[8] & 0x7F) << 7)  | size_t(buf[9] & 0x7F);
            p = 10 + tagSize;
        }

        quint32 rate = 0;
        quint64 frames = 0;
        while (p + 7 <= size_t(fileSize)) {
            // 同步字 0xFFF 且 layer==00；对不上就往前挪一个字节重新找
            if (buf[p] != 0xFF || (buf[p + 1] & 0xF6) != 0xF0) { ++p; continue; }
            const size_t frameLen = (size_t(buf[p + 3] & 0x03) << 11)
                                  | (size_t(buf[p + 4]) << 3)
                                  | (size_t(buf[p + 5]) >> 5);
            if (frameLen < 7) { ++p; continue; }
            if (rate == 0) rate = kRates[(buf[p + 2] >> 2) & 0x0F];
            ++frames;
            p += frameLen;
        }
        if (rate > 0 && frames > 0) {
            *outMs = qint64(double(frames) * 1024.0 * 1000.0 / double(rate));
            ok = true;
        }
    } while (false);

    std::fclose(f);
    return ok;
}

} // namespace

bool canDecodeAudioFile(const QString &path)
{
    if (!QFile::exists(path)) return false;
    const Fmt fmt = sniffFormat(path);
    return fmt != Fmt::None && fmt != Fmt::OggOpus;
}

bool decodeAudioFile(const QString &path, const PcmSink &sink,
                     int *srcRate, int *srcChannels, QString *error)
{
    setErr(error, QString());
    if (srcRate)     *srcRate = 0;
    if (srcChannels) *srcChannels = 0;

    if (!QFile::exists(path)) {
        setErr(error, QStringLiteral("文件不存在"));
        return false;
    }

    const Fmt fmt = sniffFormat(path, error);
    switch (fmt) {
    case Fmt::Wav:       return decodeWav(path, sink, srcRate, srcChannels, error);
    case Fmt::Mp3:       return decodeMp3(path, sink, srcRate, srcChannels, error);
    case Fmt::Flac:      return decodeFlac(path, sink, srcRate, srcChannels, error);
    case Fmt::OggVorbis: return decodeOggVorbis(path, sink, srcRate, srcChannels, error);
    case Fmt::Mp4:       return decodeMp4Aac(path, sink, srcRate, srcChannels, error);
    case Fmt::Adts:      return decodeAdtsAac(path, sink, srcRate, srcChannels, error);
    case Fmt::OggOpus:
        setErr(error, QStringLiteral("输入已经是 Ogg Opus，本解码层不处理"));
        return false;
    case Fmt::None:
    default:
        if (error && error->isEmpty())
            setErr(error, QStringLiteral("无法识别的音频/视频格式"));
        return false;
    }
}

// 这个文件是不是「视频」（容器里带视频轨）。只读元数据，不解码。详见头文件注释。
bool mediaFileHasVideoTrack(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();

    // 1) 这些后缀不可能是纯音频 —— 直接判，不碰文件内容
    static const QStringList kDefiniteVideo = {
        QStringLiteral("avi"),  QStringLiteral("mkv"),  QStringLiteral("flv"),
        QStringLiteral("webm"), QStringLiteral("ts"),   QStringLiteral("m2ts"),
        QStringLiteral("mts"),  QStringLiteral("wmv"),  QStringLiteral("asf"),
        QStringLiteral("rm"),   QStringLiteral("rmvb"), QStringLiteral("mpg"),
        QStringLiteral("mpeg"), QStringLiteral("vob"),  QStringLiteral("ogv"),
        QStringLiteral("m4v"),  QStringLiteral("f4v"),
    };
    if (kDefiniteVideo.contains(ext))
        return true;

    // 2) 混合容器：.m4a / .m4b 确定是音频；只有 .mp4 / .mov / .3gp / .3g2 得看内容
    static const QStringList kMaybeVideo = {
        QStringLiteral("mp4"), QStringLiteral("mov"),
        QStringLiteral("3gp"), QStringLiteral("3g2"),
    };
    if (!kMaybeVideo.contains(ext))
        return false;

    const QByteArray u8 = QFile::encodeName(path);   // Windows 下 fopen 认本地代码页
    FILE *f = std::fopen(u8.constData(), "rb");
    if (!f) return false;

    bool hasVideo = false;
    if (qaFseek(f, 0, SEEK_END) == 0) {
        const qint64 fileSize = qaFtell(f);
        if (fileSize > 0 && qaFseek(f, 0, SEEK_SET) == 0) {
            MP4D_demux_t mp4;
            std::memset(&mp4, 0, sizeof(mp4));
            if (MP4D_open(&mp4, mp4ReadCb, f, fileSize)) {
                for (unsigned i = 0; i < mp4.track_count; ++i) {
                    if (mp4.track[i].handler_type == MP4D_HANDLER_TYPE_VIDE) {
                        hasVideo = true;
                        break;
                    }
                }
                MP4D_close(&mp4);
            }
        }
    }
    std::fclose(f);
    return hasVideo;
}

bool probeAudioDurationMs(const QString &path, qint64 *durationMs)
{
    if (!durationMs) return false;
    *durationMs = 0;
    if (!QFile::exists(path)) return false;

    const QByteArray u8 = QFile::encodeName(path);   // Windows 下 fopen 认本地代码页，不是 UTF-8
    const char *p = u8.constData();

    switch (sniffFormat(path)) {
    case Fmt::Wav:       return probeWavMs(p, durationMs);
    case Fmt::Mp3:       return probeMp3Ms(p, durationMs);
    case Fmt::Flac:      return probeFlacMs(p, durationMs);
    case Fmt::OggVorbis:
    case Fmt::OggOpus:   return probeOggMs(p, durationMs);   // Opus 也能量：超长照样得切段
    case Fmt::Mp4:       return probeMp4Ms(p, durationMs);
    case Fmt::Adts:      return probeAdtsMs(p, durationMs);
    case Fmt::None:
    default:             return false;
    }
}
