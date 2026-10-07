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
#include <QFile>
#include <QFileInfo>
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

QString NodeProcess::hostScriptPath()
{
    return QCoreApplication::applicationDirPath() + "/node_host/host.js";
}

// 把 qrc 里的脚本释放到磁盘（内容没变就不重写，省得每次都动时间戳）
static bool dumpHostScript(const QString& resPath, const QString& outPath)
{
    QFile in(resPath);
    if (!in.exists() || !in.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = in.readAll();
    in.close();

    QFileInfo fi(outPath);
    if (!fi.absoluteDir().exists() && !QDir().mkpath(fi.absolutePath())) return false;

    QFile old(outPath);
    if (old.open(QIODevice::ReadOnly)) {
        const QByteArray cur = old.readAll();
        old.close();
        if (cur == data) return true;
    }

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool ok = (out.write(data) == data.size());
    out.close();
    return ok;
}

void NodeProcess::ensureScriptsOnDisk()
{
    const QString dir = QCoreApplication::applicationDirPath() + "/node_host";
    const QStringList names = { "host.js", "worker.js" };
    for (const QString& name : names) {
        const QString outPath = dir + "/" + name;
        // 优先用内嵌资源（分发时只发 exe 就行）；没有内嵌资源就要求磁盘上已经有
        if (dumpHostScript(":/node_host/" + name, outPath)) continue;
        if (QFile::exists(outPath)) continue;
        AppendEventLog("[JS宿主] 缺少 " + outPath + "，请确认 resources.qrc 里有 node_host/" + name, 0xff0000);
    }
}

NodeProcess::NodeProcess(QObject* parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    m_restartTimer = new QTimer(this);
    m_restartTimer->setSingleShot(true);

    connect(m_restartTimer, &QTimer::timeout, this, &NodeProcess::onRestartTimer);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &NodeProcess::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &NodeProcess::onStdErr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NodeProcess::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &NodeProcess::onProcessError);
}

NodeProcess::~NodeProcess() { stop(); }

bool NodeProcess::start(bool isManual)
{
    if (isRunning()) return true;

    ensureScriptsOnDisk();

    m_readBuffer.clear();
    if (isManual) {
        m_restartCount = 0;
        m_autoRestart = true;
    }

    const QString program = getNodePath();
    const QString script = hostScriptPath();
    const QString workDir = QCoreApplication::applicationDirPath();

    // 宿主工作目录固定为「框架运行目录」：Worker 里 process.chdir 不可用，
    // 所以插件相对路径的基准就是这个目录（和旧的「每插件一个进程、cwd=插件目录」不同）。
    m_process->setWorkingDirectory(workDir);
    m_process->start(program, QStringList() << script << QString::number(QCoreApplication::applicationPid()));

    if (!m_process->waitForStarted(5000)) {
        AppendEventLog("[JS宿主] node 启动失败：" + m_process->errorString(), 0xff0000);
        return false;
    }

    m_restartCount = 0;
    return true;
}

void NodeProcess::stop()
{
    m_autoRestart = false;
    if (m_restartTimer && m_restartTimer->isActive()) m_restartTimer->stop();
    m_restartCount = 0;
    m_pendingCallbacks.clear();

    if (m_process && m_process->state() == QProcess::Running) {
        m_process->terminate();
        if (!m_process->waitForFinished(1500)) m_process->kill();
    }
}

void NodeProcess::writeMessage(const QByteArray& jsonMsg)
{
    if (!isRunning()) return;
    QByteArray data;
    const quint32 len = jsonMsg.size();
    data.append((char)(len >> 24)).append((char)(len >> 16))
        .append((char)(len >> 8)).append((char)len);
    data.append(jsonMsg);
    m_process->write(data);
}

int NodeProcess::sendRequest(const QString& method, const QJsonObject& extra,
                            std::function<void(const QJsonValue&, const QString&)> callback)
{
    const int id = m_nextId++;          // 从 REQ_ID_BASE 起，避开插件自己的 id 空间
    if (callback) m_pendingCallbacks.insert(id, callback);

    QJsonObject req = extra;
    req["id"] = id;
    req["method"] = method;
    writeMessage(QJsonDocument(req).toJson(QJsonDocument::Compact));
    return id;
}

void NodeProcess::forgetRequest(int id)
{
    m_pendingCallbacks.remove(id);
}

void NodeProcess::sendResponse(int id, const QString& uuid, const QString& result, const QString& error)
{
    QJsonObject response;
    response["id"] = id;
    // uuid 必须带：宿主靠它把回包转回对应的 Worker
    response["uuid"] = uuid;
    if (error.isEmpty()) response["result"] = result;
    else                 response["error"] = error;
    writeMessage(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

void NodeProcess::onReadyRead()
{
    m_readBuffer.append(m_process->readAllStandardOutput());
    while (m_readBuffer.size() >= 4) {
        const quint32 len = ((quint8)m_readBuffer[0] << 24) |
                            ((quint8)m_readBuffer[1] << 16) |
                            ((quint8)m_readBuffer[2] << 8)  |
                            (quint8)m_readBuffer[3];
        if (len > 64u * 1024u * 1024u) {           // 明显不是帧，丢弃重同步
            qWarning() << "[JS宿主] 收到异常帧头，丢弃" << m_readBuffer.size() << "字节";
            m_readBuffer.clear();
            break;
        }
        if (m_readBuffer.size() < int(4 + len)) break;
        const QByteArray jsonData = m_readBuffer.mid(4, int(len));
        m_readBuffer.remove(0, int(4 + len));

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "[JS宿主] JSON parse error:" << err.errorString();
            continue;
        }
        const QJsonObject obj = doc.object();

        // 回包：{id, result, error}。result 可能是对象也可能是字符串，原样交给回调自己判；
        // error 非空 = 插件侧抛异常或方法不存在（{id, method, params} 那种请求不带 result/error）
        if (obj.contains("id") && (obj.contains("result") || obj.contains("error"))) {
            const int id = obj["id"].toInt();
            if (auto it = m_pendingCallbacks.find(id); it != m_pendingCallbacks.end()) {
                auto cb = it.value();
                m_pendingCallbacks.erase(it);
                cb(obj["result"], obj["error"].toString());
            }
        } else if (obj.contains("id") && obj.contains("method")) {
            emit requestReceived(obj["id"].toInt(),
                                 obj["uuid"].toString(),
                                 obj["method"].toString(),
                                 obj["params"].toArray());
        }
    }
}

void NodeProcess::onStdErr()
{
    const QByteArray raw = m_process->readAllStandardError();
    if (raw.trimmed().isEmpty()) return;
    for (const QByteArray& line : raw.split('\n')) {
        const QByteArray t = line.trimmed();
        if (!t.isEmpty()) AppendEventLog(QString::fromUtf8(t), 0xff0000);
    }
}

void NodeProcess::onProcessError()
{
    qWarning() << "[JS宿主] QProcess error:" << m_process->errorString();
}

void NodeProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    qDebug() << "Node host finished, code:" << exitCode << "status:" << status;
    m_pendingCallbacks.clear();
    emit exited();

    if (!m_autoRestart) return;

    if (m_restartCount >= MAX_RESTART_COUNT) {
        AppendEventLog("[JS宿主] node 宿主频繁崩溃，已达最大重启次数("
                       + QString::number(MAX_RESTART_COUNT) + ")，停止自动重启", 0xff0000);
        m_autoRestart = false;
        return;
    }

    m_restartCount++;
    m_restartTimer->start(2000);
}

void NodeProcess::onRestartTimer()
{
    if (!m_autoRestart) return;
    if (!start(false)) {
        AppendEventLog("[JS宿主] node 宿主自动重启失败", 0xff0000);
        m_autoRestart = false;
        return;
    }
    m_autoRestart = true;         // 重启成功 → 下次崩了还能再拉起来
    emit restarted();
}
