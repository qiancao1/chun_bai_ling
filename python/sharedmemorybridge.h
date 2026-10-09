/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * 与 32 位易语言模块（纯白铃32.exe）的通信桥
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
#include <QStringList>
#include <QMutex>
#include <QWaitCondition>
#include <QHash>
#include <functional>
#include <string>
#include <atomic>
#include <windows.h>

// 注意：类名沿用 SharedMemoryBridge（改名要动 5 个外部文件）；
// 2026-10-06 传输层重写时**公开接口一字未改**（writeResponseToBlock / startServer /
// stopServer / restartYiProcess / setCallback），调用方一行没动。
// ⚠ processRequestsA 在 2026-10-09 升级成**多路**（新增带 reqId 的重载 + 非阻塞三件套），
//   旧的单参版本保留为「不分路」语义，老调用点照常能编。
//
// 内部实现经历过一次来回：共享内存+事件 →（2026-10-06 上午）命名管道 →（2026-10-06 下午，用户拍板）
// **又回到共享内存+事件**。当前协议全文见 sharedmemorybridge.cpp 顶部注释，动它之前先读那段。
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

    // 与易语言约定的保留 api_id：易语言用它把「宿主命令的返回值」回吐给 Qt。
    // 易语言侧写法：_post4 (1831501026, appid, "", 结果json)
    static constexpr int API_ID_CMD_RESULT = 1831501026;

    explicit SharedMemoryBridge(QObject *parent = nullptr);
    ~SharedMemoryBridge();

    void setCallback(Callback cb);

    bool startServer(bool debug);
    void stopServer();
    bool restartYiProcess();

    // Qt -> 易语言：投递一条任务（写进任务槽 + 敲响应事件）。
    //   type = 1：交给易语言宿主窗口的反馈事件（加载/同步插件、心跳、退出…）
    //   type = 2：交给 32 位插件 DLL 的 on_message
    // ⚠ 发完就返回，不等回信 —— **唯一「等返回」的通道是 processRequestsA()**。
    //   只有 type = 1 才可能有返回值（类型 2 是甩完就走，等也没人回）。
    bool writeResponseToBlock(int type, const char *text);

    // 取易语言回吐的命令返回值（易语言 _post4(1831501026, reqId, "", 结果json) 送来的那个 json）。
    // 这是**唯一「等返回」的通道**：最多等 timeoutMs，等不到就返回空串。
    // ⚠ 命令**允许返回空值**（语义 = 「没有消息」），空串是**合法结果**、不是超时 ——
    //   调用方按「无内容」处理即可，别拿 isEmpty() 当失败判据（真超时只有日志里那条 warning）。
    // reqId 版本 = 多路：只认准自己那一条的结果；单参版本 = 兼容老写法（等价于 reqId 0 =「不分路」）。
    QString processRequestsA(int reqId, int timeoutMs);
    QString processRequestsA(int timeoutMs);

    // ---- 「命令返回值」通道：**多路**（2026-10-09 二版，替换掉「同一时间只允许一条在飞」）----
    // 起因：回执原先不带任何标识（`_post4(1830501026, appid, "", json)` 只有内容），框架无从判断
    // 它对应哪次请求，只能靠「一次只放一条在飞」来保证不串包。
    // 现在改成：发命令前先领一个请求 id，塞进发给易语言的 JSON（字段名 "reqid"），
    // 易语言回吐时**原样带回**（_post4 的第 3 个参数，原来是 ""，现在填 reqId），框架按 id 路由。
    // 于是同一时刻可以有多条命令在飞。
    //   prepareCommandWait()        发号 + 登记，一步完成（原子）；返回 >0 = 请求 id，0 = 登记失败
    //   pollCommandResult(id,out)   非阻塞看一眼；true = 拿到了（**空串也算拿到**，out 有效）
    //   endCommandWait(id)          注销（迟到的结果按「没人在等」丢弃，防串包）
    // ⚠ 易语言**没带回 id** 时（还没改的老版本）自动退化：唯一一个等待者才能拿到值，
    //   多个在等就丢弃 + warning —— 宁可失败也不串包。
    // ⚠ 「只发不等」的命令（sendData32NoWait）也要带上 "reqid" = NO_WAIT_REQ_ID，
    //   免得它的回执（无 id）被当成正在等的那条的结果喂进去。
    static constexpr int NO_WAIT_REQ_ID = -1;   // 「只发不等」哨兵：永远不会有人等它

    int  prepareCommandWait();
    bool pollCommandResult(int reqId, QString &out);
    void endCommandWait(int reqId);

private:
    // ---- 共享内存协议常量（改这里必须同步改易语言）----
    static constexpr int kSlotCount = 50;                  // 请求槽 / 任务槽各 50 个
    static constexpr int kSlotSize  = 100 * 1024;          // 每槽 100KB
    static constexpr int kShmSize   = 10 * 1024 * 1024;    // 共享内存总大小 10MB
    static constexpr int kTaskBase  = 5 * 1024 * 1024;     // 任务槽区起始偏移（后半 5MB）

    static char *slotAt(char *base, int index) { return base + static_cast<size_t>(index) * kSlotSize; }

    struct RequestData {
        int apiId = 0;
        qint64 appid = 0;      // 易语言侧 api 的 uid（4 字节）
        int reqId = 0;         // 请求 ID，回响应时必须原样带回
        int slot  = -1;        // 请求槽下标（回响应用；异步为 -1 也没关系）
        bool sync = false;     // true = 客户端在等响应，处理完必须回写
        std::string uuid;
        std::string texts[8];
    };

    class Task : public QRunnable {
    public:
        Task(SharedMemoryBridge *bridge, const RequestData &req)
            : m_bridge(bridge), m_req(req) {}
        void run() override;
    private:
        SharedMemoryBridge *m_bridge;
        RequestData m_req;
    };

    // ---- 共享内存 / 事件 ----
    bool createIpc();                    // 建共享内存（并清零）+ 建三个事件
    void closeIpc();                     // 释放全部内核对象
    bool openAckEvents();                // 打开客户端建的 50 个响应槽事件（<base>_r_1..50）
    bool ackEventsReady() const;

    // ---- 工作线程 ----
    void startWorker();
    void shutdownWorker();
    void workerLoop();
    void scanRequestSlots();
    void clearRequestSlot(int slotIndex);
    bool writeSlotResponse(int slotIndex, int reqId, const char *text);
    void parseRequestBody(const char *data, quint32 len, RequestData &req) const;
    void deliverCommandResult(const RequestData &req);

    // ---- 纯白铃32.exe 进程管理 ----
    bool launchYiProcess();
    bool hideYiWindow();             // 兜底隐藏 32 位窗口（成功藏到 ≥1 个才返回 true）
    bool yiProcessExited(DWORD &exitCode) const;              // 调用者需持 m_procMutex
    bool takeDeadYiProcess(DWORD &exitCode, DWORD &pid);      // 内部加锁；已退出则顺手回收句柄
    void closeYiProcessHandle(bool killIfAlive, const char *why);

    // ---- 名字 ----
    QString m_baseName;              // 命令行参数：基础名（旧版传的是共享内存名 + 两个事件名）
    QString m_mapName;               // <base>_hMap
    QString m_reqEventName;          // <base>_hReqEvent
    QString m_respEventName;         // <base>（响应事件就是基础名本体）
    QString m_exePath;
    QStringList m_args;

    // ---- 内核对象 ----
    HANDLE m_hMap        = nullptr;  // 共享内存句柄
    char  *m_pMem        = nullptr;  // 映射基址（10MB）
    HANDLE m_hReqEvent   = nullptr;  // 客户端 SetEvent → Qt 扫请求槽
    HANDLE m_hRespEvent  = nullptr;  // Qt SetEvent → 客户端扫任务槽
    HANDLE m_hStopEvent  = nullptr;  // Qt 内部：让等待立刻返回
    HANDLE m_ackEvents[kSlotCount] = { nullptr };   // 客户端建、Qt 开：第 N 槽的响应好了

    // ---- 线程 ----
    QThread     *m_workerThread = nullptr;
    QThreadPool  m_pool;             // 桥接专用线程池（stopServer 时能安全 waitForDone）
    QMutex       m_taskMutex;        // 串行化任务槽写入（writeResponseToBlock 可能多线程调用）
    QMutex       m_procMutex;        // 保护 m_hYiProcess / m_yiPid
    Callback     m_callback;

    // ---- 子进程 ----
    HANDLE m_hYiProcess = nullptr;   // 纯白铃32.exe 进程句柄（判活 / 取退出码 / 强杀）
    DWORD  m_yiPid      = 0;

    // ---- 命令返回值通道（唯一「等返回」的地方）----
    // 多路：key = 请求 id（由 m_cmdSeq 派发），value = 这一路的等待状态。
    // key = 0 是「不分路」的老式等待者（单参 processRequestsA），最多只允许一个。
    struct CmdWait {
        QString result;
        bool    pending = false;   // true = 结果已到（**空串也算到**）
    };
    QMutex              m_cmdMutex;
    QWaitCondition      m_cmdCond;
    QHash<int, CmdWait> m_cmdWaiters;
    int                 m_cmdSeq = 0;   // 自增发号器，从 1 开始（0 保留给「不分路」）

    bool m_debug = false;
    std::atomic<bool> m_stop{false};
};

#endif // SHAREDMEMORYBRIDGE_H
