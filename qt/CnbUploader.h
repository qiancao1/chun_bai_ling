#ifndef CNBUPLOADER_H
#define CNBUPLOADER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QTimer>
#include <QUrlQuery>

#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <future>


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
QString putFileSync(const QByteArray &fileData, const QJsonObject &uploadInfo)
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
    //AppendEventLog(uploadUrl);
    //qDebug() <<uploadUrl;

    auto future = NetManager::instance()->put(uploadUrl, fileData, headers, 30000);
    try {
        future.get();   // 等待上传完成（成功无异常，失败抛异常）

        // 构建最终访问链接
        QJsonObject assets = uploadInfo["assets"].toObject();
        QString path = assets["path"].toString();
        QString finalUrl = path.startsWith("http") ? path : "https://cnb.cool" + path;
        return finalUrl;
    } catch (const std::exception &e) {
        qWarning() << "PUT 请求异常:" << e.what();
        return QString();
    }
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
        qWarning() << "获取上传URL失败";
        return QString();
    }

    QString finalUrl = putFileSync(fileData, uploadInfo);

    if (finalUrl.isEmpty()) {

        return QString();
    }

    return finalUrl;
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

    QString finalUrl = putFileSync(fileData, uploadInfo);

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



#endif // CNBUPLOADER_H