/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
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

#include "global.h"

QByteArray convertMp3ToSilk(const QByteArray &mp3Data)
{
    if (mp3Data.isEmpty()) {
        qWarning() << "输入音频数据为空";
        return {};
    }

    // 1. 检查工具是否存在
    QString ffmpegPath = QDir(ffmpegdiv).filePath("ffmpeg.exe");
    QString encoderPath = QDir(ffmpegdiv).filePath("silk_v3_encoder.exe");
    if (!QFile::exists(ffmpegPath)) {
        AppendEventLog("ffmpeg.exe 不存在",0xff);
        return {};
    }
    if (!QFile::exists(encoderPath)) {
        AppendEventLog("silk_v3_encoder.exe 不存在");
        return {};
    }

    // 2. 创建三个临时文件（使用 QTemporaryFile，析构时自动删除）
    QTemporaryFile srcFile(QDir::temp().filePath("audio_XXXXXX.mp3"));
    QTemporaryFile pcmFile(QDir::temp().filePath("audio_XXXXXX.pcm"));
    QTemporaryFile silkFile(QDir::temp().filePath("audio_XXXXXX.silk"));

    auto openTempFile = [](QTemporaryFile &file) -> bool {
        return file.open();
    };
    if (!openTempFile(srcFile) || !openTempFile(pcmFile) || !openTempFile(silkFile)) {
        qWarning() << "创建临时文件失败";
        return {};
    }

    // 确保临时文件在函数返回时自动删除（QTemporaryFile 已经保证，但手动关闭写入）
    srcFile.setAutoRemove(true);
    pcmFile.setAutoRemove(true);
    silkFile.setAutoRemove(true);

    // 3. 写入 MP3 数据
    if (srcFile.write(mp3Data) != mp3Data.size()) {
        qWarning() << "写入源文件失败";
        return {};
    }
    srcFile.close(); // 确保数据落盘，后续 QProcess 需要读取

    // 4. 执行 ffmpeg：MP3 -> PCM
    QStringList ffmpegArgs = {
        "-y", "-i", srcFile.fileName(),
        "-f", "s16le", "-ar", "24000", "-ac", "1",
        pcmFile.fileName()
    };
    QProcess ffmpeg;
    ffmpeg.start(ffmpegPath, ffmpegArgs);
    if (!ffmpeg.waitForFinished(30000)) {
        ffmpeg.kill();
        return {};
    }
    if (ffmpeg.exitCode() != 0) {

        return {};
    }

    // 5. 执行 silk_v3_encoder：PCM -> Silk
    QStringList encoderArgs = {
        pcmFile.fileName(), silkFile.fileName(), "-tencent"
    };
    QProcess encoder;
    encoder.start(encoderPath, encoderArgs);
    if (!encoder.waitForFinished(30000)) {
        encoder.kill();
        return {};
    }
    if (encoder.exitCode() != 0) {
        return {};
    }

    // 6. 读取 Silk 数据
    if (!silkFile.open()) {
        return {};
    }
    QByteArray silkData = silkFile.readAll();
    silkFile.close();

    if (silkData.isEmpty()) {
        return {};
    }

    return silkData;
}

QByteArray convertFileToSilk(const QString &srcFilePath)
{
    // 1. 检查源文件是否存在
    if (!QFile::exists(srcFilePath)) {
        qWarning() << "源文件不存在:" << srcFilePath;
        return {};
    }

    // 2. 检查工具是否存在
    QString ffmpegPath = QDir(ffmpegdiv).filePath("ffmpeg.exe");
    QString encoderPath = QDir(ffmpegdiv).filePath("silk_v3_encoder.exe");
    if (!QFile::exists(ffmpegPath)) {
        AppendEventLog("ffmpeg.exe 不存在");
        return {};
    }
    if (!QFile::exists(encoderPath)) {
        AppendEventLog("silk_v3_encoder.exe 不存在" );
        return {};
    }
    // 3. 创建临时 PCM 和 Silk 文件（析构时自动删除）
    QTemporaryFile pcmFile(QDir::temp().filePath("audio_XXXXXX.pcm"));
    QTemporaryFile silkFile(QDir::temp().filePath("audio_XXXXXX.silk"));
    if (!pcmFile.open() || !silkFile.open()) {
        qWarning() << "创建临时文件失败";
        return {};
    }
    // 确保临时文件自动删除
    pcmFile.setAutoRemove(true);
    silkFile.setAutoRemove(true);

    // 关闭文件句柄，因为 QProcess 需要独占写入
    pcmFile.close();
    silkFile.close();

    // 4. 执行 ffmpeg：源文件 -> PCM (16kHz 单声道 16-bit)
    QStringList ffmpegArgs = {
        "-y", "-i", srcFilePath,
        "-f", "s16le", "-ar", "24000", "-ac", "1",
        pcmFile.fileName()
    };
    QProcess ffmpeg;
    ffmpeg.start(ffmpegPath, ffmpegArgs);
    if (!ffmpeg.waitForFinished(30000)) {
        qWarning() << "ffmpeg 超时";
        ffmpeg.kill();
        return {};
    }
    if (ffmpeg.exitCode() != 0) {
        qWarning() << "ffmpeg 执行失败，退出码:" << ffmpeg.exitCode()
            << "错误输出:" << ffmpeg.readAllStandardError();
        return {};
    }

    // 5. 执行 silk_v3_encoder：PCM -> Silk
    QStringList encoderArgs = {
        pcmFile.fileName(), silkFile.fileName(), "-tencent"
    };
    QProcess encoder;
    encoder.start(encoderPath, encoderArgs);
    if (!encoder.waitForFinished(30000)) {
        qWarning() << "silk 编码器超时";
        encoder.kill();
        return {};
    }
    if (encoder.exitCode() != 0) {
        qWarning() << "silk 编码失败，退出码:" << encoder.exitCode()
            << "错误输出:" << encoder.readAllStandardError();
        return {};
    }

    // 6. 读取生成的 Silk 数据
    if (!silkFile.open()) {
        qWarning() << "无法打开 Silk 文件读取";
        return {};
    }
    QByteArray silkData = silkFile.readAll();
    silkFile.close();

    if (silkData.isEmpty()) {
        qWarning() << "转换出的 Silk 数据为空";
        return {};
    }

    return silkData;
}



QString convertAudioToSilk(const QString &srcFilePath)
{

    if (!QFile::exists(srcFilePath)) {
        qWarning() << "源文件不存在:" << srcFilePath;
        return {};
    }

    QFileInfo fileInfo(srcFilePath);
    if (fileInfo.size() < 1024 * 1024) {
        return srcFilePath;
    }
    QString ffmpegPath = QDir(ffmpegdiv).filePath("ffmpeg.exe");
    QString silkFilePath = srcFilePath + ".m4a";
    QStringList ffmpegArgs = {
        "-y", "-i", srcFilePath,
        "-vn",                     // 忽略视频流/封面图
        "-c:a", "aac",             // 使用AAC编码
        "-b:a", "32k",             // 码率32kbps（可调整，如24k/16k）
        "-ar", "24000",            // 采样率24kHz
        "-ac", "1",                // 单声道
        silkFilePath
    };
    QProcess ffmpeg;
    ffmpeg.start(ffmpegPath, ffmpegArgs);
    if (!ffmpeg.waitForFinished(30000)) {
        AppendEventLog( "ffmpeg 超时");
        ffmpeg.kill();
        return srcFilePath;
    }
    if (ffmpeg.exitCode() != 0) {
        AppendEventLog("ffmpeg 执行失败，错误输出:" + ffmpeg.readAllStandardError());
        return srcFilePath;
    }
    return silkFilePath;
}