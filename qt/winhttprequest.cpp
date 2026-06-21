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

#include "WinHttpRequest.h"
#include <windows.h>
#include <winhttp.h>
#include <QUrl>
#include <QFile>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QtDebug>

#pragma comment(lib, "winhttp.lib")

static LPCWSTR toWChar(const QString& str)
{
    static thread_local std::vector<wchar_t> buffer;
    buffer.resize(str.size() + 1);
    buffer[str.toWCharArray(buffer.data())] = 0;
    return buffer.data();
}

WinHttpRequest::WinHttpRequest() = default;
WinHttpRequest::~WinHttpRequest() { /* 句柄在exec结束后已关闭 */ }

bool WinHttpRequest::parseUrl(const QString& url, QString& host, int& port, QString& path, bool& isHttps) const
{
    QUrl qurl(url);
    if (!qurl.isValid()) return false;
    host = qurl.host();
    port = qurl.port(isHttps ? 443 : 80);
    isHttps = (qurl.scheme() == "https");
    path = qurl.path();
    if (qurl.hasQuery()) path += "?" + qurl.query();
    if (path.isEmpty()) path = "/";
    return true;
}

QString WinHttpRequest::buildHeadersString() const
{
    QString headers;
    for (auto it = m_headers.begin(); it != m_headers.end(); ++it) {
        headers += it.key() + ": " + it.value() + "\r\n";
    }
    if (!m_cookie.isEmpty() && !headers.contains("Cookie:", Qt::CaseInsensitive)) {
        headers += "Cookie: " + m_cookie + "\r\n";
    }
    // 基础必要头部（如果用户未提供）
    if (!headers.contains("Accept:", Qt::CaseInsensitive))
        headers += "Accept: */*\r\n";
    if (!headers.contains("Connection:", Qt::CaseInsensitive))
        headers += "Connection: Keep-Alive\r\n";
    if (!headers.contains("User-Agent:", Qt::CaseInsensitive))
        headers += "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n";
    return headers;
}
bool WinHttpRequest::parseUrlRaw(const QString& url, QString& host, int& port, QString& path, bool& isHttps) {
    QString u = url;
    if (u.startsWith("https://", Qt::CaseInsensitive)) {
        isHttps = true;
        u = u.mid(8);
    } else if (u.startsWith("http://", Qt::CaseInsensitive)) {
        isHttps = false;
        u = u.mid(7);
    } else {
        return false;
    }
    int slashPos = u.indexOf('/');
    QString hostPort = (slashPos == -1) ? u : u.left(slashPos);
    path = (slashPos == -1) ? "/" : u.mid(slashPos);
    if (hostPort.contains(':')) {
        QStringList hp = hostPort.split(':');
        host = hp[0];
        port = hp[1].toInt();
    } else {
        host = hostPort;
        port = isHttps ? 443 : 80;
    }
    return true;
}
bool WinHttpRequest::sendRequest(const QString& url) {
    // 1. 使用原始 URL 解析（避免 QUrl 二次编码）
    QString host, path;
    int port;
    bool isHttps;
    if (!parseUrlRaw(url, host, port, path, isHttps)) {
        m_errorString = "Invalid URL";
        return false;
    }

    // 2. 自动补全必要的请求头（如果用户未设置）
    auto hasHeader = [this](const QString& name) -> bool {
        for (auto it = m_headers.begin(); it != m_headers.end(); ++it) {
            if (it.key().compare(name, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    };

    if (!hasHeader("Accept"))
        m_headers["Accept"] = "*/*";
    if (!hasHeader("Accept-Language"))
        m_headers["Accept-Language"] = "zh-CN,zh;q=0.9,en-US;q=0.8,en;q=0.7,en-GB;q=0.6";
    if (!hasHeader("User-Agent"))
        m_headers["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36 Edg/124.0.0.0";
    if (!hasHeader("Connection"))
        m_headers["Connection"] = "Keep-Alive";
    if (m_method == Post && !hasHeader("Content-Type"))
        m_headers["Content-Type"] = "application/x-www-form-urlencoded";
    // 如果设置了 Cookie，强制添加（或覆盖）
    if (!m_cookie.isEmpty())
        m_headers["Cookie"] = m_cookie;

    // 可选：添加 Referer（如果需要）
    if (!hasHeader("Referer") && !m_url.isEmpty())
        m_headers["Referer"] = m_url;

    // 3. 构建请求头字符串
    QString headerStr = buildHeadersString(); // 你需要调整 buildHeadersString 使用 m_headers
    std::wstring headersW = headerStr.toStdWString();
    LPCWSTR headersPtr = headersW.c_str();
    DWORD headersLen = static_cast<DWORD>(headersW.length());

    // 4. 初始化 WinHTTP
    HINTERNET hSession = WinHttpOpen(L"WinHttpClient/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (!hSession) {
        m_errorString = "WinHttpOpen failed";
        return false;
    }

    // 设置超时（毫秒）
    DWORD timeout = static_cast<DWORD>(m_timeoutMs);
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    // 启用 TLS 1.2 / 1.3（避免服务器拒绝旧协议）
    DWORD secureFlags = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS, &secureFlags, sizeof(secureFlags));

    HINTERNET hConnect = WinHttpConnect(hSession, toWChar(host), port, 0);
    if (!hConnect) {
        m_errorString = "WinHttpConnect failed";
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 请求方法映射
    static const wchar_t* methodStrs[] = {
        L"GET", L"POST", L"HEAD", L"PUT",
        L"DELETE", L"OPTIONS", L"TRACE", L"CONNECT"
    };
    LPCWSTR methodW = methodStrs[static_cast<int>(m_method)];
    DWORD dwFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    LPCWSTR accept[] = { L"*/*", nullptr };
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, methodW, toWChar(path),
                                            nullptr, nullptr, accept, dwFlags);
    if (!hRequest) {
        m_errorString = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 忽略证书错误（调试用）
    if (!m_verifyCert && isHttps) {
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                      SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                      SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
    }

    // 请求体
    LPCVOID bodyPtr = m_body.isEmpty() ? WINHTTP_NO_REQUEST_DATA : m_body.constData();
    DWORD bodySize = static_cast<DWORD>(m_body.size());

    // 发送请求
    BOOL bResult = WinHttpSendRequest(hRequest,
                                      headersPtr,
                                      headersLen,
                                      const_cast<LPVOID>(bodyPtr),
                                      bodySize,
                                      bodySize,
                                      0);
    if (!bResult) {
        DWORD err = GetLastError();
        m_errorString = QString("WinHttpSendRequest failed. Error: %1").arg(err);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 接收响应
    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        m_errorString = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 读取响应头（获取状态码和原始头）
    DWORD dwSize = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                        nullptr, nullptr, &dwSize, nullptr);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<wchar_t> buffer(dwSize / sizeof(wchar_t) + 1);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                nullptr, buffer.data(), &dwSize, nullptr)) {
            m_responseHeaders = QString::fromWCharArray(buffer.data());
            QRegularExpression re("HTTP/\\d\\.\\d\\s+(\\d+)");
            QRegularExpressionMatch match = re.match(m_responseHeaders);
            if (match.hasMatch())
                m_statusCode = match.captured(1).toInt();
        }
    }

    // 读取响应体
    QByteArray body;
    char buf[65536];
    DWORD read;
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &read) && read > 0) {
        body.append(buf, read);
    }
    m_resultBody = body;

    // 清理
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    m_hRequest = m_hConnect = m_hSession = nullptr;

    return (m_statusCode >= 200 && m_statusCode < 300);
}
bool WinHttpRequest::exec()
{
    return sendRequest(m_url);
}

// 链式实现
WinHttpRequest& WinHttpRequest::setUrl(const QString& url) { m_url = url; return *this; }
WinHttpRequest& WinHttpRequest::setMethod(Method method) { m_method = method; return *this; }
WinHttpRequest& WinHttpRequest::setBody(const QByteArray& body) { m_body = body; return *this; }
WinHttpRequest& WinHttpRequest::setBody(const QString& text) { m_body = text.toUtf8(); return *this; }
WinHttpRequest& WinHttpRequest::addHeader(const QString& name, const QString& value) { m_headers[name] = value; return *this; }
WinHttpRequest& WinHttpRequest::setContentType(const QString& type) { return addHeader("Content-Type", type); }
WinHttpRequest& WinHttpRequest::setTimeout(int msecs) { m_timeoutMs = msecs; return *this; }
WinHttpRequest& WinHttpRequest::setCookie(const QString& cookie) { m_cookie = cookie; return *this; }
WinHttpRequest& WinHttpRequest::setVerifyCertificate(bool verify) { m_verifyCert = verify; return *this; }

QByteArray WinHttpRequest::body() const { return m_resultBody; }
int WinHttpRequest::statusCode() const { return m_statusCode; }
QString WinHttpRequest::errorString() const { return m_errorString; }
QString WinHttpRequest::responseHeaders() const { return m_responseHeaders; }