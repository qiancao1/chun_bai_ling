#ifndef PLUGININSTALLER_H
#define PLUGININSTALLER_H

/*
 * 插件市场 → 本地安装 的公共实现
 *
 * 背景：这条流程原本在三个地方各写了一份，而且彼此不一致——
 *   1. plugin/pluginmarketwindow.cpp  按钮里的 7z 预检查（只找 7zz / 7z）
 *   2. plugin/pluginmarketwindow.cpp  finishInstall()（找 7zz / 7z / 7za）
 *   3. network/webuiadmin.cpp         handleInstallPlugin()（找 7zz / 7z / 7za + /usr/bin）
 * 结果：Linux 上只装了老 p7zip（只有 7z / 7za）的机器，点安装会被第 1 处直接拦下，
 *       后面的解压逻辑其实认得 7za —— 白拦。
 *
 * 现在统一收到这里：
 *   - resolveSevenZip()      平台感知地定位解压工具
 *                            （Linux = 7zz；Windows = 程序目录下的 7za.exe）
 *   - installDirName()       计算 plugins/ 下的落地目录名（过滤非法字符）
 *   - detectLoadTarget()     按目录内容判断该走哪种加载方式
 *   - installMarketPlugin()  下载 + 解压 + 加载（给聊天指令用）
 *
 * 加载分流（重要）：
 *   下载与解压是**同步阻塞**的，但 Python / JS 的插件**依赖安装**不是——
 *   必须交给 PluginPage 自带的两个入口，它们会补环境再加载（且是异步的）：
 *     type 0 (Python) → pluginPage->LoadPlugin_Python_pip()  （pip install -r requirements.txt）
 *     type 3 (JS)     → pluginPage->npmJSpk()                （npm install）
 *     type 1 (原生库) → pluginPage->LoadPlugin()             （无依赖，同步，可降级 32 位）
 *   用裸 LoadPlugin 装带依赖的 py/js 插件会因缺包加载失败。
 *
 * 依赖：global.h（pluginPage / nativePluginFilters，并已间接 #include ui/pluginpage.h，
 *       所以 PluginPage 的完整定义可见）、pluginmarketwindow.h（PluginInfo2）、
 *       netmanager.h（NetManager）。
 */

#include <exception>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QUuid>
#include <QUrl>

#include "global.h"
#include "pluginmarketwindow.h"
#include "netmanager.h"

namespace PluginInstaller {

// ---------------------------------------------------------------------------
// 解压工具定位
// ---------------------------------------------------------------------------
// 找到返回可执行文件绝对路径；找不到返回空串，并在 errMsg（非空时）里写入提示。
inline QString resolveSevenZip(QString *errMsg = nullptr)
{
    QString found;

#ifdef Q_OS_WIN
    // Windows 用随程序分发的 7za.exe
    found = QCoreApplication::applicationDirPath() + "/7za.exe";
    if (!QFileInfo::exists(found)) found.clear();
#else
    // Linux / macOS：先查 PATH，再兜底几个常见固定路径。
    // Linux 上的正常答案是 7zz（7-Zip 官方版），7z / 7za 只是兼容老 p7zip 的兜底。
    const QStringList candidates = {
        QStringLiteral("7zz"),   // 7-Zip 官方版 —— Linux 默认用这个
        QStringLiteral("7z"),    // p7zip-full（老系统）
        QStringLiteral("7za")    // p7zip 独立版（老系统）
    };
    for (const QString &cmd : candidates) {
        const QString p = QStandardPaths::findExecutable(cmd);
        if (!p.isEmpty()) { found = p; break; }
    }
    if (found.isEmpty()) {
        const QStringList fixed = {
            QStringLiteral("/usr/bin/7zz"), QStringLiteral("/usr/bin/7z"),
            QStringLiteral("/usr/bin/7za"), QStringLiteral("/usr/local/bin/7zz"),
            QStringLiteral("/opt/homebrew/bin/7zz")
        };
        for (const QString &p : fixed) {
            const QFileInfo fi(p);
            if (fi.exists() && fi.isExecutable()) { found = fi.absoluteFilePath(); break; }
        }
    }
#endif

    if (found.isEmpty() && errMsg) {
#ifdef Q_OS_WIN
        *errMsg = QStringLiteral("未找到解压工具 7za.exe（应放在程序所在目录）");
#else
        *errMsg = QStringLiteral("未找到解压工具 7zz，请先安装 7-Zip："
                                 "sudo apt install 7zip");
#endif
    }
    return found;
}

// ---------------------------------------------------------------------------
// 落地目录名
// ---------------------------------------------------------------------------
// 插件在 plugins/ 下的目录名：优先用插件名，过滤掉文件系统非法字符。
inline QString installDirName(const PluginInfo2 &info)
{
    QString s = info.name.trimmed();
    if (s.isEmpty()) s = info.id.trimmed();
    s.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\r\n\t])")), QStringLiteral("_"));
    s = s.trimmed();
    if (s.isEmpty())
        s = QStringLiteral("plugin_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return s;
}

// 所有插件的落地根目录：<程序目录>/plugins
inline QString pluginsRootDir()
{
    return QCoreApplication::applicationDirPath() + "/plugins";
}

// 已安装目录的绝对路径：<程序目录>/plugins/<dirName>
inline QString installTargetDir(const PluginInfo2 &info)
{
    return pluginsRootDir() + "/" + installDirName(info);
}

// ---------------------------------------------------------------------------
// 按目录内容判断加载方式（与 #扫描插件 / loadPluginFile 的识别规则一致）
// ---------------------------------------------------------------------------
// type: 0=Python 3=JS 1=原生动态库；path 传出识别到的入口路径
//      （Python/JS 传所在目录，原生库传库文件 —— 只有原生库那个会被直接交给 LoadPlugin）。
//      识别失败返回 false。
inline bool detectLoadTarget(const QString &dir, int &type, QString &path)
{
    if (QFile::exists(dir + "/main.py")) { type = 0; path = dir; return true; }
    if (QFile::exists(dir + "/main.js")) { type = 3; path = dir; return true; }

    // 顶层找原生动态库（.dll / .dylib / .so，见 global.h）
    QDir d(dir);
    const QStringList natives = d.entryList(nativePluginFilters(), QDir::Files, QDir::Name);
    if (!natives.isEmpty()) {
        type = 1;
        path = d.absoluteFilePath(natives.first());
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 同步下载（阻塞）
// ---------------------------------------------------------------------------
// 注意：NetManager 的请求策略是 NoLessSafeRedirectPolicy，重定向会自动跟随，
//      所以这里拿到的是最终响应体，不需要自己处理 302。
//      必须在**非网络线程**调用（和 pluginmarket.h 的 fetchPluginListFromUrl 同一约束）。
inline bool downloadFile(const QString &url, const QString &destPath, QString &errMsg,
                         int timeoutMs = 120000)
{
    QHash<QString, QString> headers;
    headers["Referer"] = "https://gitee.com/";
    headers["User-Agent"] = "Mozilla/5.0 (compatible; pure-white-bell/1.0)";

    QByteArray data;
    try {
        data = NetManager::instance()->get(url, headers, timeoutMs).get();
    } catch (const std::exception &e) {
        errMsg = QStringLiteral("下载异常：%1").arg(QString::fromUtf8(e.what()));
        return false;
    }

    if (data.isEmpty()) {
        errMsg = QStringLiteral("下载失败：返回内容为空");
        return false;
    }

    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly)) {
        errMsg = QStringLiteral("无法写入临时文件：%1").arg(destPath);
        return false;
    }
    out.write(data);
    out.close();
    return true;
}

// ---------------------------------------------------------------------------
// 同步解压（阻塞）
// ---------------------------------------------------------------------------
inline bool extractZip(const QString &zipPath, const QString &targetDir, QString &errMsg)
{
    QString zipErr;
    const QString sevenZip = resolveSevenZip(&zipErr);
    if (sevenZip.isEmpty()) { errMsg = zipErr; return false; }

    QDir().mkpath(targetDir);

    QProcess proc;
    proc.setProgram(sevenZip);
    proc.setArguments(QStringList() << "x" << zipPath
                                    << "-o" + targetDir << "-y" << "-aoa");
    proc.start();
    if (!proc.waitForStarted(10000)) {
        errMsg = QStringLiteral("无法启动解压工具：%1").arg(sevenZip);
        return false;
    }
    // 大插件解压慢，给 3 分钟
    if (!proc.waitForFinished(180000)) {
        proc.kill();
        proc.waitForFinished(3000);
        errMsg = QStringLiteral("解压超时（%1）").arg(sevenZip);
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString se = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        errMsg = QStringLiteral("解压失败（退出码 %1）：%2").arg(proc.exitCode()).arg(se);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 下载 + 解压 + 加载
// ---------------------------------------------------------------------------
// 成功返回空字符串；失败返回可直接展示给用户的错误信息。
// note（非空时）写入成功后的补充说明，例如落地目录 / 依赖安装进度。
// 调用者若在非主线程，本函数内部会把加载动作投回主线程执行。
//
// ⚠ 返回空串的分量随类型不同：原生库是"已经加载成功"；Python / JS 只是
//   "下载+解压成功，且已交给会补依赖的入口" —— pip/npm 在后台跑，装完才加载。
inline QString installMarketPlugin(const PluginInfo2 &info, QString &note,
                                   QString *installedDirOut = nullptr)
{
    if (info.downloadUrl.trimmed().isEmpty())
        return QStringLiteral("该插件没有提供下载链接");
    if (!pluginPage)
        return QStringLiteral("插件页面尚未就绪");

    const QString dirName    = installDirName(info);
    const QString rootDir    = pluginsRootDir();
    const QString targetDir  = rootDir + "/" + dirName;
    const QString stagingDir = rootDir + "/" + dirName + ".installing";
    if (installedDirOut) *installedDirOut = targetDir;

    QDir().mkpath(rootDir);

    // 临时 zip：<程序目录>/tmp/market/<id>.zip
    const QString tmpDir = QCoreApplication::applicationDirPath() + "/tmp/market";
    QDir().mkpath(tmpDir);
    QString zipName = info.id.trimmed();
    if (zipName.isEmpty()) zipName = dirName;
    zipName.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|\r\n\t])")), QStringLiteral("_"));
    const QString zipPath = tmpDir + "/" + zipName + ".zip";
    QFile::remove(zipPath);

    // 1. 下载
    QString err;
    if (!downloadFile(info.downloadUrl, zipPath, err,120000)) {
        QFile::remove(zipPath);
        return err;
    }

    // 2. 解压到暂存目录 —— 刻意不直接写 targetDir：
    //    万一解压失败/包内容不对，已经装好的旧版本还在，不会被毁掉。
    QDir(stagingDir).removeRecursively();   // 清理上次残留
    if (!extractZip(zipPath, stagingDir + "/", err)) {
        QFile::remove(zipPath);
        QDir(stagingDir).removeRecursively();
        return err;
    }
    QFile::remove(zipPath);

    // 3. 先在暂存目录里确认能认出入口，再换入正式目录
    {
        int probeType = -1;
        QString probePath;
        if (!detectLoadTarget(stagingDir, probeType, probePath)) {
            QDir(stagingDir).removeRecursively();
            return QStringLiteral("解压完成，但包内既没有 main.py / main.js，"
                                  "也没有原生插件库文件，无法自动加载（已回滚）");
        }
    }

    // 4. 换入：删旧目录 → 暂存目录改名
    QDir(targetDir).removeRecursively();
    if (!QDir(rootDir).rename(dirName + ".installing", dirName)) {
        QDir(stagingDir).removeRecursively();
        return QStringLiteral("无法把解压结果移动到 plugins/%1/（可能有文件占用）").arg(dirName);
    }

    // 5. 识别入口（此时路径已经是正式目录）
    int type = -1;
    QString loadPath;   // 原生库用：库文件的**绝对路径**（LoadPlugin_DLL 要求 isFile）
    if (!detectLoadTarget(targetDir, type, loadPath)) {
        return QStringLiteral("已解压到 plugins/%1/，但无法识别入口文件").arg(dirName);
    }

    // 6. 加载 —— 按类型分流，别一律用 LoadPlugin
    //
    //    ⚠ Python / JS 不能用裸 LoadPlugin：它只把插件挂上去，**不会补依赖**。
    //      带 requirements.txt 的 Python 插件、带 package.json 的 JS 插件会因缺包
    //      直接加载失败，用户看到的是"装好了却不能用"。必须走会补环境的两个入口：
    //        LoadPlugin_Python_pip()  → pip install -r requirements.txt → onPipFinished() → 加载
    //        npmJSpk()                → npm install                    → onNpmFinished() → 加载
    //      代价：这两个入口是**异步**的（依赖在后台装，装完才真正加载），所以这里只能
    //      确认"已经交给它们"，拿不到最终加载结果 —— 进度和结果在它们自己弹出的日志窗口里。
    //    原生库没有依赖概念，仍走 LoadPlugin（同步，加载失败会自动降级 32 位）。
    //    三条路都必须回主线程：要碰 UI、Python GIL 和 QProcess。
    QString loadErr;
    auto doLoad = [&]() {
        if (type == 0) {
            pluginPage->LoadPlugin_Python_pip(targetDir);   // 内含 pip 补全（异步）
        } else if (type == 3) {
            pluginPage->npmJSpk(targetDir);                 // 内含 npm 补全（异步）
        } else {
            QList<int> noDisabledAccounts;   // 新装的插件默认对所有账号启用
            loadErr = pluginPage->LoadPlugin(loadPath, type, true, noDisabledAccounts);
            if (loadErr.isEmpty()) pluginPage->savePlugins();
        }
    };

    if (QThread::currentThread() == qApp->thread()) {
        doLoad();
    } else {
        QMetaObject::invokeMethod(qApp, doLoad, Qt::BlockingQueuedConnection);
    }

    if (!loadErr.isEmpty()) {
        return QStringLiteral("已下载并解压到 plugins/%1/，但加载失败：%2").arg(dirName, loadErr);
    }

    if (type == 0 || type == 3) {
        note = QStringLiteral("落地目录：plugins/%1/；依赖安装已在后台启动（%2），"
                              "进度与最终加载结果请看弹出的日志窗口")
                   .arg(dirName, type == 0 ? QStringLiteral("pip install")
                                           : QStringLiteral("npm install"));
    } else {
        note = QStringLiteral("落地目录：plugins/%1/").arg(dirName);
    }
    return QString();
}

} // namespace PluginInstaller

#endif // PLUGININSTALLER_H
