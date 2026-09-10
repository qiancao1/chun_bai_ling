#include "netmanager.h"
#include "global.h"
#include <qhash.h>
#include <qhostaddress.h>
#include <qhostinfo.h>
#include <qmetaobject.h>
#include <qnetworkreply.h>
#include <qrunnable.h>
#include <qsslerror.h>
#include <qthread.h>
#include <qthreadpool.h>
#include <qtimer.h>
#include <qurl.h>

// 回调默认丢线程池（不落主线程、不落调用方线程）
NetManager::CallbackThread NetManager::s_cbThread = CallbackOnPoolThread;

// 把回调交出去执行。必须在网络线程里调用。
//
// CallbackOnPoolThread：包成 QRunnable 丢进全局线程池。
//   - worker 没有事件循环，所以只能「包成任务丢进去」，不能 invokeMethod 投递；
//   - 复用业务已有的 worker，不额外起线程、不额外上下文切换；
//   - 回调在 worker 里跑完即止，互相并行。
// CallbackOnNetThread：原地执行（回调极轻量时才用，重活会堵住网络线程）。
void NetManager::dispatchCallback(CallbackThread where,
                                  const QString &response,
                                  QNetworkReply::NetworkError err,
                                  Callback cb)
{
    if (!cb)
        return;

    if (where == CallbackOnNetThread) {
        cb(response, err);
        return;
    }

    // 把回调拷进一个自持有的 QRunnable，交给全局线程池。
    // 线程池会在某个 worker 线程上执行 run()，跑完自动 delete（setAutoDelete(true)）。
    QRunnable *task = QRunnable::create([cb, response, err]() {
        cb(response, err);
    });
    QThreadPool::globalInstance()->start(task);
}



void NetManager::init() {
    // 只起一条网络线程，不预建任何 QNAM。
    // 连接池改为「按 host 惰性创建、按流量自适应大小」——
    // 低频 host 只占 1 个 NAM（连接复用率最高），高频 host 再逐步加。
    m_netThread = new QThread(this);
    m_netThread->start();

    // 周期性回收：某 host 连续多轮没有在途请求时，把扩出来的多余 NAM 收掉，
    // 只留 kNamMin 个（保留 1 个能保住连接复用）。
    QTimer *gc = new QTimer(this);
    gc->setInterval(5000);
    QObject::connect(gc, &QTimer::timeout, this, [this]() {
        QMutexLocker locker(&m_managerMutex);
        for (auto it = m_pools.begin(); it != m_pools.end(); ++it) {
            HostPool *p = it.value();
            if (p->inflight > 0) {
                p->idleTicks = 0;
                continue;
            }
            if (++p->idleTicks < 3)          // 连续 15 秒空闲才缩容
                continue;
            p->idleTicks = 0;
            while (p->nams.size() > kNamMin) {
                QNetworkAccessManager *mgr = p->nams.takeLast();
                mgr->deleteLater();          // 跨线程，必须用 deleteLater
            }
        }
    });
    gc->start();
}

// 取该 host 的连接池，不存在则建。调用方必须已持有 m_managerMutex。
NetManager::HostPool *NetManager::poolFor(const QString &host)
{
    HostPool *p = m_pools.value(host, nullptr);
    if (p)
        return p;

    p = new HostPool;
    // 首个 NAM 在这里同步建出来，保证 acquire 后一定能拿到
    QNetworkAccessManager *mgr = new QNetworkAccessManager();
    mgr->moveToThread(m_netThread);
    p->nams.append(mgr);
    m_pools.insert(host, p);
    return p;
}

// 按 host 取一个 NAM，并在池内轮询。
//
// 池内 NAM 数量由「该 host 当前在途请求数」驱动：
//   在途 1~6   → 1 个 NAM（就一个连接池，复用率最高）
//   在途 7~12  → 2 个 NAM
//   ...
// 上限 kNamMax。每 NAM 对同一 host 只有 6 条连接（Qt 硬限制），
// 所以需要的 NAM 数 = ceil(目标并发 / 6)。
QNetworkAccessManager *NetManager::pickManager(const QString &url, int needConcurrency)
{
    const QString host = QUrl(url).host().toLower();

    QMutexLocker locker(&m_managerMutex);
    if (!m_netThread)
        return nullptr;

    HostPool *p = poolFor(host);
    p->inflight += 1;          // 计入本次请求
    p->idleTicks = 0;

    // ---- 按需扩容 ----
    // needConcurrency <= 0 表示不限，直接给到该 host 的上限；
    // 否则按 6 条/NAM 反推，并至少覆盖当前在途数
    // （枚举常量参与模板推导会歧义，统一先取成 int）
    const int kMin = int(kNamMin);
    const int kMax = int(kNamMax);
    const int connPer = int(kConnPerNam);

    int want = 0;
    const int byInflight = (p->inflight + connPer - 1) / connPer;
    if (needConcurrency <= 0) {
        want = kMax;
    } else {
        const int byNeed = (needConcurrency + connPer - 1) / connPer;
        want = qMax(byNeed, byInflight);
    }
    want = qBound(kMin, want, kMax);

    while (p->nams.size() < want) {
        QNetworkAccessManager *mgr = new QNetworkAccessManager();
        mgr->moveToThread(m_netThread);
        p->nams.append(mgr);
    }

    const int idx = (p->rr++) % p->nams.size();
    return p->nams[idx];
}

// 请求完成时调用：该 host 在途数 -1。
void NetManager::notifyFinished(const QString &url)
{
    const QString host = QUrl(url).host().toLower();

    QMutexLocker locker(&m_managerMutex);
    HostPool *p = m_pools.value(host, nullptr);
    if (!p)
        return;

    if (p->inflight > 0)
        p->inflight -= 1;
}

void NetManager::postAsync(const QString& url, const QByteArray& data,
                           const QHash<QString, QString>& headers, int timeoutMs,
                           Callback callback, CallbackThread where) {
    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        dispatchCallback(where, QString(), QNetworkReply::UnknownNetworkError, callback);
        return;
    }

    // 将请求投递到 mgr 所在线程（如果 mgr 在主线程，则直接执行）
    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = mgr->post(request, data);
        QTimer* timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);


        QObject::connect(reply, &QNetworkReply::finished, [self, url, callback, where, reply]() {
            // 先让连接池知道这个 host 的在途数降了
            self->notifyFinished(url);
            // 只把「数据」取出来就交给回调线程池，网络线程不做任何用户代码
            QString response = QString::fromUtf8(reply->readAll());
            QNetworkReply::NetworkError err = reply->error();
            reply->deleteLater();
            dispatchCallback(where, response, err, callback);
        });
    }, Qt::QueuedConnection);
}

std::future<QByteArray> NetManager::post(const QString &url, const QByteArray &jsonData,
                                      const QHash<QString, QString> &headers, int timeoutMs) {
    // 1. 使用 shared_ptr 管理 promise，保证跨线程安全
    auto promise = std::make_shared<std::promise<QByteArray>>();
    std::future<QByteArray> future = promise->get_future();

    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        promise->set_value(QByteArray());
        return future;
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        QNetworkReply *reply = mgr->post(request, jsonData);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        // 证书问题会直接断掉连接，连复用都谈不上
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });

        QObject::connect(reply, &QNetworkReply::finished, [self, url, promise, reply]() {
            self->notifyFinished(url);
            promise->set_value(reply->readAll());
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future; // 毫秒级返回
}
std::future<QByteArray> NetManager::put(const QString &url, const QByteArray &data,
                                     const QHash<QString, QString> &headers, int timeoutMs) {
    auto promise = std::make_shared<std::promise<QByteArray>>();
    std::future<QByteArray> future = promise->get_future();

    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        promise->set_value(QByteArray());
        return future;
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


        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        QNetworkReply *reply = mgr->put(request, data);

        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });


        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::finished, [self, url, promise, reply]() {
            self->notifyFinished(url);
            if (reply->error() != QNetworkReply::NoError) {
                promise->set_exception(std::make_exception_ptr(
                    std::runtime_error(reply->errorString().toStdString())
                    ));
            } else {
                int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (statusCode >= 200 && statusCode < 300) {
                    promise->set_value(reply->readAll());
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


std::future<QByteArray> NetManager::get(const QString &url,const QHash<QString, QString> &headers, int timeoutMs) {

    auto promise = std::make_shared<std::promise<QByteArray>>();
    std::future<QByteArray> future = promise->get_future();

    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        promise->set_value(QByteArray());
        return future;
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;

        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        request.setUrl(QUrl(url));
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        QNetworkReply *reply = mgr->get(request);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });

        QObject::connect(reply, &QNetworkReply::finished, [self, url, promise, reply]() {
            self->notifyFinished(url);
           promise->set_value(reply->readAll());
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future; // 毫秒级返回
}

void NetManager::getAsync(const QString &url,const QHash<QString, QString> &headers, int timeoutMs,
                          Callback callbacks, CallbackThread where) {



    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        dispatchCallback(where, QString(), QNetworkReply::UnknownNetworkError, callbacks);
        return;
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        QNetworkReply *reply = mgr->get(request);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });

        QObject::connect(reply, &QNetworkReply::finished, [self, url, callbacks, where, reply]() {
            self->notifyFinished(url);
            QString response = QString::fromUtf8(reply->readAll());
            QNetworkReply::NetworkError err = reply->error();
            reply->deleteLater();
            dispatchCallback(where, response, err, callbacks);
        });
    }, Qt::QueuedConnection);


}


std::future<QByteArray> NetManager::Patch (const QString &url, const QByteArray &jsonData,
                                      const QHash<QString, QString> &headers, int timeoutMs) {
    // 1. 使用 shared_ptr 管理 promise，保证跨线程安全
    auto promise = std::make_shared<std::promise<QByteArray>>();
    std::future<QByteArray> future = promise->get_future();

    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        promise->set_value(QByteArray());
        return future;
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        for(auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        QNetworkReply *reply = mgr->sendCustomRequest(request, "PATCH", jsonData);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });

        QObject::connect(reply, &QNetworkReply::finished, [self, url, promise, reply]() {
            self->notifyFinished(url);
            promise->set_value(reply->readAll());
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future; // 毫秒级返回
}
void NetManager::Delete2(const QString &url,const QByteArray &data,
                                        const QHash<QString, QString> &headers,int timeoutMs,
                                        Callback callbacks, CallbackThread where) {
    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        dispatchCallback(where, QString(), QNetworkReply::UnknownNetworkError, callbacks);
        return;
    }
    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }
        QNetworkReply *reply = mgr->sendCustomRequest(request, "DELETE", data);
        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });
        QObject::connect(reply, &QNetworkReply::finished, [self, url, callbacks, where, reply]() {
            self->notifyFinished(url);
            QString response = QString::fromUtf8(reply->readAll());
            QNetworkReply::NetworkError err = reply->error();
            reply->deleteLater();
            dispatchCallback(where, response, err, callbacks);
        });
    }, Qt::QueuedConnection);
    return ;
}

void NetManager::putAsync(const QString& url, const QByteArray& data,
                          const QHash<QString, QString>& headers, int timeoutMs,
                          Callback callback, CallbackThread where)
{
    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        dispatchCallback(where, QString(), QNetworkReply::UnknownNetworkError, callback);
        return;
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        QNetworkReply* reply = mgr->put(request, data);
        QTimer* timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        // SSL 错误忽略（内网可能证书问题）
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError>&) {
            reply->ignoreSslErrors();
        });

        QObject::connect(reply, &QNetworkReply::finished, [self, url, callback, where, reply]() {
            self->notifyFinished(url);
            QString response = QString::fromUtf8(reply->readAll());
            QNetworkReply::NetworkError err = reply->error();
            reply->deleteLater();
            dispatchCallback(where, response, err, callback);
        });
    }, Qt::QueuedConnection);
}
// NetManager.cpp
std::future<QByteArray> NetManager::Delete(const QString &url,
                                        const QByteArray &data,
                                        const QHash<QString, QString> &headers,
                                        int timeoutMs) {
    auto promise = std::make_shared<std::promise<QByteArray>>();
    std::future<QByteArray> future = promise->get_future();

    NetManager *self = this;
    QNetworkAccessManager *mgr = pickManager(url);
    if (!mgr) {
        promise->set_value(QByteArray());
        return future;
    }

    QMetaObject::invokeMethod(mgr, [=]() {
        QNetworkRequest request;
        request.setUrl(QUrl(url));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        // ★★★ 关键修改：使用 sendCustomRequest 发送 DELETE 请求体和数据 ★★★
        QNetworkReply *reply = mgr->sendCustomRequest(request, "DELETE", data);

        QTimer *timer = new QTimer(reply);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, reply, [reply]() { reply->abort(); });
        timer->start(timeoutMs);

        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) {
            reply->ignoreSslErrors();
        });

        QObject::connect(reply, &QNetworkReply::finished, [self, url, promise, reply]() {
            self->notifyFinished(url);
            promise->set_value(reply->readAll());
            reply->deleteLater();
        });
    }, Qt::QueuedConnection);

    return future;
}


void NetManager::cleanup() {

    if (!m_netThread) return;

    // 1. 退出事件循环
    m_netThread->quit();

    // 2. 等待网络线程完全结束（最多等 3 秒，防止死锁）
    if (!m_netThread->wait(3000)) {
        qWarning() << "网络线程退出超时，强制终止";
        m_netThread->terminate();
        m_netThread->wait();
    }

    // 3. 回收所有 host 池及其 QNAM。
    // 这些 QNAM 的线程亲和性是 m_netThread（已停止），直接 delete 有风险，
    // 先把亲和性改回本线程再销毁。
    {
        QMutexLocker locker(&m_managerMutex);
        for (HostPool *p : std::as_const(m_pools)) {
            for (QNetworkAccessManager *mgr : std::as_const(p->nams)) {
                mgr->moveToThread(QThread::currentThread());
                delete mgr;
            }
            p->nams.clear();
            delete p;
        }
        m_pools.clear();
    }

    // 5. 安全清理线程对象本身
    m_netThread->deleteLater();
    m_netThread = nullptr;
}