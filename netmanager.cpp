#include "netmanager.h"
#include "global.h"
#include <qhostaddress.h>
#include <qhostinfo.h>
#include <qnetworkreply.h>
#include <qthread.h>
#include <qtimer.h>



void NetManager::init() {
    m_netThread = new QThread(this);
    m_netThread->start(); // 后台线程自带 QEventLoop

    // 依然保持 50 个 QNAM 打破 6 并发限制
    for(int i = 0; i < 50; i++) {
        QNetworkAccessManager *mgr = new QNetworkAccessManager();
        mgr->moveToThread(m_netThread);
        m_netManagers.append(mgr);
    }
}

void NetManager::postAsync(const QString& url, const QByteArray& data,
                           const QHash<QString, QString>& headers, int timeoutMs,
                           QObject* context, Callback callback) {
    // 轮询选择一个 NAM
    QNetworkAccessManager *mgr = nullptr;
    {
        QMutexLocker locker(&m_managerMutex);
        mgr = m_netManagers[m_netManagerIndex++ % m_netManagers.size()];
    }

    // 将请求投递到 mgr 所在线程（如果 mgr 在主线程，则直接执行）
    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        QNetworkReply* reply = mgr->post(request, data);
        QTimer* timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        // 使用 context 作为 receiver，使得槽（lambda）在 context 的线程执行
        QObject::connect(reply, &QNetworkReply::finished, context, [reply, callback]() {
            QByteArray raw = reply->readAll();
            QString response = QString::fromUtf8(raw);
            QNetworkReply::NetworkError err = reply->error();
            reply->deleteLater();
            callback(response, err);
        });
    }, Qt::QueuedConnection);
}

std::future<QString> NetManager::post(const QString &url, const QByteArray &jsonData,
                                      const QHash<QString, QString> &headers, int timeoutMs) {
    // 1. 使用 shared_ptr 管理 promise，保证跨线程安全
    auto promise = std::make_shared<std::promise<QString>>();
    std::future<QString> future = promise->get_future();

    QNetworkAccessManager *mgr = nullptr;
    {
        QMutexLocker locker(&m_managerMutex);
        mgr = m_netManagers[m_netManagerIndex++ % m_netManagers.size()];
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        QNetworkReply *reply = mgr->post(request, jsonData);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::finished, [promise, reply]() {
            QString response = QString::fromUtf8(reply->readAll());
            // 2. 把结果填进 promise（唤醒 future.get()）
            promise->set_value(response);
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future; // 毫秒级返回
}
std::future<QString> NetManager::put(const QString &url, const QByteArray &data,
                                     const QHash<QString, QString> &headers, int timeoutMs) {
    auto promise = std::make_shared<std::promise<QString>>();
    std::future<QString> future = promise->get_future();

    QNetworkAccessManager *mgr = nullptr;
    {
        QMutexLocker locker(&m_managerMutex);
        mgr = m_netManagers[m_netManagerIndex++ % m_netManagers.size()];
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;

        QUrl originalUrl(url);
        QString originalHost = originalUrl.host();
        bool isCos = originalHost.contains(".cos.") || originalHost.contains(".myqcloud.com");

        if (isCos) {
            QHostAddress internalAddr;

            QHostInfo info = QHostInfo::fromName(originalHost);
            for (const QHostAddress &addr : info.addresses()) {
                if (addr.isInSubnet(QHostAddress::parseSubnet("10.0.0.0/8")) ||
                    addr.isInSubnet(QHostAddress::parseSubnet("100.0.0.0/8")) ||
                    addr.isInSubnet(QHostAddress::parseSubnet("169.254.0.0/16"))) {
                    internalAddr = addr;
                    break;
                }
            }

            if (internalAddr.isNull()) {
                QString guangzhouHost;
                if (originalHost.contains(".accelerate.")) {
                    guangzhouHost = originalHost;
                    guangzhouHost.replace(".accelerate.", "."+g_neiw+".");
                } else if (originalHost.contains(".cos.")) {
                    QStringList parts = originalHost.split('.');
                    int cosIdx = parts.indexOf("cos");
                    if (cosIdx != -1 && cosIdx + 1 < parts.size()) {
                        parts[cosIdx + 1] = g_neiw;
                        guangzhouHost = parts.join('.');
                    }
                }
                if (!guangzhouHost.isEmpty()) {
                    QHostInfo gzInfo = QHostInfo::fromName(guangzhouHost);
                    for (const QHostAddress &addr : gzInfo.addresses()) {
                        if (addr.isInSubnet(QHostAddress::parseSubnet("10.0.0.0/8")) ||
                            addr.isInSubnet(QHostAddress::parseSubnet("100.0.0.0/8")) ||
                            addr.isInSubnet(QHostAddress::parseSubnet("169.254.0.0/16"))) {
                            internalAddr = addr;
                            break;
                        }
                    }
                }
            }

            if (!internalAddr.isNull()) {
                QUrl newUrl = originalUrl;
                newUrl.setHost(internalAddr.toString());
                request.setUrl(newUrl);
                request.setRawHeader("Host", originalHost.toUtf8());
            } else {
                request.setUrl(originalUrl);
            }
        } else {
            request.setUrl(originalUrl);
        }

        // 继续设置其他 headers（注意跳过 Host）
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            if (it.key().toLower() != "host") {
                request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
            }
        }


        QNetworkReply *reply = mgr->put(request, data);

        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });


        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::finished, [promise, reply]() {
            if (reply->error() != QNetworkReply::NoError) {
                promise->set_exception(std::make_exception_ptr(
                    std::runtime_error(reply->errorString().toStdString())
                    ));
            } else {
                int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (statusCode >= 200 && statusCode < 300) {
                    promise->set_value(QString::fromUtf8(reply->readAll()));
                } else {
                    promise->set_exception(std::make_exception_ptr(
                        std::runtime_error(("HTTP error " + std::to_string(statusCode)).c_str())
                        ));
                }
            }
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future;
}


std::future<QString> NetManager::get(const QString &url,const QHash<QString, QString> &headers, int timeoutMs) {

    auto promise = std::make_shared<std::promise<QString>>();
    std::future<QString> future = promise->get_future();

    QNetworkAccessManager *mgr = nullptr;
    {
        QMutexLocker locker(&m_managerMutex);
        mgr = m_netManagers[m_netManagerIndex++ % m_netManagers.size()];
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        QNetworkReply *reply = mgr->get(request);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::finished, [promise, reply]() {
            QString response = QString::fromUtf8(reply->readAll());

            promise->set_value(response);
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future; // 毫秒级返回
}

std::future<QString> NetManager::Patch (const QString &url, const QByteArray &jsonData,
                                      const QHash<QString, QString> &headers, int timeoutMs) {
    // 1. 使用 shared_ptr 管理 promise，保证跨线程安全
    auto promise = std::make_shared<std::promise<QString>>();
    std::future<QString> future = promise->get_future();

    QNetworkAccessManager *mgr = nullptr;
    {
        QMutexLocker locker(&m_managerMutex);
        mgr = m_netManagers[m_netManagerIndex++ % m_netManagers.size()];
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        QNetworkReply *reply = mgr->sendCustomRequest(request, "PATCH", jsonData);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::finished, [promise, reply]() {
            QString response = QString::fromUtf8(reply->readAll());
            // 2. 把结果填进 promise（唤醒 future.get()）
            promise->set_value(response);
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future; // 毫秒级返回
}
void NetManager::Delete2(const QString &url,const QByteArray &data,
                                        const QHash<QString, QString> &headers) {
    QNetworkAccessManager *mgr = nullptr;
    {
        QMutexLocker locker(&m_managerMutex);
        mgr = m_netManagers[m_netManagerIndex++ % m_netManagers.size()];
    }
    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
        QNetworkReply *reply = mgr->sendCustomRequest(request, "DELETE", data);
        QObject::connect(reply, &QNetworkReply::finished, [reply]() {
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);
    return ;
}
// NetManager.cpp
std::future<QString> NetManager::Delete(const QString &url,
                                        const QByteArray &data,
                                        const QHash<QString, QString> &headers,
                                        int timeoutMs) {
    auto promise = std::make_shared<std::promise<QString>>();
    std::future<QString> future = promise->get_future();

    QNetworkAccessManager *mgr = nullptr;
    {
        QMutexLocker locker(&m_managerMutex);
        mgr = m_netManagers[m_netManagerIndex++ % m_netManagers.size()];
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        // ★★★ 关键修改：使用 sendCustomRequest 发送 DELETE 请求体和数据 ★★★
        QNetworkReply *reply = mgr->sendCustomRequest(request, "DELETE", data);

        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::finished, [promise, reply]() {
            QString response = QString::fromUtf8(reply->readAll());
            promise->set_value(response);   // 无论成败，都返回原始响应
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future;
}


void NetManager::cleanup() {

    if (!m_netThread) return;

    // 2. 退出事件循环
    m_netThread->quit();

    // 3. 等待网络线程完全结束（最多等 3 秒，防止死锁）
    if (!m_netThread->wait(3000)) {
        qWarning() << "网络线程退出超时，强制终止";
        m_netThread->terminate();
        m_netThread->wait();
    }

    // 4. 删除所有 QNetworkAccessManager 对象
    qDeleteAll(m_netManagers);
    m_netManagers.clear();

    // 5. 安全清理线程对象本身
    m_netThread->deleteLater();
    m_netThread = nullptr;
}