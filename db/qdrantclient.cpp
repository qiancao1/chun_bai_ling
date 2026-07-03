#include "qdrantclient.h"
#include <QJsonDocument>
#include <QDebug>

QdrantClient::QdrantClient(const QString &baseUrl, int timeoutMs, QObject *parent)
    : QObject(parent), m_baseUrl(baseUrl), m_timeoutMs(timeoutMs)
{
}

bool QdrantClient::sendRequestSync(const QUrl &url,
                                   const QString &method,
                                   const QJsonDocument &payload,
                                   QByteArray *responseData,
                                   QString *errorMsg)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = nullptr;
    if (method == "GET") {
        reply = m_nam.get(request);
    } else if (method == "PUT") {
        reply = m_nam.put(request, payload.toJson());
    } else if (method == "POST") {
        reply = m_nam.post(request, payload.toJson());
    } else if (method == "DELETE") {
        reply = m_nam.deleteResource(request);
    } else {
        if (errorMsg) *errorMsg = "Unsupported HTTP method";
        return false;
    }

    // 使用事件循环阻塞等待
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(m_timeoutMs);
    loop.exec();

    bool success = false;
    if (timer.isActive()) {
        // 正常完成（未超时）
        timer.stop();
        if (reply->error() == QNetworkReply::NoError) {
            *responseData = reply->readAll();
            success = true;
        } else {
            if (errorMsg) *errorMsg = reply->errorString();
        }
    } else {
        // 超时
        reply->abort();
        if (errorMsg) *errorMsg = "Request timeout (" + QString::number(m_timeoutMs) + "ms)";
    }

    reply->deleteLater();
    return success;
}

bool QdrantClient::createCollection(const QString &collectionName,
                                    int vectorSize,
                                    const QString &distance,
                                    QString *errorMsg)
{
    QUrl url(m_baseUrl + "/collections/" + collectionName);
    QJsonObject body;
    QJsonObject vectorsConfig;
    vectorsConfig["size"] = vectorSize;
    vectorsConfig["distance"] = distance;
    body["vectors"] = vectorsConfig;

    QByteArray response;
    if (!sendRequestSync(url, "PUT", QJsonDocument(body), &response, errorMsg))
        return false;

    // 检查返回的 status
    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull()) {
        if (errorMsg) *errorMsg = "Invalid JSON response";
        return false;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("status") && obj["status"].toString() != "ok") {
        if (errorMsg) *errorMsg = obj["status"].toString();
        return false;
    }
    return true;
}

bool QdrantClient::upsertPoints(const QString &collectionName,
                                const QJsonArray &points,
                                QString *errorMsg)
{
    QUrl url(m_baseUrl + "/collections/" + collectionName + "/points");
    QJsonObject body;
    body["points"] = points;

    QByteArray response;
    if (!sendRequestSync(url, "PUT", QJsonDocument(body), &response, errorMsg))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull()) {
        if (errorMsg) *errorMsg = "Invalid JSON response";
        return false;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("status") && obj["status"].toString() != "ok") {
        if (errorMsg) *errorMsg = obj["status"].toString();
        return false;
    }
    return true;
}

QJsonArray QdrantClient::search(const QString &collectionName,
                                const QVector<double> &queryVector,
                                int limit,
                                QString *errorMsg)
{
    QUrl url(m_baseUrl + "/collections/" + collectionName + "/points/search");
    QJsonObject body;
    QJsonArray vecArray;
    for (double v : queryVector) vecArray.append(v);
    body["vector"] = vecArray;
    body["limit"] = limit;

    QByteArray response;
    if (!sendRequestSync(url, "POST", QJsonDocument(body), &response, errorMsg))
        return QJsonArray();

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull()) {
        if (errorMsg) *errorMsg = "Invalid JSON response";
        return QJsonArray();
    }
    QJsonObject obj = doc.object();
    if (obj.contains("status") && obj["status"].toString() != "ok") {
        if (errorMsg) *errorMsg = obj["status"].toString();
        return QJsonArray();
    }

    // 提取结果，可能是 "result" 或 "points"
    if (obj.contains("result") && obj["result"].isArray()) {
        return obj["result"].toArray();
    } else if (obj.contains("points") && obj["points"].isArray()) {
        return obj["points"].toArray();
    }
    return QJsonArray();
}

bool QdrantClient::deleteCollection(const QString &collectionName,
                                    QString *errorMsg)
{
    QUrl url(m_baseUrl + "/collections/" + collectionName);
    QByteArray response;
    if (!sendRequestSync(url, "DELETE", QJsonDocument(), &response, errorMsg))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(response);
    if (doc.isNull()) {
        if (errorMsg) *errorMsg = "Invalid JSON response";
        return false;
    }
    QJsonObject obj = doc.object();
    if (obj.contains("status") && obj["status"].toString() != "ok") {
        if (errorMsg) *errorMsg = obj["status"].toString();
        return false;
    }
    return true;
}