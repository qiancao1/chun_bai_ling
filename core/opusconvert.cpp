/*
 * 纯白铃 - QQ 机器人管理平台
 * 进程内「音频/视频 → Ogg Opus」转换实现
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

#include "opusconvert.h"
#include "audiodecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRandomGenerator>
#include <QByteArray>
#include <QElapsedTimer>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

#include <opus.h>
#include "ogg_packer.h"

// ===========================================================================
// 编码参数：想调音质/码率/延迟改这里
//   · kOpusBitrate   : 单声道目标码率。语音 24k 足够；音乐/通用建议 32~48k。
//   · 若确定只发语音，把 OPUS_APPLICATION_AUDIO 换成 OPUS_APPLICATION_VOIP
//     并加 OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)，同码率下人声更清楚。
// ===========================================================================
static const int kOpusRate     = 48000;
static const int kOpusChannels = 1;      // Ogg Opus 输出统一单声道
static const int kOpusBitrate  = 32000;
static const int kOpusFrameMs  = 20;     // 20ms 帧（Opus 标准帧长）
// 编码复杂度 0~10（10 = 质量最高、最慢）。**速度/质量的唯一旋钮就是这个数**：
// 实测 49 分钟音频（2944 s）在 cx10 下编码 9.51 s（占整条链路 11.86 s 的 80%，310x 实时）；
// 降到 5~6 约快 1.5~1.8 倍，而 32kbps 单声道这个码率下听感差异很小。
static const int kOpusComplexity = 10;
static const int kFrameSamples = kOpusRate / 1000 * kOpusFrameMs;   // 960
static const int kMaxPacket    = 4000;   // 20ms 单声道远用不到

namespace {

void setErr(QString *err, const QString &msg)
{
    if (err) *err = msg;
}

void putLE32(unsigned char *p, quint32 v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

// ---------------------------------------------------------------------------
// 单声道化 + 重采样到 48kHz
//   绝大多数源是 48k（视频音轨）→ 直通，逐样本无损；
//   44.1k / 22.05k / 8k 等 → 线性插值重采样（上采样，不会有混叠）；
//   高于 48k 的源 → 先做长度为 round(比值) 的滑动平均抗混叠，再插值抽取。
//   单声道化 = 各声道取平均。
// ---------------------------------------------------------------------------
class MonoResampler
{
public:
    void init(int srcRate)
    {
        m_rate = srcRate > 0 ? srcRate : kOpusRate;
        m_step = double(m_rate) / double(kOpusRate);
        m_pos  = 0.0;
        m_base = 0;                      // 本块首样本的绝对输入序号
        m_prev = 0.0f;
        m_started = false;
        m_box = (m_step > 1.0001) ? int(m_step + 0.5) : 1;
        if (m_box < 1) m_box = 1;
        m_hist.assign(size_t(m_box), 0.0f);
        m_histFill = 0;
        m_histSum = 0.0;
        m_out.reserve(8192);
    }
    int rate() const { return m_rate; }

    // 输入 one-shot：交错 float PCM 一整块 → 48k 单声道，交给 deliver(samples, count)
    // ⚠ 形参名绝不能叫 emit：Qt 的 qobjectdefs.h 里有 `#define emit`（空展开），
    //   会把 `emit(a,b)` 变成逗号表达式 `(a,b);` —— 回调静默不执行、且不报任何错。
    // ⚠ m_pos 是「绝对输入样本序号」空间的游标，必须跟 m_base（本块首样本的绝对序号）
    //   比较；若拿块内局部下标 i 去比，第一块之后 m_pos 早已远超 frames，后面每块都
    //   一个样本都发不出来（表现＝总时长只有一块的量）。
    template <typename Deliver>
    void process(const float *in, int frames, int channels, Deliver deliver)
    {
        if (frames <= 0) return;
        if (channels < 1) channels = 1;

        if (m_mono.size() < size_t(frames)) m_mono.resize(size_t(frames));
        for (int i = 0; i < frames; ++i) {
            double acc = 0.0;
            for (int c = 0; c < channels; ++c) acc += double(in[size_t(i) * channels + c]);
            m_mono[size_t(i)] = antiAlias(float(acc / channels));
        }

        m_out.clear();
        for (int i = 0; i < frames; ++i) {
            const float cur = m_mono[size_t(i)];
            if (!m_started) {
                m_out.push_back(cur);        // 首个输出样本 = 首个输入样本
                m_pos = m_step;
                m_prev = cur;
                m_started = true;
                continue;
            }
            const double absIdx = double(m_base + i);
            while (m_pos <= absIdx) {
                const float t = float(m_pos - (absIdx - 1.0));   // 落在 [0,1)
                m_out.push_back(m_prev + (cur - m_prev) * t);
                m_pos += m_step;
            }
            m_prev = cur;
        }
        m_base += frames;
        if (!m_out.empty())
            deliver(m_out.data(), int(m_out.size()));
    }

private:
    float antiAlias(float s)
    {
        if (m_box <= 1) return s;
        m_histSum += double(s) - double(m_hist[size_t(m_histFill)]);
        m_hist[size_t(m_histFill)] = s;
        if (++m_histFill >= m_box) m_histFill = 0;
        return float(m_histSum / m_box);
    }

    int m_rate = kOpusRate;
    double m_step = 1.0;
    double m_pos = 0.0;
    int m_base = 0;                 // 本块首样本的绝对输入序号
    float m_prev = 0.0f;
    bool m_started = false;
    int m_box = 1;
    std::vector<float> m_hist;
    int m_histFill = 0;
    double m_histSum = 0.0;
    std::vector<float> m_mono;
    std::vector<float> m_out;
};

// ---------------------------------------------------------------------------
// 一个 .opus 文件的写入器：libopus 编码 + 用 ogg_packer 封 Ogg
//   granulepos 语义严格照 libopusenc 来：普通包 = preSkip + 已编码样本数，
//   最后一个包 = preSkip + 真实输入样本数（让解码端把尾部补零裁掉）。
// ---------------------------------------------------------------------------
class OpusFileWriter
{
public:
    ~OpusFileWriter()
    {
        if (m_enc) opus_encoder_destroy(m_enc);
        if (m_ogg) oggp_destroy(m_ogg);
        if (m_f)   std::fclose(m_f);
    }

    bool open(const QString &path)
    {
        m_path = path;
        // Windows 下 fopen 认本地代码页（不是 UTF-8），中文路径必须走 encodeName
        const QByteArray u8 = QFile::encodeName(path);
        m_f = std::fopen(u8.constData(), "wb");
        if (!m_f) { m_error = QStringLiteral("创建文件失败"); return false; }

        int oerr = OPUS_OK;
        m_enc = opus_encoder_create(kOpusRate, kOpusChannels, OPUS_APPLICATION_AUDIO, &oerr);
        if (!m_enc || oerr != OPUS_OK) { m_error = QStringLiteral("libopus 编码器创建失败"); return false; }
        opus_encoder_ctl(m_enc, OPUS_SET_BITRATE(kOpusBitrate));
        opus_encoder_ctl(m_enc, OPUS_SET_VBR(1));
        opus_encoder_ctl(m_enc, OPUS_SET_COMPLEXITY(kOpusComplexity));
        opus_encoder_ctl(m_enc, OPUS_SET_LSB_DEPTH(16));
        opus_int32 look = 0;
        if (opus_encoder_ctl(m_enc, OPUS_GET_LOOKAHEAD(&look)) == OPUS_OK && look > 0)
            m_preSkip = int(look);

        const quint32 serial = QRandomGenerator::global()->generate();
        m_ogg = oggp_create(oggp_int32(serial));
        if (!m_ogg) { m_error = QStringLiteral("Ogg 分页器创建失败"); return false; }
        oggp_set_muxing_delay(m_ogg, oggp_uint64(kOpusRate));   // 约 1 秒出一页

        if (!writeHeader()) return false;
        if (!writeTags())   return false;
        m_ok = true;
        return true;
    }

    // 写入一个 20ms 帧。realSamples ≤ kFrameSamples（最后一帧可能是不满帧，其余已补零）。
    // last = true 时该包带 EOS，并用真实样本数当最终 granulepos。
    bool writeFrame(const float *frame, int realSamples, bool last)
    {
        if (!m_ok) return false;
        const int nb = opus_encode_float(m_enc, frame, kFrameSamples, m_packet, kMaxPacket);
        // 20ms 帧最少也会返回 1 字节；返回 ≤0 说明编码器真出问题了
        if (nb <= 0 || nb > kMaxPacket) {
            m_error = QStringLiteral("libopus 编码失败(%1)").arg(nb);
            return false;
        }

        const qint64 granule = last ? (qint64(m_preSkip) + m_real + realSamples)
                                    : (qint64(m_preSkip) + m_encoded + kFrameSamples);
        m_encoded += kFrameSamples;
        m_real    += realSamples;
        m_frames++;

        unsigned char *buf = oggp_get_packet_buffer(m_ogg, nb);
        if (!buf) { m_error = QStringLiteral("Ogg 缓冲分配失败"); return false; }
        std::memcpy(buf, m_packet, size_t(nb));
        if (oggp_commit_packet(m_ogg, nb, oggp_uint64(granule), last ? 1 : 0) != 0) {
            m_error = QStringLiteral("Ogg 提交包失败");
            return false;
        }
        if (last) oggp_flush_page(m_ogg);
        return drainPages();
    }

    // 收尾：没有任何音频帧时也补一个空的 EOS 页，保证文件结构完整
    bool finish()
    {
        if (!m_ok) return false;
        if (m_frames == 0) {
            oggp_get_packet_buffer(m_ogg, 1);
            if (oggp_commit_packet(m_ogg, 0, oggp_uint64(m_preSkip), 1) != 0) {
                m_error = QStringLiteral("Ogg 收尾失败");
                return false;
            }
        }
        oggp_flush_page(m_ogg);
        if (!drainPages()) return false;
        if (std::fflush(m_f) != 0) { m_error = QStringLiteral("刷盘失败"); return false; }
        m_ok = false;                       // 封口：之后不再允许写
        m_finished = true;
        return true;
    }

    bool ok() const { return m_finished; }
    qint64 realSamples() const { return m_real; }
    const QString &error() const { return m_error; }
    const QString &path() const { return m_path; }

    void closeHandles()
    {
        if (m_enc) { opus_encoder_destroy(m_enc); m_enc = nullptr; }
        if (m_ogg) { oggp_destroy(m_ogg); m_ogg = nullptr; }
        if (m_f)   { std::fclose(m_f); m_f = nullptr; }
    }

private:
    bool writeHeader()
    {
        // RFC 7845 OpusHead：19 字节（mapping family 0 时没有额外的流映射表）
        unsigned char head[19];
        std::memset(head, 0, sizeof(head));
        std::memcpy(head, "OpusHead", 8);
        head[8]  = 1;                                            // version
        head[9]  = (unsigned char)kOpusChannels;                 // channel count
        head[10] = (unsigned char)(m_preSkip & 0xFF);            // pre-skip (LE)
        head[11] = (unsigned char)((m_preSkip >> 8) & 0xFF);
        putLE32(head + 12, quint32(kOpusRate));                  // 原始输入采样率
        // head[16..17] 输出增益 = 0、head[18] mapping family = 0

        unsigned char *buf = oggp_get_packet_buffer(m_ogg, 19);
        if (!buf) { m_error = QStringLiteral("Ogg 缓冲分配失败"); return false; }
        std::memcpy(buf, head, 19);
        if (oggp_commit_packet(m_ogg, 19, 0, 0) != 0) {
            m_error = QStringLiteral("写 OpusHead 失败");
            return false;
        }
        oggp_flush_page(m_ogg);                                  // 头部独占第一页（自动带 BOS）
        return drainPages();
    }

    bool writeTags()
    {
        const QByteArray vendor = QByteArrayLiteral("qiancao");
        const int size = 8 + 4 + int(vendor.size()) + 4;         // "OpusTags" + 长度 + vendor + 0 条注释
        unsigned char *buf = oggp_get_packet_buffer(m_ogg, size);
        if (!buf) { m_error = QStringLiteral("Ogg 缓冲分配失败"); return false; }
        std::memcpy(buf, "OpusTags", 8);
        putLE32(buf + 8, quint32(vendor.size()));
        std::memcpy(buf + 12, vendor.constData(), size_t(vendor.size()));
        putLE32(buf + 12 + vendor.size(), 0);
        if (oggp_commit_packet(m_ogg, size, 0, 0) != 0) {
            m_error = QStringLiteral("写 OpusTags 失败");
            return false;
        }
        oggp_flush_page(m_ogg);
        return drainPages();
    }

    bool drainPages()
    {
        unsigned char *page = nullptr;
        oggp_int32 bytes = 0;
        while (oggp_get_next_page(m_ogg, &page, &bytes)) {
            if (bytes > 0 && std::fwrite(page, 1, size_t(bytes), m_f) != size_t(bytes)) {
                m_error = QStringLiteral("写文件失败");
                return false;
            }
        }
        return true;
    }

    QString m_path;
    QString m_error;
    FILE *m_f = nullptr;
    oggpacker *m_ogg = nullptr;
    OpusEncoder *m_enc = nullptr;
    int m_preSkip = 0;
    qint64 m_encoded = 0;      // 送进编码器的样本数（含末帧补零）
    qint64 m_real = 0;         // 真实输入样本数
    qint64 m_frames = 0;
    bool m_ok = false;
    bool m_finished = false;
    unsigned char m_packet[kMaxPacket] = {0};
};

// ---------------------------------------------------------------------------
// 分片器：喂 48k 单声道 PCM，自动按 segSec 秒切出多个独立 .opus
//   · 每段长度按帧对齐（segMax 取 960 的整数倍）→ 严格 ≤ segSec 秒
//   · 帧要「延迟一帧」提交：只有看到下一帧/收尾时，才知道上一帧是不是本段最后一帧，
//     因为 Ogg 的 EOS 标志是打在该段**最后一个包**上的
//   · 第一段先写 <src>.opus，一旦确定要切第二段就把它改名成 <src>_seg000.opus，
//     这样「单文件」这条最常见路径零改名开销
// ---------------------------------------------------------------------------
class OpusSegmenter
{
public:
    OpusSegmenter(const QString &srcPath, int segSec)
        : m_src(srcPath)
        , m_segSec(segSec > 0 ? segSec : 298)
    {
        m_segMax = (qint64(m_segSec) * kOpusRate / kFrameSamples) * kFrameSamples;
        if (m_segMax <= 0) m_segMax = qint64(kOpusRate);      // 兜底 1 秒
    }

    ~OpusSegmenter()
    {
        if (!m_committed) cleanup();
    }

    const QString &error() const { return m_error; }
    qint64 totalRealSamples() const { return m_total; }

    // 一帧完整数据到手（real ≤ kFrameSamples，仅最后一帧可能不满）
    bool pushFrame(const float *frame, int real)
    {
        if (m_segMax > 0 && m_curSamples > 0 && m_curSamples + kFrameSamples > m_segMax) {
            if (!rollSegment()) return false;
        }
        if (!m_w) {
            if (!openSegment()) return false;
        }
        if (m_hasPend) {
            if (!m_w->writeFrame(m_pend, m_pendReal, false)) {
                m_error = m_w->error();
                return false;
            }
            m_total += m_pendReal;
        }
        std::memcpy(m_pend, frame, sizeof(float) * size_t(kFrameSamples));
        m_pendReal = real;
        m_hasPend = true;
        m_curSamples += kFrameSamples;
        return true;
    }

    // 收尾并给出最终文件列表
    bool finish(QStringList *out)
    {
        if (!m_w) { m_error = QStringLiteral("没有任何可写入的音频帧"); return false; }
        if (m_hasPend) {
            if (!m_w->writeFrame(m_pend, m_pendReal, true)) { m_error = m_w->error(); return false; }
            m_total += m_pendReal;
            m_hasPend = false;
        }
        if (m_total <= 0) { m_error = QStringLiteral("解码出来的音频是空的"); return false; }
        if (!m_w->finish()) { m_error = m_w->error(); return false; }
        m_w->closeHandles();
        if (out) *out = m_files;
        m_committed = true;                 // 成功，析构不再清理
        return true;
    }

private:
    bool openSegment()
    {
        const QString path = (m_files.isEmpty())
                ? (m_src + QStringLiteral(".opus"))
                : QStringLiteral("%1_seg%2.opus").arg(m_src).arg(m_files.size(), 3, 10, QLatin1Char('0'));
        m_w = new OpusFileWriter();
        if (!m_w->open(path)) { m_error = m_w->error(); return false; }
        m_files << path;
        return true;
    }

    bool rollSegment()
    {
        // 当前段的最后一帧（延迟提交的那一帧）打上 EOS
        if (m_hasPend) {
            if (!m_w->writeFrame(m_pend, m_pendReal, true)) { m_error = m_w->error(); return false; }
            m_total += m_pendReal;
            m_hasPend = false;
        }
        if (!m_w->finish()) { m_error = m_w->error(); return false; }
        m_w->closeHandles();
        delete m_w;
        m_w = nullptr;
        m_curSamples = 0;

        // 第一段（<src>.opus）要退位改名成 <src>_seg000.opus
        if (m_files.size() == 1) {
            const QString from = m_files[0];
            const QString to   = QStringLiteral("%1_seg000.opus").arg(m_src);
            QFile::remove(to);
            if (!QFile::rename(from, to)) {
                m_error = QStringLiteral("分片改名失败: ") + from;
                return false;
            }
            m_files[0] = to;
        }
        return true;
    }

    void cleanup()
    {
        if (m_w) { m_w->closeHandles(); delete m_w; m_w = nullptr; }
        for (const QString &f : m_files) QFile::remove(f);
        m_files.clear();
    }

    QString m_src;
    int m_segSec = 298;
    qint64 m_segMax = 0;
    qint64 m_curSamples = 0;
    qint64 m_total = 0;
    OpusFileWriter *m_w = nullptr;
    QStringList m_files;
    QString m_error;
    bool m_hasPend = false;
    float m_pend[kFrameSamples] = {0};
    int m_pendReal = 0;
    bool m_committed = false;
};

// 已经切好的分片 / 单文件（存在即复用）
QStringList findExistingSegments(const QString &src)
{
    const QFileInfo fi(src);
    const QDir dir = fi.absoluteDir();
    const QStringList names =
            dir.entryList(QStringList{ fi.fileName() + QStringLiteral("_seg*.opus") },
                          QDir::Files, QDir::Name);
    QStringList out;
    for (const QString &n : names) out << dir.absoluteFilePath(n);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// 顶层：解码 → 单声道化 → 重采样 48k → libopus 编码 → Ogg 封装（带分片）
// ---------------------------------------------------------------------------
QStringList convertAudioToOpusSegmentsEx(const QString &srcFilePath, int segSec,
                                         const PcmDecoder &decoder, QString *error,
                                         OpusBenchStats *stats)
{
    if (stats) *stats = OpusBenchStats{};
    setErr(error, QString());
    if (segSec <= 0) segSec = 298;
    if (!decoder) {
        setErr(error, QStringLiteral("没有提供解码器"));
        return {};
    }
    if (!QFile::exists(srcFilePath)) {
        setErr(error, QStringLiteral("源文件不存在"));
        return {};
    }

    // 1) 已有分片 → 直接复用，一次解码都不做
    const QStringList exist = findExistingSegments(srcFilePath);
    if (!exist.isEmpty()) {
        if (stats) {
            stats->reused   = true;
            stats->segments = exist.size();
            for (const QString &f : exist) stats->outBytes += QFileInfo(f).size();
        }
        return exist;
    }

    // 2) 已有单文件 → 直接复用（见头文件里的不变式）
    const QString single = srcFilePath + QStringLiteral(".opus");
    if (QFile::exists(single)) {
        if (stats) {
            stats->reused   = true;
            stats->segments = 1;
            stats->outBytes = QFileInfo(single).size();
        }
        return QStringList{ single };
    }

    // 3) 真转一次
    //    计时点只在传了 stats 时才跑：否则一行 QPC 都不查，热路径零开销。
    const bool bench = (stats != nullptr);
    QElapsedTimer tAll, tCb, tEnc;
    // ⚠ 必须按**纳秒**累加：解码器每次回调只喂一小块（AAC 一帧 = 1024 样本 ≈ 21 ms），
    //   回调里的重采样 + 编码只有几十微秒，毫秒分辨率的 elapsed() 会把它整成 0 ——
    //   50 分钟音频会报成「编码 11 ms」，全部时间被兜进 decodeMs（2026-10-02 实测踩过）。
    qint64 cbNs = 0, encNs = 0;
    if (bench) tAll.start();

    OpusSegmenter seg(srcFilePath, segSec);
    MonoResampler rs;

    int srcRate = 0;
    int srcChannels = 0;
    bool encFailed = false;
    bool rsReady = false;
    // 攒帧缓冲：解码块长度是任意的，编码必须是 20ms 整帧
    std::vector<float> frameBuf(size_t(kFrameSamples), 0.0f);
    int frameFill = 0;

    const bool decOk = decoder(srcFilePath,
        [&](const float *samples, int frames, int channels) -> bool {
            if (bench) tCb.start();
            if (!rsReady || rs.rate() != srcRate) {
                if (srcRate <= 0) return false;
                rs.init(srcRate);
                rsReady = true;
            }
            bool keep = true;
            rs.process(samples, frames, channels, [&](const float *mono, int n) {
                // 编码（libopus + Ogg 分页）单独计时；内层回调可能被多次调用，逐次累加
                if (bench) tEnc.start();
                int i = 0;
                while (i < n && keep) {
                    const int need = kFrameSamples - frameFill;
                    const int take = (n - i < need) ? (n - i) : need;
                    std::memcpy(frameBuf.data() + frameFill, mono + i, sizeof(float) * size_t(take));
                    frameFill += take;
                    i += take;
                    if (frameFill == kFrameSamples) {
                        frameFill = 0;
                        if (!seg.pushFrame(frameBuf.data(), kFrameSamples)) {
                            keep = false;
                            encFailed = true;
                        }
                    }
                }
                if (bench) encNs += tEnc.nsecsElapsed();
            });
            if (bench) cbNs += tCb.nsecsElapsed();
            return keep;
        }, &srcRate, &srcChannels, error);

    if (encFailed) {
        setErr(error, seg.error().isEmpty() ? QStringLiteral("Opus 编码失败") : seg.error());
        return {};
    }
    if (!decOk) {
        if (!error || error->isEmpty())
            setErr(error, QStringLiteral("解码失败"));
        return {};
    }
    // 尾巴上的不满帧：补零成整帧，但真实样本数照原样传给编码器（决定末页 granulepos）
    if (frameFill > 0) {
        std::memset(frameBuf.data() + frameFill, 0, sizeof(float) * size_t(kFrameSamples - frameFill));
        const int real = frameFill;
        frameFill = 0;
        if (bench) tEnc.start();
        const bool okTail = seg.pushFrame(frameBuf.data(), real);
        if (bench) encNs += tEnc.nsecsElapsed();
        if (!okTail) {
            setErr(error, seg.error().isEmpty() ? QStringLiteral("Opus 编码失败") : seg.error());
            return {};
        }
    }

    QStringList out;
    if (bench) tEnc.start();
    const bool okFin = seg.finish(&out);
    if (bench) encNs += tEnc.nsecsElapsed();
    if (!okFin) {
        setErr(error, seg.error().isEmpty() ? QStringLiteral("Opus 封装失败") : seg.error());
        return {};
    }

    if (stats) {
        // 统一在纳秒域算完、最后四舍五入成毫秒，保证 解 + 重采样 + 编码 ≈ 共
        const qint64 totalNs = tAll.nsecsElapsed();
        const auto nsToMs = [](qint64 ns) { return (ns + 500000) / 1000000; };
        stats->totalMs     = nsToMs(totalNs);
        stats->encodeMs    = nsToMs(encNs);
        // 回调总时间 = 重采样 + 编码，扣掉编码就是重采样
        stats->resampleMs  = nsToMs(cbNs > encNs ? (cbNs - encNs) : 0);
        // 剩下的（墙钟 - 回调总时间）= 解码器内部（解封装 + 解码 + 格式归一化）
        stats->decodeMs    = nsToMs(totalNs > cbNs ? (totalNs - cbNs) : 0);
        stats->realSamples = seg.totalRealSamples();
        stats->segments    = out.size();
        for (const QString &f : out) stats->outBytes += QFileInfo(f).size();
    }
    return out;
}

QStringList convertAudioToOpusSegments(const QString &srcFilePath, int segSec, QString *error,
                                       OpusBenchStats *stats)
{
    // 默认用自研解码层（audiodecoder）；它不认识的容器由调用方改用 Ex 版本换解码器
    return convertAudioToOpusSegmentsEx(srcFilePath, segSec, decodeAudioFile, error, stats);
}

QString convertAudioToOpus(const QString &srcFilePath, QString *error)
{
    // 一个足够大的段长 = 不切段
    const QStringList r = convertAudioToOpusSegments(srcFilePath, 24 * 3600, error);
    return r.size() == 1 ? r.first() : QString();
}
