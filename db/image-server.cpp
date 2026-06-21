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

// http_image_server.cpp
// 本地 HTTP 图床服务，支持上传和读取，上传时基于内容 SHA-256 哈希命名并自动去重
// 修改：IP 白名单 -> Token 认证；新增 /remote_upload 接口

#include "global.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrlQuery>
#include <QUrl>
#include <QByteArray>
#include <QString>
#include <QTextStream>
#include <QDebug>
#include <QDir>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QSet>
#include <QCryptographicHash>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QFileInfo>
#include <QDebug>

// 全局服务器指针和端口
QTcpServer *g_server = nullptr;
static quint16 g_port = 0;
static QString g_ip = "127.0.0.1";
static QString g_allowedReadDir = QDir::current().absolutePath() + "/uploads";
// Token 认证相关
static QSet<QString> g_validTokens;      // 存储合法的 Token
static bool g_tokenAuthEnabled = false;  // 若为 true 且列表非空，才要求 Token

void setUploadTokens(const QStringList &tokens)
{
    g_validTokens.clear();
    for (const QString &token : tokens) {
        g_validTokens.insert(token);
    }
    g_tokenAuthEnabled = true;
}

void addUploadToken(const QString &token)
{
    g_validTokens.insert(token);
    g_tokenAuthEnabled = true;
}
static bool isPathInAllowedDir(const QString &requestedPath)
{
    QDir allowed(g_allowedReadDir);
    QString absoluteAllowed = allowed.absolutePath();
    QFileInfo fi(requestedPath);
    QString absoluteRequest = QDir::cleanPath(fi.absoluteFilePath());
    // 绝对路径必须位于 allowed 目录下，或者是 allowed 目录本身
    return absoluteRequest.startsWith(absoluteAllowed + QDir::separator()) ||
           absoluteRequest == absoluteAllowed;
}
// 从请求中提取 Token：优先从 Authorization 头，其次从查询参数 token=
static QString extractToken(const QByteArray &headers, const QByteArray &pathQuery)
{
    // 1. 检查 Authorization: Bearer <token>
    QRegularExpression authRe("Authorization: Bearer (.+)", QRegularExpression::CaseInsensitiveOption);
    auto match = authRe.match(QString::fromUtf8(headers));
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    // 2. 检查查询参数 ?token=xxx
    QUrl url(QString::fromUtf8(pathQuery));
    QUrlQuery query(url);
    QString tokenFromQuery = query.queryItemValue("token");
    if (!tokenFromQuery.isEmpty()) {
        return tokenFromQuery;
    }

    return QString();
}

static bool isTokenValid(const QString &token)
{
    if (!g_tokenAuthEnabled || g_validTokens.isEmpty())
        return false;   // 未配置 Token 列表时默认拒绝（提高安全性）
    return g_validTokens.contains(token);
}

static int getContentLength(const QByteArray &headers)
{
    QRegularExpression re("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption);
    auto match = re.match(headers);
    if (match.hasMatch())
        return match.captured(1).toInt();
    return -1;
}

static QString getBoundary(const QByteArray &headers)
{
    QRegularExpression re("boundary=(.+)", QRegularExpression::CaseInsensitiveOption);
    auto match = re.match(headers);
    if (match.hasMatch())
        return match.captured(1).trimmed();
    return QString();
}

// 解析 multipart 文件数据，同时返回原始文件名和文件内容
static QByteArray extractFileFromMultipart(const QByteArray &body, const QString &boundary, QString &outFileName)
{
    if (boundary.isEmpty())
        return QByteArray();

    QString boundaryStr = "--" + boundary;
    QByteArray boundaryBytes = boundaryStr.toUtf8();
    QByteArray endBoundaryBytes = (boundaryStr + "--").toUtf8();

    int start = body.indexOf(boundaryBytes);
    if (start < 0)
        return QByteArray();

    int partStart = start + boundaryBytes.size();
    if (body.mid(partStart, 2) == "\r\n")
        partStart += 2;

    int partEnd = body.indexOf(boundaryBytes, partStart);
    if (partEnd < 0)
        partEnd = body.indexOf(endBoundaryBytes, partStart);
    if (partEnd < 0)
        return QByteArray();

    QByteArray part = body.mid(partStart, partEnd - partStart);
    int headerEnd = part.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return QByteArray();

    QByteArray headersPart = part.left(headerEnd);
    QByteArray fileData = part.mid(headerEnd + 4);
    while (fileData.endsWith("\r\n"))
        fileData.chop(2);

    QRegularExpression filenameRe("filename=\"([^\"]+)\"", QRegularExpression::CaseInsensitiveOption);
    auto match = filenameRe.match(QString::fromUtf8(headersPart));
    if (match.hasMatch())
        outFileName = match.captured(1);

    return fileData;
}

static void sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType, const QByteArray &body)
{
    QByteArray response;
    response += QString("HTTP/1.1 %1 OK\r\n").arg(statusCode).toUtf8();
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

static void sendErrorResponse(QTcpSocket *socket, int code, const QString &message)
{
    QByteArray body = message.toUtf8();
    sendResponse(socket, code, "text/plain; charset=utf-8", body);
}

static void handleGet(QTcpSocket *socket, const QByteArray &pathQuery)
{
    QUrl url(QString::fromUtf8(pathQuery));
    QUrlQuery query(url);
    QString filePath = query.queryItemValue("path");

    if (filePath.isEmpty()) {
        sendErrorResponse(socket, 400, "Missing 'path' parameter");
        return;
    }

    // 安全检查：只能访问 uploads 目录下的文件
    if (!isPathInAllowedDir(filePath)) {
        sendErrorResponse(socket, 403, "Access denied: only files under uploads/ are allowed");
        return;
    }

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        sendErrorResponse(socket, 404, "File not found");
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        sendErrorResponse(socket, 500, "Failed to open file");
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QMimeDatabase mimeDb;
    QString mimeType = mimeDb.mimeTypeForFile(filePath).name();
    if (mimeType.isEmpty())
        mimeType = "application/octet-stream";

    sendResponse(socket, 200, mimeType.toUtf8(), data);
}
// 计算 MD5 哈希（十六进制小写）
static QString calculateMd5(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex();
}

// 核心上传逻辑：保存文件并返回 JSON 响应
static void processUpload(QTcpSocket *socket, const QByteArray &headers, const QByteArray &body, const QByteArray &pathQuery)
{
    // 1. 验证 Token
    QString token = extractToken(headers, pathQuery);
    if (!isTokenValid(token)) {
        sendErrorResponse(socket, 401, "Unauthorized: valid token required");
        return;
    }


    QDir dir;
    if (!dir.exists("uploads"))
        dir.mkdir("uploads");

    // 检查 Content-Type 是否为 multipart/form-data
    QString contentType;
    QRegularExpression ctRe("Content-Type: (.+)", QRegularExpression::CaseInsensitiveOption);
    auto match = ctRe.match(QString::fromUtf8(headers));
    if (match.hasMatch())
        contentType = match.captured(1);

    if (!contentType.contains("multipart/form-data")) {
        sendErrorResponse(socket, 400, "Expected multipart/form-data");
        return;
    }

    QString boundary = getBoundary(headers);
    if (boundary.isEmpty()) {
        sendErrorResponse(socket, 400, "Missing boundary");
        return;
    }

    QString originalFileName;
    QByteArray fileData = extractFileFromMultipart(body, boundary, originalFileName);
    if (fileData.isEmpty()) {
        sendErrorResponse(socket, 400, "No file data found");
        return;
    }

    // 计算哈希
    QString hashHex = calculateMd5(fileData);

    // 提取扩展名
    QString extension;
    if (!originalFileName.isEmpty()) {
        QFileInfo fi(originalFileName);
        QString suffix = fi.suffix();
        if (!suffix.isEmpty())
            extension = "." + suffix;
    }

    QString savePath = QDir::current().absolutePath() + "/uploads/" + hashHex + extension;

    // 去重保存
    bool fileAlreadyExists = QFile::exists(savePath);
    if (!fileAlreadyExists) {
        QFile newFile(savePath);
        if (!newFile.open(QIODevice::WriteOnly)) {
            sendErrorResponse(socket, 500, "Failed to save file");
            return;
        }
        newFile.write(fileData);
        newFile.close();
    }

    // 构造访问 URL
    QString accessUrl = QString("http://%1:%2/?path=%3").arg(g_ip).arg(g_port).arg(QString::fromUtf8(QUrl::toPercentEncoding(savePath)));
    QJsonObject respObj;
    respObj["url"] = accessUrl;
    respObj["hash"] = hashHex;
    QByteArray respBody = QJsonDocument(respObj).toJson(QJsonDocument::Compact);
    setA->incrementTokenUsage(token);
    sendResponse(socket, 200, "application/json", respBody);
}

// 原有 /upload 接口（改用 Token 认证）
static void handlePost(QTcpSocket *socket, const QByteArray &headers, const QByteArray &body, const QByteArray &pathQuery)
{
    processUpload(socket, headers, body, pathQuery);
}

// 新增远程上传接口 /remote_upload（与 /upload 逻辑完全相同，只是路径不同）
static void handleRemotePost(QTcpSocket *socket, const QByteArray &headers, const QByteArray &body, const QByteArray &pathQuery)
{
    processUpload(socket, headers, body, pathQuery);
}
static void handleUploadByPath(QTcpSocket *socket, const QByteArray &headers, const QByteArray &body, const QByteArray &pathQuery)
{
    // 1. 可选：只允许本机访问
    if (!socket->peerAddress().isLoopback()) {
        sendErrorResponse(socket, 403, "Only localhost is allowed");
        return;
    }

    // 2. 解析 JSON body
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        sendErrorResponse(socket, 400, "Invalid JSON, expected {\"path\": \"...\"}");
        return;
    }
    QJsonObject obj = doc.object();
    if (!obj.contains("path") || !obj["path"].isString()) {
        sendErrorResponse(socket, 400, "Missing 'path' field");
        return;
    }
    QString localPath = obj["path"].toString();

    // 3. 读取本地文件
    QFileInfo srcInfo(localPath);
    if (!srcInfo.exists() || !srcInfo.isFile()) {
        sendErrorResponse(socket, 404, "Local file not found");
        return;
    }
    QFile srcFile(localPath);
    if (!srcFile.open(QIODevice::ReadOnly)) {
        sendErrorResponse(socket, 500, "Failed to open local file");
        return;
    }
    QByteArray fileData = srcFile.readAll();
    srcFile.close();

    // 4. 保存到 uploads 目录（去重）
    QString hashHex = calculateMd5(fileData);
    QString extension = srcInfo.suffix();
    if (!extension.isEmpty())
        extension = "." + extension;
    QString savePath = QDir::current().absolutePath() + "/uploads/" + hashHex + extension;

    if (!QFile::exists(savePath)) {
        QFile destFile(savePath);
        if (!destFile.open(QIODevice::WriteOnly)) {
            sendErrorResponse(socket, 500, "Failed to save file");
            return;
        }
        destFile.write(fileData);
        destFile.close();
    }

    // 5. 返回 URL
    QString accessUrl = QString("http://%1:%2/?path=%3")
                            .arg(g_ip)
                            .arg(g_port)
                            .arg(QString::fromUtf8(QUrl::toPercentEncoding(savePath)));
    QJsonObject respObj;
    respObj["url"] = accessUrl;
    respObj["hash"] = hashHex;
    QByteArray respBody = QJsonDocument(respObj).toJson(QJsonDocument::Compact);
    sendResponse(socket, 200, "application/json", respBody);
}// 使用 Qt 的 HTTP 客户端直接调用 /upload_by_path




QString uploadImageByPath(const QString &serverUrl, const QString &localPath, int timeoutMs, QString *errorMsg)
{
    // 1. 检查本地文件是否存在且可读
    QFileInfo fi(localPath);
    if (!fi.exists()) {
        if (errorMsg) *errorMsg = QString("Local file does not exist: %1").arg(localPath);
        return QString();
    }
    if (!fi.isFile()) {
        if (errorMsg) *errorMsg = QString("Path is not a file: %1").arg(localPath);
        return QString();
    }
    if (!fi.isReadable()) {
        if (errorMsg) *errorMsg = QString("File is not readable: %1").arg(localPath);
        return QString();
    }

    // 2. 构造上传 URL
    QUrl url(serverUrl);
    QString path = url.path();
    if (!path.endsWith('/'))
        path += '/';
    path += "upload_by_path";
    url.setPath(path);

    // 3. 构造 JSON 请求体
    QJsonObject reqObj;
    reqObj["path"] = localPath;
    QByteArray jsonData = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);

    // 4. 设置 Qt 请求
    QNetworkRequest request;
    request.setUrl(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // 忽略证书错误（与原代码 setVerifyCertificate(false) 一致）
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    // 5. 发送请求（同步阻塞）
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, jsonData);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    // 6. 处理响应
    QByteArray responseBody;
    QString reqErrorString;
    bool ok = false;

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        ok = true;
        responseBody = reply->readAll();
    } else {
        if (!timer.isActive()) {
            reqErrorString = "Request timeout";
        } else {
            reqErrorString = reply->errorString();
        }
        // 尝试读取可能的部分响应
        responseBody = reply->readAll();
    }
    reply->deleteLater();

    if (!ok) {
        if (errorMsg) *errorMsg = reqErrorString;
        return QString();
    }

    // 7. 解析 JSON 响应
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = QString("JSON parse error: %1").arg(parseErr.errorString());
        return QString();
    }
    QString urlResult = doc.object().value("url").toString();
    if (urlResult.isEmpty()) {
        if (errorMsg) *errorMsg = "Response missing 'url' field";
        return QString();
    }
    return urlResult;
}
// 全局请求处理（带缓冲区）
static void handleRequest(QTcpSocket *socket)
{
    static QHash<QTcpSocket*, QByteArray> buffers;
    buffers[socket].append(socket->readAll());

    QByteArray &buffer = buffers[socket];
    int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd == -1)
        return;

    QByteArray headerPart = buffer.left(headerEnd);
    int contentLength = getContentLength(headerPart);
    int totalNeeded = headerEnd + 4 + contentLength;
    if (buffer.size() < totalNeeded)
        return;

    QByteArray requestData = buffer.left(totalNeeded);
    buffer.remove(0, totalNeeded);

    QList<QByteArray> lines = requestData.split('\n');
    if (lines.isEmpty()) {
        sendErrorResponse(socket, 400, "Bad request");
        socket->disconnectFromHost();
        buffers.remove(socket);
        return;
    }
    QByteArray requestLine = lines[0].trimmed();
    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendErrorResponse(socket, 400, "Malformed request line");
        socket->disconnectFromHost();
        buffers.remove(socket);
        return;
    }

    QByteArray method = parts[0];
    QByteArray path = parts[1];

    int bodyStart = requestData.indexOf("\r\n\r\n") + 4;
    QByteArray body;
    if (bodyStart > 0 && bodyStart < requestData.size())
        body = requestData.mid(bodyStart);

    if (method == "GET") {
        handleGet(socket, path);

    }else if (method == "POST") {
        if (path.startsWith("/upload")) {            // 原有的 /upload 或 /remote_upload
            handlePost(socket, headerPart, body, path);
        } else if (path == "/remote_upload" || path == "/remote_upload/") {
            handleRemotePost(socket, headerPart, body, path);
        } else if (path == "/upload_by_path" || path == "/upload_by_path/") {
            handleUploadByPath(socket, headerPart, body, path);   // 新增
        } else {
            sendErrorResponse(socket, 404, "Not Found");
        }
    } else {
        sendErrorResponse(socket, 404, "Not Found");
    }

    socket->disconnectFromHost();
    buffers.remove(socket);
}
// 新增处理函数
// TCP 服务器子类
class HttpImageServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit HttpImageServer(QObject *parent = nullptr) : QTcpServer(parent) {}

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        QTcpSocket *socket = new QTcpSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            delete socket;
            return;
        }
        connect(socket, &QTcpSocket::readyRead, this, [socket]() {
            handleRequest(socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
};

// 本地直接保存文件的辅助函数（供内部调用）
QString upload(const QString &path)
{
    return QString("http://%1:%2/?path=%3").arg(g_ip).arg(g_port).arg(QString::fromUtf8(QUrl::toPercentEncoding(path)));
}

QString upload(const QByteArray &data)
{
    QString hashHex = calculateMd5(data);
    QString savePath = QDir::current().absolutePath() + "/uploads/" + hashHex + ".png";
    bool fileAlreadyExists = QFile::exists(savePath);
    if (!fileAlreadyExists) {
        QFile newFile(savePath);
        if (!newFile.open(QIODevice::WriteOnly)) {
            return QString();
        }
        newFile.write(data);
        newFile.close();
    }
    return QString("http://%1:%2/?path=%3").arg(g_ip).arg(g_port).arg(QString::fromUtf8(QUrl::toPercentEncoding(savePath)));
}

// 对外启动函数
bool startImageServer(const QString &ip, quint16 port)
{
    if (g_server) {
        qDebug() << "Server already running on port" << g_port;
        return false;
    }
    if (!QDir().mkpath(g_allowedReadDir)) {

        return false;
    }
    g_server = new HttpImageServer();
    if (!g_server->listen(QHostAddress::Any, port)) {
        qDebug() << "Failed to start server on port" << port;
        delete g_server;
        g_server = nullptr;
        return false;
    }
    g_port = port;
    g_ip = ip;
    return true;
}


void stopImageServer()
{
    if (!g_server) {
        return;
    }
    g_server->close();
    delete g_server;
    g_server = nullptr;
}



static QString generateBoundary()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).replace("-", "");
}

static QByteArray buildMultipart(const QString& fieldName, const QString& fileName, const QByteArray& fileData, const QString& boundary)
{
    QByteArray body;
    body.append("--" + boundary.toUtf8() + "\r\n");
    body.append("Content-Disposition: form-data; name=\"" + fieldName.toUtf8() + "\"; filename=\"" + fileName.toUtf8() + "\"\r\n");
    body.append("Content-Type: application/octet-stream\r\n\r\n");
    body.append(fileData);
    body.append("\r\n");
    body.append("--" + boundary.toUtf8() + "--\r\n");
    return body;
}

/**
 * 同步上传图片到图床
 * @param serverUrl  图床上传地址，例如 "http://127.0.0.1:8080/remote_upload"
 * @param token       认证 token
 * @param filePath    本地图片路径
 * @param timeoutMs   超时时间（毫秒），默认30秒
 * @param errorMsg    可选，输出错误信息
 * @return 成功返回图片 URL，失败返回空字符串
 */
QString uploadImageSync(const QString& serverUrl, const QString &token, const QString& filePath,
                        int timeoutMs, QString* errorMsg)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMsg) *errorMsg = QString("Cannot open file: %1").arg(file.errorString());
        return QString();
    }
    QByteArray fileData = file.readAll();
    file.close();
    if (fileData.isEmpty()) {
        if (errorMsg) *errorMsg = "File is empty";
        return QString();
    }

    QString boundary = generateBoundary();
    QFileInfo fi(filePath);
    QString fileName = fi.fileName();
    QByteArray postData = buildMultipart("file", fileName, fileData, boundary);
    QString contentType = QString("multipart/form-data; boundary=%1").arg(boundary);

    // 3. 构造请求 URL（将 token 作为查询参数）
    QUrl urlObj(serverUrl);
    QUrlQuery query(urlObj);
    query.addQueryItem("token", token);
    urlObj.setQuery(query);
    QString finalUrl = urlObj.toString();

    // 4. 设置 Qt 请求
    QNetworkRequest request;
    request.setUrl(QUrl(finalUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    // 忽略 SSL 证书错误（与原 setVerifyCertificate(false) 一致）
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConfig);

    // 5. 发送请求（同步阻塞）
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, postData);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    // 6. 收集响应结果
    bool ok = false;
    int status = 0;
    QByteArray responseBody;
    QString reqErrorString;

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        ok = true;
        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        responseBody = reply->readAll();
    } else {
        if (!timer.isActive()) {
            reqErrorString = "Request timeout";
        } else {
            reqErrorString = reply->errorString();
        }
        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        responseBody = reply->readAll();
    }
    reply->deleteLater();

    if (!ok || status != 200) {
        if (errorMsg) {
            if (!responseBody.isEmpty())
                *errorMsg = QString("HTTP %1: %2").arg(status).arg(QString::fromUtf8(responseBody));
            else
                *errorMsg = reqErrorString;
        }
        return QString();
    }

    // 7. 解析 JSON 获取 url 字段
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(responseBody, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        if (errorMsg) *errorMsg = QString("JSON parse error: %1").arg(parseErr.errorString());
        return QString();
    }
    QString url = doc.object().value("url").toString();
    if (url.isEmpty()) {
        if (errorMsg) *errorMsg = "Response missing 'url' field";
        return QString();
    }
    return url;
}

#include "image-server.moc"