/*
 * 纯白铃 - QQ 机器人管理平台
 * libav 封装层：用「自编译的精简版 FFmpeg 动态库」在进程内做几件原来要起 ffmpeg.exe 的事。
 *
 * 对应 libs/ffmpeg/（ffmpeg 7.1.5 / LGPL-2.1 / 只要音频解封装 + 解码，见其 README.md）。
 * 只封装本项目真正用得到的三件事，不暴露任何 libav 类型：
 *   · 只读元数据估时长   —— 认得的容器比自研的 audiodecoder 多得多
 *   · 容器里有没有视频轨
 *   · 把音频轨解成交错 float32 PCM —— 直接喂给进程内的重采样 + libopus
 *
 * 编译期开关：CMake 里 QIANCAO_WITH_LIBAV=ON（且 libs/ffmpeg 齐备）时才定义
 * `QIANCAO_WITH_LIBAV`，没编时本文件照样参与编译，所有函数返回「不可用」，
 * 上层会照旧退回 ffmpeg.exe 那条路。
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

#ifndef LIBAVIO_H
#define LIBAVIO_H

#include <QString>

#include "audiodecoder.h"   // 复用 PcmSink 的签名，好让两者可以直接互换

#if defined(QIANCAO_WITH_LIBAV)
#  define QA_HAVE_LIBAV 1
#else
#  define QA_HAVE_LIBAV 0
#endif

// 本编译单元有没有真的链上 libav。上层拿它决定「要不要试一次」，
// 免得在没编的构建里白跑一趟。注意：这是编译期常量，不检查运行时 DLL 是否被删。
inline bool libavAvailable() { return QA_HAVE_LIBAV != 0; }

// 版本串，例如 "libavcodec 61.19.101 / libavformat 61.7.103 / libavutil 59.39.100"。
// 没编 libav 时返回空串。用来在首次用到时打一条日志，确认 DLL 真被加载进来了。
QString libavVersionString();

// 只读容器元数据估算时长（毫秒），**不解码**。
// 覆盖队列比 audiodecoder.h 的 probeAudioDurationMs 宽：多了 asf(wma/wmv)、amr、
// matroska(mkv/webm)、avi、flv、ac3/eac3、dts、ape、wv 等。
// 返回 true = 算出来了；false = 打不开、没有音轨，或容器里压根没写时长（按「未知」处理）。
bool libavProbeDurationMs(const QString &path, qint64 *durationMs);

// 容器里有没有视频轨（只读元数据，不解码）。
// 打不开 / 解析失败一律返回 false —— 不确定就别启用强制转换，不额外引入失败路径。
bool libavHasVideoTrack(const QString &path);

// 用 libav 把音频/视频里的音轨解码成「交错」float32 PCM（-1.0 ~ 1.0），
// 保持源采样率与声道数、不做重采样。sink / srcRate / srcChannels / error 的语义与
// audiodecoder.h 的 decodeAudioFile 完全一致，所以能直接当它的替代品塞给
// convertAudioToOpusSegmentsEx()。
//
// ⚠ 路径按 UTF-8 传给 libav（FFmpeg 在 Windows 上就是这么解文件名的）。
bool libavDecodeAudioFile(const QString &path, const PcmSink &sink,
                          int *srcRate, int *srcChannels, QString *error);

#endif // LIBAVIO_H
