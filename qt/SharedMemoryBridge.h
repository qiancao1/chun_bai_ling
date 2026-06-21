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

#ifndef SHAREDMEMORYBRIDGE_H
#define SHAREDMEMORYBRIDGE_H

#include <QObject>
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QString>
#include <functional>
#include <windows.h>

class SharedMemoryBridge : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<const char*(
        const char* uuid, int apiId, qint64 uid,
        const char* _1, const char* _2,
        const char* _3, const char* _4,
        const char* _5, const char* _6,
        const char* _7, const char* _8)>;

    explicit SharedMemoryBridge(QObject *parent = nullptr);
    ~SharedMemoryBridge();
    bool writeResponseToBlock(quint32 resultAddr, const char* response);
    QString processRequestsA(int timeoutMs);
    void setCallback(Callback cb);
    bool startServer(bool debug);
    void stopServer();
    HANDLE m_hReqEvent = nullptr;   // 易语言 → Qt 通知有请求
    HANDLE m_hRespEvent = nullptr;  // Qt → 易语言 通知有响应
    bool restartYiProcess();   // 重启易语言进程

private:
    void workerLoop();
    void processRequests();
    QString m_exePath;             // miaomiao32.exe 完整路径
    QStringList m_args;            // 命令行参数（共享内存名、事件名等）
    struct RequestData {
        quint32 resultAddr = 0;
        int apiId = 0;
        qint64 uid = 0;
        std::string uuid;
        std::string texts[8];
    };
    bool copyRequestFromBlock(int blockIndex, RequestData &outReq);

    static std::string readNullTerminatedString(const char* start, size_t maxLen);

    class Task : public QRunnable {
    public:
        Task(SharedMemoryBridge *bridge, const RequestData &req)
            : m_bridge(bridge), m_req(req) {}
        void run() override;
    private:
        SharedMemoryBridge *m_bridge;
        RequestData m_req;
    };

    QString m_sharedMemName;
    HANDLE m_hMapFile = nullptr;
    void* m_pView = nullptr;

    bool m_debug;
    std::atomic<bool> m_stop{false};

    QThread* m_workerThread = nullptr;
    Callback m_callback;
};

#endif // SHAREDMEMORYBRIDGE_H