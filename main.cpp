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
    // 搜索 python3.14t 可执行文件
    QString exePath = QStandardPaths::findExecutable("python3.14t");
    if (exePath.isEmpty()) {
        // 降级：尝试搜索 python3.14（无 t）
        exePath = QStandardPaths::findExecutable("python3.14");
    }
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
    dir.mkpath("tmp/img");
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

double totalMemMB=0;
qint64 g_totalRuntime=0;
int main(int argc, char *argv[]) {
    qputenv("QT_DEBUG_PLUGINS", "1");
    SetUnhandledExceptionFilter(CrashHandler);
    QApplication a(argc, argv);
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
    QString pythonHome= g_config["pythonHome"].toString();
    if(pythonHome.isEmpty()) pythonHome = getPythonPrefix();

    if (!pythonHome.isEmpty()) {
        qputenv("PYTHONHOME", pythonHome.toUtf8());  // Qt 方式
        g_config["pythonHome"] = pythonHome;
    }
    std::unique_ptr<py::scoped_interpreter> interpreter;
    try {
        interpreter = std::make_unique<py::scoped_interpreter>();
    } catch (...) {
        qputenv("PYTHONHOME", QByteArray());
        try {
            interpreter = std::make_unique<py::scoped_interpreter>();
        } catch (...) {
            // 两次都失败，无法继续
            QMessageBox::critical(nullptr, "错误", "Python 解释器初始化失败，程序无法运行。 请尝试安装py3.14t 后试试");
            return -1;
        }
    }
    py::gil_scoped_release release;
    cache_db = new LmdbKV("botdb/file_db");
    g_logdb = new LogDB("botdb/logdb",1000000);
    if (QFile::exists("miaomiao32.exe")) {
        bridge = new SharedMemoryBridge;
        bridge->setCallback(myCallback);
        if (!bridge->startServer(false)) qCritical("Bridge start failed");
    }

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
