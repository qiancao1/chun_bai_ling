#ifndef QDRANTCLIENT_H
#define QDRANTCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>

class QdrantClient : public QObject
{
    Q_OBJECT

public:
    explicit QdrantClient(const QString &baseUrl = "http://localhost:6333",
                          int timeoutMs = 30000,
                          QObject *parent = nullptr);

    // ---------- 同步接口（阻塞等待结果）----------

    // 创建集合
    bool createCollection(const QString &collectionName,
                          int vectorSize,
                          const QString &distance = "Cosine",
                          QString *errorMsg = nullptr);

    // 插入/更新向量点
    bool upsertPoints(const QString &collectionName,
                      const QJsonArray &points,
                      QString *errorMsg = nullptr);

    // 相似性搜索（返回结果数组，若失败返回空数组）
    QJsonArray search(const QString &collectionName,
                      const QVector<double> &queryVector,
                      int limit = 10,
                      QString *errorMsg = nullptr);

    // 删除集合
    bool deleteCollection(const QString &collectionName,
                          QString *errorMsg = nullptr);

private:
    QNetworkAccessManager m_nam;
    QString m_baseUrl;
    int m_timeoutMs;

    // 内部同步请求函数
    bool sendRequestSync(const QUrl &url,
                         const QString &method,
                         const QJsonDocument &payload,
                         QByteArray *responseData,
                         QString *errorMsg);
};

#endif // QDRANTCLIENT_H