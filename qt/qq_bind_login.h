#pragma once


#include "global.h"
#include "netmanager.h"
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QUrl>
#include <QHash>
#include <QSet>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <functional>

#include <openssl/evp.h>   // AES-256-GCM 解密
#include <qdialog.h>
#include <qwidget.h>

extern QString m_taskId_login;
extern QDialog *m_qrDialog;

QString addbot(int appid,const QString &secret,const QString &wsAddress,int type,const QString  &markdown,int wsIntents);
namespace qq_bind_detail {

inline const char kCreateUrl[]   = "https://q.qq.com/lite/create_bind_task";
inline const char kPollUrl[]     = "https://q.qq.com/lite/poll_bind_result";
inline const char kBindPageFmt[] = "https://q.qq.com/qqbot/openclaw/connect.html?task_id=%1&source=chunbailing&_wv=2";

// 与项目 Python 版一致的头信息 (两接口无需登录 cookie)
inline QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Mozilla/5.0 (Linux; U; Android 14; zh-cn) "
                                 "AppleWebKit/537.36 Chrome/109.0.5414.118 Mobile Safari/537.36"));
    req.setRawHeader("Host",    "q.qq.com");
    req.setRawHeader("Origin",  "https://q.qq.com");
    req.setRawHeader("Referer", "https://q.qq.com/");
    return req;
}

// AES-256-GCM 解密: 密文布局 = nonce(12) + 正文 + tag(16)  (OpenSSL EVP)
inline bool aesGcmDecrypt(const QByteArray &cipher, const QByteArray &key,
                          QByteArray &plain, QString &errorMessage)
{
    errorMessage.clear();
    plain.clear();
    if (cipher.size() < 12 + 16 || key.size() != 32) {
        errorMessage = QStringLiteral("密文或 key 长度不对");
        return false;
    }
    const QByteArray nonce = cipher.left(12);
    const QByteArray tag   = cipher.right(16);
    const QByteArray body  = cipher.mid(12, cipher.size() - 12 - 16);

    bool ok = false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
            break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                                static_cast<int>(nonce.size()), nullptr) != 1)
            break;
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                               reinterpret_cast<const unsigned char *>(key.constData()),
                               reinterpret_cast<const unsigned char *>(nonce.constData())) != 1)
            break;

        plain.resize(body.size());
        int len = 0;
        if (EVP_DecryptUpdate(ctx,
                              reinterpret_cast<unsigned char *>(plain.data()),
                              &len,
                              reinterpret_cast<const unsigned char *>(body.constData()),
                              static_cast<int>(body.size())) != 1)
            break;
        plain.resize(len);   // GCM 流式模式: 明文长度 == 密文长度

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                                static_cast<int>(tag.size()),
                                const_cast<unsigned char *>(
                                    reinterpret_cast<const unsigned char *>(tag.constData()))) != 1)
            break;
        int finalLen = 0;
        if (EVP_DecryptFinal_ex(ctx,
                                reinterpret_cast<unsigned char *>(plain.data()) + len,
                                &finalLen) != 1) {
            // 认证失败: key 与密文不属于同一次绑定任务
            errorMessage = QStringLiteral("InvalidTag: 解密失败, key 与密文不匹配");
            break;
        }
        plain.resize(plain.size() + finalLen);
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

}

class QQBindLogin : public QWidget
{
    Q_OBJECT
public:

    struct Session
    {
        QString sessionId;    // 会话唯一标识 (== task_id)
        QString taskId;       // 腾讯绑定任务 id
        QString keyB64;       // 本地生成的 32B 密钥(base64), 用于最后解密 secret
        QString qrUrl;        // 二维码内容(给用户扫的链接)
        qint64  createdMs = 0;  // 创建时间戳(用于 600s 过期清理)
        bool    finished  = false;
    };

    void start(std::function<void(bool, const QString&, const QString&, const QString&)> callback = nullptr)
    {
        // 1. 本地生成 32 字节随机 key (base64)
        QByteArray key(32, Qt::Uninitialized);
        quint32 *p = reinterpret_cast<quint32 *>(key.data());
        for (int i = 0; i < 8; ++i)
            p[i] = QRandomGenerator::system()->generate();
        const QString keyB64 = QString::fromLatin1(key.toBase64());

        // 2. 构造请求头和 body
        QHash<QString, QString> headers;
        headers["Content-Type"] = "application/json";
        headers["User-Agent"]   = "Mozilla/5.0 (Linux; U; Android 14; zh-cn) "
                                "AppleWebKit/537.36 Chrome/109.0.5414.118 Mobile Safari/537.36";
        headers["Host"]    = "q.qq.com";
        headers["Origin"]  = "https://q.qq.com";
        headers["Referer"] = "https://q.qq.com/";

        QJsonObject body;
        body["key"] = keyB64;
        QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);

        // 3. 异步发送请求
        NetManager::instance()->postAsync(
            QString::fromLatin1(qq_bind_detail::kCreateUrl),
            data,
            headers,
            15000,  // 超时 15s
            [this, keyB64, callback](const QString& response, QNetworkReply::NetworkError err) {
                // 回调可能在任意线程，切回主线程操作 sessions_
                QMetaObject::invokeMethod(this, [=]() {
                    if (err != QNetworkReply::NoError) {
                        if (callback)
                            callback(false, QString(), QString(),
                                     QStringLiteral("网络错误: %1").arg(response));
                        return;
                    }

                    QJsonParseError pe;
                    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &pe);
                    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
                        if (callback)
                            callback(false, QString(), QString(),
                                     QStringLiteral("响应解析失败: %1").arg(pe.errorString()));
                        return;
                    }

                    QJsonObject resp = doc.object();
                    int retcode = resp.value("retcode").toInt();
                    if (retcode != 0) {
                        QString msg = resp.value("msg").toString(QStringLiteral("创建绑定任务失败"));
                        if (callback)
                            callback(false, QString(), QString(), msg);
                        return;
                    }

                    QJsonObject dataObj = resp.value("data").toObject();
                    QString taskId = dataObj.value("task_id").toString();
                    if (taskId.isEmpty()) {
                        if (callback)
                            callback(false, QString(), QString(),
                                     QStringLiteral("创建绑定任务失败: 缺少 task_id"));
                        return;
                    }

                    // 保存会话
                    Session s;
                    s.sessionId = taskId;
                    s.taskId    = taskId;
                    s.keyB64    = keyB64;
                    s.qrUrl     = QString::fromLatin1(qq_bind_detail::kBindPageFmt).arg(taskId);
                    s.createdMs = QDateTime::currentMSecsSinceEpoch();
                    sessions_.insert(taskId, s);

                    // 成功回调
                    if (callback)
                        callback(true, taskId, s.qrUrl, QString());
                });
            }
            );
    }

    void poll()
    {
        // 0. 先清理超过 600s 的过期会话
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (now - it.value().createdMs > 120000) {
                const QString sid = it.key();
                inflight_.remove(sid);
                it = sessions_.erase(it);
                if (onError)
                    onError(sid, QStringLiteral("二维码已过期(120s), 会话已清理"));
            } else {
                ++it;
            }
        }

        // 1. 逐个会话异步查询扫码状态
        const auto ids = sessions_.keys();
        for (const QString &sid : ids) {
            if (inflight_.contains(sid))
                continue;
            inflight_.insert(sid);

            QNetworkRequest req = qq_bind_detail::makeRequest(
                QUrl(QString::fromLatin1(qq_bind_detail::kPollUrl)));
            req.setTransferTimeout(15000);
            QNetworkReply *reply = nam_.post(
                req, QJsonDocument(QJsonObject{{QStringLiteral("task_id"), sid}})
                    .toJson(QJsonDocument::Compact));
            connect(reply, &QNetworkReply::finished, this,
                    [this, sid, reply]() {
                        inflight_.remove(sid);
                        onPollFinished(sid, reply);
                        reply->deleteLater();
                    });
        }
    }

    void cancel(const QString &sessionId)
    {
        inflight_.remove(sessionId);
        sessions_.remove(sessionId);
    }

    QStringList sessionIds() const
    {
        QStringList out;
        for (auto it = sessions_.constBegin(); it != sessions_.constEnd(); ++it)
            out << it.key();
        return out;
    }

    bool session(const QString &sessionId, Session *out) const
    {
        auto it = sessions_.constFind(sessionId);
        if (it == sessions_.constEnd())
            return false;
        if (out)
            *out = it.value();
        return true;
    }

    std::function<void(const QString &sessionId, const QString &message)> onError;


    static QQBindLogin &instance()
    {
        static QQBindLogin mgr;
        return mgr;
    }

private:

    void onPollFinished(const QString &sid, QNetworkReply *reply)
    {
        if (!sessions_.contains(sid))
            return;

        const QByteArray raw = reply->readAll();          // gzip 已自动解压
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject())
            return;                                       // 解析失败: 下次 poll 重试
        const QJsonObject resp = doc.object();
        if (resp.value(QStringLiteral("retcode")).toInt() != 0)
            return;                                       // 网络/服务抖动: 静默重试

        const QJsonObject data = resp.value(QStringLiteral("data")).toObject();
        const int status = data.value(QStringLiteral("status")).toInt();
        if (status == 1)                                  // 1 = 还在等待扫码
            return;
        if (status == 3) {                                // 3 = 过期
            cancel(sid);
            if (onError)
                onError(sid, QStringLiteral("二维码已过期"));
            return;
        }
        if (status != 2) {                                // 未知状态
            cancel(sid);
            if (onError)
                onError(sid, QStringLiteral("未知状态: %1").arg(status));
            return;
        }

        // status == 2: 扫码完成 → 取 appid + 密文 → 本地解密
        const Session s = sessions_.value(sid);
        const QString appid = data.value(QStringLiteral("bot_appid")).toString();
        const QByteArray enc = QByteArray::fromBase64(
            data.value(QStringLiteral("bot_encrypt_secret")).toString().toUtf8());
        if (appid.isEmpty() || enc.isEmpty()) {
            cancel(sid);
            if (onError)
                onError(sid, QStringLiteral("绑定结果缺少 AppID/Secret"));
            return;
        }
        QString decryptErr;
        QByteArray plain;
        if (!qq_bind_detail::aesGcmDecrypt(
                enc, QByteArray::fromBase64(s.keyB64.toLatin1()), plain, decryptErr)) {
            cancel(sid);
            if (onError)
                onError(sid, decryptErr);
            return;
        }
        const QString secret = QString::fromUtf8(plain);
        int appid2 = appid.toInt();
        addbot(appid2,secret,QString(),0,"1",0);

        if (m_taskId_login == sid) {
            if (m_qrDialog) {
                m_qrDialog->close();
                m_qrDialog->deleteLater();
                m_qrDialog = nullptr;
            }
            m_taskId_login.clear();
        }
        if (g_CW.contains(appid2)) {
            CardWidget *card = g_CW[appid2];
            QWidget *topLevel = card->window();
            if (topLevel) {
                topLevel->raise();
                topLevel->activateWindow();
                if (topLevel->isMinimized())
                    topLevel->showNormal();
            }

            QTimer::singleShot(100, [card]() {
                card->onLoginButtonA();
            });
        }



        cancel(sid);
    }


private:
    QNetworkAccessManager nam_;                 // 共享一个 HTTP 管理器
    QHash<QString, Session> sessions_;          // sessionId -> 会话数据
    QSet<QString> inflight_;                    // 正在途查询的会话(防重发)
};
