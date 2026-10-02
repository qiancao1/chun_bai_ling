// core/opusconvert + core/audiodecoder 的自测程序（不进主工程）
// 目的：不依赖 ffmpeg，验证「任意音视频 → Ogg Opus」的正确性：
//   1) 生成参考 WAV（44.1k 立体声 5 秒）
//   2) 转 opus，逐页校验 Ogg 结构 / CRC / granulepos / EOS
//   3) 用 libopus 把转出来的文件解回来，核对样本数与 granulepos 是否自洽
//   4) 校验分片：segSec=2 时应切出 3 段，每段 ≤ 2 秒
//   5) 校验 MP4（视频取音轨）与 MP3 的真实素材
#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QtEndian>

#include <opus.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "opusconvert.h"
#include "audiodecoder.h"
#include "libavio.h"    // 主工程第②层：MP4/M4A 音轨、裸 ADTS 现在靠它（不再用 faad2）

static const double kPi = 3.14159265358979323846;
static QStringList g_log;
static int g_pass = 0;
static int g_fail = 0;

static void note(const QString &s) { g_log << s; }
static void check(bool ok, const QString &what)
{
    g_log << (ok ? QStringLiteral("[ OK ] ") : QStringLiteral("[FAIL] ")) + what;
    if (ok) ++g_pass; else ++g_fail;
}

// 主工程 core/api.cpp 那条链路的等价物：
//   ① 自研解码层（audiodecoder）→ ② libav（自编译 FFmpeg 的那几个 DLL）
// 第三层 ffmpeg.exe 不在本测试里跑。
// MP4/M4A 里的音轨、裸 ADTS 这类 AAC 容器就是靠 ② 兜住的 —— 第①层已经没有 faad2 了。
static QStringList convertInProcess(const QString &src, int segSec, QString *err)
{
    QStringList r = convertAudioToOpusSegments(src, segSec, err);              // ① 自研
    if (r.isEmpty() && libavAvailable())                                       // ② libav
        r = convertAudioToOpusSegmentsEx(src, segSec, libavDecodeAudioFile, err);
    return r;
}

// ---------------------------------------------------------------- WAV 生成
static bool writeWav(const QString &path, int rate, int channels, double seconds, double freq)
{
    const int frames = int(rate * seconds);
    QByteArray pcm;
    pcm.reserve(frames * channels * 2);
    for (int i = 0; i < frames; ++i) {
        const double t = double(i) / rate;
        const qint16 l = qint16(std::sin(2.0 * kPi * freq * t) * 12000.0);
        const qint16 r = qint16(std::sin(2.0 * kPi * freq * 1.5 * t) * 6000.0);
        const qint16 v[2] = { l, r };
        for (int c = 0; c < channels; ++c) {
            const qint16 s = v[c % 2];
            pcm.append(char(s & 0xFF));
            pcm.append(char((s >> 8) & 0xFF));
        }
    }
    char h[44];
    std::memcpy(h, "RIFF", 4);
    qToLittleEndian<quint32>(quint32(36 + pcm.size()), h + 4);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    qToLittleEndian<quint32>(16, h + 16);
    qToLittleEndian<quint16>(1, h + 20);                                  // PCM
    qToLittleEndian<quint16>(quint16(channels), h + 22);
    qToLittleEndian<quint32>(quint32(rate), h + 24);
    qToLittleEndian<quint32>(quint32(rate * channels * 2), h + 28);
    qToLittleEndian<quint16>(quint16(channels * 2), h + 32);
    qToLittleEndian<quint16>(16, h + 34);
    std::memcpy(h + 36, "data", 4);
    qToLittleEndian<quint32>(quint32(pcm.size()), h + 40);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(h, 44);
    f.write(pcm);
    return true;
}

// ---------------------------------------------------------------- Ogg 解析
static quint32 oggCrc(const unsigned char *p, int len)
{
    static quint32 tbl[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) {
            quint32 r = quint32(i) << 24;
            for (int j = 0; j < 8; ++j)
                r = (r & 0x80000000u) ? ((r << 1) ^ 0x04c11db7u) : (r << 1);
            tbl[i] = r;
        }
        built = true;
    }
    quint32 crc = 0;
    for (int i = 0; i < len; ++i) {
        // ⚠ 校验字段（22..25）必须「当 0 参与运算」，不是跳过不算：
        //   crc_reg 的更新里那 4 个 0 字节依然会推进寄存器。
        //   ogg_packer 的 ogg_page_checksum_set 就是先 page[22..25]=0 再算，
        //   写成 `continue` 会算出完全不同的值 → 每页都报 CRC 不符。
        const unsigned char b = (i >= 22 && i < 26) ? 0 : p[i];
        crc = (crc << 8) ^ tbl[((crc >> 24) & 0xFF) ^ b];
    }
    return crc;
}

struct PageInfo { quint32 seq = 0; quint64 granule = 0; int flags = 0; int payload = 0; };

// 解析整份文件，顺带把音频包喂给 libopus 解一遍
struct OpusProbe {
    QVector<PageInfo> pages;
    int preSkip = 0;
    int channels = 0;
    qint64 decodedSamples = 0;      // libopus 解出来的样本数（每声道）
    int packetCount = 0;
    int audioPacketCount = 0;
    qint64 audioPayloadBytes = 0;
    bool headOk = false;
    bool tagsOk = false;
    QString err;
};

static bool probeOpus(const QString &path, OpusProbe *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { out->err = QStringLiteral("打不开"); return false; }
    const QByteArray d = f.readAll();
    const unsigned char *base = reinterpret_cast<const unsigned char *>(d.constData());

    OpusDecoder *dec = nullptr;
    std::vector<unsigned char> cur;
    int pos = 0;
    qint64 granuleLast = 0;
    int pageno = 0;

    while (pos + 27 <= d.size()) {
        const unsigned char *p = base + pos;
        if (std::memcmp(p, "OggS", 4) != 0) {
            out->err = QStringLiteral("第 %1 页起始不是 OggS（偏移 %2）").arg(out->pages.size()).arg(pos);
            return false;
        }
        const int nseg = p[26];
        const int hdr = 27 + nseg;
        if (pos + hdr > d.size()) { out->err = QStringLiteral("页头截断"); return false; }
        int payload = 0;
        for (int i = 0; i < nseg; ++i) payload += p[27 + i];
        const int total = hdr + payload;
        if (pos + total > d.size()) { out->err = QStringLiteral("页数据截断"); return false; }

        const quint32 want = quint32(p[22]) | (quint32(p[23]) << 8) | (quint32(p[24]) << 16) | (quint32(p[25]) << 24);
        const quint32 got = oggCrc(p, total);
        if (want != got) {
            out->err = QStringLiteral("第 %1 页 CRC 不符 (want %2 got %3)").arg(pageno).arg(want).arg(got);
            return false;
        }

        PageInfo pi;
        pi.seq = quint32(p[18]) | (quint32(p[19]) << 8) | (quint32(p[20]) << 16) | (quint32(p[21]) << 24);
        quint64 g = 0;
        for (int i = 13; i >= 6; --i) g = (g << 8) | p[i];
        pi.granule = g;
        pi.flags = p[5];
        pi.payload = payload;
        out->pages << pi;
        granuleLast = g;
        ++pageno;

        int off = 0;
        for (int i = 0; i < nseg; ++i) {
            const int len = p[27 + i];
            cur.insert(cur.end(), p + hdr + off, p + hdr + off + len);
            off += len;
            if (len < 255) {
                const int idx = out->packetCount++;
                if (idx == 0) {
                    out->headOk = cur.size() >= 19 && std::memcmp(cur.data(), "OpusHead", 8) == 0;
                    if (out->headOk) {
                        out->channels = cur[9];
                        out->preSkip = cur[10] | (cur[11] << 8);
                    }
                } else if (idx == 1) {
                    out->tagsOk = cur.size() >= 8 && std::memcmp(cur.data(), "OpusTags", 8) == 0;
                    if (out->headOk) {
                        int e2 = 0;
                        dec = opus_decoder_create(48000, out->channels, &e2);
                    }
                } else if (dec && !cur.empty()) {
                    std::vector<opus_int16> pcm(5760 * (out->channels > 0 ? out->channels : 1));
                    const int n = opus_decode(dec, cur.data(), int(cur.size()),
                                              pcm.data(), 5760, 0);
                    if (n > 0) out->decodedSamples += n;
                    ++out->audioPacketCount;
                    out->audioPayloadBytes += qint64(cur.size());
                }
                cur.clear();
            }
        }
        pos += total;
    }
    if (dec) opus_decoder_destroy(dec);
    if (out->pages.isEmpty()) { out->err = QStringLiteral("没有任何 Ogg 页"); return false; }
    return true;
}

// ---------------------------------------------------------------- 工具
static QString human(qint64 bytes)
{
    return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
}

static void dumpFile(const QString &path, const QString &tag)
{
    const QFileInfo fi(path);
    OpusProbe pr;
    if (!probeOpus(path, &pr)) {
        note(QStringLiteral("  %1: 解析失败 - %2").arg(tag, pr.err));
        return;
    }
    const qint64 total = qint64(pr.pages.last().granule) - pr.preSkip;
    note(QStringLiteral("  %1: %2 字节=%3 页=%4 包=%5 audio包=%6 时长=%7s preSkip=%8 ch=%9 BOS=%10 EOS=%11")
         .arg(tag).arg(fi.fileName()).arg(human(fi.size()))
         .arg(pr.pages.size()).arg(pr.packetCount).arg(pr.audioPacketCount)
         .arg(double(total) / 48000.0, 0, 'f', 3).arg(pr.preSkip).arg(pr.channels)
         .arg(int(pr.pages.first().flags & 0x02)).arg(int(pr.pages.last().flags & 0x04)));
}

// ---------------------------------------------------------------- main
// ---------------------------------------------------------------------------
// 测速模式：qatest.exe --bench <文件> [段秒数]
//
// 把「进程内转换」完整跑一遍并打印分阶段耗时，用来跟 ffmpeg.exe 那条路对比。
// ⚠ 会先删掉该文件已存在的旧产物（<源>.opus / <源>_seg*.opus），保证是"真转"
//   而不是命中「存在即复用」那条捷径 —— 否则测出来是 0 ms。
// ⚠ 链路与本工程一致：① 自研解码层 → ② libav（就是 core/api.cpp 那两层，
//   第三层 ffmpeg.exe 不在这里跑）。
// ---------------------------------------------------------------------------
static void runBench(const QString &file, int segSec)
{
    const QFileInfo fi(file);
    std::printf("==== in-process conversion benchmark ====\n");
    if (!fi.exists()) {
        std::printf("!! file not found: %s\n", file.toLocal8Bit().constData());
        return;
    }
    if (segSec <= 0) segSec = 298;

    const double srcBytes = double(fi.size());
    std::printf("file      : %s\n", fi.fileName().toLocal8Bit().constData());
    std::printf("size      : %.2f MB\n", srcBytes / 1048576.0);
    std::printf("segSec    : %d\n", segSec);
    std::printf("layer 1   : %s\n", canDecodeAudioFile(file) ? "self-decoder accepts it" : "not accepted -> will use layer 2");
    std::printf("layer 2   : %s\n", libavAvailable() ? "libav ready" : "libav NOT built in");
    std::printf("\n");

    // 只读探时长（顺带测一下这条快路径的开销）
    qint64 durMs = 0;
    QElapsedTimer tp; tp.start();
    const bool gotDur = probeAudioDurationMs(file, &durMs);
    const qint64 probeMs = tp.elapsed();
    if (gotDur)
        std::printf("probe     : %lld ms (%.2f s)  cost %lld ms\n",
                    (long long)durMs, double(durMs) / 1000.0, (long long)probeMs);
    else
        std::printf("probe     : n/a (read-only probe failed)  cost %lld ms\n", (long long)probeMs);

    // 清掉旧产物 → 保证真转
    QFile::remove(file + QStringLiteral(".opus"));
    {
        const QDir d = fi.absoluteDir();
        const QStringList stale = d.entryList(QStringList{ fi.fileName() + QStringLiteral("_seg*.opus") },
                                              QDir::Files);
        for (const QString &n : stale) QFile::remove(d.absoluteFilePath(n));
    }

    QString err;
    OpusBenchStats bs;
    QElapsedTimer wall; wall.start();
    QStringList r = convertAudioToOpusSegments(file, segSec, &err, &bs);            // ① 自研
    const char *layer = "layer 1 (self-decoder)";
    if (r.isEmpty() && libavAvailable()) {                                          // ② libav
        layer = "layer 2 (libav)";
        r = convertAudioToOpusSegmentsEx(file, segSec, libavDecodeAudioFile, &err, &bs);
    }
    const qint64 wallMs = wall.elapsed();

    if (r.isEmpty()) {
        std::printf("RESULT    : FAILED -- %s\n", err.toLocal8Bit().constData());
        std::printf("            (this is the case where the real program falls back to ffmpeg.exe)\n");
        return;
    }
    if (bs.reused) {
        std::printf("!! reused an existing output -- numbers below are NOT a real run\n");
    }

    qint64 outBytes = 0;
    for (const QString &f : r) outBytes += QFileInfo(f).size();
    const double audioSec = double(bs.realSamples) / 48000.0;
    const qint64 useMs = wallMs > 0 ? wallMs : 1;

    std::printf("\n---- result ----\n");
    std::printf("decoder   : %s\n", layer);
    std::printf("decode    : %lld ms\n", (long long)bs.decodeMs);
    std::printf("resample  : %lld ms\n", (long long)bs.resampleMs);
    std::printf("encode    : %lld ms\n", (long long)bs.encodeMs);
    std::printf("total     : %lld ms  (sum of stages)\n", (long long)bs.totalMs);
    std::printf("wall      : %lld ms  (probe + convert)\n", (long long)wallMs);
    if (audioSec > 0.0)
        std::printf("audio     : %.2f s  ->  %.1fx realtime\n", audioSec, audioSec * 1000.0 / double(useMs));
    std::printf("segments  : %d\n", bs.segments);
    std::printf("output    : %.1f KB", double(outBytes) / 1024.0);
    if (outBytes > 0)
        std::printf("   (source is %.1fx larger)\n", srcBytes / double(outBytes));
    else
        std::printf("\n");
    for (const QString &f : r)
        std::printf("            %s  (%.1f KB)\n",
                    QFileInfo(f).fileName().toLocal8Bit().constData(),
                    double(QFileInfo(f).size()) / 1024.0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- 测速模式：不进自测流程，跑完直接返回 ----
    {
        const QStringList args = QCoreApplication::arguments();
        const int bi = args.indexOf(QStringLiteral("--bench"));
        if (bi >= 0) {
            if (bi + 1 >= args.size()) {
                std::printf("usage: qatest.exe --bench <file> [segSec]\n");
                return 2;
            }
            const int sec = (bi + 2 < args.size()) ? args.at(bi + 2).toInt() : 298;
            runBench(args.at(bi + 1), sec);
            return 0;
        }
    }
    const QString tmp = QCoreApplication::applicationDirPath() + QStringLiteral("/work");
    QDir().mkpath(tmp);

    const QString wav    = tmp + QStringLiteral("/参考音频.wav");     // 故意带中文，顺便验证路径编码
    const QString wavOpus = wav + QStringLiteral(".opus");
    const QString mp4    = tmp + QStringLiteral("/视频样本.mp4");
    const QString mp3    = tmp + QStringLiteral("/音频样本.mp3");
    QFile::remove(wavOpus);
    for (int i = 0; i < 8; ++i) QFile::remove(tmp + QStringLiteral("/参考音频.wav_seg%1.opus").arg(i, 3, 10, QLatin1Char('0')));

    // 真实素材（可选）：优先用环境变量 QATEST_MEDIA_DIR 指向的目录，否则试几个常见位置。
    // 都找不到就跳过 T4/T5，不影响其它用例 —— 免得每次清 build 都要手工往里丢文件。
    {
        const QString envDir = qEnvironmentVariable("QATEST_MEDIA_DIR");
        const QStringList dirs = envDir.isEmpty()
            ? QStringList{ QStringLiteral("C:/Users/Airuan/Documents/菲菲/ffmpeg") }
            : QStringList{ envDir };
        const QPair<QString, QString> seeds[2] = {
            { QStringLiteral("input2.mp4"), mp4 },
            { QStringLiteral("audio.mp3"),  mp3 }
        };
        for (int i = 0; i < 2; ++i) {
            if (QFile::exists(seeds[i].second)) continue;
            for (const QString &d : dirs) {
                const QString from = d + QLatin1Char('/') + seeds[i].first;
                if (QFile::exists(from) && QFile::copy(from, seeds[i].second)) break;
            }
        }
    }

    note(QStringLiteral("==== 进程内 Opus 转换自测 ===="));
    note(QStringLiteral("工作目录: %1").arg(tmp));

    // ---------- T1: WAV（44.1k 立体声 5 秒）→ 单文件 ----------
    note(QStringLiteral("---- T1 生成 WAV 并转单文件 .opus ----"));
    check(writeWav(wav, 44100, 2, 5.0, 440.0), QStringLiteral("写出 44.1k/立体声/5s 的参考 WAV"));
    note(QStringLiteral("  WAV 大小 = %1").arg(human(QFileInfo(wav).size())));

    QString err;
    // ---------- T0: 直接探解码层 ----------
    {
        note(QStringLiteral("---- T0 直接调用解码层 ----"));
        note(QStringLiteral("  canDecodeAudioFile(wav) = %1").arg(canDecodeAudioFile(wav) ? 1 : 0));
        int r = 0, c = 0;
        QString de;
        qint64 blocks = 0, samples = 0;
        const bool ok = decodeAudioFile(wav, [&](const float *s, int frames, int ch) -> bool {
            ++blocks;
            samples += frames;
            if (blocks == 1) {
                float mn = s[0], mx = s[0];
                for (int i = 0; i < frames * ch; ++i) { if (s[i] < mn) mn = s[i]; if (s[i] > mx) mx = s[i]; }
                note(QStringLiteral("  首块: frames=%1 ch=%2 值域=[%3, %4]")
                     .arg(frames).arg(ch).arg(double(mn), 0, 'f', 4).arg(double(mx), 0, 'f', 4));
            }
            return true;
        }, &r, &c, &de);
        note(QStringLiteral("  decodeAudioFile = %1  块数=%2 帧数=%3 采样率=%4 声道=%5 err=[%6]")
             .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
             .arg(blocks).arg(samples).arg(r).arg(c).arg(de));
        check(ok && blocks > 0 && samples == 220500 && r == 44100 && c == 2,
              QStringLiteral("WAV 解码层回调正常"));
    }

    QElapsedTimer tmr;
    tmr.start();
    const QStringList one = convertAudioToOpusSegments(wav, 298, &err);
    const qint64 msOne = tmr.elapsed();
    check(one.size() == 1 && one.first() == wavOpus,
          QStringLiteral("返回单文件 %1（实际 %2，err=%3）").arg(wavOpus, one.join(','), err));
    if (one.size() == 1) {
        OpusProbe pr;
        check(probeOpus(one.first(), &pr), QStringLiteral("逐页解析 + CRC 校验全部通过（%1）").arg(pr.err));
        check(pr.headOk && pr.tagsOk, QStringLiteral("首页是 OpusHead、次页是 OpusTags"));
        check((pr.pages.first().flags & 0x02) != 0, QStringLiteral("第一页带 BOS 标志"));
        check((pr.pages.last().flags & 0x04) != 0, QStringLiteral("最后一页带 EOS 标志"));
        bool seqOk = true;
        for (int i = 0; i < pr.pages.size(); ++i)
            if (pr.pages[i].seq != quint32(i)) seqOk = false;
        check(seqOk, QStringLiteral("页序号从 0 连续递增（%1 页）").arg(pr.pages.size()));
        bool granOk = true;
        for (int i = 1; i < pr.pages.size(); ++i)
            if (pr.pages[i].granule < pr.pages[i - 1].granule) granOk = false;
        check(granOk, QStringLiteral("granulepos 单调不减"));
        const qint64 dur = qint64(pr.pages.last().granule) - pr.preSkip;
        check(qAbs(dur - 5 * 48000) <= 960,
              QStringLiteral("末页 granulepos 推出的时长 = %1s（期望 5.000±0.02）").arg(double(dur) / 48000.0, 0, 'f', 4));
        check(qAbs(pr.decodedSamples - dur) <= 960,
              QStringLiteral("libopus 解回来的样本数 %1 ≈ granulepos 推算值 %2（差 %3，末帧补零 ≤ 960）")
              .arg(pr.decodedSamples).arg(dur).arg(dur - pr.decodedSamples));
        check(QFileInfo(wav).size() > 0 && QFileInfo(one.first()).size() * 4 < QFileInfo(wav).size(),
              QStringLiteral("压缩比合理：%1 → %2").arg(human(QFileInfo(wav).size()), human(QFileInfo(one.first()).size())));
        dumpFile(one.first(), QStringLiteral("结果"));
        note(QStringLiteral("  转换耗时 = %1 ms（老方案要起 1~3 次 ffmpeg 进程）").arg(msOne));
    }

    // ---------- T2: 复用（存在即不重转） ----------
    note(QStringLiteral("---- T2 已存在 .opus 时直接复用 ----"));
    tmr.restart();
    err.clear();
    const QStringList again = convertAudioToOpusSegments(wav, 298, &err);
    const qint64 msAgain = tmr.elapsed();
    check(again == one && msAgain < 50,
          QStringLiteral("第二次调用直接命中缓存，耗时 %1 ms（err=%2）").arg(msAgain).arg(err));

    // ---------- T3: 分片 ----------
    note(QStringLiteral("---- T3 segSec=2 分片（5 秒音频应切 3 段）----"));
    // 换个文件名的副本：分段判定的复用规则是「同源文件已转过就不再转」，
    // 拿同一个源换 segSec 测不出切段（生产里 segSec 是常量 298，不存在这个场景）
    const QString wav2 = tmp + QStringLiteral("/参考音频_分片.wav");
    QFile::remove(wav2);
    check(QFile::copy(wav, wav2), QStringLiteral("复制出用于分片测试的副本"));
    {
        // ⚠ 必须把「单文件缓存」也一起删掉：复用规则是「同名 .opus 存在就直接返回」，
        //   只清 _seg*.opus 的话，上一轮跑残留下来的 <源>.opus 会被直接命中，
        //   于是 segSec 根本没机会生效（表现为只切出 1 段）。
        QFile::remove(wav2 + QStringLiteral(".opus"));
        const QStringList old2 = QDir(tmp).entryList(QStringList{ QStringLiteral("参考音频_分片.wav_seg*.opus") }, QDir::Files);
        for (const QString &o : old2) QFile::remove(tmp + "/" + o);
    }
    err.clear();
    const QStringList segs = convertAudioToOpusSegments(wav2, 2, &err);
    check(segs.size() == 3, QStringLiteral("切出 3 段（实际 %1，err=%2）").arg(segs.size()).arg(err));
    qint64 segSum = 0;
    bool segOk = true;
    for (int i = 0; i < segs.size(); ++i) {
        OpusProbe pr;
        if (!probeOpus(segs[i], &pr)) { segOk = false; note(QStringLiteral("  段%1 解析失败: %2").arg(i).arg(pr.err)); continue; }
        const qint64 dur = qint64(pr.pages.last().granule) - pr.preSkip;
        segSum += dur;
        if (dur > 2 * 48000) segOk = false;
        // 末帧补零到整 20ms 帧 → libopus 解出的样本数可能比 granulepos 推算值多 ≤960
        if (qAbs(pr.decodedSamples - dur) > 960) segOk = false;
        const QFileInfo fi(segs[i]);
        note(QStringLiteral("  段%1: %2  时长=%3s  解码样本=%4  EOS=%5")
             .arg(i).arg(fi.fileName()).arg(double(dur) / 48000.0, 0, 'f', 4)
             .arg(pr.decodedSamples).arg(int(pr.pages.last().flags & 0x04)));
    }
    check(segOk, QStringLiteral("每段都是独立合规的 .opus，且每段 ≤ 2 秒、granulepos 与解码样本自洽"));
    check(qAbs(segSum - 5 * 48000) <= 960 * 3,
          QStringLiteral("三段合计 %1s ≈ 原来的 5.000s").arg(double(segSum) / 48000.0, 0, 'f', 4));

    // ---------- T4/T5: 真实素材（MP4 取音轨 / MP3） ----------
    note(QStringLiteral("---- T4/T5 真实素材（中文路径）----"));
    note(QStringLiteral("    解码链路：① 自研 audiodecoder → ② libav（libavAvailable=%1）")
         .arg(libavAvailable() ? 1 : 0));
    const QPair<QString, QString> media[2] = {
        { mp4, QStringLiteral("MP4(视频取 AAC 音轨)") },
        { mp3, QStringLiteral("MP3") }
    };
    for (int i = 0; i < 2; ++i) {
        const QString src = media[i].first;
        if (!QFile::exists(src)) { note(QStringLiteral("  跳过 %1：样本不存在").arg(src)); continue; }
        const QString out = src + QStringLiteral(".opus");
        QFile::remove(out);
        // 清理可能存在的分片
        const QStringList old = QDir(tmp).entryList(QStringList{ QFileInfo(src).fileName() + "_seg*.opus" }, QDir::Files);
        for (const QString &o : old) QFile::remove(tmp + "/" + o);

        QString e2;
        tmr.restart();
        const QStringList r = convertInProcess(src, 298, &e2);
        const qint64 ms = tmr.elapsed();
        check(r.size() == 1, QStringLiteral("%1 → 转出 %2 个文件（err=%3）").arg(media[i].second).arg(r.size()).arg(e2));
        if (r.size() == 1) {
            OpusProbe pr;
            const bool ok = probeOpus(r.first(), &pr);
            check(ok, QStringLiteral("%1 逐页 CRC 校验通过（%2）").arg(media[i].second, pr.err));
            if (ok) {
                const qint64 dur = qint64(pr.pages.last().granule) - pr.preSkip;
                check(qAbs(pr.decodedSamples - dur) <= 960 && dur > 0,
                      QStringLiteral("%1 时长 = %2s，解码样本自洽").arg(media[i].second)
                      .arg(double(dur) / 48000.0, 0, 'f', 3));
                note(QStringLiteral("  %1: %2 → %3（%4 ms）")
                     .arg(media[i].second, human(QFileInfo(src).size()), human(QFileInfo(r.first()).size())).arg(ms));
            }
        }
    }

    // ---------- TX: 长视频（30 分钟级真实素材，用它验证「视频取音轨 + 分片」）----------
    // 用法：QATEST_LONG_VIDEO=<某个长视频> 后再跑本程序即可，不设置就跳过。
    {
        const QString longVid = qEnvironmentVariable("QATEST_LONG_VIDEO");
        if (!longVid.isEmpty() && QFile::exists(longVid)) {
            note(QStringLiteral("---- TX 长视频取音轨 + 分片 ----"));
            const QFileInfo lfi(longVid);
            const QDir ldir = lfi.absoluteDir();
            const QString lbase = lfi.fileName();
            QFile::remove(longVid + QStringLiteral(".opus"));
            const QStringList oldSeg = ldir.entryList(QStringList{ lbase + QStringLiteral("_seg*.opus") }, QDir::Files);
            for (const QString &o : oldSeg) QFile::remove(ldir.absoluteFilePath(o));

            QString eL;
            tmr.restart();
            const QStringList r = convertInProcess(longVid, 298, &eL);
            const qint64 ms = tmr.elapsed();
            note(QStringLiteral("  源 = %1（%2）").arg(lbase, human(lfi.size())));
            note(QStringLiteral("  段数 = %1   耗时 = %2 ms   err = [%3]").arg(r.size()).arg(ms).arg(eL));
            check(!r.isEmpty(), QStringLiteral("长视频能解出音轨并切段"));
            qint64 totalGran = 0;
            for (int i = 0; i < r.size(); ++i) {
                OpusProbe pr;
                const bool ok = probeOpus(r[i], &pr);
                const qint64 dur = ok ? (qint64(pr.pages.last().granule) - pr.preSkip) : -1;
                if (dur > 0) totalGran += dur;
                note(QStringLiteral("  段[%1] %2  %3   时长 = %4s   %5 %6")
                     .arg(i)
                     .arg(QFileInfo(r[i]).fileName(), human(QFileInfo(r[i]).size()))
                     .arg(double(dur) / 48000.0, 0, 'f', 3)
                     .arg(ok ? QStringLiteral("CRC-OK") : QStringLiteral("CRC-FAIL"), pr.err));
            }
            note(QStringLiteral("  各段时长合计 = %1 s（源时长应与之接近）").arg(double(totalGran) / 48000.0, 0, 'f', 3));
        } else {
            note(QStringLiteral("---- TX 长视频：未设置 QATEST_LONG_VIDEO，跳过 ----"));
        }
    }

    // ---------- T6/T7: 失败路径 ----------
    note(QStringLiteral("---- T6/T7 失败路径 ----"));
    QString e3;
    check(convertAudioToOpusSegments(tmp + QStringLiteral("/不存在.mp3"), 298, &e3).isEmpty() && !e3.isEmpty(),
          QStringLiteral("不存在的文件 → 返回空 + 有错误说明：%1").arg(e3));
    const QString junk = tmp + QStringLiteral("/假音频.bin");
    {
        QFile j(junk);
        if (j.open(QIODevice::WriteOnly)) j.write("这不是音频也不是视频，随便写点字节。", 48);
    }
    QFile::remove(junk + QStringLiteral(".opus"));
    QString e4;
    check(convertAudioToOpusSegments(junk, 298, &e4).isEmpty() && !e4.isEmpty(),
          QStringLiteral("垃圾文件 → 返回空（交回 ffmpeg 兜底）+ 错误说明：%1").arg(e4));
    check(!canDecodeAudioFile(junk) && canDecodeAudioFile(wav),
          QStringLiteral("canDecodeAudioFile 判定正确"));

    // ---------- T8: 进程内时长探测（只读元数据，不启动任何进程）----------
    note(QStringLiteral("---- T8 进程内时长探测 ----"));
    {
        struct DurCase { QString path; double expectMs; double tolMs; const char *what; };
        const DurCase cases[4] = {
            { wav,     5000.0,    30.0, "WAV 5.000s" },
            { wavOpus, 5000.0,    30.0, "Ogg Opus 5.000s（本程序刚转出来的）" },
            { mp4,     5130.0,   250.0, "MP4 取 AAC 音轨" },
            { mp3,   143140.0, 1200.0, "MP3 2:23.14" },
        };
        for (const DurCase &c : cases) {
            if (!QFile::exists(c.path)) {
                note(QStringLiteral("  （跳过 %1：样本不存在）").arg(QString::fromUtf8(c.what)));
                continue;
            }
            QElapsedTimer t; t.start();
            qint64 ms = 0;
            const bool got = probeAudioDurationMs(c.path, &ms);
            const qint64 el = t.elapsed();
            check(got && qAbs(double(ms) - c.expectMs) <= c.tolMs,
                  QStringLiteral("%1 → 探到 %2 ms（期望 %3±%4，耗时 %5 ms）")
                  .arg(QString::fromUtf8(c.what)).arg(ms)
                  .arg(c.expectMs, 0, 'f', 0).arg(c.tolMs, 0, 'f', 0).arg(el));
        }
        qint64 ms = 0;
        check(!probeAudioDurationMs(junk, &ms),
              QStringLiteral("垃圾文件 → 探不出时长（交回 ffmpeg 判定）"));
        check(!probeAudioDurationMs(tmp + QStringLiteral("/不存在.mp3"), &ms),
              QStringLiteral("不存在的文件 → 探不出时长"));
    }

    // ---------- T9: 视频判定（决定「视频当音频发」要不要强制提取音轨）----------
    note(QStringLiteral("---- T9 视频判定 ----"));
    {
        if (QFile::exists(mp4))
            check(mediaFileHasVideoTrack(mp4),
                  QStringLiteral("真 MP4（含视频轨）→ 判为视频，强制走提取音轨"));
        else
            note(QStringLiteral("  （跳过真 MP4：样本不存在）"));

        check(!mediaFileHasVideoTrack(mp3),  QStringLiteral("MP3 → 不是视频"));
        check(!mediaFileHasVideoTrack(wav),  QStringLiteral("WAV → 不是视频"));
        check(!mediaFileHasVideoTrack(wavOpus),
              QStringLiteral("Ogg Opus → 不是视频"));
        check(!mediaFileHasVideoTrack(junk),
              QStringLiteral("垃圾文件 → 按「不是视频」处理，不额外引入失败路径"));
        check(!mediaFileHasVideoTrack(tmp + QStringLiteral("/不存在.mp4")),
              QStringLiteral("不存在的 .mp4 → 按「不是视频」处理"));

        // 明确的视频后缀是「直判」路径，不读文件内容 —— 用不存在的路径正好验证这点
        check(mediaFileHasVideoTrack(tmp + QStringLiteral("/随便.mkv")),
              QStringLiteral("明确的视频后缀（.mkv）→ 直接判视频，不碰文件内容"));
        check(mediaFileHasVideoTrack(tmp + QStringLiteral("/随便.avi")),
              QStringLiteral("明确的视频后缀（.avi）→ 直接判视频"));

        // .mp4 后缀但内容不是 MP4（m4a 也是 mp4 容器，光看后缀分不开）→ 解析失败按「不是视频」
        const QString fakeMp4 = tmp + QStringLiteral("/假.mp4");
        QFile::remove(fakeMp4);
        if (QFile::copy(wav, fakeMp4)) {
            check(!mediaFileHasVideoTrack(fakeMp4),
                  QStringLiteral(".mp4 后缀 + 非 MP4 内容（解析失败）→ 按「不是视频」处理"));
            QFile::remove(fakeMp4);
        }
    }

    note(QStringLiteral("==== 结果: %1 通过 / %2 失败 ====").arg(g_pass).arg(g_fail));

    QFile out(QCoreApplication::applicationDirPath() + QStringLiteral("/qatest_report.txt"));
    if (out.open(QIODevice::WriteOnly)) out.write(g_log.join(QLatin1Char('\n')).toUtf8());
    return g_fail == 0 ? 0 : 1;
}
