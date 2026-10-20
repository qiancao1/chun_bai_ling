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
#pragma comment(lib, "DbgHelp.lib")

// ---------- 生成 minidump（带时间戳） ----------
static void writeMiniDump(EXCEPTION_POINTERS* ep)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char name[MAX_PATH];
    wsprintfA(name, "crash_%04d%02d%02d_%02d%02d%02d.dmp",
              st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileA(name, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId          = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers    = FALSE;

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                      hFile, MiniDumpNormal, &mei, NULL, NULL);
    CloseHandle(hFile);
}

// ---------- 重启循环保护：60 秒内最多重启 3 次 ----------
static bool shouldRestart()
{
    const char* flagFile = "restart_guard.tmp";
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

// ---------- Windows 崩溃处理 ----------
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
    writeMiniDump(ep);

    // 先判断是否还允许重启，再决定清理
    bool doRestart = shouldRestart();

    // 崩溃现场清理（每个都独立保护）
    safeCleanup();

    // 记录崩溃日志（异步安全：只写文件）
    {
        QFile f("crash.log");
        if (f.open(QIODevice::Append)) {
            f.write(QDateTime::currentDateTime()
                        .toString("[yyyy-MM-dd hh:mm:ss] ").toUtf8());
            f.write(QString("Crash code=0x%1\n")
                        .arg((quintptr)ep->ExceptionRecord->ExceptionCode, 0, 16)
                        .toUtf8());
            f.close();
        }
    }

    // 重启自身
    if (doRestart) {
        restartSelf();
    } else {
        // 超过重启次数，弹一次提示（此路径已很少走到，相对安全）
        MessageBoxA(NULL,
                    "程序反复崩溃，已停止自动重启。\n请查看 crash_*.dmp。",
                    "崩溃", MB_OK | MB_ICONERROR);
    }

    // 直接结束，不跑 atexit / 全局析构，避免二次崩溃
    TerminateProcess(GetCurrentProcess(), 1);
    return EXCEPTION_EXECUTE_HANDLER;
}

void setupCrashHandler()
{
    SetUnhandledExceptionFilter(CrashHandler);
    // 关掉 CRT 的 abort 弹框，让 SEH 接管
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
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
            QObject::connect(cacheRotateTimer, &QTimer::timeout, &a, []() { ensureCacheDb(); });
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
