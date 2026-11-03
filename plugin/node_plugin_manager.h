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

#ifndef NODE_PLUGIN_MANAGER_H
#define NODE_PLUGIN_MANAGER_H

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>       // callShopAsync 的回调（std::function）

class NodeProcess;

// JS 插件管理器：对外接口和以前一样（按 uuid 调），但内部只有**一个** node 宿主进程，
// 每个 JS 插件是宿主里的一个 Worker 线程（互相同名/全局/崩溃都隔离）。
class NodePluginManager : public QObject {
    Q_OBJECT
public:
    static NodePluginManager& instance();

    // 加载插件：返回元数据，若失败则包含 "error" 字段
    QJsonObject loadPlugin(const QString& dirPath, const QString& uuid);
    bool unloadPlugin(const QString& uuid);
    bool enablePlugin(const QString& uuid);
    bool disablePlugin(const QString& uuid);
    bool isPluginEnabled(const QString& uuid) const;

    // 投递事件（可跨线程，自动转主线程）
    void postEvent(const QString& uuid, const QString& eventType, const QString &data, const QString &fun);
    void postEventAsync(const QString& uuid, const QString& eventType, const QString &data, const QString &fun);

    // 批量投递：一条消息命中多个 JS 插件时，把「目标 + 命中规则名」合成**一帧**发过去
    // （`data` 只序列化/跨进程传一次），宿主 host.js 按 items 逐个分发给对应 Worker。
    // items 形如 [{"uuid": "...", "funs": ["函数名", ...]}]；funs 是框架**已经匹配好**的结果，
    // 插件侧拿到后直接执行，不用再自己匹配一遍。
    void postEventBatch(const QJsonArray& items, const QString& eventType, const QString &data, const QString &fun);
    void postEventBatchAsync(const QJsonArray& items, const QString& eventType, const QString &data, const QString &fun);

    QString processApiRequest(const QString& uuid, const QString& method, const QJsonArray& params);
    void onApiRequest(const QString& uuid, int id, const QString& method, const QJsonArray& params);

    // ---- 插件配置面板（对齐 Python / x64 DLL 那两个 SDK 的 get_config_list / set_config_value）----
    // 插件的这两个函数跑在 Worker 线程里，只能跨进程请求，所以这里**阻塞等待**（internal 超时），
    // 和 loadPlugin 一样只允许在主线程调用。插件没实现 / 没加载 / 超时 → 返回空串（配置区自动隐藏）。
    //   get_config_list()          -> JSON 数组文本 [{"desc","type","id","default"}]，type ∈ input/checkbox/button
    //   set_config_value(id,val)   -> 文本；空串 = 成功，非空 = 失败原因
    QString callGetConfigList(const QString& uuid);
    QString callSetConfigValue(const QString& uuid, const QString& id, const QString& value);

    // ---- 铃铛商店：把商品处理函数当一次「方法调用」发给插件 Worker（异步）----
    // bridge.js 收到 {id, method, params} 会执行 `plugin[method](...params)` 并把返回值回包，
    // 所以 funcName 就是插件 main.js 里注册的函数名，params 是实参数组。
    // ⚠ **不阻塞**：结果只通过 cb 回来，且 cb **一定在主线程、只执行一次**（回包与超时先到为准）。
    //    success = 插件返回 true / 1（约定：true = 成交，框架保留已扣的积分）；
    //    err 非空  = 超时 / 方法不存在 / 插件抛异常（框架应当退款）。
    // 跨线程调用是安全的（内部会投递到主线程再写 QProcess）。
    void callShopAsync(const QString& uuid, const QString& funcName, const QJsonArray& params,
                       std::function<void(bool success, const QString& err)> cb,
                       int timeoutMs = 60000);   // 1 分钟：插件可能要发 HTTP 等结果，别卡太死

    // 框架退出时关掉常驻宿主（否则会留一个孤儿 node 进程）
    void shutdown();

    NodeProcess* host() const { return m_host; }

private slots:
    void onHostRestarted();

private:
    NodePluginManager() = default;
    NodeProcess* ensureHost();
    void setEnabled(const QString& uuid, bool enabled);

    // 向某个插件的 Worker 发一次请求并阻塞等回包（超时返回 isUndefined 的 QJsonValue，
    // *ok=false）*errOut 拿到插件侧 error（方法不存在 / 插件抛异常）。
    // 内部用局部 QEventLoop：超时会 forgetRequest 把回调摘掉，避免它事后写到已析构的栈内存。
    QJsonValue callHostRequest(const QString& uuid, const QString& method,
                               const QJsonArray& params, int timeoutMs,
                               bool* ok = nullptr, QString* errOut = nullptr);

    NodeProcess* m_host = nullptr;
    QHash<QString, QString> m_dirs;      // uuid -> 插件目录
    QHash<QString, bool>    m_enabled;   // uuid -> 是否启用
};

#endif // NODE_PLUGIN_MANAGER_H
