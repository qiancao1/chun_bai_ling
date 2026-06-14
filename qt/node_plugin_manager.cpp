#include "node_plugin_manager.h"
#include "global.h"
#include "node_process.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QDebug>
#include <QThread>
#include <qjsonarray.h>
#include <QtConcurrent/QtConcurrent>
#include <QPointer>
#include <QThread>
// 声明外部 myCallback 函数（根据您的实际定义）
extern const char* myCallback(const char* uuid, int api_id, int appid,
                              const char* p1, const char* p2, const char* p3,
                              const char* p4, const char* p5, const char* p6,
                              const char* p7, const char* p8);

NodePluginManager& NodePluginManager::instance() {
    static NodePluginManager mgr;
    return mgr;
}

QVariantMap NodePluginManager::loadPlugin(const QString& dirPath, const QString& uuid) {
    if (m_processes.contains(uuid))
        return {{"error", "Already loaded"}};

    NodeProcess* proc = new NodeProcess(uuid, dirPath, this);
    if (!proc->start()) {
        delete proc;
        return {{"error", "Failed to start node process"}};
    }

    // 主动请求插件信息
    QEventLoop loop;
    QVariantMap metadata;
    bool gotInfo = false;

    // 发送 get_plugin_info 请求
    proc->sendRequest("get_plugin_info", QJsonArray(), [&](const QJsonObject& result) {
        metadata["name"] = result["name"].toString();
        metadata["version"] = result["version"].toString();
        metadata["author"] = result["author"].toString();
        metadata["description"] = result["description"].toString();
        metadata["icon"] = result["icon"].toString();
        gotInfo = true;
        loop.quit();
    });

    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    if (!gotInfo) {
        proc->stop();
        delete proc;
        return {{"error", "Timeout waiting for plugin info"}};
    }

    // 连接后续 API 请求处理（如果需要）
    connect(proc, &NodeProcess::requestReceived, this, [this, uuid](int id, const QString& method, const QJsonArray& params) {

        onApiRequest(uuid, id, method, params);
    });
    connect(proc, &NodeProcess::exited, this, [this, uuid]() { onProcessExited(uuid); });

    m_processes[uuid] = proc;
    proc->setEnabled(false);  // 默认禁用
    return metadata;
}
bool NodePluginManager::unloadPlugin(const QString& uuid) {
    auto* proc = m_processes.take(uuid);
    if (!proc) return false;
    proc->stop();
    delete proc;
    return true;
}

bool NodePluginManager::enablePlugin(const QString& uuid) {
    auto* proc = m_processes.value(uuid);
    if (!proc || !proc->isRunning()) return false;
    proc->setEnabled(true);
    return true;
}

bool NodePluginManager::disablePlugin(const QString& uuid) {
    auto* proc = m_processes.value(uuid);
    if (!proc || !proc->isRunning()) return false;
    proc->setEnabled(false);
    return true;
}

bool NodePluginManager::isPluginEnabled(const QString& uuid) const {
    auto* proc = m_processes.value(uuid);
    return proc ? proc->isEnabled() : false;
}

void NodePluginManager::postEvent(const QString& uuid, const QString& eventType, const QString& data) {
    auto* proc = m_processes.value(uuid);
    if (!proc) return;
    QJsonObject ev;
    ev["type"] = eventType;
    ev["data"] = data;
    proc->writeMessage(QJsonDocument(ev).toJson(QJsonDocument::Compact));
}

void NodePluginManager::postEventAsync(const QString& uuid, const QString& eventType, const QString& data) {
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, [=]() { postEvent(uuid, eventType, data); }, Qt::QueuedConnection);
    } else {
        postEvent(uuid, eventType, data);
    }
}

QString NodePluginManager::processApiRequest(const QString& uuid, const QString& method, const QJsonArray& params) {
    // 映射 method 到 api_id
    int api_id = -1;
    if (method == "outlog") api_id = 1;
    else if (method == "send_message") api_id = 2;
    else if (method == "send_ark") api_id = 3;
    else if (method == "delete_message") api_id = 4;
    else if (method == "generate_share_link") api_id = 5;
    else if (method == "respond_interaction") api_id = 6;
    else if (method == "botlist") api_id = 7;
    else if (method == "get_openid") api_id = 8;
    else if (method == "get_user_name") api_id = 9;
    else if (method == "http_request") api_id = 10;
    else if (method == "get_user_id") api_id = 11;
    else if (method == "ok") api_id = 10002;
    else {
        return QString("Unknown method: %1").arg(method);
    }


    if (api_id == 10002) {

        if (params.size() >= 3) {
            int type = params[0].toInt();
            QString groupId = params[1].toString();
            QString msgId = params[2].toString();
            botnomsg(type, groupId, msgId);
        }
        return "{}";
    }

    bool hasAppid = true;
    if (api_id == 1  // outlog
        || api_id == 7  // botlist
        || api_id == 10) // http_request
    {
        hasAppid = false;
    }

    // 准备 strParams (最多8个)
    QStringList strParams;
    int startIdx = 0;
    int appid = 0;

    if (hasAppid && params.size() > 0) {
        // 第一个参数是 appid
        QJsonValue val = params[0];
        if (val.isDouble()) appid = val.toInt();
        else if (val.isString()) appid = val.toString().toInt();
        startIdx = 1;
    }

    // 提取剩余参数 (最多8个)
    for (int i = startIdx; i < qMin(params.size(), startIdx + 8); ++i) {
        QJsonValue val = params[i];
        if (val.isString()) strParams << val.toString();
        else if (val.isDouble()) strParams << QString::number(val.toDouble());
        else if (val.isBool()) strParams << (val.toBool() ? "true" : "false");
        else if (val.isObject()) strParams << QString::fromUtf8(QJsonDocument(val.toObject()).toJson(QJsonDocument::Compact));
        else if (val.isArray()) strParams << QString::fromUtf8(QJsonDocument(val.toArray()).toJson(QJsonDocument::Compact));
        else strParams << val.toVariant().toString();
    }
    while (strParams.size() < 8) strParams.append("");

    // 调用 myCallback
    const char* result_cstr = myCallback(uuid.toUtf8().constData(),
                                         api_id,
                                         appid,
                                         strParams[0].toUtf8().constData(),
                                         strParams[1].toUtf8().constData(),
                                         strParams[2].toUtf8().constData(),
                                         strParams[3].toUtf8().constData(),
                                         strParams[4].toUtf8().constData(),
                                         strParams[5].toUtf8().constData(),
                                         strParams[6].toUtf8().constData(),
                                         strParams[7].toUtf8().constData());

    return QString::fromUtf8(result_cstr);

}
void NodePluginManager::onApiRequest(const QString& uuid, int id, const QString& method, const QJsonArray& params) {
    NodeProcess* proc = m_processes.value(uuid);
    if (!proc || !proc->isRunning()) return;


    JsApiTask* task = new JsApiTask(this, uuid, id, method, params, QPointer<NodeProcess>(proc));
    QThreadPool::globalInstance()->start(task);
}
void NodePluginManager::onProcessExited(const QString& uuid) {
    m_processes.remove(uuid);
    emit pluginExited(uuid);
}