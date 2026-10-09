/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * 与 32 位易语言模块（纯白铃32.exe）的通信桥 —— 共享内存 + 事件版
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

/*
 * ==================== 通信协议（Qt = 服务端，易语言 = 客户端） ====================
 * 2026-10-06 下午起：命名管道 → 共享内存 + 事件（易语言侧同步改的，Qt 侧跟着改）。
 * ⚠ 别再改回命名管道：易语言用的是**同步** WriteFile，只要 Qt 那端一停止读，
 *   它的 UI 线程就会永久卡死在 WriteFile 上（实测过，非常难查）。共享内存没有这个问题。
 *
 * 一、四个内核对象（**都由 Qt 先建，易语言 Open 打开**）
 *   共享内存    <base>_hMap          10MB
 *   请求事件    <base>_hReqEvent     自动重置；客户端 SetEvent → 「有新请求，去遍历 50 个请求槽」
 *   响应事件    <base>               自动重置；Qt SetEvent     → 「有任务，去遍历 50 个任务槽」
 *   响应槽事件  <base>_r_1 .. _r_50  自动重置；**客户端 CreateEventA 创建，Qt OpenEventA 打开**；
 *                                    Qt SetEvent(_r_N) = 「第 N 号请求槽的响应写好了」
 *   ⚠ 三个事件**必须全部是「自动重置」**（CreateEvent 的 bManualReset = FALSE）。
 *     建成手动重置的话，客户端 WaitForSingleObject 醒来后事件不会清 → 立刻又返回
 *     → 空转遍历 50 个槽，跑满一个核。
 *   ⚠ 启动命令行只传 1 个参数 = <base>（旧版传的是共享内存名 + 两个事件名，三个参数）。
 *     调试版（在易语言 IDE 里直接运行）名字写死 "aaaaa"/"bbbbb"/"ccccc"，不看命令行 ——
 *     Qt 的 debug 模式跟着用这三个，且不自己启动进程。
 *   ⚠ 启动进程用 CreateProcessW + CREATE_NEW_CONSOLE，**不要**用 QProcess::startDetached
 *     （它内部固定 CREATE_NO_WINDOW，会把易语言那个控制台程序的窗口藏掉，看不到任何输出）；
 *     且 lpCommandLine **必须以 exe 路径开头**（易语言「取命令行」会丢掉第一个 token）。
 *
 * 二、内存布局（10MB，全部小端）
 *   pMem + 0            请求槽区  50 × 100KB   客户端 → Qt
 *   pMem + 5×1024×1024  任务槽区  50 × 100KB   Qt → 客户端
 *
 *   请求槽：[0]状态 [4]api_id [8]uid [12]请求ID [16]数据长度 [20]数据体
 *       状态：0 = 空，1 = 客户端已占用（**可能还在写数据**），2 = 响应就绪
 *       数据体 = uuid\0 text1\0 text2\0 ... text8\0（总字节数 = 数据长度）
 *   任务槽：[0]状态 [4]目标 [8]长度 [12]数据
 *       状态：0 = 空，2 = 任务就绪
 *       目标：1 = 宿主窗口反馈事件，2 = 32 位插件 on_message
 *       ← writeResponseToBlock(type, text) 的 type 就是这里的「目标」，所以旧写法一字不改
 *
 * 三、一次「同步」请求（客户端要结果，99% 是这种）
 *   ① 客户端：找空槽 → 置 1 占坑 → 写 api_id/uid/请求ID → 写 uuid+8 个 text → 回填长度
 *   ② 客户端：ResetEvent(_r_N) → SetEvent(_hReqEvent) → WaitForSingleObject(_r_N, 16000)
 *   ③ Qt    ：被 _hReqEvent 唤醒 → 遍历 50 槽，见「状态 == 1 且 长度 > 0」→ 拷出数据
 *             → 置状态 4（服务端处理中）→ 丢线程池（绝不在接收逻辑里就地跑回调，否则堵住后面的请求）
 *   ④ Qt    ：回调拿结果 → 结果写回 offset 20（补 \0）→ 长度归零 → 置状态 2 → SetEvent(_r_N)
 *   ⑤ 客户端：醒来校验「状态 == 2 且 offset 12 == 请求ID」→ 读结果 → 把槽清回 0
 *
 * 四、「异步」请求（客户端不等结果，槽由 **Qt** 清空）
 *   判定：api_id <= 1（0/1，其中 1 = 插件输出日志）或 api_id >= 10000（10000 心跳存活、
 *         10001/10002 异常通知），或 api_id == 2 且 text6 == "true"。
 *   Qt 拷完数据立刻把槽清 0，然后照样丢线程池跑回调（结果丢弃）。
 *   api_id == 1831501026（API_ID_CMD_RESULT）单独走：易语言把「宿主命令的返回值」回吐给 Qt，
 *   Qt 按**请求 id** 唤醒对应的等待者，**不跑插件回调**、不占槽。
 *   ⚠ 该返回值**允许为空**（语义 =「没有消息」，很多命令本来就没输出）：空串是**合法结果**，
 *     照样唤醒等待者立刻返回；只有「一条都没等到」才算真超时。
 *   ⚠⚠ **回执必须带请求 id 才能多路**（2026-10-09）：Qt 发命令时在 JSON 里塞了 `"reqid"`，
 *     易语言回吐时把它填进 `_post4` 的**第 3 个参数**（原来是 `""` 那个位置）：
 *         _post4 (1831501026, appid, 到文本 (reqid), 结果json)
 *     Qt 从 `req.uuid`（= 第 3 个参数）读这个数字，路由到对应的等待者。
 *     没带回 id（老易语言）→ 只有「唯一一个等待者」时才敢交付；多个在等就丢弃（宁可失败也不串包）。
 *     只发不等的命令（reqid = -1）回执永远匹配不上任何等待者，天然被丢弃。
 *
 * 五、⚠ 竞态与硬约定（Qt 侧已做兜底，但客户端那边最好也配合改）
 *   ① 「占坑」与「写数据」之间有窗口：状态已是 1、长度还是旧值或 0。
 *      Qt 用「**长度 == 0 视为尚未写完**」来跳过（Qt 每次用完槽都把长度清 0，客户端那侧
 *      的槽被客户端自己清 0 时长度也已经是 0）。跳过的槽不会丢：客户端写完后 SetEvent(_hReqEvent)，
 *      Qt 会再扫一遍。而 _hReqEvent 是自动重置的，扫描期间新来的信号也不会丢。
 *      ⚠ 客户端那边建议把 ResetEvent(_r_N) 挪到「占坑之后、写数据之前」（现在它在写完数据之后），
 *        否则 Qt 被别的线程的请求唤醒时，有可能顺手处理了这个还没 SetEvent 的槽，
 *        紧接着客户端的 ResetEvent 就把这个响应信号吃掉 → 白等 16 秒超时。
 *   ② 状态值两边只在 0（空）和 2（就绪）上互认；Qt 内部用 4 = 「服务端处理中」，
 *      客户端只判断 0（找空槽）和 2（收任务 / 取响应），不会误用 4。
 *   ③ Qt 建好共享内存后**必须 memset 清零**（客户端那边 OpenFileMapping 成功就不清零，
 *      残留数据会被当成有效槽）。
 *   ④ 客户端最多等 16 秒。超时后它会自己把槽清 0，别人可能马上占用同一个槽 ——
 *      所以 Qt 回响应前必须确认「状态还是 4 且 请求ID 还是我这条」，否则丢弃响应。
 * ==============================================================================
 */

#include "sharedmemorybridge.h"
#include "global.h"

#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include <QUuid>
#include <QFile>
#include <QElapsedTimer>
#include <QTimer>
#include <QDebug>
#include <QByteArray>

#include <cstring>
#include <climits>

// ---------------------------- 常量 ----------------------------
static const DWORD IDLE_POLL_MS     = 1000;      // 空闲轮询间隔（顺便用来查客户端进程还活着没）
static const int   IDLE_LOG_SEC     = 60;        // 空闲多久在日志里报一次「仍在监听」
static const DWORD ACK_OPEN_WAIT_MS = 30000;     // 等客户端把 50 个响应槽事件建出来（最多 30 秒）

// 槽内偏移（请求槽与任务槽共用前 20 字节的排布）
static const int SLOT_OFF_STATE = 0;
static const int SLOT_OFF_X     = 4;    // 请求槽 = api_id，任务槽 = 目标
static const int SLOT_OFF_Y     = 8;    // 请求槽 = uid，   任务槽 = 长度
static const int SLOT_OFF_REQID = 12;   // 请求槽 = 请求ID，任务槽 = 数据起始
static const int SLOT_OFF_LEN   = 16;   // 只有请求槽用
static const int SLOT_OFF_DATA  = 20;   // 只有请求槽用

// 状态值
static const LONG SLOT_STATE_EMPTY = 0;   // 两边共用：空
static const LONG SLOT_STATE_REQ   = 1;   // 客户端：已占用（可能还在写）
static const LONG SLOT_STATE_RDY   = 2;   // 客户端：响应就绪 / 任务就绪
static const LONG SLOT_STATE_BUSY  = 4;   // Qt 私有：服务端处理中（客户端不认这个值，会跳过）

SharedMemoryBridge *bridge = nullptr;

// 心跳计数（定义在 core/global.cpp）。mainwindow 的心跳 timer 每 3 秒把它 +1，
// 满 4 次就重启 纯白铃32.exe；旧实现**只有**易语言发 api_id = 10000 才清零。
// 共享内存时代没必要把判活条件卡这么窄：**只要有请求过来就说明它活着**，
// 而且进程句柄还在我们手上（真崩了 WaitForSingleObject 立刻就知道）→ 顺手清零，避免误重启。
extern int miaomiao32;

static inline void    put32(char *p, quint32 v) { memcpy(p, &v, 4); }
static inline quint32 get32(const char *p) { quint32 v = 0; memcpy(&v, p, 4); return v; }

// 状态字段的读写都走 InterlockedExchange（带全屏障）：Qt 是「先写数据、最后写状态」，
// 客户端是「先读状态、再读数据」，中间必须有屏障，否则编译器/CPU 重排会让对端读到半截数据。
static inline void setSlotState(char *slot, LONG v)
{
    MemoryBarrier();
    InterlockedExchange(reinterpret_cast<volatile LONG *>(slot), v);
}

static inline LONG getSlotState(const char *slot)
{
    const LONG v = *reinterpret_cast<const volatile LONG *>(slot);
    MemoryBarrier();
    return v;
}

// 易语言的 约等于_Airuan(text6, "true") —— 宽松比较，这里按「去空白 + 忽略大小写」处理
static bool isTrueish(const std::string &s)
{
    int b = 0, e = static_cast<int>(s.size());
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    if (e - b != 4) return false;
    const char *p = s.c_str() + b;
    return (p[0] == 't' || p[0] == 'T') && (p[1] == 'r' || p[1] == 'R')
        && (p[2] == 'u' || p[2] == 'U') && (p[3] == 'e' || p[3] == 'E');
}

// ---------------------------- 生命周期 ----------------------------

SharedMemoryBridge::SharedMemoryBridge(QObject *parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(4);            // 桥接专用池：stopServer 时能安全 waitForDone
    m_pool.setExpiryTimeout(30000);
}

SharedMemoryBridge::~SharedMemoryBridge()
{
    stopServer();
}

void SharedMemoryBridge::setCallback(Callback cb)
{
    m_callback = std::move(cb);
}

bool SharedMemoryBridge::startServer(bool debug)
{
    if (m_hMap || m_pMem) return false;
    m_debug = debug;

    if (debug) {
        // 易语言调试版（IDE 里直接跑、不带命令行）名字写死，且**不是** base + 后缀，跟着它用
        m_baseName      = QStringLiteral("aaaaa");
        m_mapName       = QStringLiteral("aaaaa");
        m_reqEventName  = QStringLiteral("bbbbb");
        m_respEventName = QStringLiteral("ccccc");
    } else {
        m_baseName      = QString("QtBridge_%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
        m_mapName       = m_baseName + QStringLiteral("_hMap");
        m_reqEventName  = m_baseName + QStringLiteral("_hReqEvent");
        m_respEventName = m_baseName;                  // 响应事件 = 基础名本体（无后缀）
    }

    // 先把共享内存和事件建好，再启动易语言（它起来就去 Open，晚一点就会弹「打开事件失败」）
    if (!createIpc()) {
        closeIpc();
        qCritical() << "共享内存: 初始化失败，桥接未启动";
        return false;
    }

    m_exePath = QCoreApplication::applicationDirPath() + "/纯白铃32.exe";
    m_args.clear();
    m_args << m_baseName;          // 只传 1 个参数：基础名（旧版传的是 共享内存名 + 两个事件名）

    qDebug() << "共享内存: 已就绪 base =" << m_baseName
             << " 共享内存 =" << m_mapName
             << " 请求事件 =" << m_reqEventName
             << " 响应事件 =" << m_respEventName;

    startWorker();
    if (!debug)
        launchYiProcess();
    else
        qDebug() << "共享内存: 调试模式，不自动启动 纯白铃32.exe（请自己在易语言 IDE 里运行）";

    return true;
}

void SharedMemoryBridge::stopServer()
{
    shutdownWorker();

    // 顺手把 32 位宿主也收掉：Qt 一走，它就成了任务管理器里清不掉的孤儿进程
    closeYiProcessHandle(true, "停止桥接");

    {
        QMutexLocker lk(&m_cmdMutex);
        m_cmdWaiters.clear();       // 所有等待者一并注销：各自醒来会发现自己在等的那路没了 → 立刻返回
    }
    m_cmdCond.wakeAll();            // 唤醒可能还在等响应的调用者
}

bool SharedMemoryBridge::restartYiProcess()
{
    if (m_debug) return true;

    DWORD pid = 0;
    {
        QMutexLocker lk(&m_procMutex);
        pid = m_yiPid;
    }

    qWarning() << "共享内存: 重启 纯白铃32.exe（心跳连续无回执，或上层主动要求）";

    writeResponseToBlock(1, "{\"type\":6}");     // 先礼：请它自己退

    // 等它自己退，最多 1.2 秒；不退就强杀。
    // ⚠ 只 sleep(300) 就往下走的话，来不及退的进程会一个又一个叠成僵尸进程。
    bool exited = false;
    DWORD code = 0;
    for (int i = 0; i < 12; ++i) {
        {
            QMutexLocker lk(&m_procMutex);
            if (!m_hYiProcess || yiProcessExited(code)) { exited = true; break; }
        }
        QThread::msleep(100);
    }
    if (pid) {
        if (exited)
            qWarning("共享内存: 上一个进程(pid=%lu)已自行退出，退出码=0x%08lX",
                     static_cast<unsigned long>(pid), static_cast<unsigned long>(code));
        else
            qWarning("共享内存: 上一个进程(pid=%lu)还活着但没回执",
                     static_cast<unsigned long>(pid));
    }
    closeYiProcessHandle(!exited, exited ? "重启：已自行退出" : "重启：等 1.2 秒未退出");

    // 旧客户端一退，它建的 50 个响应槽事件就随之消失，共享内存里也会留下「已占用」的残槽。
    // 所以整块重来：释放 → 重建 → 清零。
    shutdownWorker();
    if (!createIpc()) return false;
    startWorker();

    return launchYiProcess();
}

// ---------------------------- 共享内存 / 事件 ----------------------------

bool SharedMemoryBridge::createIpc()
{
    if (m_hMap || m_pMem) return false;

    const QByteArray mapName = m_mapName.toLocal8Bit();
    m_hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                0, static_cast<DWORD>(kShmSize), mapName.constData());
    if (!m_hMap) {
        qCritical("CreateFileMapping(%s) failed, err=%lu", mapName.constData(), GetLastError());
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 上一次运行（或另一个实例）留下的对象。我们仍然是唯一的管理者，靠下面的 memset 复位。
        qWarning() << "共享内存:" << m_mapName << "已存在（上一次运行的残留？），将清零后复用";
    }

    m_pMem = static_cast<char *>(MapViewOfFile(m_hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0));
    if (!m_pMem) {
        qCritical("MapViewOfFile(%s) failed, err=%lu", mapName.constData(), GetLastError());
        closeIpc();
        return false;
    }

    // ⚠ 必须自己清零：易语言那边 OpenFileMapping 成功就不清零，残留数据会被当成有效槽
    memset(m_pMem, 0, static_cast<size_t>(kShmSize));

    const QByteArray reqName = m_reqEventName.toLocal8Bit();
    const QByteArray rspName = m_respEventName.toLocal8Bit();
    // ⚠ 第二个参数 FALSE = **自动重置**，别改（见文件头约定）
    m_hReqEvent  = CreateEventA(nullptr, FALSE, FALSE, reqName.constData());
    m_hRespEvent = CreateEventA(nullptr, FALSE, FALSE, rspName.constData());
    m_hStopEvent = CreateEventA(nullptr, TRUE,  FALSE, nullptr);      // 这个是 Qt 内部用的，手动重置
    if (!m_hReqEvent || !m_hRespEvent || !m_hStopEvent) {
        qCritical("CreateEvent failed, err=%lu", GetLastError());
        closeIpc();
        return false;
    }

    m_stop = false;
    ResetEvent(m_hStopEvent);
    return true;
}

void SharedMemoryBridge::closeIpc()
{
    if (m_hReqEvent)  { CloseHandle(m_hReqEvent);  m_hReqEvent  = nullptr; }
    if (m_hRespEvent) { CloseHandle(m_hRespEvent); m_hRespEvent = nullptr; }
    if (m_hStopEvent) { CloseHandle(m_hStopEvent); m_hStopEvent = nullptr; }
    for (int i = 0; i < kSlotCount; ++i) {
        if (m_ackEvents[i]) { CloseHandle(m_ackEvents[i]); m_ackEvents[i] = nullptr; }
    }
    if (m_pMem) { UnmapViewOfFile(m_pMem); m_pMem = nullptr; }
    if (m_hMap) { CloseHandle(m_hMap); m_hMap = nullptr; }
}

bool SharedMemoryBridge::openAckEvents()
{
    if (m_respEventName.isEmpty()) return false;

    const QByteArray base = m_respEventName.toLocal8Bit();
    int missing = 0;
    for (int i = 0; i < kSlotCount; ++i) {
        if (m_ackEvents[i]) continue;

        const QByteArray name = base + "_r_" + QByteArray::number(i + 1);
        HANDLE h = OpenEventA(EVENT_MODIFY_STATE, FALSE, name.constData());
        if (h)
            m_ackEvents[i] = h;
        else
            ++missing;
    }
    return missing == 0;
}

bool SharedMemoryBridge::ackEventsReady() const
{
    for (int i = 0; i < kSlotCount; ++i)
        if (!m_ackEvents[i]) return false;
    return true;
}

// ---------------------------- 工作线程 ----------------------------

void SharedMemoryBridge::startWorker()
{
    m_stop = false;
    if (m_hStopEvent) ResetEvent(m_hStopEvent);

    m_workerThread = QThread::create([this] { workerLoop(); });
    m_workerThread->start();
}

void SharedMemoryBridge::shutdownWorker()
{
    m_stop = true;
    if (m_hStopEvent) SetEvent(m_hStopEvent);

    if (m_workerThread) {
        if (!m_workerThread->wait(5000)) {
            qWarning() << "共享内存: 工作线程未能在 5 秒内退出，跳过资源释放以免崩溃";
            m_workerThread = nullptr;
            return;
        }
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    // 线程池里可能还有正在跑的回调（它们会写共享内存），必须等它们结束再 Unmap
    if (!m_pool.waitForDone(5000))
        qWarning() << "共享内存: 线程池还有任务没结束，仍继续释放（未结束的任务会被 m_pMem 判空拦掉）";

    closeIpc();
}

void SharedMemoryBridge::workerLoop()
{
    qDebug() << "共享内存: 开始监听请求槽（收到请求会打 api_id）";


    QElapsedTimer ackTimer;
    ackTimer.start();
    while (!m_stop && !openAckEvents()) {
        if (ackTimer.elapsed() > static_cast<qint64>(ACK_OPEN_WAIT_MS)) {
            int opened = 0;
            for (int i = 0; i < kSlotCount; ++i) if (m_ackEvents[i]) ++opened;
            qWarning() << "共享内存:" << (ACK_OPEN_WAIT_MS / 1000)
                       << "秒内只打开" << opened << "/" << kSlotCount
                       << "个响应槽事件 —— 同步请求会等 16 秒超时（先确认易语言真的起来了）";
            break;
        }
        QThread::msleep(100);
    }
    if (!m_stop && ackEventsReady())
        qDebug() << "共享内存: 易语言已连接（50 个响应槽事件全部就绪）";

    int idleTicks = 0;
    while (!m_stop) {
        HANDLE waits[2] = { m_hReqEvent, m_hStopEvent };
        const DWORD w = WaitForMultipleObjects(2, waits, FALSE, IDLE_POLL_MS);
        if (m_stop) break;

        if (w == WAIT_OBJECT_0) {
            scanRequestSlots();
            idleTicks = 0;
        } else if (w == WAIT_TIMEOUT) {
            // 客户端刚起来 / 刚重启过 → 补开还缺的响应槽事件
            if (!ackEventsReady())
                openAckEvents();
            if (++idleTicks * (IDLE_POLL_MS / 1000) >= IDLE_LOG_SEC) {
                idleTicks = 0;
                qDebug() << "共享内存: 仍在监听请求槽（" << IDLE_LOG_SEC << "秒内没有新请求）";
            }
        }

        // 客户端进程还在不在？—— 进程句柄就在我们手上，比等心跳回执可靠得多。
        // 真发现它退出了就把心跳计数顶到阈值，让上一层的 timer（≤3 秒）去重启。
        DWORD code = 0, pid = 0;
        if (takeDeadYiProcess(code, pid)) {
            AppendEventLog(QString("共享内存: 易语言进程(pid=%lu)已退出，退出码=0x%08lX（0xC0000005=访问冲突崩溃，0xC0000409=栈溢出，0x0=正常退出）→ 置心跳计数，等上层重启")
                               .arg(static_cast<unsigned long>(pid))
                               .arg(static_cast<unsigned long>(code))
                           );
            qWarning("共享内存: 易语言进程(pid=%lu)已退出，退出码=0x%08lX（0xC0000005=访问冲突崩溃，0xC0000409=栈溢出，0x0=正常退出）→ 置心跳计数，等上层重启",
                     static_cast<unsigned long>(pid), static_cast<unsigned long>(code));
            miaomiao32 = 4;
        }
    }

    if (!m_stop)
        qWarning() << "共享内存: 工作线程即将退出";
}

void SharedMemoryBridge::scanRequestSlots()
{
    if (!m_pMem) return;

    for (int i = 0; i < kSlotCount; ++i) {
        char *slot = slotAt(m_pMem, i);
        if (getSlotState(slot) != SLOT_STATE_REQ) continue;

        const quint32 len = get32(slot + SLOT_OFF_LEN);
        if (len == 0 || len > static_cast<quint32>(kSlotSize - SLOT_OFF_DATA)) {
            // 长度 0 = 客户端刚占坑、还没写完（或者客户端那边长度还没回填）。
            // 跳过即可，它写完会 SetEvent(_hReqEvent)，我们会再扫一遍。
            continue;
        }

        const quint32 apiId = get32(slot + SLOT_OFF_X);
        const qint64  uid   = static_cast<qint32>(get32(slot + SLOT_OFF_Y));
        const int     reqId = static_cast<int>(get32(slot + SLOT_OFF_REQID));

        RequestData req;
        req.apiId = static_cast<int>(apiId);
        req.appid = uid;
        req.reqId = reqId;
        req.slot  = i;
        parseRequestBody(slot + SLOT_OFF_DATA, len, req);

        // 异步判定：客户端那边同一套（api_id <= 1 或 >= 10000，或 api_id==2 且 text6=="true"）
        const bool async = (apiId <= 1u) || (apiId >= 10000u) || (apiId == 2u && isTrueish(req.texts[5]));
        req.sync = !async;

        if (apiId > 1 && apiId<10000) {      // api_id 1 是插件输出日志，最频繁，不刷屏
            qDebug() << "共享内存: 收到请求 api_id =" << req.apiId
                     << "uid =" << static_cast<qint32>(uid)
                     << "reqId =" << reqId
                     << "len =" << len << "槽 =" << i
                     << (async ? "（异步）" : "（同步）");
        }

        miaomiao32 = 0;         // 有动静 = 活着，别让上层误判成「无回执」把进程重启掉

        // 命令返回值专用通道：塞给 processRequestsA 的等待者，不跑插件回调、不占槽
        if (req.apiId == API_ID_CMD_RESULT) {
            deliverCommandResult(req);
            clearRequestSlot(i);
            continue;
        }

        if (async) {
            clearRequestSlot(i);        // 异步的槽由服务端清空（客户端不等，也不会自己清）
        } else {
            setSlotState(slot, SLOT_STATE_BUSY);   // 服务端处理中：防止被重复取走；客户端会跳过这个值
        }

        // 耗时的 api（发消息 / 网络 / HTML 转图）丢线程池，接收逻辑立刻回去继续扫
        m_pool.start(new Task(this, req));
    }
}

void SharedMemoryBridge::clearRequestSlot(int slotIndex)
{
    if (!m_pMem || slotIndex < 0 || slotIndex >= kSlotCount) return;
    char *slot = slotAt(m_pMem, slotIndex);

    // 长度归零：这是「这个槽还没被写过」的标记，scanRequestSlots 靠它跳过占坑中的半成品
    put32(slot + SLOT_OFF_LEN, 0);
    setSlotState(slot, SLOT_STATE_EMPTY);
}

bool SharedMemoryBridge::writeSlotResponse(int slotIndex, int reqId, const char *text)
{
    if (!m_pMem || slotIndex < 0 || slotIndex >= kSlotCount) return false;
    if (!text) text = "";

    char *slot = slotAt(m_pMem, slotIndex);

    // 客户端最多等 16 秒；超时后它会自己把槽清 0，别的线程可能已经占用了同一个槽 ——
    // 这时绝不能往里写（会把别人的请求数据踩烂）。靠「状态还是我标的 4 且 请求ID 还是我这条」确认归属。
    if (getSlotState(slot) != SLOT_STATE_BUSY ||
        static_cast<int>(get32(slot + SLOT_OFF_REQID)) != reqId) {
        qWarning() << "共享内存: 请求槽" << slotIndex << "已不属于本次请求（客户端超时后复用了它），丢弃响应";
        return false;
    }

    QByteArray payload(text);
    if (payload.size() > kSlotSize - SLOT_OFF_DATA - 1) {
        qWarning() << "共享内存: 响应体过大(" << payload.size() << ")已截断为错误提示";
        payload = R"({"error":"返回数据超过 100KB 上限"})";
    }

    memcpy(slot + SLOT_OFF_DATA, payload.constData(), static_cast<size_t>(payload.size()));
    slot[SLOT_OFF_DATA + payload.size()] = '\0';

    // 回响应时顺手把长度也清 0：客户端取完结果会把状态清 0，但不会动长度；
    // 不清的话，下一个用这个槽的请求在「占坑之后、回填长度之前」会被我们误判成已写完。
    put32(slot + SLOT_OFF_LEN, 0);
    setSlotState(slot, SLOT_STATE_RDY);

    HANDLE ev = m_ackEvents[slotIndex];
    if (!ev) {
        qWarning() << "共享内存: 响应槽事件" << (slotIndex + 1) << "还没打开，客户端会等 16 秒超时";
        return false;
    }
    SetEvent(ev);
    return true;
}

void SharedMemoryBridge::parseRequestBody(const char *data, quint32 len, RequestData &req) const
{
    const char *p   = data;
    const char *end = data + len;

    auto take = [&p, end](std::string &out) {
        const char *z = p;
        while (z < end && *z != '\0') ++z;
        out.assign(p, static_cast<size_t>(z - p));
        p = (z < end) ? z + 1 : z;                      // 跳过结尾的 0
    };

    take(req.uuid);
    for (int i = 0; i < 8 && p < end; ++i)
        take(req.texts[i]);
}

void SharedMemoryBridge::deliverCommandResult(const RequestData &req)
{
    // 命令的返回值：易语言 _post4(1831501026, appid, reqId, json)
    //   第 3 个参数（落到 req.uuid） = 请求 id 的十进制字符串  ← **新协议**（老版本这里是 ""）
    //   第 4 个参数（落到 texts[0]） = 结果 json 本体
    int reqId = 0;
    if (!req.uuid.empty()) {
        const QString u = QString::fromUtf8(req.uuid.c_str(), static_cast<int>(req.uuid.size()));
        bool ok = false;
        const qlonglong v = u.trimmed().toLongLong(&ok);
        if (ok && v > 0 && v <= INT_MAX)
            reqId = static_cast<int>(v);
    }

    // 正文以 texts[0] 为准；只有「没带 reqId 且 texts[0] 也是空」时才回退成 uuid
    // （兼容极老写法：把结果塞在第 3 个参数里 —— 现在第 3 参已让给 reqId）
    const std::string &body = (!req.texts[0].empty() || reqId > 0) ? req.texts[0] : req.uuid;

    // ⚠ 空串是**合法返回值**（很多命令就是没有输出，比如心跳/加载成功），不是错误 ——
    //   照样唤醒等待者，让它立刻拿到空串返回，而不是白等满 5 秒。
    const QString text = QString::fromUtf8(body.c_str(), static_cast<int>(body.size()));
    if (!text.isEmpty())
        qDebug() << "共享内存: 命令返回值 reqid =" << reqId << "内容 =" << text;

    QMutexLocker lk(&m_cmdMutex);

    CmdWait *slot = nullptr;
    if (reqId > 0) {
        auto it = m_cmdWaiters.find(reqId);
        if (it == m_cmdWaiters.end()) {
            // 正常来源：只发不等的命令（reqid = NO_WAIT_REQ_ID）回吐了值，或上层已超时注销。
            // 不暂存（防串包），也不值得刷 warning。
            qDebug() << "共享内存: 命令返回值 reqid =" << reqId << "没人在等（只发不等 / 已超时），丢弃";
            return;
        }
        slot = &it.value();
    } else {
        // 老易语言没把 id 带回来（_post4 第 3 参还是 ""）：
        // 只有「唯一一个等待者」时才能确定这条值是谁的；多个在等就直接丢弃 —— 宁可失败也不串包。
        if (m_cmdWaiters.isEmpty()) {
            qDebug() << "共享内存: 收到命令返回值但没人在等（只发不等 / 上层已超时），丢弃";
            return;
        }
        if (m_cmdWaiters.size() > 1) {
            qWarning() << "共享内存: 同时有" << m_cmdWaiters.size()
                       << "条命令在等，但回执没带请求 id（易语言 _post4 第 3 个参数仍是空），无法路由，丢弃";
            return;
        }
        slot = &m_cmdWaiters.begin().value();
        qDebug() << "共享内存: 回执未带请求 id，退化按「唯一等待者」交付（id ="
                 << m_cmdWaiters.begin().key() << "）";
    }

    slot->result  = text;
    slot->pending = true;
    m_cmdCond.wakeAll();
}

// ---------------------------- 投递任务（Qt → 易语言） ----------------------------

bool SharedMemoryBridge::writeResponseToBlock(int type, const char *text)
{
    if (!m_pMem) {
        qWarning() << "共享内存: 还没启动，推送失败 type =" << type;
        return false;
    }
    if (!text) text = "";

    QByteArray payload(text);
    if (payload.size() > kSlotSize - SLOT_OFF_REQID)
        payload = R"({"error":"推送数据超过 100KB 上限"})";

    QMutexLocker lk(&m_taskMutex);

    char *base = m_pMem + kTaskBase;
    for (int i = 0; i < kSlotCount; ++i) {
        char *slot = slotAt(base, i);
        if (getSlotState(slot) != SLOT_STATE_EMPTY) continue;   // 客户端正在取用（状态 2），跳过

        put32(slot + SLOT_OFF_X,    static_cast<quint32>(type));
        put32(slot + SLOT_OFF_Y,    static_cast<quint32>(payload.size()));
        memcpy(slot + SLOT_OFF_REQID, payload.constData(), static_cast<size_t>(payload.size()));

        // 数据写完了才把状态置 2（客户端是「先看状态、再读数据」），中间必须有屏障
        setSlotState(slot, SLOT_STATE_RDY);

        if (m_hRespEvent) SetEvent(m_hRespEvent);
        return true;
    }

    qWarning() << "共享内存: 50 个任务槽全满（客户端没在取？），推送失败 type =" << type;
    return false;
}

// ---------------------------- 命令返回值等待（多路，2026-10-09 二版） ----------------------------
// 每一路用一个请求 id 标识：发命令前 prepareCommandWait() 领号（同时登记），把号塞进 JSON 的
// "reqid"，易语言回吐时原样带回 → deliverCommandResult 精确路由。同一时刻可以有多条在飞。

// 发号 + 登记，一步完成（原子）。返回 >0 = 请求 id；0 = 失败（号不够用了）
int SharedMemoryBridge::prepareCommandWait()
{
    QMutexLocker lk(&m_cmdMutex);
    for (int tries = 0; tries < 100000; ++tries) {
        // 从 1 往上派；到顶就回到 1（0 保留给「不分路」的老式等待者，见单参 processRequestsA）
        m_cmdSeq = (m_cmdSeq >= INT_MAX) ? 1 : m_cmdSeq + 1;
        if (m_cmdWaiters.contains(m_cmdSeq)) continue;   // 回绕后撞上还活着的号 → 换一个
        m_cmdWaiters.insert(m_cmdSeq, CmdWait());
        return m_cmdSeq;
    }
    qWarning() << "共享内存: 命令请求号用尽，同时有" << m_cmdWaiters.size() << "条在等？";
    return 0;
}

// 兼容老调用点：「不分路」的等待者（id = 0），同一时间只允许一个在等。
QString SharedMemoryBridge::processRequestsA(int timeoutMs)
{
    {
        QMutexLocker lk(&m_cmdMutex);
        if (m_cmdWaiters.contains(0)) {
            // 理论上不会发生（这些调用都在主线程顺序执行）；真重入了就直接放行，免得抢别人的结果
            qWarning() << "共享内存: 命令返回值通道重入，忽略本次等待";
            return QString();
        }
        m_cmdWaiters.insert(0, CmdWait());
    }
    return processRequestsA(0, timeoutMs);
}

QString SharedMemoryBridge::processRequestsA(int reqId, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    {
        QMutexLocker lk(&m_cmdMutex);
        if (!m_cmdWaiters.contains(reqId)) {
            // 调用方没先 prepareCommandWait（或已被别处注销）→ 兜底补登，别让等待凭空失败
            m_cmdWaiters.insert(reqId, CmdWait());
        }
    }

    bool got = false;                       // 是否真收到了返回值（**空串也算收到**）
    QString ret;
    while (timer.elapsed() < timeoutMs) {
        QMutexLocker lk(&m_cmdMutex);
        auto it = m_cmdWaiters.find(reqId);
        if (it == m_cmdWaiters.end()) break;    // 被别处注销（如 stopServer 清空）→ 立刻放弃
        if (it.value().pending) {
            ret = it.value().result;
            got = true;
            break;
        }
        m_cmdCond.wait(&m_cmdMutex, 50);
    }

    {
        QMutexLocker lk(&m_cmdMutex);
        auto it = m_cmdWaiters.find(reqId);
        if (it != m_cmdWaiters.end()) {
            if (it.value().pending) {           // 最后一刻才到的结果也别丢
                ret = it.value().result;
                got = true;
            }
            m_cmdWaiters.erase(it);             // 注销：迟到的结果按「没人在等」丢弃
        }
    }

    // ⚠ 不能拿「返回串是不是空的」当超时判据 —— 命令**允许返回空值**（相当于「没有消息」），
    //   空串是合法结果。只有连一条返回值都没等到（got == false）才是真超时。
    if (!got)
        qWarning() << "共享内存: 等待命令返回超时（reqid =" << reqId << "）";
    return ret;
}

// ---------------------------- 命令返回值：非阻塞（轮询）用法 ----------------------------
// 给「不等，但 100ms 后回头看一眼」的调用方用（32 位插件命令的异步版）。
// 用法：prepareCommandWait() 领号（登记）→ 写任务槽 → 定时器里 pollCommandResult(reqId, out)
//       → 拿到 / 超时都调 endCommandWait(reqId)。和 processRequestsA 共用 m_cmdWaiters，
//       同步 / 异步 / 多条并发都互不干扰。

bool SharedMemoryBridge::pollCommandResult(int reqId, QString &out)
{
    QMutexLocker lk(&m_cmdMutex);
    auto it = m_cmdWaiters.find(reqId);
    if (it == m_cmdWaiters.end()) return false;   // 已注销（超时 / 会话结束）
    if (!it.value().pending) return false;        // 还没回来
    out = it.value().result;
    it.value().result.clear();
    it.value().pending = false;
    return true;                                  // ⚠ 空串也算「拿到了」，调用方别拿 isEmpty() 当没拿到
}

void SharedMemoryBridge::endCommandWait(int reqId)
{
    QMutexLocker lk(&m_cmdMutex);
    m_cmdWaiters.remove(reqId);
    // 注销后到达的结果会被 deliverCommandResult 按「没人在等」丢弃 —— 超时后别把下一条的
    // 返回值串到这次调用上。
}

// ---------------------------- 纯白铃32.exe 进程管理 ----------------------------

bool SharedMemoryBridge::launchYiProcess()
{
    if (m_exePath.isEmpty()) return false;
    if (!QFile::exists(m_exePath)) {
        qWarning("纯白铃32.exe 不存在，跳过: %s", qPrintable(m_exePath));
        return false;
    }
    // 启动这件事不藏着：日志里写清楚起了哪个 exe、给了什么参数，方便对账
    qDebug() << "共享内存: 启动" << m_exePath << "参数:" << m_args.join(' ');

#ifdef Q_OS_WIN
    // 直接 CreateProcessW（不塞 Job，效果等同 detached）—— 为的是能精确控制窗口创建标志，
    // 也就是下面的 CREATE_NO_WINDOW。
    const std::wstring wexe = QDir::toNativeSeparators(m_exePath).toStdWString();

    // 参数拼接沿用 Qt 的规矩：只有空串/含空格/含 Tab 才加引号（免得易语言「取命令行」拿到多余的引号）
    QString argLine;
    for (const QString &a : m_args) {
        if (!argLine.isEmpty()) argLine += QLatin1Char(' ');
        const bool needQuote = a.isEmpty() || a.contains(QLatin1Char(' ')) || a.contains(QLatin1Char('\t'));
        argLine += needQuote ? (QLatin1Char('"') + a + QLatin1Char('"')) : a;
    }
    std::wstring wargs = argLine.toStdWString();

    // ⚠ 命令行**必须以程序名开头**：易语言「取命令行」走 CommandLineToArgvW 的规矩，
    //   会把第一个 token 当程序名丢掉。只把参数扔进去（lpApplicationName 已单独给了 exe），
    //   它那边一个参数都取不到 → 直接弹「请不要直接运行本程序」退出。
    std::wstring cmdLine = L"\"" + wexe + L"\"";
    if (!wargs.empty()) {
        cmdLine += L' ';
        cmdLine += wargs;
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    // ⚠ 一律 CREATE_NO_WINDOW：**根本不创建控制台窗口**，而不是起来之后再 ShowWindow 去藏它。
    //   之前是 CREATE_NEW_CONSOLE + 事后 SW_HIDE，那条路在现在这台机器上藏不掉（窗口被 Windows
    //   Terminal / conhost 攥着，SW_HIDE 只把它最小化）—— 「隐藏变成最小化」「启动后藏不掉」就是这么来的。
    //   代价：看不到它的控制台输出。要看得把这里换回 CREATE_NEW_CONSOLE。
    const BOOL ok = CreateProcessW(wexe.c_str(),
                                   cmdLine.data(),
                                   nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr,
                                   nullptr,          // 工作目录继承 Qt 进程
                                   &si, &pi);
    if (!ok) {
        qWarning("启动 纯白铃32.exe 失败，错误码 %lu，路径: %s",
                 static_cast<unsigned long>(GetLastError()), qPrintable(m_exePath));
        return false;
    }
    CloseHandle(pi.hThread);

    // ⚠ 不关 pi.hProcess：留着它才能知道这个子进程是活着、崩了、还是被谁杀了。
    {
        QMutexLocker lk(&m_procMutex);
        if (m_hYiProcess) CloseHandle(m_hYiProcess);
        m_hYiProcess = pi.hProcess;
        m_yiPid      = pi.dwProcessId;
        qDebug() << "共享内存: 纯白铃32.exe 已启动，pid =" << static_cast<quint32>(m_yiPid);
    }

    // 32 位窗口一律藏掉（放锁外调：hideYiWindow 内部自己会拿 m_procMutex）。
    // CREATE_NO_WINDOW 已经挡掉了控制台窗口，这一步是兜底 —— 易语言若还自建了别的窗口，一并隐藏。
    // ⚠ 窗口可能要几百毫秒才建出来，一次没藏到就 800ms 后再补一次；
    //    singleShot 投到本对象所在线程执行，不阻塞启动/心跳线程。
    if (!hideYiWindow())
        QTimer::singleShot(800, this, [this]() { hideYiWindow(); });

    return true;
#else
    const bool ok = QProcess::startDetached(m_exePath, m_args);
    if (!ok)
        qWarning("Failed to launch 纯白铃32.exe, path: %s", qPrintable(m_exePath));
    return ok;
#endif
}

bool SharedMemoryBridge::yiProcessExited(DWORD &exitCode) const
{
    exitCode = 0;
    if (!m_hYiProcess) return true;                       // 没启动过 → 视作"不在"
    if (WaitForSingleObject(m_hYiProcess, 0) != WAIT_OBJECT_0) return false;
    GetExitCodeProcess(m_hYiProcess, &exitCode);
    return true;
}

bool SharedMemoryBridge::takeDeadYiProcess(DWORD &exitCode, DWORD &pid)
{
    QMutexLocker lk(&m_procMutex);
    if (!m_hYiProcess) return false;
    if (!yiProcessExited(exitCode)) return false;

    pid = m_yiPid;
    CloseHandle(m_hYiProcess);
    m_hYiProcess = nullptr;
    m_yiPid      = 0;
    return true;
}

void SharedMemoryBridge::closeYiProcessHandle(bool killIfAlive, const char *why)
{
    QMutexLocker lk(&m_procMutex);
    if (!m_hYiProcess) return;

    const DWORD pid = m_yiPid;
    DWORD code = 0;
    if (yiProcessExited(code)) {
        qWarning("共享内存: 纯白铃32.exe(pid=%lu) 已退出，退出码=0x%08lX（%s）",
                 static_cast<unsigned long>(pid), static_cast<unsigned long>(code), why);
    } else if (killIfAlive) {
        TerminateProcess(m_hYiProcess, 0);
        WaitForSingleObject(m_hYiProcess, 3000);
        qWarning("共享内存: 强制结束残留的 纯白铃32.exe(pid=%lu)（%s）—— 它已经不响应，留着只会占资源",
                 static_cast<unsigned long>(pid), why);
    } else {
        qWarning("共享内存: 放走还在运行的 纯白铃32.exe(pid=%lu)（%s）",
                 static_cast<unsigned long>(pid), why);
    }

    CloseHandle(m_hYiProcess);
    m_hYiProcess = nullptr;
    m_yiPid      = 0;
}

// ---------------------------- 32 位窗口：藏掉 ----------------------------
//
// ⚠ 主路径根本不在这个函数里：启动时用 CreateProcessW(CREATE_NO_WINDOW) **不建控制台窗口**，
//   所以正常情况下这里无事可做。这个函数是兜底 —— 易语言若自己又建了窗口（编成窗口程序），照样藏。
//
// 纯白铃32.exe 那个窗口**不在本进程里**，两种来源都要照顾到：
//   · 它是控制台程序 → 窗口属于 conhost，只能 AttachConsole(它的 pid) 后 GetConsoleWindow() 拿到
//     （EnumWindows 按 pid 找不到它：窗口的 pid 是 conhost 的）；
//   · 它若编成窗口程序 → 窗口属于该 pid 自己，枚举顶层窗口才拿得到。
// 两种都做，取并集。隐藏只是 SW_HIDE：进程照常跑、照常写它的控制台缓冲区，只是看不见。

static QString winClassOf(HWND h)
{
    wchar_t buf[256] = { 0 };
    GetClassNameW(h, buf, 255);
    return QString::fromWCharArray(buf);
}

static QString winTitleOf(HWND h)
{
    wchar_t buf[256] = { 0 };
    GetWindowTextW(h, buf, 255);
    return QString::fromWCharArray(buf);
}

// 取 pid 自己创建的顶层窗口（窗口程序的主窗口在这一类里）
struct TopWinEnum { DWORD pid; HWND hwnd[64]; int n; };

static BOOL CALLBACK collectTopWinProc(HWND h, LPARAM lp)
{
    auto *t = reinterpret_cast<TopWinEnum *>(lp);
    DWORD p = 0;
    GetWindowThreadProcessId(h, &p);
    if (p == t->pid && t->n < 64) t->hwnd[t->n++] = h;
    return TRUE;
}

// 取 pid 的控制台窗口（属于 conhost，不在上面那批里）；拿不到返回 nullptr
static HWND consoleWindowOf(DWORD pid)
{
    if (!AttachConsole(pid)) {
        // ⚠ 本进程（GUI）本来没有控制台；万一有，一个进程只能挂一个 → 先摘掉再挂目标
        FreeConsole();
        if (!AttachConsole(pid)) return nullptr;
    }
    HWND h = GetConsoleWindow();
    FreeConsole();      // 用完立刻摘掉，别把本进程挂在别人的控制台上
    return h;
}

bool SharedMemoryBridge::hideYiWindow()
{
    return false;
}

// ---------------------------- 线程池任务 ----------------------------

void SharedMemoryBridge::Task::run()
{
    const char *textPtrs[8] = { nullptr };
    for (int i = 0; i < 8; ++i)
        textPtrs[i] = m_req.texts[i].empty() ? nullptr : m_req.texts[i].c_str();

    const char *result = "";
    if (m_bridge->m_callback) {
        try {
            const char *r = m_bridge->m_callback(
                m_req.uuid.c_str(), m_req.apiId, m_req.appid,
                textPtrs[0], textPtrs[1], textPtrs[2], textPtrs[3],
                textPtrs[4], textPtrs[5], textPtrs[6], textPtrs[7]);
            result = r ? r : "";
        } catch (const std::exception &e) {
            qWarning() << "共享内存: 处理 api_id" << m_req.apiId << "异常:" << e.what();
            result = R"({"error":"api 处理异常"})";
        } catch (...) {
            qWarning() << "共享内存: 处理 api_id" << m_req.apiId << "未知异常";
            result = R"({"error":"api 处理异常"})";
        }
    }

    // 只有同步请求才回响应（异步的槽早就被清掉了，客户端也不在等）
    if (m_req.sync)
        m_bridge->writeSlotResponse(m_req.slot, m_req.reqId, result);
}
