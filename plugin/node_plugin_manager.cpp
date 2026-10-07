/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "node_plugin_manager.h"
#include "global.h"
#include "node_process.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrent>

// 声明外部 myCallback 函数（根据您的实际定义）
extern const char* myCallback(const char* uuid, int api_id, int appid,
                              const char* p1, const char* p2, const char* p3,
                              const char* p4, const char* p5, const char* p6,
                              const char* p7, const char* p8);

NodePluginManager& NodePluginManager::instance() {
    static NodePluginManager mgr;
    return mgr;
}

// 宿主是懒启动的：没有 JS 插件就不会有 node 进程
NodeProcess* NodePluginManager::ensureHost()
{
    if (!m_host) {
        m_host = new NodeProcess(this);
        connect(m_host, &NodeProcess::requestReceived, this,
                [this](int id, const QString& uuid, const QString& method, const QJsonArray& params) {
                    onApiRequest(uuid, id, method, params);
                });
        connect(m_host, &NodeProcess::restarted, this, &NodePluginManager::onHostRestarted);
    }
    if (!m_host->isRunning()) {
        if (!m_host->start()) return nullptr;
    }
    return m_host;
}

QJsonObject NodePluginManager::loadPlugin(const QString& dirPath, const QString& uuid) {
    if (uuid.isEmpty()) return {{"error", "uuid 为空"}};
    if (m_dirs.contains(uuid)) return {{"error", "Already loaded"}};

    NodeProcess* host = ensureHost();
    if (!host) return {{"error", "Failed to start node host"}};

    // 主动请求插件元数据（宿主会去相应的 Worker 里问 get_plugin_info）
    QEventLoop loop;
    QJsonObject metadata;
    bool gotInfo = false;

    QJsonObject extra;
    extra["uuid"] = uuid;
    extra["dir"] = dirPath;

    const int reqId = host->sendRequest("load_plugin", extra, [&](const QJsonValue& result, const QString& err) {
        if (err.isEmpty()) metadata = result.toObject();      // get_plugin_info 回的是对象
        else               metadata = QJsonObject{{"error", err}};
        gotInfo = true;
        loop.quit();
    });

    QTimer::singleShot(15000, &loop, &QEventLoop::quit);   // 宿主内部还有 10 秒超时
    loop.exec();

    if (!gotInfo) {
        host->forgetRequest(reqId);
        return {{"error", "Timeout waiting for plugin info"}};
    }
    if (metadata.contains("error")) return metadata;

    m_dirs.insert(uuid, dirPath);
    m_enabled.insert(uuid, false);
    return metadata;
}

bool NodePluginManager::unloadPlugin(const QString& uuid) {
    const bool known = m_dirs.remove(uuid) > 0;
    m_enabled.remove(uuid);
    if (m_host && m_host->isRunning()) {
        QJsonObject req;
        req["method"] = "unload_plugin";
        req["uuid"] = uuid;
        // 不需要回包，直接写（宿主会 terminate 掉对应的 Worker）
        m_host->writeMessage(QJsonDocument(req).toJson(QJsonDocument::Compact));
    }
    return known;
}

// ==================== 插件配置面板（get_config_list / set_config_value）====================
// Python / x64 DLL 是进程内直接调函数，JS 插件在另一个进程的 Worker 里，只能发请求等回包。
// 调用链：C++ sendRequest → host.js 按 uuid 转给 Worker → 插件 bridge.js 调 plugin[method]
//         → 回包 {id,result} → host.js 打上 uuid 转回 C++ → 这里 resolve 局部 event loop。
// ⚠ 请求 id 用 NodeProcess 的 REQ_ID_BASE 段，和插件自己的 API id（1 起）、宿主负 id 都不重叠。

QJsonValue NodePluginManager::callHostRequest(const QString& uuid, const QString& method,
                                              const QJsonArray& params, int timeoutMs,
                                              bool* ok, QString* errOut)
{
    bool got = false;
    QJsonValue out;
    QString err;
    if (ok) *ok = false;
    if (errOut) errOut->clear();

    // 没加载过 / 宿主不在 → 直接返回，别白等一次超时
    if (uuid.isEmpty() || !m_dirs.contains(uuid)) return out;
    NodeProcess* host = m_host;
    if (!host || !host->isRunning()) return out;

    QEventLoop loop;
    QJsonObject extra;
    extra["uuid"] = uuid;
    extra["params"] = params;          // bridge.js 会展开成 plugin[method](...params)

    const int reqId = host->sendRequest(method, extra, [&](const QJsonValue& v, const QString& e) {
        out = v;
        err = e;
        got = true;
        loop.quit();
    });

    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();

    // 超时：把回调摘掉，否则它以后被调用时会写到已经析构的栈变量上。
    // （单线程事件循环，exec() 返回到这里之间不会插进 onReadyRead，所以此摘除是安全的。）
    if (!got) host->forgetRequest(reqId);
    if (ok) *ok = got;
    if (errOut) *errOut = err;
    return out;
}

QString NodePluginManager::callGetConfigList(const QString& uuid)
{
    bool ok = false;
    QString err;
    const QJsonValue v = callHostRequest(uuid, "get_config_list", QJsonArray(), 3000, &ok, &err);
    if (!ok || !err.isEmpty()) return QString();   // 没实现 / 超时 / 抛异常 → 配置区隐藏

    // 插件按 SDK 约定回**JSON 文本**；宽容一点，万一它直接回了数组/对象也接受
    if (v.isString()) return v.toString();
    if (v.isArray())  return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    return QString();
}

QString NodePluginManager::callSetConfigValue(const QString& uuid, const QString& id, const QString& value)
{
    bool ok = false;
    QString err;
    QJsonArray params;
    params.append(id);
    params.append(value);
    const QJsonValue v = callHostRequest(uuid, "set_config_value", params, 3000, &ok, &err);

    if (!ok) return QStringLiteral("插件未响应（超时，或插件没有加载）");
    if (!err.isEmpty()) return err;                 // 方法不存在 / 插件抛异常
    if (v.isString()) return v.toString();          // 约定：空串 = 成功，非空 = 失败原因
    return QString();
}

void NodePluginManager::setEnabled(const QString& uuid, bool enabled)
{
    if (!m_dirs.contains(uuid)) return;
    if (m_enabled.value(uuid) == enabled) return;
    if (!m_host || !m_host->isRunning()) return;

    m_enabled[uuid] = enabled;
    QJsonObject ev;
    ev["type"] = enabled ? "on_enable" : "on_disable";
    ev["uuid"] = uuid;
    m_host->writeMessage(QJsonDocument(ev).toJson(QJsonDocument::Compact));
}

bool NodePluginManager::enablePlugin(const QString& uuid) {
    if (!m_dirs.contains(uuid)) return false;
    if (!m_host || !m_host->isRunning()) return false;
    setEnabled(uuid, true);
    return true;
}

bool NodePluginManager::disablePlugin(const QString& uuid) {
    if (!m_dirs.contains(uuid)) return false;
    if (!m_host || !m_host->isRunning()) return false;
    setEnabled(uuid, false);
    return true;
}

bool NodePluginManager::isPluginEnabled(const QString& uuid) const {
    return m_enabled.value(uuid, false);
}

void NodePluginManager::postEvent(const QString& uuid, const QString& eventType, const QString& data, const QString& fun) {
    if (!m_host || !m_host->isRunning()) return;
    if (!m_dirs.contains(uuid)) return;
    QJsonObject ev;
    ev["type"] = eventType;
    ev["uuid"] = uuid;
    ev["data"] = data;
    ev["fun"] = fun;
    m_host->writeMessage(QJsonDocument(ev).toJson(QJsonDocument::Compact));
}

void NodePluginManager::postEventAsync(const QString& uuid, const QString& eventType, const QString& data, const QString& fun) {
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, [=]() { postEvent(uuid, eventType, data, fun); }, Qt::QueuedConnection);
    } else {
        postEvent(uuid, eventType, data, fun);
    }
}

void NodePluginManager::postEventBatch(const QJsonArray& items, const QString& eventType, const QString& data, const QString& fun) {
    if (!m_host || !m_host->isRunning()) return;

    // 过滤掉已卸载 / 从未加载的（uuid 不在 m_dirs 里）+ 去重
    QJsonArray valid;
    QStringList seen;
    for (const QJsonValue& v : items) {
        const QJsonObject o = v.toObject();
        const QString u = o["uuid"].toString();
        if (u.isEmpty() || seen.contains(u)) continue;
        if (!m_dirs.contains(u)) continue;
        seen.append(u);
        valid.append(o);
    }
    if (valid.isEmpty()) return;

    // 只有一个目标时退化成普通单帧：省掉一层数组解析，也保持老格式兼容
    if (valid.size() == 1) {
        const QJsonObject o = valid.first().toObject();
        QJsonObject ev;
        ev["type"] = eventType;
        ev["uuid"] = o["uuid"];
        ev["data"] = data;
        ev["fun"]  = fun;
        if (o.contains("funs")) ev["funs"] = o["funs"];
        m_host->writeMessage(QJsonDocument(ev).toJson(QJsonDocument::Compact));
        return;
    }

    // 多目标帧：`data` 只在这里序列化一次，宿主按 items 逐个分发
    QJsonObject ev;
    ev["type"]  = eventType;
    ev["data"]  = data;
    ev["fun"]   = fun;
    ev["items"] = valid;
    m_host->writeMessage(QJsonDocument(ev).toJson(QJsonDocument::Compact));
}

void NodePluginManager::postEventBatchAsync(const QJsonArray& items, const QString& eventType, const QString& data, const QString& fun) {
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, [=]() { postEventBatch(items, eventType, data, fun); }, Qt::QueuedConnection);
    } else {
        postEventBatch(items, eventType, data, fun);
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
    else if (method == "html_t_img1") api_id = 12;
    else if (method == "html_to_img2") api_id = 13;
    else if (method == "addScheduledTask") api_id = 14;
    else if (method == "getMember") api_id = 16;
    else if (method == "getMemberList") api_id = 17;
    else if (method == "getGroupInfo") api_id = 18;
    else if (method == "getBotGroupState") api_id = 19;
    else if (method == "handleJoinRequest") api_id = 20;
    else if (method == "getJoinRequestList") api_id = 21;
    else if (method == "setGroupMute") api_id = 22;
    else if (method == "getMuteList") api_id = 23;

    else if (method == "batchRemoveMembers") api_id = 24;
    else if (method == "getGroupBlacklist") api_id = 25;
    else if (method == "modifyGroupBlacklist") api_id = 26;


    else if (method == "ok") api_id = 10002;
    else {
        return QString("Unknown method: %1").arg(method);
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
    if (api_id == 10002) {

        if (params.size() >= 3) {
            int type = params[0].toInt();
            QString groupId = params[1].toString();
            QString msgId = params[2].toString();
            botnomsg(appid,type, groupId, msgId);
        }
        return "{}";
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
    if (!m_host || !m_host->isRunning()) return;

    JsApiTask* task = new JsApiTask(this, uuid, id, method, params, QPointer<NodeProcess>(m_host));
    QThreadPool::globalInstance()->start(task);
}

// 宿主崩了自动重启后：Worker 全没了，按记录把插件一个个恢复回来
void NodePluginManager::onHostRestarted()
{
    if (m_dirs.isEmpty()) return;

    AppendEventLog("[JS宿主] node 宿主已自动重启，正在恢复 " + QString::number(m_dirs.size()) + " 个 JS 插件");

    const QHash<QString, QString> dirs = m_dirs;
    const QHash<QString, bool>    wasEnabled = m_enabled;
    m_dirs.clear();
    m_enabled.clear();

    for (auto it = dirs.constBegin(); it != dirs.constEnd(); ++it) {
        const QJsonObject meta = loadPlugin(it.value(), it.key());
        if (meta.contains("error")) {
            AppendEventLog("[JS宿主] 恢复插件失败：" + it.key() + " " + meta["error"].toString(), 0xff0000);
            continue;
        }
        if (wasEnabled.value(it.key(), false)) enablePlugin(it.key());
    }
}

void NodePluginManager::shutdown()
{
    m_dirs.clear();
    m_enabled.clear();
    if (m_host) m_host->stop();
}
