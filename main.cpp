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

#include <QApplication>
#include "cardwidget.h"
#include "mainwindow.h"
#include <QFile>
#include <QJsonObject>
#include "global.h"
#include <QDir>
#include <QResource>
#include "lmdbkv.h"
#include "logdb.h"



namespace py = pybind11;

QJsonObject g_config;
QList<PluginInfo> m_pluginList;
LmdbKV *cache_db=nullptr;
LogDB *g_logdb = nullptr;
QHash<int, CardWidget*> g_CW;
QHash<int, BotDB*> g_botdb;
QString g_admin;
LmdbKV *aidb=nullptr;
LmdbKV *dsdb=nullptr;

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

}

void saveConfig()
{
    if(框架退出) return;
    QFile file("data/config.json");
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(g_config).toJson(QJsonDocument::Compact));
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
#include <windows.h>
#include <QProcess>
QString getPythonExecutable() {

    QString exePath = QStandardPaths::findExecutable("python3.14t");

    return exePath;
}

QString getPythonPrefix() {
    QString pythonExe = getPythonExecutable();
    if (pythonExe.isEmpty()) {
        return QString();
    }

    QProcess process;
    process.start(pythonExe, QStringList() << "-c" << "import sys; print(sys.prefix)");
    if (!process.waitForFinished(2000)) {
        return QString();
    }
    QString output = process.readAllStandardOutput().trimmed();
    if (output.isEmpty()) {
        return QString();
    }
    return output;
}

void initdiv()
{
    QDir dir;
    dir.mkpath("tmp/video");
    dir.mkpath("tmp/audio");
    dir.mkpath("tmp/image");
    dir.mkpath("tmp/file");
    dir.mkpath("data");
    dir.mkpath("botdb");
    dir.mkpath("plugin");
    dir.mkpath("plugin_data");
}
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")



LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep)
{
    // 创建 minidump 文件
    HANDLE hFile = CreateFileA("crash.dmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &mei, NULL, NULL);
        CloseHandle(hFile);
    }


    QMessageBox::about(NULL,  "Error","程序崩溃，已生成 crash.dmp");
    return EXCEPTION_EXECUTE_HANDLER; // 终止进程
}


QString browseWeb(const QString &urlString);
double totalMemMB=0;
qint64 g_totalRuntime=0;
int main(int argc, char *argv[]) {


    qputenv("QT_DEBUG_PLUGINS", "1");
    SetUnhandledExceptionFilter(CrashHandler);
    QApplication a(argc, argv);
    qRegisterMetaType<MessageEvent>("MessageEvent");
    g_totalRuntime = QDateTime::currentSecsSinceEpoch();
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);

    if (GlobalMemoryStatusEx(&memStatus)) {
        totalMemMB = memStatus.ullTotalPhys / (1024.0 * 1024.0);
    } else {
        totalMemMB = 8192.0;
    }

    QUuid uuid = QUuid::createUuid();
    g_keyuuid = uuid.toString(QUuid::WithoutBraces).toStdString();
    int len = g_keyuuid.length();

    g_keyuuid2 = new char[len + 1];
    strcpy_s(g_keyuuid2, len + 1, g_keyuuid.c_str());
    initdiv();
    loadconfig();
    clearPTmpFolder();
    QString pythonHome;
    if(g_config["yiyu"].toInt()!=1)
    {
        pythonHome= g_config["pythonHome"].toString();
        if(pythonHome.isEmpty()) pythonHome = getPythonPrefix();

        if (!pythonHome.isEmpty()) {
            qputenv("PYTHONHOME", pythonHome.toUtf8());  // Qt 方式
            g_config["pythonHome"] = pythonHome;
        }
    }
    //QMessageBox::warning(nullptr,"",pythonHome);
    std::unique_ptr<py::scoped_interpreter> interpreter;
    try {
        interpreter = std::make_unique<py::scoped_interpreter>();
    } catch (...) {
        QString text ="Python 解释器初始化失败，程序无法运行。 因为设置了py3.14t 路径问题 实际上本程序并不需要服务器安装py环境"
                       "\n为什么出现这个错误 代表 程序获取到了 你py路径 但是初始化失败了\n\n\n最后 需要不设置 路径运行吗？ 实际上本项设置只是为了找到你pip的包\n"
                       "如果你可以 设置到lib/site-packages/ 里面也是可以引用到的\n->"+pythonHome;
        auto res = QMessageBox::critical(nullptr, "错误",text ,QMessageBox::Yes | QMessageBox::No);
        if(res == QMessageBox::Yes)
        {
            g_config["yiyu"] = 1;
            g_config["pythonHome"] = QString();
            saveConfig();
        }
        return -1;
    }
    py::gil_scoped_release release;
    cache_db = new LmdbKV("botdb/file_db");
    g_logdb = new LogDB("botdb/logdb",1000000);
    g_logdb->open();
    if (QFile::exists("miaomiao32.exe")) {
        bridge = new SharedMemoryBridge;
        bridge->setCallback(myCallback);
        if (!bridge->startServer(false)) qCritical("Bridge start failed");
    }
    aidb= new LmdbKV("botdb/aidb");
    dsdb = new LmdbKV("botdb/dsdb");




    //QString res = browseWeb("https://cn.bing.com/search?q=%e6%b1%bd%e6%b0%b4%e9%9f%b3%e4%b9%90&first=1");

    //qDebug()<< res;


    //res = browseWeb("https://cn.bing.com/search?q=%e6%b1%bd%e6%b0%b4%e9%9f%b3%e4%b9%90&first=10");

    //Debug()<< res;



    MainWindow w;
    w.show();
    int ret = a.exec();
    框架退出=true;
    for(auto &c :m_botClients)
    {
        c->stop();
    }
    if(bridge)
    {
        bridge->writeResponseToBlock(1,"{\"type\":6}");
        bridge->stopServer();
    }
    pluginPage->foruninstall_Plugin();
    QThreadPool::globalInstance()->waitForDone();

    return ret;
}
