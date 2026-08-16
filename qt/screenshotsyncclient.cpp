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

#include "ScreenshotSyncClient.h"
#include "cpphighlighter.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>

#include <QTimer>
#include "netmanager.h"

ScreenshotSyncClient::ScreenshotSyncClient(QObject *parent)
    : QObject(parent),
    m_serverUrl("http://127.0.0.1:8080/screenshot")
{
}

QByteArray ScreenshotSyncClient::captureHtmlSync(const QString &html, int width, int height, int timeoutMs)
{
    if(width<=0) width=400;
    if(height<=0) height=400;
    QString url = QString(m_serverUrl+"?width=%1&height=%2").arg(width).arg(height);
    auto f = NetManager::instance()->post(url,html.toUtf8(),QHash<QString,QString>(),timeoutMs);
    return f.get();
}

QByteArray ScreenshotSyncClient::captureUrlSync(const QString &url, int width, int height, int timeoutMs)
{
    if(width<=0) width=400;
    if(height<=0) height=400;
    QString url2 = QString(m_serverUrl+"?width=%1&height=%2&url=%3").arg(width).arg(height).arg(url);
    auto f = NetManager::instance()->post(url2,QByteArray(),QHash<QString,QString>(),timeoutMs);
    return f.get();
}


#include <QTextDocument>
#include <QPainter>
#include <QUuid>



QString renderInThread(const QString &htmlContent,int width = 400) {
    if(htmlContent.isEmpty()) return QString();
    if(width<=0)
        width=400;
    QTextDocument doc;
    CppHighlighter highlighter(&doc);
    doc.setDefaultStyleSheet(
        "p, h1, h2, h3, h4, ul, ol, blockquote { margin: 0; padding: 0; } "
        "img { vertical-align: middle; margin: 0; padding: 0; }"
        "pre { background: #f4f4f4; padding: 10px; border-radius: 4px; }" // 代码块背景
        );

    QString text = htmlContent;
    if (text.trimmed().startsWith("<!DOCTYPE html>", Qt::CaseInsensitive)) {
        doc.setHtml(htmlContent);
    } else {
        text.replace("\n","\n\n");
        doc.setMarkdown(text);
    }
    doc.setTextWidth(width);
    doc.setDocumentMargin(10);
    QSizeF size = doc.size();
    if (size.isEmpty()) return QString();
    QImage image(size.width(), size.height(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    doc.drawContents(&painter);
    painter.end();
    QString path = QString("tmp/image/%1.png").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    image.save(path);
    return path;
}
