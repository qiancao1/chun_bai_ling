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
    explicit NodeProcess(const QString& uuid, const QString& pluginPath, QObject* parent = nullptr);
    ~NodeProcess();

    bool start(bool isManual = true);
    void stop();

    void writeMessage(const QByteArray& jsonMsg);
    int sendRequest(const QString& method, const QJsonArray& params,
                    std::function<void(const QJsonObject&)> callback);
    void sendResponse(int id, const QString& result, const QString& error = QString());
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    bool isRunning() const { return m_process->state() == QProcess::Running; }
    const QString& uuid() const { return m_uuid; }

signals:
    void requestReceived(int id, const QString& method, const QJsonArray& params);
    void exited();

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onRestartTimer();

private:
    QProcess* m_process;
    QString m_uuid;
    QString m_pluginPath;
    QByteArray m_readBuffer;
    bool m_enabled;
    int m_nextId;
    QHash<int, std::function<void(const QJsonObject&)>> m_pendingCallbacks;

    // 崩溃重启相关
    QTimer* m_restartTimer;
    int m_restartCount;
    bool m_autoRestart;
    static const int MAX_RESTART_COUNT = 5;
};

#endif // NODE_PROCESS_H