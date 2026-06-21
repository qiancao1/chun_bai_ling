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

#ifndef SCREENSHOTSYNCCLIENT_H
#define SCREENSHOTSYNCCLIENT_H

#include <QObject>
#include <QPixmap>

class ScreenshotSyncClient : public QObject
{
    Q_OBJECT
public:
    explicit ScreenshotSyncClient(QObject *parent = nullptr);

    // 同步截图 HTML，返回 QPixmap，如果失败则返回空
    QByteArray captureHtmlSync(const QString &html, int width = 800, int height = 600, int timeoutMs = 30000);
    // 同步截图 URL
    QByteArray captureUrlSync(const QString &url, int width = 800, int height = 600, int timeoutMs = 30000);

private:
    QString m_serverUrl;
};

#endif // SCREENSHOTSYNCCLIENT_H