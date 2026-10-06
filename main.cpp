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
        cache_db = new LmdbKV("botdb/file_db");
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
