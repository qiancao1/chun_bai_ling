#include "cosclient.h"
#include <QByteArray>
#include <QUrl>
#include <QHttpMultiPart>
#include <QFile>
#include <QHttpPart>
#include <qmessageauthenticationcode.h>
#include "NetManager.h"
#include "global.h"

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


QString uploadFileSync_cos(const QString &localPath)
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QByteArray data = file.readAll();

    QString encodedKey = QUrl::toPercentEncoding(g_cos.secretKey, "/");
    QString url = g_cos.baseUrl + "/" + encodedKey;

    QHash<QString, QString> headers;
    headers.insert("Content-Type", "application/octet-stream");
    headers.insert("Host", g_cos.host);

    QString path = "/" + g_cos.secretKey;
    QString auth = generateAuthorization(g_cos.secretId, g_cos.secretKey, "PUT", path, g_cos.host);
    headers.insert("Authorization", auth);
    try {
        auto future = NetManager::instance()->put(url, data, headers, 30000); // 超时30秒
        QString response = future.get();  // 阻塞等待
    } catch (const std::exception &e) {
        qWarning() << "Upload failed:" << e.what();
    }
    return url;
}