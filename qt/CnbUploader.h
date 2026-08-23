#ifndef CNBUPLOADER_H
#define CNBUPLOADER_H

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTimer>
#include <QUrlQuery>
#include <QFileInfo>
#include <QUrl>
#include <future>
#include <qmessageauthenticationcode.h>
#include "NetManager.h"   // 请替换为实际路径
#include "global.h"

QJsonObject getUploadUrlSync(const QString &fileName, qint64 size)
{
    QString url = QString("https://api.cnb.cool/%1/-/upload/imgs").arg(g_cnb.repo);

    QHash<QString, QString> headers;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "application/json";
    headers["Authorization"] = "Bearer " + g_cnb.key;

    QJsonObject body;
    body["name"] = fileName;
    body["size"] = size;
    QByteArray jsonData = QJsonDocument(body).toJson();

    auto future = NetManager::instance()->post(url, jsonData, headers, 30000);
    try {
        QString response = future.get();   // 阻塞等待

        QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
        QJsonObject obj = doc.object();
        if (obj.contains("upload_url")) {
            return obj;
        } else {
            qWarning() << "响应缺少 upload_url 字段:" << response;
            return QJsonObject();
        }
    } catch (const std::exception &e) {
        qWarning() << "POST 请求异常:" << e.what();

        return QJsonObject();
    }
}
void DelFileSync_Cnb()
{

    if(!g_cnb.e || g_cnb.key.isEmpty() || g_cnb.repo.isEmpty()) return;
    if(g_cnb.qcjs<60) return;
    g_cnb.qcjs=0;
    QString url = QString("https://api.cnb.cool/%1/-/list-assets?page=1&page_size=5000").arg(g_cnb.repo);
    QHash<QString, QString> headers;
    headers["Accept"] = "application/vnd.cnb.api+json";
    headers["Authorization"] = "Bearer " + g_cnb.key;
    auto future = NetManager::instance()->get(url, headers, 30000);


    try {
        QString response = future.get();   // 阻塞等待

        QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
        QJsonArray arr = doc.array();
        QDateTime currentUTC = QDateTime::currentDateTimeUtc();
        int i=0;
        for (const auto &a : std::as_const(arr))
        {
            QJsonObject obj = a.toObject();
            const QString created_at = obj["created_at"].toString();
            QDateTime createdUTC = QDateTime::fromString(created_at, Qt::ISODate);

            if (createdUTC.isValid()) {
                qint64 secondsDiff = createdUTC.secsTo(currentUTC);
                if (secondsDiff >= 0 && secondsDiff < 300) {
                    continue;
                }
            }
            i++;
            const QString url2 = QString("https://api.cnb.cool/%1/-/assets/%2").arg(g_cnb.repo,obj["id"].toString());
            NetManager::instance()->Delete2(url2,QByteArray(), headers);
        }
        AppendEventLog(QString("本次删除Cnb图片：%1张").arg(i));
        return ;
    } catch (const std::exception &e) {
        qWarning() << "api.cnb.cool 请求异常:" << e.what();
    }
    return ;
}

// 同步执行 PUT 上传（使用 NetManager::put），返回最终访问链接
QString putFile(const QByteArray &fileData, const QJsonObject &uploadInfo)
{
    QString uploadUrl = uploadInfo["upload_url"].toString();
    QString token = uploadInfo["token"].toString();
    QJsonObject form = uploadInfo["form"].toObject();

    // 如果 form 非空，将参数拼接到 URL
    QUrl url(uploadUrl);
    if (!form.isEmpty()) {
        QUrlQuery query;
        for (auto it = form.begin(); it != form.end(); ++it) {
            query.addQueryItem(it.key(), it.value().toString());
        }
        url.setQuery(query);
    }

    QHash<QString, QString> headers;
    headers["Content-Type"] = "application/octet-stream";
    headers["Content-Length"] = QString::number(fileData.size());
    if (!token.isEmpty()) {
        headers["Authorization"] = "Bearer " + token;
    }

    if(!g_neiw.isEmpty()){
        auto future = NetManager::instance()->put(uploadUrl, fileData, headers, 30000);
        try {
            future.get();   // 等待上传完成（成功无异常，失败抛异常）
        } catch (const std::exception &e) {
            qWarning() << "PUT 请求异常:" << e.what();
            return QString();
        }
    }else{
        NetManager::instance()->putAsync(uploadUrl, fileData, headers, 30000,
                                         [&](const QString &response, QNetworkReply::NetworkError error) {
                                            return ;
                                         });
    }

    QJsonObject assets = uploadInfo["assets"].toObject();
    QString path = assets["path"].toString();
    QString finalUrl = path.startsWith("http") ? path : "https://cnb.cool" + path;
    return finalUrl;
}

QAtomicInt m_index;
QString uploadFileSync(const QString &filePath)
{
    if(g_cnb.repo.isEmpty() || g_cnb.key.isEmpty()) return QString();
    // 1. 读取文件
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开文件:" << filePath;
        return QString();
    }
    QByteArray fileData = file.readAll();
    int idx = m_index.fetchAndAddOrdered(1) % 10000;  // 原子递增并返回旧值
    QString fileName = QString("%1.png").arg(idx);
    QJsonObject uploadInfo = getUploadUrlSync(fileName, fileData.size());
    if (uploadInfo.isEmpty()) {
        g_cnb.errcs++;
        if(g_cnb.errcs>=16) g_cnb.e = false;

        return QString();
    }
    g_cnb.errcs=0;

    return putFile(fileData, uploadInfo);
}



QString uploadFileSync_test(const QString &filePath)
{
    if(g_cnb.repo.isEmpty() || g_cnb.key.isEmpty()) return QString();
    // 1. 读取文件
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(nullptr, "无法打开文件:" , "无法打开文件:" +filePath)  ;
        return QString();
    }
    QByteArray fileData = file.readAll();
    int idx = m_index.fetchAndAddOrdered(1) % 10000;  // 原子递增并返回旧值
    QString fileName = QString("%1.png").arg(idx);
    QJsonObject uploadInfo = getUploadUrlSync(fileName, fileData.size());
    if (uploadInfo.isEmpty()) {
        QMessageBox::warning(nullptr, "获取上传URL失败:" ,"获取上传URL失败")  ;

        return QString();
    }

    QString finalUrl = putFile(fileData, uploadInfo);

    if (finalUrl.isEmpty()) {
         QMessageBox::warning(nullptr, "put失败:" ,"put失败")  ;
        return QString();
    }

    return finalUrl;
}



// 上传函数：回调方式，支持自动删除
void uploadFileAsync(const QString& filePath,
                     std::function<void(const QString& url, const QString& error)> callback)
{

    QHash<QString, QString> headers;
    headers["Content-Type"] = "application/json";
    headers["Authorization"] = "Bearer " + g_cnb.key;   // 全局 token

    QString url = QString("https://api.cnb.cool/%1/-/upload/files").arg(g_cnb.repo);
    QFileInfo info(filePath);

    int idx = m_index.fetchAndAddOrdered(1) % 10000;  // 原子递增并返回旧值
    QString fileName = QString("%1.png").arg(idx);


    QJsonObject body;
    body["name"] = fileName;
    body["size"] = info.size();
    QByteArray jsonData = QJsonDocument(body).toJson();

    NetManager::instance()->postAsync(url, jsonData, headers, 30000,
                                      [=](const QString& response, QNetworkReply::NetworkError err) {
                                          if (err != QNetworkReply::NoError) {
                                              if (callback) callback(QString(), "获取上传URL失败: " + response);
                                              return;
                                          }

                                          QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
                                          if (doc.isNull()) {
                                              if (callback) callback(QString(), "无效的 JSON 响应");
                                              return;
                                          }
                                          QJsonObject obj = doc.object();
                                          if (obj.isEmpty()) {
                                              if (callback) callback(QString(), "响应为空");
                                              return;
                                          }

                                          QString uploadUrl = obj["upload_url"].toString();
                                          QString token = obj["token"].toString();
                                          QJsonObject assets = obj["assets"].toObject();
                                          QJsonObject form = obj["form"].toObject();

                                          // 2. 读取本地文件
                                          QFile file(filePath);
                                          if (!file.open(QIODevice::ReadOnly)) {
                                              if (callback) callback(QString(), "无法打开文件: " + filePath);
                                              return;
                                          }
                                          QByteArray fileData = file.readAll();
                                          file.close();

                                          QHash<QString, QString> putHeaders;
                                          putHeaders["Content-Type"] = "application/octet-stream";
                                          putHeaders["Content-Length"] = QString::number(fileData.size());
                                          if (!token.isEmpty())
                                              putHeaders["Authorization"] = "Bearer " + token;

                                          // 处理 form 参数（拼接到 URL query）
                                          if (!form.isEmpty()) {
                                              QUrl urlObj(uploadUrl);
                                              QUrlQuery query(urlObj);
                                              for (auto it = form.begin(); it != form.end(); ++it) {
                                                  query.addQueryItem(it.key(), it.value().toString());
                                              }
                                              urlObj.setQuery(query);
                                              uploadUrl = urlObj.toString();
                                          }

                                          // 4. 发起 PUT 异步上传
                                          NetManager::instance()->putAsync(uploadUrl, fileData, putHeaders, 30000,
                                                                           [=](const QString& putResp, QNetworkReply::NetworkError putErr) {
                                                                               if (putErr != QNetworkReply::NoError) {
                                                                                   if (callback) callback(QString(), "文件上传失败: " + putResp);
                                                                                   return;
                                                                               }

                                                                               // 5. 构造最终访问 URL
                                                                               QString path = assets["path"].toString();
                                                                               QString finalUrl = path;
                                                                               if (!finalUrl.startsWith("http"))
                                                                                   finalUrl = "https://cnb.cool" + finalUrl;
                                                                               // 7. 回调成功
                                                                               if (callback) callback(finalUrl, QString());
                                                                           });
                                      });
}


void doCnbUpload(const QByteArray &fileData, std::function<void(QString)> callback)
{
    if (!g_cnb.e) {
        callback(QString());
        return;
    }
    int idx = m_index.fetchAndAddOrdered(1) % 10000;
    QString fileName = QString("%1.png").arg(idx);

    QJsonObject uploadInfo = getUploadUrlSync(fileName, fileData.size());
    if (uploadInfo.isEmpty()) {
        callback(QString());
        return;
    }

    QString uploadUrl = uploadInfo["upload_url"].toString();
    QString token = uploadInfo["token"].toString();
    QJsonObject form = uploadInfo["form"].toObject();

    QUrl url(uploadUrl);
    if (!form.isEmpty()) {
        QUrlQuery query;
        for (auto it = form.begin(); it != form.end(); ++it)
            query.addQueryItem(it.key(), it.value().toString());
        url.setQuery(query);
    }

    QHash<QString, QString> headers;
    headers["Content-Type"] = "application/octet-stream";
    headers["Content-Length"] = QString::number(fileData.size());
    if (!token.isEmpty())
        headers["Authorization"] = "Bearer " + token;

    struct Ctx { QJsonObject uploadInfo; };
    auto ctx = std::make_shared<Ctx>();
    ctx->uploadInfo = uploadInfo;

    NetManager::instance()->putAsync(
        uploadUrl,
        fileData,
        headers,
        30000,
        [ctx, callback](const QString &response, QNetworkReply::NetworkError error) {
            if (error == QNetworkReply::NoError) {
                QJsonObject assets = ctx->uploadInfo["assets"].toObject();
                QString path = assets["path"].toString();
                QString finalUrl = path.startsWith("http") ? path : "https://cnb.cool" + path;
                callback(finalUrl);
            } else {
                callback(QString());
            }
        }
        );
}


void uploadToMhimgAsync(const QByteArray &imageData,
                        const QString &originalFileName,
                        std::function<void(QString)> callback)
{
    // 静态计数器与禁用标志（线程安全，程序重启后自动重置）
    static std::atomic<int> errorCount(0);
    static std::atomic<bool> disabled(false);

    // 如果已被禁用，直接回调空字符串
    if (disabled.load()) {
        callback(QString());
        return;
    }

    if (imageData.isEmpty()) {
        callback(QString());
        return;
    }

    // 构造 multipart 数据（与原代码相同）
    QByteArray hash = QCryptographicHash::hash(imageData, QCryptographicHash::Md5);
    QString hashHex = hash.toHex();
    QString extension = ".jpg";
    if (!originalFileName.isEmpty()) {
        int dot = originalFileName.lastIndexOf('.');
        if (dot != -1) extension = originalFileName.mid(dot);
    }
    QString fileName = hashHex + extension;
    QString boundary = "----WebKitFormBoundary" +
                       QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);

    QByteArray body;
    body.append("--" + boundary.toUtf8() + "\r\n");
    body.append("Content-Disposition: form-data; name=\"Filedata\"; filename=\"" + fileName.toUtf8() + "\"\r\n");
    body.append("Content-Type: image/jpeg\r\n\r\n");
    body.append(imageData);
    body.append("\r\n");
    body.append("--" + boundary.toUtf8() + "--\r\n");

    QString contentType = "multipart/form-data; boundary=" + boundary;
    QHash<QString,QString> headers;
    headers["User-Agent"]= "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";
    headers["Accept"]= "*/*";
    headers["Origin"]= "https://cli.im";
    headers["Referer"]= "https://cli.im/deqr/";
    headers["Sec-Fetch-Site"]= "same-site";
    headers["Sec-Fetch-Mode"]= "cors";
    headers["Content-Type"]= contentType;

    // 发起异步 POST
    NetManager::instance()->postAsync(
        "https://upload.api.cli.im/upload.php?kid=cliim",
        body,
        headers,
        30000,
        [callback](const QString &responseBody, QNetworkReply::NetworkError error) {
            // 成功时，重置错误计数并返回 URL
            auto onSuccess = [&]() {
                errorCount.store(0);
            };
            // 失败时，增加错误计数，达到阈值则禁用
            auto onFailure = [&]() {
                int newCount = errorCount.fetch_add(1) + 1;
                if (newCount >= 16) {
                    disabled.store(true);
                }
                callback(QString());
            };

            if (error != QNetworkReply::NoError) {
                onFailure();
                return;
            }
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(responseBody.toUtf8(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                onFailure();
                return;
            }
            QJsonObject obj = doc.object();
            QString status = obj.value("status").toString();
            if (status != "1") {
                onFailure();
                return;
            }
            QJsonObject dataObj = obj.value("data").toObject();
            QString url = dataObj.value("path").toString();
            if (url.isEmpty()) {
                onFailure();
                return;
            }
            // 成功
            errorCount.store(0);
            callback(url);
        }
        );
}
void uploadImageByPathAsync(const QString &serverUrl,
                            const QString &localPath,
                            int timeoutMs,
                            std::function<void(QString)> callback)
{
    QFileInfo fi(localPath);
    if (!fi.exists() || !fi.isFile() || !fi.isReadable()) {
        callback(QString());
        return;
    }

    QString url = serverUrl + "/upload_by_path";
    QByteArray data = QString(R"({"path":"%1"})").arg(localPath).toUtf8();

    NetManager::instance()->postAsync(
        url,
        data,
        QHash<QString, QString>(),
        timeoutMs,
        [callback](const QString &responseBody, QNetworkReply::NetworkError error) {
            if (error != QNetworkReply::NoError) {
                callback(QString());
                return;
            }
            QJsonParseError parseErr;
            QJsonDocument doc = QJsonDocument::fromJson(responseBody.toUtf8(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                callback(QString());
                return;
            }
            QString urlResult = doc.object().value("url").toString();
            callback(urlResult);
        }
        );
}





static QString hmacSha1Hex(const QString &key, const QString &data)
{
    QMessageAuthenticationCode code(QCryptographicHash::Sha1, key.toUtf8());
    code.addData(data.toUtf8());
    QByteArray result = code.result();
    return QString(result.toHex().toLower());
}


QString generateAuthorization(const QString &secretId,
                              const QString &secretKey,
                              const QString &method,
                              const QString &uri,          // 必须以 "/" 开头
                              const QString &host,         // 例如 "bucket.cos.ap-guangzhou.myqcloud.com"
                              const QDateTime &now = QDateTime::currentDateTimeUtc())
{
    // 1. 时间窗口
    qint64 start = now.toSecsSinceEpoch();
    qint64 end = start + 3600;  // 1 小时有效期
    QString keyTime = QString::number(start) + ";" + QString::number(end);
    QMap<QString, QString> headers;
    headers.insert("host", host.toLower());  // key 小写，value 原样
    QStringList headerKeys = headers.keys();  // 已排序
    QString headerList = headerKeys.join(";");
    QStringList headerValuePairs;
    for (const QString &key : headerKeys) {
        headerValuePairs << key + "=" + headers.value(key);
    }
    QString headerValueList = headerValuePairs.join("&");
    QString paramList = "";
    QString paramValueList = "";
    QString httpMethod = method.toLower();
    QString formatStr = httpMethod + "\n" + uri + "\n" + paramValueList + "\n" + headerValueList + "\n";
    QByteArray sha1Format = QCryptographicHash::hash(formatStr.toUtf8(), QCryptographicHash::Sha1).toHex();
    QString stringToSign = "sha1\n" + keyTime + "\n" + QString(sha1Format) + "\n";
    QString signKey = hmacSha1Hex(secretKey, keyTime);
    QString signature = hmacSha1Hex(signKey, stringToSign);

    // 9. 拼接最终 Authorization
    QString auth = "q-sign-algorithm=sha1"
                   "&q-ak=" + secretId +
                   "&q-sign-time=" + keyTime +
                   "&q-key-time=" + keyTime +
                   "&q-header-list=" + headerList +
                   "&q-url-param-list=" + paramList +
                   "&q-signature=" + signature;

    return auth;
}



// 辅助函数：SHA1 返回十六进制小写字符串
static QString sha1Hex(const QString &data)
{
    return QString(QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Sha1).toHex());
}


QString generatePresignedUrl(const QString &objectKey,int expireSeconds = 60)
{

    // 2. 路径：必须以 "/" 开头
    QString path = "/" + objectKey;

    // 3. 计算时间戳（Unix 秒）
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 end = now + expireSeconds;
    QString keyTime = QString::number(now) + ";" + QString::number(end);

    // 4. 构造 HttpString（GET 请求，无参数，无头部）
    //    格式：method + "\n" + path + "\n" + params + "\n" + headers + "\n"
    //    参数和头部均为空，所以是 "\n\n" 夹在中间
    QString httpString = "get\n" + path + "\n\n\n";

    // 5. 计算 StringToSign
    QString sha1Http = sha1Hex(httpString);
    QString stringToSign = "sha1\n" + keyTime + "\n" + sha1Http + "\n";

    // 6. 计算 SignKey
    QString signKey = hmacSha1Hex(g_cos.secretKey, keyTime);

    // 7. 计算 Signature
    QString signature = hmacSha1Hex(signKey, stringToSign);

    // 8. 拼接 URL 参数（查询字符串）
    //    注意：q-header-list 和 q-url-param-list 为空字符串
    QString query = QString(
                        "q-sign-algorithm=sha1"
                        "&q-ak=%1"
                        "&q-sign-time=%2"
                        "&q-key-time=%3"
                        "&q-header-list="
                        "&q-url-param-list="
                        "&q-signature=%4"
                        ).arg(g_cos.secretId, keyTime, keyTime, signature);

    // 9. 编码 objectKey（保留斜杠和普通字符）
    QString encodedKey = QUrl::toPercentEncoding(objectKey, "/");

    // 10. 拼接最终 URL
    QString url = g_cos.baseUrl + "/" + encodedKey + "?" + query;

    return url;
}
std::atomic<int> g_counter{0};
const int MAX_FILES = 2000; //2000张 1张1m 也就2G cos一般 10g

QString generateObjectKey() {
    int idx = g_counter.fetch_add(1) % MAX_FILES;
    return QString("temp/%1.jpg").arg(idx); // 文件名循环覆盖
}
QString uploadFileSync_cos(const QString &localPath)
{
    // 1. 读取文件数据
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file:" << localPath;
        return QString();
    }
    QByteArray data = file.readAll();
    file.close();
    QString objectKey = generateObjectKey();

    QString encodedKey = QUrl::toPercentEncoding(objectKey);
    QString url = g_cos.baseUrl + "/" + encodedKey;

    // 3. 准备请求头
    QHash<QString, QString> headers;
    headers.insert("Content-Type", "image/png"); // 替换原来的 "application/octet-stream"

    QString path = "/" + objectKey;  // 注意：使用原始 objectKey，未编码，但签名规范要求原始字符串
    QString auth = generateAuthorization(g_cos.secretId, g_cos.secretKey, "PUT", path, g_cos.host);
    headers.insert("Authorization", auth);
    if(!g_neiw.isEmpty()){
        NetManager::instance()->putAsync(url, data, headers, 30000,
                                         [&](const QString &response, QNetworkReply::NetworkError error) {
                                             if (error == QNetworkReply::NoError) {
                                                 g_cos.errcs=0;
                                             } else {
                                                 g_cos.errcs++;
                                                 if(g_cos.errcs>=16)
                                                 {
                                                     g_cos.e = false;
                                                 }
                                             }
                                         });
    }else{
        auto f =  NetManager::instance()->put(url, data, headers, 30000);
        f.get();
    }
    return generatePresignedUrl(objectKey);
}
void doCosUploadAsync(const QByteArray &data, std::function<void(QString)> callback)
{
    if (!g_cos.e || g_cos.errcs >= 16) {
        callback(QString());
        return;
    }

    QString objectKey = generateObjectKey();
    QString encodedKey = QUrl::toPercentEncoding(objectKey);
    QString url = g_cos.baseUrl + "/" + encodedKey;

    QString path = "/" + objectKey;
    QString auth = generateAuthorization(g_cos.secretId, g_cos.secretKey, "PUT", path, g_cos.host);

    QHash<QString, QString> headers;
    headers.insert("Content-Type", "image/png");
    headers.insert("Authorization", auth);

    NetManager::instance()->putAsync(
        url,
        data,
        headers,
        30000,
        [callback, objectKey](const QString &response, QNetworkReply::NetworkError error) {
            if (error == QNetworkReply::NoError) {
                g_cos.errcs = 0;
                QString presignedUrl = generatePresignedUrl(objectKey);
                callback(presignedUrl);
            } else {
                g_cos.errcs++;
                if (g_cos.errcs >= 16) {
                    g_cos.e = false;
                }
                callback(QString());
            }
        }
        );
}




// 富媒体（占位，您后续修改）
void doRichMediaUpload(const QString &filePath, std::function<void(QString)> callback)
{
    // 例如：uploadRichMedia(...); 成功后回调 URL，否则空
    callback(QString());
}

std::future<QString> uploadimg(const QString &filePath)
{
    auto promise = std::make_shared<std::promise<QString>>();
    std::future<QString> future = promise->get_future();

    // 预读文件数据
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        promise->set_value(QString());
        return future;
    }
    QByteArray fileData = file.readAll();
    file.close();


    // 使用上下文对象管理所有数据，生命周期与回调链一致
    struct Context {
        QByteArray fileData;
        QString filePath;

        std::shared_ptr<std::promise<QString>> promise;
    };
    auto ctx = std::make_shared<Context>();
    ctx->fileData = std::move(fileData);
    ctx->filePath  = filePath;
    ctx->promise = promise;

    // 回调链调度器
    std::function<void(int)> tryNext;
    tryNext = [ctx, &tryNext](int index) {
        if (index >= 6) {
            ctx->promise->set_value(QString());
            return;
        }

        auto onDone = [ctx, &tryNext, index](const QString &result) {
            if (!result.isEmpty()) {
                ctx->promise->set_value(result);
            } else {
                tryNext(index + 1);
            }
        };

        switch (index) {
        case 0: // CNB
            doCnbUpload(ctx->fileData, onDone);
            break;
        case 1: // COS
            doCosUploadAsync(ctx->fileData, onDone);
            break;
        case 2: // 本地上传
            onDone(upload(ctx->filePath));
            break;
        case 3: // 远程服务器
            if (!setA || !setA->远程服务器) {
                onDone(QString());
                break;
            }
            uploadImageByPathAsync("http://127.0.0.1:" + setA->远程端口 + "/", ctx->filePath , 30000, onDone);
            break;

        case 4: // 富媒体
            doRichMediaUpload(ctx->filePath , onDone);
            break;
        case 5: // CDN
            uploadToMhimgAsync(ctx->fileData, ctx->filePath , onDone);
            break;
        default:
            onDone(QString());
        }
    };

    tryNext(0);
    return future;
}



#endif // CNBUPLOADER_H