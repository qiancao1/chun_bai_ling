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

// node_process.cpp
#include "node_process.h"
#include "global.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>

static QString getNodePath() {
    QString nodePath = qEnvironmentVariable("NODE_EXE");
    if (!nodePath.isEmpty() && QFile::exists(nodePath)) return nodePath;
    const QStringList candidates = {
        "C:/Program Files/nodejs/node.exe",
        "C:/Program Files (x86)/nodejs/node.exe",
        QDir::homePath() + "/AppData/Local/Programs/nodejs/node.exe",
        "node"
    };
    for (const QString& cand : candidates) {
        if (QFile::exists(cand)) return cand;
    }
    return "node";
}

NodeProcess::NodeProcess(const QString& uuid, const QString& pluginPath, QObject* parent)
    : QObject(parent), m_uuid(uuid), m_pluginPath(pluginPath), m_enabled(false), m_nextId(1),
    m_restartCount(0), m_autoRestart(true)
{
    m_process = new QProcess(this);
    m_restartTimer = new QTimer(this);
    m_restartTimer->setSingleShot(true);




    connect(m_restartTimer, &QTimer::timeout, this, &NodeProcess::onRestartTimer);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &NodeProcess::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, [this](){
        for (auto &p : m_pluginList) {
            if(p.uuid == m_uuid) {
                AppendEventLog("["+p.name+"]信息：" + m_process->readAllStandardError(), 0xff0000);
                return;
            }
        }
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NodeProcess::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error){
        Q_UNUSED(error)
        for (auto &p : m_pluginList) {
            if(p.uuid == m_uuid) {
                AppendEventLog("["+p.name+"]信息：" + m_process->readAllStandardError(), 0xff0000);
                return;
            }
        }
    });
}

NodeProcess::~NodeProcess() { stop(); }

bool NodeProcess::start(bool isManual) {

    stop();
    m_readBuffer.clear();
    m_pendingCallbacks.clear();
    m_nextId = 1;

    if (isManual) {

        m_restartCount = 0;
        m_autoRestart = true;
    }

    QString program = getNodePath();
    QString scriptPath = QDir(m_pluginPath).absoluteFilePath("main.js");
    m_process->setWorkingDirectory(m_pluginPath);

    m_process->start(program, QStringList() << scriptPath  <<  QString::number(QCoreApplication::applicationPid()));
    if (!m_process->waitForStarted(3000)) {
        AppendEventLog("NodeProcess start failed:" + m_process->errorString(), 0xff0000);
        return false;
    }

    // 启动成功，重置重启计数（表示已稳定运行一次）
    m_restartCount = 0;
    return true;
}

void NodeProcess::stop() {
    m_autoRestart = false;
    if (m_restartTimer && m_restartTimer->isActive())
        m_restartTimer->stop();
    m_restartCount = 0;

    if (m_process->state() == QProcess::Running) {
        m_process->terminate();
        m_process->waitForFinished(1000);
        m_process->kill();
    }
}

void NodeProcess::writeMessage(const QByteArray& jsonMsg) {
    if (m_process->state() != QProcess::Running) return;
    QByteArray data;
    quint32 len = jsonMsg.size();
    data.append((char)(len >> 24)).append((char)(len >> 16))
        .append((char)(len >> 8)).append((char)len);
    data.append(jsonMsg);
    m_process->write(data);
}

int NodeProcess::sendRequest(const QString& method, const QJsonArray& params,
                             std::function<void(const QJsonObject&)> callback) {
    int id = m_nextId++;
    m_pendingCallbacks.insert(id, callback);
    QJsonObject req;
    req["id"] = id;
    req["method"] = method;
    req["params"] = params;
    writeMessage(QJsonDocument(req).toJson(QJsonDocument::Compact));
    return id;
}

void NodeProcess::sendResponse(int id, const QString& result, const QString& error) {
    QJsonObject response;
    response["id"] = id;
    if (error.isEmpty())
        response["result"] = result;
    else
        response["error"] = error;
    writeMessage(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

void NodeProcess::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    QJsonObject ev;
    ev["type"] = enabled ? "on_enable" : "on_disable";
    writeMessage(QJsonDocument(ev).toJson(QJsonDocument::Compact));
}

void NodeProcess::onReadyRead() {
    m_readBuffer.append(m_process->readAllStandardOutput());
    while (m_readBuffer.size() >= 4) {
        quint32 len = ((quint8)m_readBuffer[0] << 24) |
                      ((quint8)m_readBuffer[1] << 16) |
                      ((quint8)m_readBuffer[2] << 8)  |
                      (quint8)m_readBuffer[3];
        if (m_readBuffer.size() < 4 + len) break;
        QByteArray jsonData = m_readBuffer.mid(4, len);
        m_readBuffer.remove(0, 4 + len);
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(jsonData, &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "JSON parse error:" << err.errorString();
            continue;
        }
        QJsonObject obj = doc.object();
        if (obj.contains("id") && obj.contains("result")) {
            int id = obj["id"].toInt();
            if (auto it = m_pendingCallbacks.find(id); it != m_pendingCallbacks.end()) {
                it.value()(obj["result"].toObject());
                m_pendingCallbacks.erase(it);
            }
        } else if (obj.contains("id") && obj.contains("method")) {
            int id = obj["id"].toInt();
            QString method = obj["method"].toString();
            QJsonArray params = obj["params"].toArray();
            emit requestReceived(id, method, params);
        }
    }
}

void NodeProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    qDebug() << "Node process finished, code:" << exitCode << "status:" << status;
    emit exited();

    // 只有允许自动重启时才进行重启逻辑
    if (!m_autoRestart)
        return;

    // 超过最大重启次数则停止自动重启
    if (m_restartCount >= MAX_RESTART_COUNT) {
        AppendEventLog("Node进程频繁崩溃，已达最大重启次数(" + QString::number(MAX_RESTART_COUNT) + ")，停止自动重启", 0xff0000);
        m_autoRestart = false;
        return;
    }

    m_restartCount++;
    // 延迟2秒后重启，避免瞬间疯狂重启
    m_restartTimer->start(2000);
}

void NodeProcess::onRestartTimer() {
    // 重启前再次检查是否允许自动重启（可能在等待期间被 stop() 禁止）
    if (!m_autoRestart)
        return;

    // 内部状态已在 start(false) 中清理，这里直接调用自动重启
    if (!start(false)) {
        AppendEventLog("Node进程自动重启失败", 0xff0000);
        // 启动失败不再继续尝试，避免无限循环
        m_autoRestart = false;
    }
}