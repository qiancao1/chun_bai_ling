#ifndef PLUGINIMPORTER_H
#define PLUGINIMPORTER_H

/*
 * 「从 JSON 导入插件」—— 把一条插件市场 JSON 直接粘进来下载安装
 *
 * 与 plugin/plugininstaller.h 的分工：
 *   plugininstaller.h  installMarketPlugin()  市场 / 聊天指令那套（先下载、直接覆盖落地目录）
 *   这里              installFromMeta()       手动粘贴导入：**先下载并解压到暂存目录**，
 *                                             确认包内有可加载的入口之后，才动旧的插件目录。
 *
 * 为什么要多一层暂存：导入的 JSON 可能是**已装插件的新版本**。原生库的依赖 DLL / Python
 * 的 .pyd 在加载状态下是锁住的，直接往 plugins/<名字>/ 里解压会失败，而"先卸旧再装新"
 * 又会在下载 / 解压失败时把用户已经能用的旧版本弄没。所以顺序是：
 *   后台线程：下载 → 解压到 <目录>.importing → 探测入口（失败就只剩一个空目录要清）
 *   主线程  ：卸载同 id 的旧实例 → 删旧目录 → 暂存目录改名换入 → 按类型加载
 *
 * 加载分流与市场保持一致（见 plugininstaller.h 顶部注释）：
 *     type 0 (Python) → pluginPage->LoadPlugin_Python_pip()   （pip install，异步）
 *     type 3 (JS)     → pluginPage->npmJSpk()                 （npm install，异步）
 *     type 1 (原生库) → pluginPage->LoadPlugin()              （同步，失败自动降级 32 位）
 *
 * ⚠ installFromMeta() 是**同步阻塞**的（下载 + 解压最长几分钟），只能在非 GUI 线程调用；
 *   它自己会把「卸载 / 换入 / 加载」投回主线程（BlockingQueuedConnection），所以调用方
 *   **不能阻塞主线程等它结束**（否则死锁）—— 直接 QThread::create(...) 起线程就好。
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDirIterator>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QThread>
#include <QUuid>

#include "plugininstaller.h"   // 已间接包含 global.h（pluginPage）/ pluginmarketwindow.h（PluginInfo2）

namespace PluginImporter {

// ---------------------------------------------------------------------------
// 把模板里的行内说明（`"name": "视频解析",<--插件名字`）剥掉
// ---------------------------------------------------------------------------
// 给别人看的模板常这么写，但它不是合法 JSON：直接粘进来会解析失败。
// 只在 `"` 之外（引号计数为偶数）才把 `<--` 之后截断，所以字符串内容里的 `<--` 不会被误伤。
inline QString stripInlineComments(const QString &src)
{
    QStringList lines = src.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        int quotes = 0;
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QLatin1Char('"') && (i == 0 || line.at(i - 1) != QLatin1Char('\\')))
                ++quotes;
            if (ch == QLatin1Char('<') && quotes % 2 == 0
                && line.mid(i, 3) == QLatin1String("<--")) {
                line.truncate(i);
                break;
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------
// 解析：一段 JSON 文本 → 一个插件的市场元数据
// ---------------------------------------------------------------------------
// 支持两种数据形态：
//   ① 本地导入用的极简格式（只有名字 / 入口 / 下载地址，type 可省 → 按包内容自动识别）
//        { "name": "视频解析", "index": "qiancao_videoparse",
//          "downloadUrl": "...", "downloadUrl_linux": "..." }
//   ② 插件市场的完整对象 / 接口壳 / 数组（带 id / type / version 等，全部照收）
//     市场接口壳：{ "code":0, "data": { "list": [ {...} ] } }；数组：取第一条。
// 字段兜底与 pluginmarket.h 的 parsePluginListFromJson 保持一致。
inline bool parsePluginJson(const QByteArray &raw, PluginInfo2 &out, QString &errMsg)
{
    QByteArray data = raw;
    // 去掉 UTF-8 BOM（从文件复制过来时常带）
    if (data.size() >= 3
        && static_cast<unsigned char>(data[0]) == 0xEF
        && static_cast<unsigned char>(data[1]) == 0xBB
        && static_cast<unsigned char>(data[2]) == 0xBF) {
        data.remove(0, 3);
    }

    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    // 解析失败时再试一次「剥掉模板里的 <-- 说明」，还不行就报错
    if (doc.isNull()) {
        const QByteArray cleaned = stripInlineComments(QString::fromUtf8(data)).toUtf8();
        if (cleaned != data)
            doc = QJsonDocument::fromJson(cleaned, &pe);
    }
    if (doc.isNull()) {
        errMsg = QStringLiteral("JSON 解析失败（偏移 %1）：%2\n\n"
                                "提示：如果是从模板复制的，请把 <-- 后面的说明文字删掉。")
                     .arg(pe.offset)
                     .arg(pe.errorString());
        return false;
    }

    QJsonObject item;
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        if (arr.isEmpty()) {
            errMsg = QStringLiteral("JSON 数组是空的，里面没有插件");
            return false;
        }
        item = arr.first().toObject();
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonValue dataVal = root.value(QStringLiteral("data"));
        const QJsonArray wrapped =
            dataVal.isObject() ? dataVal.toObject().value(QStringLiteral("list")).toArray() : QJsonArray();
        const QJsonArray direct = root.value(QStringLiteral("list")).toArray();
        if (!wrapped.isEmpty())      item = wrapped.first().toObject();
        else if (!direct.isEmpty())  item = direct.first().toObject();
        else                         item = root;      // 单条插件对象
    } else {
        errMsg = QStringLiteral("JSON 顶层既不是对象也不是数组");
        return false;
    }

    if (item.isEmpty()) {
        errMsg = QStringLiteral("JSON 里没有插件信息（顶层对象是空的）");
        return false;
    }

    PluginInfo2 info;
    info.id               = item.value(QStringLiteral("id")).toString().trimmed();
    info.name             = item.value(QStringLiteral("name")).toString();
    info.iconPath         = item.value(QStringLiteral("icon")).toString();
    info.remark           = item.value(QStringLiteral("remark")).toString();
    info.detailUrl        = item.value(QStringLiteral("homepage")).toString();
    info.author           = item.value(QStringLiteral("author")).toString();
    info.versionCode      = item.value(QStringLiteral("versionCode")).toInt();
    info.versionName      = item.value(QStringLiteral("versionName")).toString();
    info.downloadUrl      = item.value(QStringLiteral("downloadUrl")).toString();
    info.downloadUrlLinux = item.value(QStringLiteral("downloadUrl_linux")).toString();
    info.index            = item.value(QStringLiteral("index")).toString().trimmed();
    info.type             = item.value(QStringLiteral("type")).toString();

    // 兜底（与市场解析一致）
    if (info.id.isEmpty())   info.id = info.name;
    if (info.type.isEmpty()) info.type = QStringLiteral("未知");
    if (info.index.isEmpty() && PluginMarketMeta::isNativeType(info.type))
        info.index = info.name;

    const QJsonArray tags = item.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &tag : tags)
        info.tags << tag.toString();
    PluginMarketMeta::applyPlatformTags(info);

    if (info.name.trimmed().isEmpty() && info.id.isEmpty()) {
        errMsg = QStringLiteral("这条 JSON 既没有 id 也没有 name，无法确定插件身份");
        return false;
    }
    if (info.downloadUrl.trimmed().isEmpty() && info.downloadUrlLinux.trimmed().isEmpty()) {
        errMsg = QStringLiteral("这条 JSON 里没有 downloadUrl / downloadUrl_linux，没有可下载的包");
        return false;
    }

    out = info;
    return true;
}

// ---------------------------------------------------------------------------
// 原生库兜底：目录里递归找第一个本平台原生库
// ---------------------------------------------------------------------------
// 用在「没法按名字定位」的场合：
//   · 极简 JSON 根本没写 index（只要包里有 dll / so 就装）
//   · index 写了，但对不上包里的文件名 / 作者套了一层目录打包
// detectLoadTarget 只扫顶层，所以这里要自己递归。
inline QString findFirstNativeRecursive(const QString &dir)
{
    QString best;
    QDirIterator it(dir, nativePluginFilters(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        // 取字典序最小的那个：同一个包每次装到的都是同一个文件，行为稳定
        if (best.isEmpty() || QString::compare(p, best, Qt::CaseInsensitive) < 0)
            best = p;
    }
    return best;
}

// ---------------------------------------------------------------------------
// 安装：下载 → 暂存解压 → 主线程换入并加载
// ---------------------------------------------------------------------------
// 成功返回空串；失败返回可直接展示给用户的错误信息。
// note（非空时）写入成功后的补充说明（落地目录 / 依赖安装进度）。
// replacedOld（非空时）传出本次是否覆盖了已装的旧版本。
inline QString installFromMeta(const PluginInfo2 &info, QString &note, bool *replacedOld = nullptr)
{
    const QString dlUrl = PluginMarketMeta::activeDownloadUrl(info);
    if (dlUrl.isEmpty()) {
        return QStringLiteral("该插件没有 %1 版本的包（JSON 里 %2 为空）")
            .arg(PluginMarketMeta::platformName(),
                 PluginMarketMeta::downloadUrlFieldName(info.type));
    }
    if (!pluginPage)
        return QStringLiteral("插件页面尚未就绪");

    const QString dirName    = PluginInstaller::installDirName(info);
    const QString rootDir    = PluginInstaller::pluginsRootDir();
    const QString targetDir  = rootDir + "/" + dirName;
    const QString stagingDir = rootDir + "/" + dirName + ".importing";

    QDir().mkpath(rootDir);

    // 临时包：<程序目录>/tmp/import/<随机>.zip
    // 刻意不用插件名当文件名：名字是中文明 / 带符号时，7z 的路径编码容易出岔子，
    // 而这里只是个临时文件，用 UUID 最省事（7z 按内容识别格式，扩展名对不对无所谓）。
    const QString tmpDir = QCoreApplication::applicationDirPath() + "/tmp/import";
    QDir().mkpath(tmpDir);
    const QString zipPath =
        tmpDir + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".zip";
    QFile::remove(zipPath);

    // 1. 下载（阻塞）
    QString err;
    if (!PluginInstaller::downloadFile(dlUrl, zipPath, err, 180000)) {
        QFile::remove(zipPath);
        return err;
    }

    // 2. 解压到暂存目录 —— 刻意不直接写 targetDir：这个阶段失败时旧版本一点没动
    QDir(stagingDir).removeRecursively();   // 清理上次残留
    if (!PluginInstaller::extractZip(zipPath, stagingDir + "/", err)) {
        QFile::remove(zipPath);
        QDir(stagingDir).removeRecursively();
        return err;
    }
    QFile::remove(zipPath);

    // 3. 暂存目录里先确认能认出入口（原生库按 index 递归找；没写 index 就找包里第一个原生库）
    int probeType = -1;
    QString probePath;
    if (!PluginInstaller::detectLoadTarget(stagingDir, probeType, probePath)
        && PluginInstaller::resolveNativeEntry(stagingDir, info.index).isEmpty()
        && findFirstNativeRecursive(stagingDir).isEmpty()) {
        QDir(stagingDir).removeRecursively();
        return QStringLiteral("下载并解压完成，但包里既没有 main.py / main.js，"
                              "也没有可用的原生库文件，无法自动加载（已回滚，旧版本未受影响）");
    }

    // 4. 换入 + 加载 —— 必须回主线程：要碰 UI、Python GIL 与插件表
    QString swapErr;
    int loadedType = -1;
    bool didReplace = false;
    auto swapAndLoad = [&]() {
        if (!pluginPage) { swapErr = QStringLiteral("插件页面尚未就绪"); return; }

        // 覆盖安装：先卸旧实例（原生库依赖 DLL / Python 的 .pyd 在加载状态下删不掉）
        // 先按市场 id 找，找不到再按落地目录找 —— 两条都要有：JS / Python 插件注册的 id
        // 常常和作者在 JSON 里手写的市场 id 不是同一个值，只认 id 会漏卸。
        int oldIndex = pluginPage->findPluginIndexByIdentity(info.id, info.name);
        if (oldIndex < 0) oldIndex = pluginPage->findPluginIndexByInstallDir(targetDir);
        if (oldIndex >= 0) {
            didReplace = true;
            pluginPage->uninstall_PluginForReinstall(oldIndex);
        }

        QDir(targetDir).removeRecursively();
        if (!QDir(rootDir).rename(dirName + ".importing", dirName)) {
            QDir(stagingDir).removeRecursively();
            swapErr = QStringLiteral("无法把导入结果移动到 plugins/%1/（可能有文件正在被占用）").arg(dirName);
            return;
        }

        // 入口定位：
        //   原生库（JSON 写了 DLL / DLL32）→ 按 index 递归找库文件；对不上就退回「包里第一个原生库」。
        //   其它（Python / JS / **type 没写**）→ 按目录内容判 main.py / main.js，都不中再按原生库兜底。
        //     ⚠ 极简 JSON 不写 type，这里是唯一的类型来源，所以三层兜底都要有。
        //     ⚠ 不能只靠 detectLoadTarget：它只扫顶层，作者套一层同名目录打包
        //       （解压出 视频解析/视频解析.dll）时会漏。
        int type = -1;
        QString loadPath;
        if (PluginMarketMeta::isNativeType(info.type)) {
            loadPath = PluginInstaller::resolveNativeEntry(targetDir, info.index);
            if (loadPath.isEmpty()) loadPath = findFirstNativeRecursive(targetDir);
            if (loadPath.isEmpty()) {
                swapErr = QStringLiteral("已导入到 plugins/%1/，但包里没找到可用的原生库（index = %2）")
                              .arg(dirName, info.index);
                return;
            }
            // JSON 写了 DLL32 就直接走 32 位入口；其余按 64 位加载，失败时 LoadPlugin 自己会降级
            type = (info.type.compare(QStringLiteral("DLL32"), Qt::CaseInsensitive) == 0) ? 2 : 1;
        } else if (!PluginInstaller::detectLoadTarget(targetDir, type, loadPath)) {
            loadPath = PluginInstaller::resolveNativeEntry(targetDir, info.index);
            if (loadPath.isEmpty()) loadPath = findFirstNativeRecursive(targetDir);
            if (loadPath.isEmpty()) {
                swapErr = QStringLiteral("已导入到 plugins/%1/，但包里没有 main.py / main.js，"
                                         "也没有可用的原生库文件").arg(dirName);
                return;
            }
            type = 1;
        }
        loadedType = type;

        if (type == 0) {
            pluginPage->LoadPlugin_Python_pip(targetDir);    // 内含 pip 补全（异步）
        } else if (type == 3) {
            pluginPage->npmJSpk(targetDir);                  // 内含 npm 补全（异步）
        } else {
            QList<int> noDisabledAccounts;                   // 新装的插件默认对所有账号启用
            const QString loadErr = pluginPage->LoadPlugin(loadPath, type, true, noDisabledAccounts);
            if (!loadErr.isEmpty()) {
                swapErr = QStringLiteral("已导入到 plugins/%1/，但加载失败：%2").arg(dirName, loadErr);
                // 极简 JSON 不写 index 时，入口是「包里第一个原生库」猜的；多 dll 的包容易猜错
                if (info.index.trimmed().isEmpty())
                    swapErr += QStringLiteral("\n\n提示：包里如果有多个 dll，请在 JSON 里用 \"index\" "
                                              "指定入口文件（例如 \"index\": \"qiancao_videoparse\"）。");
            } else {
                pluginPage->savePlugins();
            }
        }
    };

    if (QThread::currentThread() == qApp->thread()) {
        swapAndLoad();
    } else if (!QMetaObject::invokeMethod(qApp, swapAndLoad, Qt::BlockingQueuedConnection)) {
        // 投递失败 = 主线程收不到（正在退出 / 没有事件循环）→ 不能当成功，否则会弹「导入完成」
        swapErr = QStringLiteral("无法把安装动作交回界面线程（程序正在退出？）");
    }

    if (replacedOld) *replacedOld = didReplace;
    if (!swapErr.isEmpty()) {
        // 覆盖安装时旧实例已经被卸掉了 —— 必须说清楚，否则用户会以为插件凭空消失
        if (didReplace)
            swapErr += QStringLiteral("\n\n（旧版本已在覆盖前卸载，请再导入一次；"
                                      "若一直失败，通常是插件目录里的 dll / pyd 仍被占用）");
        return swapErr;
    }

    if (loadedType == 0 || loadedType == 3) {
        note = QStringLiteral("落地目录：plugins/%1/；依赖安装已在后台启动（%2），"
                              "进度与最终加载结果请看弹出的日志窗口")
                   .arg(dirName, loadedType == 0 ? QStringLiteral("pip install")
                                                 : QStringLiteral("npm install"));
    } else {
        note = QStringLiteral("落地目录：plugins/%1/").arg(dirName);
    }
    return QString();
}

} // namespace PluginImporter

#endif // PLUGINIMPORTER_H
