/*
 * 纯白铃 - QQ 机器人管理平台
 * libav 封装层实现（见 libavio.h）
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

// ⚠⚠ 这两个宏必须在**任何头文件之前**定义，见下面 include 段的说明
#ifndef __STDC_CONSTANT_MACROS
#  define __STDC_CONSTANT_MACROS
#endif

#include "libavio.h"

#if QA_HAVE_LIBAV

// ⚠⚠ 在 C++ 里用 FFmpeg 必须**自己包 extern "C"**：
//   FFmpeg 的头文件（7.1.5 实测：整个 include 树里一处 `extern "C"` 都没有、也不看
//   __cplusplus）导出的是 C 符号，直接 include 会按 C++ 修饰名去找，
//   链接期一片 LNK2019「无法解析的外部符号 ?avutil_version@@YAIXZ」。
//   用 C 写（.c 文件）时没这问题 —— 所以官方的 example 直接 include 就行，别照抄。
//
//   另外 libavutil/common.h 里有：
//       #if defined(__cplusplus) && !defined(__STDC_CONSTANT_MACROS) && !defined(UINT64_C)
//       #error missing -D__STDC_CONSTANT_MACROS
//   MSVC 的 <inttypes.h> 会自带 UINT64_C 所以侥幸能过，glibc 下会直接报错 ——
//   所以文件最开头就把 __STDC_CONSTANT_MACROS 定义掉（必须在 <stdint.h> 之前）。
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

#include <vector>
#include <cstdint>

namespace {

void setErr(QString *error, const QString &msg)
{
    if (error) *error = msg;
}

// av_strerror 的 QString 版
QString avErrText(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromLocal8Bit(buf);
}

QString avVersionTriple(unsigned v)
{
    return QStringLiteral("%1.%2.%3").arg(v >> 16).arg((v >> 8) & 0xff).arg(v & 0xff);
}

// 打开容器并定位音频流。失败返回 nullptr（内部已把 fmt 关掉）。
AVFormatContext *openWithAudioStream(const QString &path, int *audioIndex, QString *error)
{
    // ⚠ FFmpeg 在 Windows 上按 UTF-8 解文件名，不能传 toLocal8Bit()
    const QByteArray p = path.toUtf8();

    AVFormatContext *fmt = nullptr;
    int ret = avformat_open_input(&fmt, p.constData(), nullptr, nullptr);
    if (ret < 0) {
        setErr(error, QStringLiteral("libav 打不开文件：%1").arg(avErrText(ret)));
        return nullptr;
    }

    // 这一步只读头部/索引（不解码音频本体），时长、流参数都靠它填全
    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0) {
        setErr(error, QStringLiteral("libav 读流信息失败：%1").arg(avErrText(ret)));
        avformat_close_input(&fmt);
        return nullptr;
    }

    const int idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (idx < 0) {
        setErr(error, QStringLiteral("容器里没有音频流"));
        avformat_close_input(&fmt);
        return nullptr;
    }

    if (audioIndex) *audioIndex = idx;
    return fmt;
}

} // namespace

// ---------------------------------------------------------------------------
QString libavVersionString()
{
    return QStringLiteral("libavcodec %1 / libavformat %2 / libavutil %3 / libswresample %4")
            .arg(avVersionTriple(avcodec_version()),
                 avVersionTriple(avformat_version()),
                 avVersionTriple(avutil_version()),
                 avVersionTriple(swresample_version()));
}

// ---------------------------------------------------------------------------
bool libavProbeDurationMs(const QString &path, qint64 *durationMs)
{
    if (durationMs) *durationMs = 0;

    int ast = -1;
    AVFormatContext *fmt = openWithAudioStream(path, &ast, nullptr);
    if (!fmt) return false;

    qint64 ms = 0;
    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0) {
        // fmt->duration 以 AV_TIME_BASE(1e6) 为单位
        ms = fmt->duration / (AV_TIME_BASE / 1000);
    } else {
        // 容器头没写总时长 → 退回音轨自己的时长
        const AVStream *st = fmt->streams[ast];
        if (st->duration != AV_NOPTS_VALUE && st->duration > 0
                && st->time_base.num > 0 && st->time_base.den > 0) {
            const AVRational msTB = { 1, 1000 };
            ms = av_rescale_q(st->duration, st->time_base, msTB);
        }
    }
    avformat_close_input(&fmt);

    if (ms <= 0) return false;          // 没写时长 = 当「未知」处理
    if (durationMs) *durationMs = ms;
    return true;
}

// ---------------------------------------------------------------------------
bool libavHasVideoTrack(const QString &path)
{
    const QByteArray p = path.toUtf8();
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, p.constData(), nullptr, nullptr) < 0)
        return false;

    bool has = false;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters *cp = fmt->streams[i] ? fmt->streams[i]->codecpar : nullptr;
        if (cp && cp->codec_type == AVMEDIA_TYPE_VIDEO) {
            has = true;
            break;
        }
    }
    avformat_close_input(&fmt);
    return has;
}

// ---------------------------------------------------------------------------
bool libavDecodeAudioFile(const QString &path, const PcmSink &sink,
                          int *srcRate, int *srcChannels, QString *error)
{
    if (srcRate)     *srcRate = 0;
    if (srcChannels) *srcChannels = 0;
    if (!sink) {
        setErr(error, QStringLiteral("没有提供 PCM 回调"));
        return false;
    }

    int ast = -1;
    AVFormatContext *fmt = openWithAudioStream(path, &ast, error);
    if (!fmt) return false;

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[ast]->codecpar->codec_id);
    if (!codec) {
        setErr(error, QStringLiteral("libav 没有对应的解码器（codec_id=%1）")
                          .arg(int(fmt->streams[ast]->codecpar->codec_id)));
        avformat_close_input(&fmt);
        return false;
    }

    AVCodecContext *dec = avcodec_alloc_context3(codec);
    if (!dec) {
        setErr(error, QStringLiteral("avcodec_alloc_context3 失败"));
        avformat_close_input(&fmt);
        return false;
    }

    int ret = avcodec_parameters_to_context(dec, fmt->streams[ast]->codecpar);
    if (ret >= 0) ret = avcodec_open2(dec, codec, nullptr);
    if (ret < 0) {
        setErr(error, QStringLiteral("libav 打开解码器失败（%1）：%2")
                          .arg(QString::fromLatin1(codec->name), avErrText(ret)));
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return false;
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame  *frm = av_frame_alloc();

    // 用 swresample 把「任意采样格式/声道布局」统一成「交错 float32、源采样率、源声道数」。
    // 真正的重采样（→48k 单声道）还是交给 opusconvert 里的 MonoResampler，这里只做格式归一。
    SwrContext *swr = nullptr;
    int outChannels = 0;
    std::vector<float> outBuf;
    std::vector<const uint8_t *> inPtr;
    bool ok = true;
    qint64 fedFrames = 0;

    auto feed = [&](const AVFrame *f) -> bool {
        if (!swr) {
            // 参数以「解码器实际吐出来的第一帧」为准（codecpar 可能与之一致也可能被解码器改写）
            const int ch = f->ch_layout.nb_channels;
            if (f->sample_rate <= 0 || ch <= 0 || f->format < 0)
                return false;

            AVChannelLayout outLayout;
            if (av_channel_layout_copy(&outLayout, &f->ch_layout) < 0)
                return false;
            ret = swr_alloc_set_opts2(&swr,
                                      &outLayout, AV_SAMPLE_FMT_FLT, f->sample_rate,
                                      &f->ch_layout, AVSampleFormat(f->format), f->sample_rate,
                                      0, nullptr);
            av_channel_layout_uninit(&outLayout);
            if (ret < 0 || swr_init(swr) < 0)
                return false;

            outChannels = ch;
            inPtr.assign(size_t(ch), nullptr);
            if (srcRate)     *srcRate = f->sample_rate;
            if (srcChannels) *srcChannels = ch;
        }

        const int outN = swr_get_out_samples(swr, f->nb_samples);
        if (outN <= 0) return true;

        outBuf.resize(size_t(outN) * size_t(outChannels));
        uint8_t *dst[1] = { reinterpret_cast<uint8_t *>(outBuf.data()) };
        for (int i = 0; i < outChannels; ++i)
            inPtr[size_t(i)] = f->extended_data[i];

        const int got = swr_convert(swr, dst, outN, inPtr.data(), f->nb_samples);
        if (got < 0) return false;
        if (got > 0) {
            fedFrames += got;
            if (!sink(outBuf.data(), got, outChannels))
                return false;         // 调用方要求立刻中止
        }
        return true;
    };

    while (ok && pkt && frm) {
        ret = av_read_frame(fmt, pkt);
        if (ret < 0) break;           // EOF（或读错误）→ 去 flush

        if (pkt->stream_index == ast) {
            if (avcodec_send_packet(dec, pkt) >= 0) {
                while (avcodec_receive_frame(dec, frm) >= 0) {
                    if (!feed(frm)) { ok = false; }
                    av_frame_unref(frm);
                    if (!ok) break;
                }
            }
        }
        av_packet_unref(pkt);
    }

    // flush 解码器里缓着的尾巴
    if (ok && avcodec_send_packet(dec, nullptr) >= 0) {
        while (avcodec_receive_frame(dec, frm) >= 0) {
            if (!feed(frm)) { ok = false; }
            av_frame_unref(frm);
            if (!ok) break;
        }
    }

    if (ok && fedFrames == 0) {
        setErr(error, QStringLiteral("libav 没解出任何 PCM 数据"));
        ok = false;
    } else if (ok && outChannels <= 0) {
        ok = false;
    }
    if (!ok && error && error->isEmpty())
        setErr(error, QStringLiteral("libav 解码失败"));

    if (swr) swr_free(&swr);
    av_frame_free(&frm);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return ok;
}

#else  // ---------------- 没编 libav：全部返回「不可用」 ----------------

QString libavVersionString() { return {}; }

bool libavProbeDurationMs(const QString &, qint64 *durationMs)
{
    if (durationMs) *durationMs = 0;
    return false;
}

bool libavHasVideoTrack(const QString &) { return false; }

bool libavDecodeAudioFile(const QString &, const PcmSink &,
                          int *srcRate, int *srcChannels, QString *error)
{
    if (srcRate)     *srcRate = 0;
    if (srcChannels) *srcChannels = 0;
    if (error) *error = QStringLiteral("本次构建没有编入 libav（QIANCAO_WITH_LIBAV=OFF）");
    return false;
}

#endif // QA_HAVE_LIBAV
