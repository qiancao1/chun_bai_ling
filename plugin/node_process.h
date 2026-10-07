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

// node_process.h
//
// ⚠ 语义已变：这个类**不再是「一个插件一个 node 进程」**，而是**整个程序唯一的那个 node 宿主进程**。
//   所有 JS 插件都由宿主内部用 Worker 线程承载（一个插件一个 Worker，互相隔离），
//   所以：一条消息发给谁，靠消息里的 uuid；插件的 API 调用回包也靠 uuid 转回对应 Worker。
//   宿主脚本 host.js / worker.js 由 qrc 释放到 <程序目录>/node_host/，插件作者不需要管它们。

#ifndef NODE_PROCESS_H
#define NODE_PROCESS_H

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <QHash>
#include <QTimer>

class NodeProcess : public QObject {
    Q_OBJECT
public:
    explicit NodeProcess(QObject* parent = nullptr);
    ~NodeProcess();

    bool start(bool isManual = true);
    void stop();
    bool isRunning() const { return m_process && m_process->state() == QProcess::Running; }

    void writeMessage(const QByteArray& jsonMsg);

    // extra 里放额外字段（比如 load_plugin 的 uuid / dir / params），会自动补上 id / method。
    // 回调拿到的是回包里的 `result` **原始 JSON 值**：可能是对象（get_plugin_info），
    // 也可能是字符串（get_config_list 返回 JSON 文本、set_config_value 返回错误文本）；
    // 第二个参数是回包里的 `error`（插件侧抛异常 / 方法不存在时非空，正常情况下是空串）。
    int sendRequest(const QString& method, const QJsonObject& extra,
                    std::function<void(const QJsonValue&, const QString&)> callback);
    // 请求没人应（超时）时把回调摘掉，避免 m_pendingCallbacks 一直涨
    void forgetRequest(int id);

    // uuid 非空 = 这是给某个插件的 API 回包，宿主据此把结果转回对应 Worker
    void sendResponse(int id, const QString& uuid, const QString& result,
                      const QString& error = QString());

    // 宿主脚本释放后的路径：<程序目录>/node_host/host.js
    static QString hostScriptPath();

signals:
    void requestReceived(int id, const QString& uuid, const QString& method, const QJsonArray& params);
    void exited();
    void restarted();          // 崩溃后自动重启成功

private slots:
    void onReadyRead();
    void onStdErr();
    void onProcessError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onRestartTimer();

private:
    void ensureScriptsOnDisk();

    QProcess* m_process = nullptr;
    QByteArray m_readBuffer;

    // ⚠ 请求 id 必须和「插件自己发起的 API 请求 id」错开：
    //   插件侧 bridge.js 的 id 从 1 往上走（nextId++），宿主内部用**负数**
    //   （host.js 的 localId--，用于 get_plugin_info 等）。这里再从 1000000 起，
    //   三者互不重叠 —— 否则插件会把框架发来的请求当成自己某个请求的回包 resolve 掉，
    //   表现是插件函数永远不被调用、框架侧静默超时，极难排查。
    static const int REQ_ID_BASE = 1000000;
    int m_nextId = REQ_ID_BASE;
    QHash<int, std::function<void(const QJsonValue&, const QString&)>> m_pendingCallbacks;
    QTimer* m_restartTimer = nullptr;
    int m_restartCount = 0;
    bool m_autoRestart = true;

    static const int MAX_RESTART_COUNT = 5;
};

#endif // NODE_PROCESS_H
