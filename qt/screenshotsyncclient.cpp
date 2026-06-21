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
#include <QEventLoop>
#include <QTimer>
#include <QDebug>

ScreenshotSyncClient::ScreenshotSyncClient(QObject *parent)
    : QObject(parent),
    m_serverUrl("http://127.0.0.1:8080/screenshot")
{
}

QByteArray ScreenshotSyncClient::captureHtmlSync(const QString &html, int width, int height, int timeoutMs)
{
    QUrl url(m_serverUrl);
    QUrlQuery query;
    if(width<=0) width=400;
    if(height<=0) height=400;
    query.addQueryItem("width", QString::number(width));
    query.addQueryItem("height", QString::number(height));

    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");

    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.post(request, html.toUtf8());

    // 设置超时定时器
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);

    loop.exec(); // 阻塞直到 finished 或超时
    if (timer.isActive()) {
        timer.stop(); // 正常完成
        if (reply->error() == QNetworkReply::NoError) {
            return reply->readAll();
        }else{
            qDebug() << reply->readAll();
        }
    } else {

        reply->abort();
    }

    reply->deleteLater();
    return QByteArray();
}

QByteArray ScreenshotSyncClient::captureUrlSync(const QString &url, int width, int height, int timeoutMs)
{
    QUrl requestUrl(m_serverUrl);
    QUrlQuery query;
    query.addQueryItem("url", url);
    query.addQueryItem("width", QString::number(width));
    query.addQueryItem("height", QString::number(height));
    requestUrl.setQuery(query);

    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");

    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.post(request, QByteArray());

    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (timer.isActive()) {
        timer.stop();
        if (reply->error() == QNetworkReply::NoError) {
            return reply->readAll();
        }
    } else {
        reply->abort();
    }
    reply->deleteLater();
    return QByteArray();
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
