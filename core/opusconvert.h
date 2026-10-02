/*
 * 纯白铃 - QQ 机器人管理平台
 * 进程内「音频/视频 → Ogg Opus」转换（不再靠反复启动 ffmpeg.exe）
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

#ifndef OPUSCONVERT_H
#define OPUSCONVERT_H

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <functional>

#include "audiodecoder.h"   // PcmSink（解出来的 PCM 往哪儿送）

// 「把文件解成 PCM」的可注入实现。签名与 audiodecoder.h 的 decodeAudioFile 完全一致，
// 目的是让调用方能在自研解码层不认识的容器上换一个解码器（例如 libavio.h 里的
// libavDecodeAudioFile），而**后面的单声道化/重采样/libopus 编码/Ogg 封装完全不变**。
using PcmDecoder = std::function<bool(const QString &path, const PcmSink &sink,
                                      int *srcRate, int *srcChannels, QString *error)>;

// ---------------------------------------------------------------------------
// 可选的耗时统计（**传 nullptr 表示不统计**，此时链路里一行计时都不会跑）
//
// 用途：想知道「转一条音频到底花了多久、卡在哪一段」时传进来。
// 三段耗时的划分依据是本链路的结构 —— 解码器是通过回调把 PCM 推出来的，
// 所以回调里的时间可以精确切出来（重采样 + 编码），剩下的就是解码器内部的耗时：
//
//   totalMs  = 解码器调用 → 收尾封盘的墙钟
//   encodeMs = 所有 pushFrame（libopus 编码 + Ogg 分页写盘）+ 收尾
//   resampleMs = 回调总时间 - encodeMs      = 单声道化 + 48k 重采样
//   decodeMs = totalMs - 回调总时间          = 解封装 + 解码（含格式归一化/记账开销）
//
// ⚠⚠ 三段耗时**必须按纳秒累加**（nsecsElapsed），绝不能逐次求和 elapsed()：
//   解码器每次回调只喂一小块（AAC 一帧 = 1024 样本 ≈ 21 ms 音频），回调里的
//   重采样 + 编码只有几十微秒，毫秒分辨率的 elapsed() 会把它**四舍五入成 0** ——
//   结果是 50 分钟音频报成「编码 11 ms」，全部时间被兜进 decodeMs。
//   （2026-10-02 实测踩过：解 11852 / 重采样 0 / 编码 11，而产物确有 11.7 MB。）
//
// ⚠ 打开统计后每次回调多 4 次 QPC 查询：31 分钟音频（约 9.4 万次回调）合计 < 20 ms，
//   对判读无影响，但**追求极限数字时不要传 stats**。
// ---------------------------------------------------------------------------
struct OpusBenchStats
{
    bool    reused      = false;  // true = 命中已有的 .opus/分片，一次解码都没做
    qint64  totalMs     = 0;      // 全流程墙钟
    qint64  decodeMs    = 0;      // 解封装 + 解码
    qint64  resampleMs  = 0;      // 单声道化 + 重采样到 48k
    qint64  encodeMs    = 0;      // libopus 编码 + Ogg 封装写盘
    qint64  realSamples = 0;      // 实际音频样本数（48k 单声道，可除 48000 得秒）
    qint64  outBytes    = 0;      // 产物总字节
    int     segments    = 0;      // 段数
};

// 把音频/视频转成 Ogg Opus（.opus）。全过程在本进程内完成，不启动任何外部进程。
//
// 输出文件与「复用」规则（与老 m4a 链路一致：存在就不再转）：
//   · 已存在 <源文件>_seg*.opus  → 直接返回这些分片
//   · 已存在 <源文件>.opus       → 直接返回它
//        （不变式：只在该音频总时长 ≤ segSec 时才会生成这个单文件，
//          所以「有单文件」就等于「不需要分片」）
//   · 都没有 → 解码一次，边编码边按 segSec 秒切段：
//        只切出 1 段 → <源文件>.opus
//        切出多段    → <源文件>_seg000.opus、_seg001.opus …
//     每段都是**独立完整**的 .opus 文件（自带 OpusHead/OpusTags/EOS），
//     段长严格按 48kHz 帧对齐，保证 ≤ segSec 秒。
//
// 失败（格式不支持 / 解码出错）返回空表，并把原因写进 *error，调用方应退回 ffmpeg 老链路。
QStringList convertAudioToOpusSegments(const QString &srcFilePath, int segSec = 298,
                                      QString *error = nullptr,
                                      OpusBenchStats *stats = nullptr);

// 同上，但解码那一步用调用方给的 decoder。用来在自研解码层不支持时换 libav：
//   auto r = convertAudioToOpusSegments(src, sec, &e);              // 先试自研
//   if (r.isEmpty()) r = convertAudioToOpusSegmentsEx(src, sec, libavDecodeAudioFile, &e);
// ⚠ decoder 为空返回空表。
QStringList convertAudioToOpusSegmentsEx(const QString &srcFilePath, int segSec,
                                         const PcmDecoder &decoder, QString *error = nullptr,
                                         OpusBenchStats *stats = nullptr);

// 只要一份整文件（不切段）。成功返回 .opus 路径，失败返回空串。
QString convertAudioToOpus(const QString &srcFilePath, QString *error = nullptr);

#endif // OPUSCONVERT_H
