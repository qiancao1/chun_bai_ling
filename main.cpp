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
    qDebug() << "保存时间" << QDateTime::currentDateTime().toString();
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


QString getSystemPythonPrefix() {
    QProcess process;
    process.start("python", QStringList() << "-c" << "import sys; print(sys.prefix)");
    if (!process.waitForFinished(3000)) {
        qWarning() << "Failed to get python prefix";
        return QString();
    }
    QString output = process.readAllStandardOutput().trimmed();
    if (output.isEmpty()) {
        qWarning() << "Python prefix is empty";
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


double totalMemMB=0;
qint64 g_totalRuntime=0;
int main(int argc, char *argv[]) {


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
    qDebug() << len << "|" << g_keyuuid.c_str();
    g_keyuuid2 = new char[len + 1];
    strcpy_s(g_keyuuid2, len + 1, g_keyuuid.c_str());

    QString pythonHome = getSystemPythonPrefix();
    if (!pythonHome.isEmpty()) {
        qputenv("PYTHONHOME", pythonHome.toUtf8());  // Qt 方式
    }
    py::scoped_interpreter guard{};
    py::gil_scoped_release release;
    initdiv();
    loadconfig();
    clearPTmpFolder();


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
    if(bridge)
    {
        bridge->stopServer();
        bridge->writeResponseToBlock(1,"{\"type\":6}");
    }
    pluginPage->foruninstall_Plugin();
    QThreadPool::globalInstance()->waitForDone();

    return ret;
}
