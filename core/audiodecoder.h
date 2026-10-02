/*
 * 纯白铃 - QQ 机器人管理平台
 * 进程内音频解码层：把常见音频/视频容器里的音轨解成 float32 PCM（不启动 ffmpeg）
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

#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include <QString>
#include <functional>

// 解码回调：samples 是「交错」的 float32 PCM（-1.0 ~ 1.0），frameCount 是每声道帧数，
// channels 是声道数。返回 false 表示调用方要求立刻中止（例如写盘失败），解码随即结束。
using PcmSink = std::function<bool(const float *samples, int frameCount, int channels)>;

// 把音频/视频文件解码成 float32 PCM（保持源采样率与声道数，不做重采样）。
// 支持：WAV / MP3 / FLAC / OGG-Vorbis / MP4(含 MOV、M4A、3GP 等) 里的 AAC 音轨 / 裸 ADTS AAC。
//   sink        : 数据回调
//   srcRate     : [out] 源采样率（可传 nullptr）
//   srcChannels : [out] 源声道数（可传 nullptr）
//   error       : [out] 失败原因（可传 nullptr）
// 返回 true 表示解码完成。任何失败（格式不支持、文件损坏、无音轨）都返回 false，
// 调用方应当退回 ffmpeg 老链路兜底。
bool decodeAudioFile(const QString &path, const PcmSink &sink,
                     int *srcRate, int *srcChannels, QString *error);

// 只按「文件头魔数 + 扩展名」判断这本解码器是否受理该文件，不解码。
// 用于提前决策（走进程内转换还是直接交给 ffmpeg）。
bool canDecodeAudioFile(const QString &path);

// 只读容器/帧头元数据估算时长（毫秒），**不解码** —— 比解码一遍便宜好几个数量级，
// 适合「这条音频会不会超出发送时长上限、要不要切段」这类决策。
// 支持：WAV / MP3 / FLAC / Ogg(Vorbis 或 Opus) / MP4(含 M4A、MOV，取音轨时长) / 裸 ADTS AAC。
// 返回 true = 算出来了；false = 格式不认识、文件打不开，或容器里压根没写时长
// （调用方按「未知」处理，通常就是原样发送）。
bool probeAudioDurationMs(const QString &path, qint64 *durationMs);

// 这个文件是不是「视频」（容器里带视频轨）。只读容器元数据，不解码。
//   - 明确的视频后缀（avi/mkv/flv/webm/ts/wmv/rmvb 等）直接判 true，不碰文件内容；
//   - .mp4 / .mov / .3gp 这类混合容器（m4a 同样是 mp4 容器，光看后缀分不开）读一次 moov，
//     看有没有 handler_type == 'vide' 的轨。
// 用途：视频当音频发送时必须先提取音轨转码，不能因为「体积够小」就原样直传 ——
// 否则发给音频接口的是个视频文件。
// 打开失败 / 解析失败一律按 false 处理（不确定就别启用强制转换，不额外引入失败路径）。
bool mediaFileHasVideoTrack(const QString &path);

#endif // AUDIODECODER_H
