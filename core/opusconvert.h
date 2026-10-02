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
                                      QString *error = nullptr);

// 只要一份整文件（不切段）。成功返回 .opus 路径，失败返回空串。
QString convertAudioToOpus(const QString &srcFilePath, QString *error = nullptr);

#endif // OPUSCONVERT_H
