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




#include <pybind11/embed.h>
#include <pybind11/functional.h>


#define _CRTDBG_MAP_ALLOC   // 映射 malloc/free 到调试版本，提供更详细信息

#include <stdlib.h>
#include <QApplication>
#include "cnbuploader.h"
#include "pluginmarketwindow.h"
#include "cardwidget.h"
#include <QThreadPool>
#include "mainwindow.h"
#include <QFile>
#include <QJsonObject>
#include "global.h"
#include <QDir>
#include <QDate>
#include <QDateTime>   // ensureCacheDb 的槽位算法（文件头部就要用，不能等到 249 行那组）
#include <QFileInfo>
#include <QResource>
#include "lmdbkv.h"
#include "logdb.h"
#include <QStandardPaths>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <qmenu.h>


namespace py = pybind11;
cos_data g_cos = cos_data{};
cnb_data g_cnb = cnb_data{};
QJsonObject g_config;
QList<PluginInfo> m_pluginList;
LmdbKV *cache_db=nullptr;
std::array<std::unique_ptr<LogDB>, 5> g_logdb;
std::unique_ptr<LogDB> g_errlogdb;      // 发送错误独立库（见 global.h 的说明）
QHash<int, CardWidget*> g_CW;
QHash<int, BotDB*> g_botdb;
QList<PluginInfo2> m_allPlugins;
LmdbKV *aidb=nullptr;
LmdbKV *dsdb=nullptr;
LmdbKV *accdb=nullptr;
MessageEvent *g_cqev=nullptr;
QString g_admin;
int g_appid;


// ─────────────────────────────────────────────────────────────────────────────
// 上传缓存 cache_db：双库按日轮换（替代「全库扫过期条目」）
//
// 为什么不用 removeExpired 扫：LMDB 没有 TTL，过期条目只能靠游标把整个库走一遍才删得掉。
//   库到上百万条时，这一趟既慢（在 GUI 线程跑，界面会顿），又要全程占着 LmdbKV::m_mutex
//   （期间所有上传的 put/get 都得排队等）；更麻烦的是「只上传过一次、再没人发」的条目
//   永远等不到被读到的那次顺手删除，只能干等这一趟扫描。
//
// 所以换成：开两个目录 botdb/file_db_a / botdb/file_db_b，**每 2 天轮换一次**。
//   · 日界**不在 0 点，而是每天早上 05:00**（`kCacheDbResetHour`）—— 0 点前后还在跑夜间任务，
//     5 点换库时基本没人用，那一瞬间的重传撞不到高峰
//   · 任意时刻只有一个是 cache_db —— 查缓存跟单库完全一样，不会多查一个库、也没有「查不到再回退」
//   · 轮换时切到另一个库，**立刻把刚退役的旧库整个目录删掉** → 盘上永远只有 1 份
//   · 过期条目不用逐条删，跟着目录一起没（LMDB 删条目不缩文件，只有删文件才真正把磁盘还回去）
//
// 「槽位 slot」算法（`cacheDbSlotOf`）：把时间**拨回 5 小时** → 取本地日期 → **整除 2**。
//   于是 slot 每 2 天在 **05:00 整**变一次；slot 的奇偶决定用 a 还是 b，相邻槽位必然交替。
//   例：10-20 23:00 与 10-21 04:59 属于同一个 slot；10-21 05:00 起进入下一个 slot。
//
// 代价：换库那一下，同一个文件会重新上传一次（图片缓存本来也只有 24h；内网传图约 100ms）。
// 安全性：删之前必须先 close()（Windows 下 env 还映射着 data.mdb 就删不掉；close 之后任何
//         残留调用都是安全空操作）；旧对象**不 delete**（攒到退出时统一删）—— api.cpp 里有
//         std::thread 上传路径可能攥着旧指针。
// ─────────────────────────────────────────────────────────────────────────────
static const int      kCacheDbResetHour = 5;   // 日界：每天本地时间几点算「新的一天」
static const qint64   kCacheDbDays      = 2;   // 一个库用几天

static qint64         s_cacheDbSlot = -1;      // 当前 cache_db 所属的槽位
static QList<LmdbKV*> s_retiredCacheDbs;       // 已 close、等退出时统一 delete 的旧库

// 任一时间点 → 它属于哪个「kCacheDbDays 天槽位」（日界 = 每天 kCacheDbResetHour 点）
static qint64 cacheDbSlotOf(const QDateTime &dt)
{
    const QDate d = dt.addSecs(-qint64(kCacheDbResetHour) * 3600).date();   // 拨回 5 小时再取日期
    return d.toJulianDay() / kCacheDbDays;
}

static qint64 cacheDbSlotNow()
{
    return cacheDbSlotOf(QDateTime::currentDateTime());
}

// 槽位奇偶决定用哪个目录：同槽位恒定，相邻槽位必然相反
static QString cacheDbDirFor(qint64 slot)
{
    return (slot % 2 == 0) ? QStringLiteral("botdb/file_db_a")
                           : QStringLiteral("botdb/file_db_b");
}

// 这个目录里的库是不是「本槽位写过的」—— 用来区分两种情况：
//   · 同一槽位内重启 → 保留，别白丢还热着的缓存
//   · 跨了槽位 / 停机好几天后重启 → 那份数据早不该用了，作废重建
static bool cacheDbDirInSlot(const QString &dir, qint64 slot)
{
    const QFileInfo fi(dir + QStringLiteral("/data.mdb"));
    return fi.exists() && cacheDbSlotOf(fi.lastModified()) == slot;
}

// 启动时调一次，运行期每分钟调一次；只有「槽位变了」或「还没有库」时才真的动。
static void ensureCacheDb()
{
    const qint64 slot = cacheDbSlotNow();
    if (cache_db && slot == s_cacheDbSlot) return;        // 同一槽位（同一个 2 天周期），什么都不做

    const QString want  = cacheDbDirFor(slot);            // 本槽位该用哪个
    const QString other = cacheDbDirFor(slot + 1);        // 奇偶相反的那个 = 上个槽位在用的库

    // ① 旧库摘下并关闭：必须先关 env，Windows 才有权限删它映射着的 data.mdb
    LmdbKV *old = cache_db;
    cache_db = nullptr;
    if (old) {
        old->close();
        s_retiredCacheDbs.append(old);                    // 不 delete：可能还有线程攥着旧指针
    }

    // ② want 里装的不是「本槽位写的」→ 整目录作废重建（跨槽位/久停后重启就靠这行）
    if (!cacheDbDirInSlot(want, slot))
        QDir(want).removeRecursively();

    // ③ 开本槽位该用的库，立刻把 cache_db 指回去（中间只有几毫秒的「不缓存」窗口）
    cache_db      = new LmdbKV(want);
    s_cacheDbSlot = slot;

    // ④ 刚退役的旧库（other，① 里已 close）+ 老版本遗留的 botdb/file_db：直接删
    QDir(other).removeRecursively();
    QDir(QStringLiteral("botdb/file_db")).removeRecursively();
}


void loadconfig()
{
    bool ok = false;
    QFile file("data/config.json");
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray data = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull()) {
            g_config = doc.object();
            ok = true;
        } else {
            g_config = QJsonObject();
        }
        file.close();
    }
    if (!ok) g_config = QJsonObject();   // 文件打开失败或解析失败，都主动清空
    g_admin = g_config["admin"].toString();
    QJsonObject obj = g_config["zdcq"].toObject();
    if (!obj.isEmpty()) {

        g_config.remove("zdcq");
        saveConfig();

        // 创建新的事件对象（使用 new，或 QSharedPointer）
        MessageEvent *ev = new MessageEvent;
        ev->type   = obj["type"].toInt();
        ev->msgId  = obj["msgid"].toString();
        ev->groupId= obj["openid"].toString();
        ev->log    = obj["time"].toDouble();
        ev->appid  = obj["appid"].toInt();
        g_cqev = ev;   // 全局指针指向动态对象
    }
    if(!g_config["___dyc"].toBool())
    {
        QMessageBox::warning(nullptr,"特别说明","本框架禁止黑灰违法使用");
        g_config["___dyc"] = true;
        saveConfig();
    }

}

void saveConfig()
{
    if(框架退出) return;
    QFile file("data/config.json");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(g_config).toJson(QJsonDocument::Indented));
        file.close();
    }
}

bool clearPTmpFolder()
{
    QString folderPath = QCoreApplication::applicationDirPath() + "/p_tmp";
    QDir dir(folderPath);
    if (!dir.exists()) {
        return true;
    }
    const QStringList dllFiles = dir.entryList(QDir::Files);
    bool ok = true;
    for (const QString &file : dllFiles) {
        if (!dir.remove(file)) {
            ok = false;
        }
    }
    return ok;
}

void initdir()
{
    QDir dir;
    dir.mkpath("tmp/video");
    dir.mkpath("tmp/audio");
    dir.mkpath("tmp/聊天图片");
    dir.mkpath("tmp/image");
    dir.mkpath("tmp/file");
    dir.mkpath("data");
    dir.mkpath("botdb/memory");
    dir.mkpath("plugin");
    dir.mkpath("plugins");
    dir.mkpath("plugin_data");
}




#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QDateTime>
#include <QTimer>

// ---------- 通用：重启自身 ----------
static void restartSelf()
{
    QString exePath = QCoreApplication::applicationFilePath();
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty()) args.removeFirst();   // 去掉 argv[0]

    // 用 startDetached，脱离父子关系，父进程退出不影响子进程
    QProcess::startDetached(exePath, args, QCoreApplication::applicationDirPath());
}

// ---------- 通用：安全清理业务对象 ----------
static void safeCleanup()
{
    // 每个 stop 独立 try-catch，避免一个挂掉影响其他
    for (auto& c : m_botClients) {
        if (!c) continue;
        try { c->stop(); } catch (...) {}
    }
#ifdef _WIN32
    if (bridge) {
        try { bridge->writeResponseToBlock(1, "{\"type\":6}"); } catch (...) {}
        try { bridge->stopServer(); } catch (...) {}
    }
#endif
    if (pluginPage) {
        try { pluginPage->foruninstall_Plugin(); } catch (...) {}
    }
}

#ifdef _WIN32
#include <windows.h>
#include <DbgHelp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <exception>
#include <QDebug>
#pragma comment(lib, "DbgHelp.lib")

// ═══════════════════════════════════════════════════════════════════════════
// 崩溃捕获（2026-10-23 重写：旧版只有 SetUnhandledExceptionFilter，漏得多）
//
// 旧版「崩溃了没抓到」的四类真实原因：
//   ① c0000374 堆损坏 / c0000409 栈缓冲区溢出 / 一切 __fastfail：
//      ntdll 检查到之后直接 NtRaiseFailFastException —— **根本不是 SEH 异常**，
//      SetUnhandledExceptionFilter 一次都不会被调用，只在系统事件日志里留
//      APPCRASH（故障模块 StackHash_xxx、偏移 ...FROM_ntdll+...）。
//      → 这类进程内抓不到，只能靠 WER LocalDumps 落盘（工程根目录 enable_wer_localdumps.reg）。
//   ② std::terminate（未捕获 C++ 异常 / noexcept 违约 / 线程析构时仍 joinable）
//      → abort()。旧代码为了关弹框把 _CALL_REPORTFAULT 也清掉了，于是 abort 变成
//      _exit(3)：进程秒退、dump 没有、日志也没有 —— 「跑一段时间突然消失」多半是这条。
//   ③ qFatal → abort()，同上。
//   ④ 插件 DLL（Go/C# 运行时、Python 扩展）运行期 LoadLibrary 后会自己调
//      SetUnhandledExceptionFilter，把我们的顶掉 —— 跑得越久加载的插件越多，越容易中。
//
// 现在四道网：
//   VCH   AddVectoredContinueHandler  —— 真正的最后一道，不受 filter 被顶掉影响
//   SUEF  SetUnhandledExceptionFilter —— 普通访问违例等
//   terminate / qFatal / 非法参数 / 纯虚调用
//   定时重挂 filter（reArmCrashHandler，搭主循环里那个每分钟的 QTimer）
// ═══════════════════════════════════════════════════════════════════════════

static char   s_appDir[MAX_PATH] = {0};   // 程序目录（不带末尾反斜杠）
static char   s_exePath[MAX_PATH] = {0};
static HANDLE s_fatalEvent = NULL;        // 「崩溃处理已开始」，watchdog 盯着它

// 崩溃路径里一律 Win32 + 静态缓冲：堆很可能已经坏了，QString/QFile 会二次崩
static void nowStamp(char *out, size_t n)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, n, _TRUNCATE, "[%04d-%02d-%02d %02d:%02d:%02d]",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static void rawLogf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    if (n <= 0) return;

    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\crash.log", s_appDir);
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    DWORD wrote = 0;
    WriteFile(h, buf, (DWORD)n, &wrote, NULL);
    FlushFileBuffers(h);
    CloseHandle(h);
}

// 崩溃地址 → 「模块名+偏移」。插件 DLL 崩了能一眼看出是哪个 .dll，不用猜。
static void addrToModule(uintptr_t addr, char *out, size_t n)
{
    _snprintf_s(out, n, _TRUNCATE, "0x%llX", (unsigned long long)addr);
    if (!addr) return;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            const uintptr_t base = (uintptr_t)me.modBaseAddr;
            if (addr >= base && addr < base + me.modBaseSize) {
                char name[MAX_PATH * 2] = {0};
                WideCharToMultiByte(CP_ACP, 0, me.szModule, -1, name, sizeof(name) - 1, NULL, NULL);
                _snprintf_s(out, n, _TRUNCATE, "%s+0x%llX", name,
                            (unsigned long long)(addr - base));
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
}

// ---------- 生成 minidump（带时间戳 + 触发路径） ----------
// 路径固定用程序目录：服务器上多是快捷方式/计划任务启动，工作目录未必是程序目录，
// 旧版写相对路径 → dump 掉到别处去了，看起来就像「没抓到」。
static bool writeMiniDump(EXCEPTION_POINTERS* ep, const char* tag)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char path[MAX_PATH];
    _snprintf_s(path, sizeof(path), _TRUNCATE,
                "%s\\crash_%04d%02d%02d_%02d%02d%02d_%s.dmp", s_appDir,
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, tag);

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    MINIDUMP_EXCEPTION_INFORMATION mei;
    MINIDUMP_EXCEPTION_INFORMATION* pMei = NULL;
    if (ep && ep->ExceptionRecord) {
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;
        pMei = &mei;
    }

    // 比 MiniDumpNormal 多带数据段 + 间接引用的内存：Release 下只留 Normal 的话，
    // 局部变量全是问号，栈也常走不全。故意不选 MiniDumpWithFullMemory ——
    // 跑久了的进程内存很大，写盘几十秒会把重启拖死（而堆损坏反正也靠 WER 那边取）。
    const MINIDUMP_TYPE mt = (MINIDUMP_TYPE)(
        MiniDumpWithDataSegs | MiniDumpWithThreadInfo |
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
        MiniDumpWithProcessThreadData);

    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                      hFile, mt, pMei, NULL, NULL);
    CloseHandle(hFile);
    return ok != FALSE;
}

// ---------- 重启循环保护：60 秒内最多重启 3 次 ----------
static bool shouldRestart()
{
    // 和 dump / crash.log 放一起（程序目录），不跟工作目录走
    const QString flagFile = QString::fromLocal8Bit(s_appDir) + QStringLiteral("/restart_guard.tmp");
    const int   MAX_RESTART = 3;
    const qint64 WINDOW_SEC = 60;

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 first = 0;
    int    count = 0;

    QFile f(flagFile);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray data = f.readAll();
        f.close();
        QList<QByteArray> parts = data.split(' ');
        if (parts.size() == 2) {
            first = parts[0].toLongLong();
            count = parts[1].toInt();
        }
    }

    if (first == 0 || (now - first) > WINDOW_SEC) {
        first = now;
        count = 0;
    }
    count++;

    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QByteArray::number(first) + " " + QByteArray::number(count));
        f.close();
    }

    return count <= MAX_RESTART;
}

// ---------- 真正的崩溃处理：dump → 日志 → 清理 → 重启（顺序不能换） ----------
static void fatalWork(EXCEPTION_POINTERS* ep, const char* tag)
{
    const DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : nullptr;

    char where[600] = {0};
    if (addr) addrToModule((uintptr_t)addr, where, sizeof(where));

    // ① 先落盘：dump 比什么都值钱，任何清理都不能跑在它前面
    const bool dumped = writeMiniDump(ep, tag);

    char ts[64];
    nowStamp(ts, sizeof(ts));
    rawLogf("%s Crash tag=%s code=0x%08lX tid=%lu at=%s dump=%s\r\n",
            ts, tag, (unsigned long)code, (unsigned long)GetCurrentThreadId(),
            addr ? where : "(none)", dumped ? "ok" : "FAILED");

    // ② 现场清理：让 32 位加载器/插件知道我没了（每个都独立保护）
    safeCleanup();

    // ③ 重启自身
    if (shouldRestart()) {
        restartSelf();
        rawLogf("%s restart launched\r\n", ts);
    } else {
        // 超过重启次数，弹一次提示（用 W 版，源码是 /utf-8，A 版会显示成乱码）
        MessageBoxW(NULL,
                    L"程序反复崩溃，已停止自动重启。\n请查看程序目录下的 crash_*.dmp / crash.log。",
                    L"崩溃", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
}

struct FatalCtx { EXCEPTION_POINTERS* ep; const char* tag; };

static DWORD WINAPI fatalOnNewThread(LPVOID p)
{
    FatalCtx* c = (FatalCtx*)p;
    fatalWork(c->ep, c->tag);
    return 0;
}

static void handleFatal(EXCEPTION_POINTERS* ep, const char* tag)
{
    // 只跑一次：可能 VCH 和 SUEF 都进来，也可能几个线程同时崩
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) Sleep(INFINITE);

    if (s_fatalEvent) SetEvent(s_fatalEvent);   // 告诉 watchdog：开始干活了

    const DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    if (code == 0xC00000FD /*STATUS_STACK_OVERFLOW*/) {
        // 栈已经爆了，当前线程随便一个调用都可能再炸 —— 换个 1MB 栈的线程干活
        static FatalCtx ctx;
        ctx.ep  = ep;
        ctx.tag = tag;
        HANDLE h = CreateThread(NULL, 1 << 20, fatalOnNewThread, &ctx, 0, NULL);
        if (h) { WaitForSingleObject(h, 120000); CloseHandle(h); }
    } else {
        fatalWork(ep, tag);
    }

    // 直接结束，不跑 atexit / 全局析构，避免二次崩溃
    TerminateProcess(GetCurrentProcess(), 1);
    Sleep(INFINITE);
}

// ---------- 入口一：SetUnhandledExceptionFilter（普通访问违例等） ----------
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
    handleFatal(ep, "SEH");
    return EXCEPTION_EXECUTE_HANDLER;
}

// ---------- 入口二：Vectored Continue Handler ----------
// SEH 全都没接住之后、UnhandledExceptionFilter 之前被调用。
// 它不依赖那个「可以被别人 SetUnhandledExceptionFilter 覆盖」的全局槽位，
// 所以插件 DLL 抢了 filter 也照样能进这里。
//
// ⚠ 2026-10-27：VCH 看得到 **已经被人处理掉的 first-chance 异常**，不能一律当崩溃 ——
//   Debug 版 Qt5Cored 用 RaiseException(0x406D1388) 给线程起名（Release 版没这段代码），
//   Qt 自己用 __try/__except 吃掉了，我们却当场写 dump + TerminateProcess + 重启，
//   于是「Debug 裸跑必崩、挂调试器不崩、Release 不崩」。这类"通知"一律放行，
//   只有下面这张真·崩溃表里的码才走 fatalWork。
static bool isFatalSehCode(DWORD code)
{
    switch (code) {
    case 0xC0000005:   // STATUS_ACCESS_VIOLATION
    case 0xC000001D:   // STATUS_ILLEGAL_INSTRUCTION
    case 0xC00000FD:   // STATUS_STACK_OVERFLOW
    case 0xC000008C:   // STATUS_ARRAY_BOUNDS_EXCEEDED
    case 0xC000008E:   // STATUS_FLOAT_DIVIDE_BY_ZERO
    case 0xC0000090:   // STATUS_FLOAT_INVALID_OPERATION
    case 0xC0000094:   // STATUS_INTEGER_DIVIDE_BY_ZERO
    case 0xC0000095:   // STATUS_INTEGER_OVERFLOW
    case 0xC0000096:   // STATUS_PRIVILEGED_INSTRUCTION
    case 0xC0000374:   // STATUS_HEAP_CORRUPTION（fail-fast，正常到不了这）
    case 0xC0000409:   // STATUS_STACK_BUFFER_OVERRUN（fail-fast，同上）
        return true;
    default:
        return false;
    }
}

// 被放行的通知码只在 crash.log 里留个痕（限条数，避免 qDebug 级别刷屏）：
//   0x406D1388 MS_VC_EXCEPTION（线程命名）  0x40010006 / 0x4001000A（OutputDebugString）
//   0x40080201 / 0x40080202（RoOriginateError）  0xE06D7363（C++ 异常）
//   0x80000003 / 0x80000004（__debugbreak / 单步）
static volatile LONG s_notifyLogged = 0;

static LONG CALLBACK VectoredContinueCrashHandler(EXCEPTION_POINTERS* ep)
{
    const DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    if (!isFatalSehCode(code)) {
        if (InterlockedIncrement(&s_notifyLogged) <= 200) {
            char ts[64];
            nowStamp(ts, sizeof(ts));
            rawLogf("%s ignore notify code=0x%08lX tid=%lu\r\n",
                    ts, (unsigned long)code, (unsigned long)GetCurrentThreadId());
        }
        return EXCEPTION_CONTINUE_SEARCH;   // 让 Qt / 插件自己的 __except 去处理
    }

    handleFatal(ep, "VCH");
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------- 入口三：std::terminate（未捕获 C++ 异常 / noexcept 违约 / joinable 线程析构） ----------
// 默认行为是 abort()，什么痕迹都不留 —— 「跑一段时间突然消失」的头号嫌疑。
static void onTerminate()
{
    static char what[512];
    what[0] = 0;

    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            _snprintf_s(what, sizeof(what), _TRUNCATE, "%s", e.what() ? e.what() : "(null what)");
        } catch (...) {
            _snprintf_s(what, sizeof(what), _TRUNCATE, "(non-std exception)");
        }
    } else {
        _snprintf_s(what, sizeof(what), _TRUNCATE, "(no active exception)");
    }

    char ts[64];
    nowStamp(ts, sizeof(ts));
    rawLogf("%s terminate: %s\r\n", ts, what);

    handleFatal(nullptr, "terminate");
}

// ---------- 入口四：CRT 非法参数 / 纯虚调用 ----------
static void onInvalidParameter(const wchar_t* expression, const wchar_t* function,
                               const wchar_t* file, unsigned int line, uintptr_t)
{
    char ts[64];
    nowStamp(ts, sizeof(ts));
    rawLogf("%s invalid_parameter: func=%ls file=%ls line=%u expr=%ls\r\n",
            ts,
            function ? function : L"",
            file ? file : L"",
            line,
            expression ? expression : L"");
    handleFatal(nullptr, "invalid_param");
}

static void onPurecall()
{
    char ts[64];
    nowStamp(ts, sizeof(ts));
    rawLogf("%s pure_virtual_call\r\n", ts);
    handleFatal(nullptr, "purecall");
}

// ---------- 入口五：qFatal（Qt 自己的 abort 路径） ----------
static QtMessageHandler s_prevQtHandler = nullptr;

static void qcCrashMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    if (type == QtFatalMsg) {
        const QByteArray u8 = msg.toUtf8();       // 中文会变乱码，但至少知道是哪条
        char ts[64];
        nowStamp(ts, sizeof(ts));
        rawLogf("%s qFatal: %s\r\n", ts, u8.constData());
        handleFatal(nullptr, "qFatal");
    }

    // 必须把非致命的照原样放出去，否则 qDebug/qCritical 的输出全没了
    if (s_prevQtHandler) {
        s_prevQtHandler(type, ctx, msg);
        return;
    }
    // 之前没人装过 handler → 我们自己补 Qt 默认行为（stderr + 调试器输出）
    const QByteArray line = msg.toLocal8Bit() + '\n';
    fputs(line.constData(), stderr);
    fflush(stderr);
    const QString dbg = msg + QLatin1Char('\n');
    OutputDebugStringW(reinterpret_cast<const wchar_t*>(dbg.utf16()));
}

// ---------- watchdog：handler 自己卡住时强杀 ----------
// MiniDumpWriteDump 在「堆被锁住 / 堆已坏」的进程里很容易卡死，那就变成一个没有 dump
// 也没有重启的僵尸进程。给 30 秒，超时直接从外部杀掉。
static DWORD WINAPI crashWatchdogThread(LPVOID)
{
    WaitForSingleObject(s_fatalEvent, INFINITE);
    Sleep(30000);
    TerminateProcess(GetCurrentProcess(), 3);
    return 0;
}

void setupCrashHandler()
{
    // ① 路径先固定下来：崩溃时不能再依赖工作目录
    GetModuleFileNameA(NULL, s_exePath, MAX_PATH);
    strcpy_s(s_appDir, MAX_PATH, s_exePath);
    for (int i = (int)strlen(s_appDir) - 1; i >= 0; --i) {
        if (s_appDir[i] == '\\' || s_appDir[i] == '/') { s_appDir[i] = 0; break; }
    }

    // ② 别让 WER 弹框把进程挂在服务器桌面上（dump 我们自己写）
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // ③ 堆检查：发现堆被踩坏就立刻中止（默认就开，这里显式设一次）
    HeapSetInformation(GetProcessHeap(), HeapEnableTerminationOnCorruption, NULL, 0);

    // ④ 五道入口全挂上
    SetUnhandledExceptionFilter(CrashHandler);
    AddVectoredContinueHandler(1, VectoredContinueCrashHandler);
    std::set_terminate(onTerminate);
    _set_invalid_parameter_handler(onInvalidParameter);
    _set_purecall_handler(onPurecall);
    s_prevQtHandler = qInstallMessageHandler(qcCrashMessageHandler);

    // ⑤ abort() 只关掉弹框文字，**保留 _CALL_REPORTFAULT**：
    //    这样 abort 会以 STATUS_FATAL_APP_EXIT 抛出来、进得了 filter；
    //    旧代码连 REPORTFAULT 一起清了 → abort 变 _exit(3)，无 dump 无日志，静默消失。
    _set_abort_behavior(0, _WRITE_ABORT_MSG);

    // ⑥ watchdog
    s_fatalEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 64 * 1024, crashWatchdogThread, NULL, 0, NULL);

    rawLogf("---- crash handler armed (pid=%lu) dir=%s ----\r\n",
            (unsigned long)GetCurrentProcessId(), s_appDir);
}

// 定时重挂：插件 DLL 运行期 LoadLibrary 后可能把 filter / terminate 换掉。
// 在主循环那个每分钟的 QTimer 里顺手调一次，代价可以忽略。
void reArmCrashHandler()
{
    SetUnhandledExceptionFilter(CrashHandler);
    std::set_terminate(onTerminate);
}

#else
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

// ---------- Linux 信号处理：只做异步安全的事 ----------
static void posixSignalHandler(int sig)
{
    // 1. 写崩溃标记（只 open/write/close，异步安全）
    int fd = open("/tmp/myapp_crash_marker",
                  O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd != -1) {
        char buf[64];
        int n = snprintf(buf, sizeof(buf), "signal=%d\n", sig);
        write(fd, buf, n);
        close(fd);
    }

    // 2. 恢复默认并重新触发，让内核生成 core dump
    signal(sig, SIG_DFL);
    raise(sig);
    // 理论上不会再执行到这里
}

// 用 atexit / 正常退出路径做清理，而不是在信号处理里
void setupCrashHandler()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = posixSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    // 可选：SIGBUS, SIGSYS
}
#endif
void initDBs() {
    for (int i = 0; i < 5; ++i) {
        auto db = std::make_unique<LogDB>(QString("botdb/logdb_%1").arg(i));
        if (!db->open()) {
            qCritical() << "打开数据库" << i << "失败";

        } else {
            g_logdb[i] = std::move(db);
        }
    }
    // 发送错误独立库：结构与 logdb_* 完全一样（同一个 LogDB 类），只是单独一个目录，
    // 专门存「发送失败」的记录，方便单独看/单独清。开不了就留 nullptr（写入点全判空）。
    {
        auto edb = std::make_unique<LogDB>(QStringLiteral("botdb/logdb_err"));
        if (!edb->open()) {
            qCritical() << "打开发送错误数据库失败";
        } else {
            g_errlogdb = std::move(edb);
        }
    }
}


QString browseWeb(const QString &urlString);
double totalMemMB=0;
qint64 g_totalRuntime=0;

void detectOptimalRegion();
QString processText(const QString &text, int timeoutMs);


int main(int argc, char *argv[]) {


    int ret=0;
    {

        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
        QApplication::setHighDpiScaleFactorRoundingPolicy(
            Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
            );

        //qputenv("QT_DEBUG_PLUGINS", "1");
        setupCrashHandler();
        QApplication a(argc, argv);
        qRegisterMetaType<MessageEvent>("MessageEvent");
        g_totalRuntime = QDateTime::currentSecsSinceEpoch();
        QUuid uuid = QUuid::createUuid();
        g_keyuuid = uuid.toString(QUuid::WithoutBraces).toStdString();
        int len = g_keyuuid.length();

        g_keyuuid2 = new char[len + 1];
        #ifdef _WIN32
                strcpy_s(g_keyuuid2, len + 1, g_keyuuid.c_str());
                MEMORYSTATUSEX memStatus;
                memStatus.dwLength = sizeof(memStatus);
                if (GlobalMemoryStatusEx(&memStatus))
                    totalMemMB = memStatus.ullTotalPhys / (1024.0 * 1024.0);
                else
                    totalMemMB = 8192.0;
        #else
                strcpy(g_keyuuid2, g_keyuuid.c_str());
                long pages = sysconf(_SC_PHYS_PAGES);
                long pageSize = sysconf(_SC_PAGE_SIZE);
                if (pages > 0 && pageSize > 0)
                    totalMemMB = (pages * pageSize) / (1024.0 * 1024.0);
                else
                    totalMemMB = 8192.0;
        #endif


        initdir();
        loadconfig();
        clearPTmpFolder();

        std::unique_ptr<py::scoped_interpreter> interpreter;
        try {
            interpreter = std::make_unique<py::scoped_interpreter>();
            py::exec(R"(
    import sys
    import os

    base = os.path.abspath('.')  # 或你指定的根目录
    plugin_root = os.path.join(base, 'plugin')
    plugins_root = os.path.join(base, 'plugins')
    if os.path.exists(plugin_root):
        sys.path.insert(0, base)  # 父目录，以便 import plugin.xxx
    if os.path.exists(plugins_root):
        sys.path.insert(0, base)
    )");
        } catch (const std::exception& e) {
            QString text = QString("Python 解释器初始化失败(看看lib文件夹在不在):\n%1").arg(e.what());
            QMessageBox::critical(nullptr, "错误", text);
            return -1;
        } catch (...) {
            QMessageBox::critical(nullptr, "错误", "未知异常");
            return -1;
        }

        py::gil_scoped_release release;
        // 上传缓存：双库按日轮换（见 ensureCacheDb 上方说明）。
        // 不再做「启动全库扫过期条目」—— 跨天时旧库整个目录被删，本来就没有过期条目留下。
        ensureCacheDb();
        initDBs();
        #ifdef _WIN32
            if (QFile::exists("纯白铃32.exe")) {
                bridge = new SharedMemoryBridge;
                bridge->setCallback(myCallback);
                if (!bridge->startServer(false)) qCritical("Bridge start failed");
            }
        #endif
        aidb= new LmdbKV("botdb/aidb");
        dsdb = new LmdbKV("botdb/dsdb");
        accdb = new LmdbKV("botdb/accountinfo");

        MainWindow *w = new MainWindow();
        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            QSystemTrayIcon *trayIcon = new QSystemTrayIcon(&a);
            trayIcon->setIcon(QIcon(":/icons/app_icon.ico")); // 从资源文件加载[reference:2][reference:3]
            trayIcon->setToolTip("纯白铃铛");
            QMenu *menu = new QMenu();
            QAction *showAction = new QAction("显示主窗口", menu);
            QAction *hideAction = new QAction("隐藏主窗口", menu);
            menu->addSeparator(); // 添加分隔线
            QAction *quitAction = new QAction("退出", menu);
            menu->addAction(showAction);
            menu->addAction(hideAction);
            menu->addAction(quitAction);
            trayIcon->setContextMenu(menu);
            QObject::connect(showAction, &QAction::triggered, w, &QMainWindow::show);
            QObject::connect(hideAction, &QAction::triggered, w, &QMainWindow::hide);
            QObject::connect(quitAction, &QAction::triggered, &a, &QApplication::quit);
            QObject::connect(trayIcon, &QSystemTrayIcon::activated,
                             [&w](QSystemTrayIcon::ActivationReason reason) {
                                 if (reason == QSystemTrayIcon::Trigger) { // 单击[reference:9]
                                     if (w->isHidden()) {
                                         w->show();
                                     } else {
                                         w->hide();
                                     }
                                 }
                             });
            trayIcon->show();
        }

        w->show();
        detectOptimalRegion();

        // 上传缓存：每分钟比一次「槽位」（一次 QDateTime + 整除，微秒级），
        // 走到 05:00 那个槽位边界（每 2 天一次）就换库并把旧库整目录删掉 —— 最迟 05:01 生效。
        // 不做全库扫描，所以条数再多也不会有卡顿，也不会长时间占着缓存库的锁。
        {
            QTimer *cacheRotateTimer = new QTimer(&a);
            QObject::connect(cacheRotateTimer, &QTimer::timeout, &a, []() {
                ensureCacheDb();
                // 插件 DLL 运行期加载后可能把 SetUnhandledExceptionFilter 顶掉，每分钟重挂一次
#ifdef _WIN32
                reArmCrashHandler();
#endif
            });
            cacheRotateTimer->start(60 * 1000);
        }

        //QString uploadToMhimg(const QString &filePath, QString *errorMsg);
        //QString err;
        //qDebug() << uploadToMhimg("C:\\Users\\Airuan\\Pictures\\AI绘画\\下载.png",&err);

        //void uploadToMhimgAsync(const QByteArray &imageData,
        //                        const QString &originalFileName,
        //                        std::function<void(QString)> callback);

        //uploadToMhimgAsync(R_file("C:\\Users\\Airuan\\Pictures\\AI绘画\\下载.png"),"纳西妲.png",[](const QString& url){
        //    qDebug() << url;
        //});
        AppendEventLog("接收消息延迟高？ 到设置 提高线程池数量 即可",0xff);
        ret = a.exec();
        框架退出=true;
        for(auto &c :m_botClients)
        {
            c->stop();
        }
        #ifdef _WIN32
        if(bridge)
        {
            bridge->writeResponseToBlock(1,"{\"type\":6}");
            bridge->stopServer();
        }
        #endif
        pluginPage->foruninstall_Plugin();

        py::gil_scoped_acquire acquire;
        delete w;
        w = nullptr;

        delete cache_db;
        cache_db = nullptr;
        for (LmdbKV *p : s_retiredCacheDbs) delete p;   // 双库轮换期间 close 掉的旧库（析构里 m_env 已空，重复关无害）
        s_retiredCacheDbs.clear();
        delete aidb;
        aidb = nullptr;
        delete dsdb;
        dsdb = nullptr;
        delete accdb;
        accdb = nullptr;
        a.sendPostedEvents(nullptr, QEvent::DeferredDelete);

        #ifdef _WIN32
                TerminateProcess(GetCurrentProcess(), 0);
        #else
                _exit(0);   // 立刻终止，不调用任何清理函数
        #endif
    }
    return ret;
}
