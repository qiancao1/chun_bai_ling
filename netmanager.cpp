#include "netmanager.h"
#include <qnetworkreply.h>
#include <qthread.h>
#include <qtimer.h>



void NetManager::init() {
    m_netThread = new QThread(this);
    m_netThread->start(); // 后台线程自带 QEventLoop

    // 依然保持 12 个 QNAM 打破 6 并发限制
    for(int i = 0; i < 12; i++) {
        QNetworkAccessManager *mgr = new QNetworkAccessManager();
        mgr->moveToThread(m_netThread);
        m_netManagers.append(mgr);
    }
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