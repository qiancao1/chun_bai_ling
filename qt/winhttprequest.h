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

#ifndef WINHTTPREQUEST_H
#define WINHTTPREQUEST_H

#include <QString>
#include <QByteArray>
#include <QMap>

class WinHttpRequest
{
public:
    enum Method { Get, Post, Head, Put, Delete, Options, Trace, Connect };

    WinHttpRequest();
    ~WinHttpRequest();

    // 链式设置接口
    WinHttpRequest& setUrl(const QString& url);
    WinHttpRequest& setMethod(Method method);
    WinHttpRequest& setBody(const QByteArray& body);
    WinHttpRequest& setBody(const QString& text);
    WinHttpRequest& addHeader(const QString& name, const QString& value);
    WinHttpRequest& setContentType(const QString& contentType);
    WinHttpRequest& setTimeout(int msecs);          // 总超时（毫秒）
    WinHttpRequest& setCookie(const QString& cookie);
    WinHttpRequest& setVerifyCertificate(bool verify); // 是否验证证书
    bool parseUrlRaw(const QString& url, QString& host, int& port, QString& path, bool& isHttps);
    // 执行请求（同步阻塞）
    bool exec();

    // 获取结果
    QByteArray body() const;
    int statusCode() const;
    QString errorString() const;
    QString responseHeaders() const;

private:
    bool sendRequest(const QString& url);
    bool parseUrl(const QString& url, QString& host, int& port, QString& path, bool& isHttps) const;
    QString buildHeadersString() const;

    // 参数
    QString m_url;
    Method m_method = Get;
    QByteArray m_body;
    QMap<QString, QString> m_headers;
    int m_timeoutMs = 30000;
    QString m_cookie;
    bool m_verifyCert = true;

    // 结果
    QByteArray m_resultBody;
    int m_statusCode = 0;
    QString m_errorString;
    QString m_responseHeaders;

    // WinHTTP 句柄
    void* m_hSession = nullptr;
    void* m_hConnect = nullptr;
    void* m_hRequest = nullptr;
};

#endif // WINHTTPREQUEST_H