// webuiadmin.cpp
// 纯白铃 - WebUI 管理面板扩展接口实现
//
// 提供 action：
//   系统
//     getSysInfo          概览数据
//   账号
//     getAccountList      账号列表（可带 secret）
//     saveBot             新增 / 编辑账号
//     qrLoginStart        取登录二维码（QQBindLogin create_bind_task）
//     qrLoginCheck        查询扫码结果（waiting / ok / expired / closed）
//     qrLoginCancel       放弃本次扫码
//   插件
//     getPluginList       已安装插件列表（含 disabledAccounts）
//     setPluginEnabled    整体启用 / 禁用
//     setPluginAccount    针对某个账号启用 / 禁用（appid 列表 = 黑名单）
//     reloadPlugin        重载
//     uninstallPlugin     从框架卸载（uninstall_Plugin2，不弹窗、不删磁盘文件）
//     scanPluginFiles     扫描 plugin/ 与 plugins/ 目录里可加载的插件
//     loadPluginFile      按路径加载插件（自动识别 DLL / Python / JS）
//   插件市场
//     getPluginMarket     拉取 gitee PluginList.json
//     installPlugin       下载 + 解压 + 加载

#include "webuiadmin.h"
#include "clientconnection.h"

#include "global.h"
#include "plugininstaller.h" // 解压工具定位等市场安装公共逻辑
#include "qq_bind_login.h"   // QQBindLogin：扫码登录会话

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <functional>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Psapi.h>
#else
#include <fstream>
#include <sstream>
#include <string>
#endif

namespace {

const char *kMarketUrl =
    "https://gitee.com/linglan2/pure-white-bell--plugin-sdk/raw/master/PluginList.json";

// ---------------------------------------------------------------- 基础工具

void sendReply(ClientConnection *client, const QString &cmd, bool ok,
               const QJsonValue &data, const QString &reqId,
               const QString &msg = QString())
{
    if (!client) return;
    QJsonObject r;
    r["cmd"] = cmd;
    r["success"] = ok;
    if (!data.isUndefined() && !data.isNull()) r["data"] = data;
    if (!msg.isEmpty()) r["msg"] = msg;
    if (!reqId.isEmpty()) r["reqId"] = reqId;
    client->sendMessage(r);
}

QNetworkAccessManager *nam()
{
    static QNetworkAccessManager *m = nullptr;
    if (!m) m = new QNetworkAccessManager(qApp);
    return m;
}

QString typeName(int type)
{
    switch (type) {
    case 0: return QStringLiteral("Python");
    case 1: return QStringLiteral("DLL");
    case 2: return QStringLiteral("DLL32");
    case 3: return QStringLiteral("JS");
    default: return QStringLiteral("未知");
    }
}

QString formatRuntime(qint64 ms)
{
    if (ms < 0) ms = 0;
    qint64 sec = ms / 1000;
    const qint64 h = sec / 3600; sec %= 3600;
    const qint64 m = sec / 60;   sec %= 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'));
}

// 当前进程实际占用的内存（MB）。注意：总内存 totalMemMB 是"机器总量"，
// 直接上报会变成"占用 15741 MB"这种假数据，必须报工作集。
double processMemMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    return 0.0;
#else
    long rss = 0;
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::istringstream iss(line.substr(6));
            iss >> rss;   // 单位 kB
            break;
        }
    }
    return static_cast<double>(rss) / 1024.0;
#endif
}

// g_totalRuntime 存的是"启动时刻的 Unix 秒"（见 main.cpp），不是已运行毫秒，
// 所以必须用 now - 启动 才算得出运行时长。
qint64 runtimeSeconds()
{
    const qint64 sec = QDateTime::currentSecsSinceEpoch() - g_totalRuntime;
    return sec > 0 ? sec : 0;
}

QByteArray stripBom(QByteArray d)
{
    if (d.size() >= 3 && static_cast<unsigned char>(d[0]) == 0xEF
        && static_cast<unsigned char>(d[1]) == 0xBB
        && static_cast<unsigned char>(d[2]) == 0xBF) {
        d.remove(0, 3);
    }
    return d;
}

QString safeFileName(const QString &in)
{
    QString s = in;
    s.replace(QRegularExpression(R"([\\/:*?"<>|\r\n\t])"), "_");
    s = s.trimmed();
    if (s.isEmpty()) s = QString("plugin_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return s;
}

// 通用异步 GET，自动处理重定向；onDone(ok, 数据, 错误信息)
void httpGet(const QUrl &url, int depth,
             std::function<void(bool, const QByteArray &, const QString &)> onDone,
             std::function<void(qint64, qint64)> onProgress = nullptr)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    req.setRawHeader("Referer", "https://gitee.com/");
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");

    QNetworkReply *reply = nam()->get(req);
    if (onProgress) {
        QObject::connect(reply, &QNetworkReply::downloadProgress, qApp, onProgress);
    }
    QObject::connect(reply, &QNetworkReply::finished, qApp,
                     [reply, depth, onDone, onProgress]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            onDone(false, QByteArray(), reply->errorString());
            return;
        }
        const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (!redirect.isNull() && depth < 5) {
            const QUrl next = reply->url().resolved(redirect.toUrl());
            httpGet(next, depth + 1, onDone, onProgress);
            return;
        }
        QByteArray data = reply->readAll();
        // gitee 有时返回 html 跳转页
        if (data.contains("redirected") && data.contains("<a href=")) {
            QRegularExpression rx("<a\\s+href\\s*=\\s*\"([^\"]+)\"");
            const auto m = rx.match(QString::fromUtf8(data));
            if (m.hasMatch() && depth < 5) {
                httpGet(QUrl(m.captured(1)), depth + 1, onDone, onProgress);
                return;
            }
        }
        onDone(true, data, QString());
    });
}

// ---------------------------------------------------------------- 账号

QJsonObject accountToJson(const AccountInfo *a, bool withSecret)
{
    QJsonObject o;
    o["appid"]        = a->appid_int;
    o["appidStr"]     = a->appid.isEmpty() ? QString::number(a->appid_int) : a->appid;
    o["qq"]           = a->botqq;
    o["name"]         = a->nickname;
    o["avatarPath"]   = a->avatarPath;
    o["unid"]         = a->unid;
    o["pduid"]        = a->pduid;
    o["online"]       = a->online;
    o["type"]         = a->type;
    o["wsAddress"]    = a->wsAddress;
    o["wsIntents"]    = a->wsIntents;
    o["markdown"]     = a->markdown;
    o["markdown_pd"]  = a->markdown_pd;
    o["markdown_pd_mb"] = a->markdown_pd_mb;
    o["autoConnect"]  = a->autoConnect;
    o["admin"]        = a->admin;
    o["times"]        = a->times;
    o["tiaoshu"]      = a->tiaoshu;
    o["welcomeMsg"]   = a->welcomeMsg;
    o["fallbackReply"]= a->fallbackReply;
    o["received"]     = a->received;
    o["sent"]         = a->sent;
    o["total_received"] = a->message_received;
    o["total_sent"]   = a->message_sent;
    o["err"]          = a->err;
    o["hasSecret"]    = !a->secret.isEmpty();
    if (withSecret) o["secret"] = a->secret;
    return o;
}

int findAccountIndex(int appid)
{
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i] && m_accounts[i]->appid_int == appid) return i;
    }
    return -1;
}

void handleGetAccountList(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const bool withSecret = params.value("withSecret").toBool(false);
    QJsonArray arr;
    for (const auto &p : std::as_const(m_accounts)) {
        if (!p) continue;
        arr.append(accountToJson(p.get(), withSecret));
    }
    sendReply(client, "getAccountList", true, arr, reqId);
}

void handleSaveBot(const QJsonObject &p, ClientConnection *client, const QString &reqId)
{
    const int appid = p.value("appid").toInt(0);
    if (appid == 0) {
        sendReply(client, "saveBot", false, QJsonValue(), reqId, "appid 不能为 0");
        return;
    }
    if (!accountPage) {
        sendReply(client, "saveBot", false, QJsonValue(), reqId, "accountPage 未就绪");
        return;
    }

    QString secret = p.value("secret").toString();
    const int existing = findAccountIndex(appid);
    const bool isNew = (existing == -1);

    if (isNew && secret.isEmpty()) {
        sendReply(client, "saveBot", false, QJsonValue(), reqId, "新增账号必须填写 secret");
        return;
    }

    std::shared_ptr<AccountInfo> info;
    if (isNew) {
        info = std::make_shared<AccountInfo>();
        info->appid_int = appid;
        info->appid = QString::number(appid);
        m_accounts.append(info);
    } else {
        info = m_accounts[existing];
    }

    if (!secret.isEmpty()) info->secret = secret;
    if (p.contains("qq"))            info->botqq        = p.value("qq").toString();
    if (p.contains("name"))          info->nickname     = p.value("name").toString();
    if (p.contains("wsAddress"))     info->wsAddress    = p.value("wsAddress").toString();
    if (p.contains("admin"))         info->admin        = p.value("admin").toString();
    if (p.contains("welcomeMsg"))    info->welcomeMsg   = p.value("welcomeMsg").toString();
    if (p.contains("fallbackReply")) info->fallbackReply= p.value("fallbackReply").toString();

    if (p.contains("type"))         info->type      = p.value("type").toInt(info->type);
    if (p.contains("wsIntents"))    info->wsIntents = p.value("wsIntents").toInt(info->wsIntents);
    if (p.contains("times"))        info->times     = p.value("times").toInt(info->times);
    if (p.contains("tiaoshu"))      info->tiaoshu   = p.value("tiaoshu").toInt(info->tiaoshu);

    if (p.contains("markdown"))         info->markdown       = p.value("markdown").toBool(info->markdown);
    if (p.contains("markdown_pd"))      info->markdown_pd    = p.value("markdown_pd").toBool(info->markdown_pd);
    if (p.contains("markdown_pd_mb"))   info->markdown_pd_mb = p.value("markdown_pd_mb").toBool(info->markdown_pd_mb);
    if (p.contains("autoConnect"))      info->autoConnect    = p.value("autoConnect").toBool(info->autoConnect);

    // 先落盘（accdb），再补 UI 卡片
    accountPage->saveAccounts(info.get());
    if (isNew) accountPage->refreshCards2(info.get());

    AppendEventLog(QString("[WebUI] %1账号 %2 (%3)")
                       .arg(isNew ? QStringLiteral("新增") : QStringLiteral("编辑"), info->nickname.isEmpty() ? info->appid : info->nickname)
                       .arg(appid));

    QJsonObject ret;
    ret["isNew"] = isNew;
    ret["appid"] = appid;
    ret["data"]  = accountToJson(info.get(), false);
    sendReply(client, "saveBot", true, ret, reqId,
              isNew ? "账号已添加" : "账号已更新");
}

// ---------------------------------------------------------------- 扫码登录
//
// 复用桌面端那套 QQBindLogin：create_bind_task 拿 task_id →
// 二维码内容给用户扫 → 主线程的心跳定时器（mainwindow.cpp 每 3s）会自动 poll，
// 扫成功后由 QQBindLogin::onPollFinished 解密 secret 并调用全局 addbot() 落库。
// 这里只负责：发起会话 + 把二维码给前端 + 判断结果。

QHash<QString, QSet<int>> g_qrBeforeAppids;   // sid -> 发起前已有的账号，用于判断"新增了谁"

QJsonArray qrAddedAppids(const QSet<int> &before, QStringList *names)
{
    QJsonArray arr;
    for (const auto &a : std::as_const(m_accounts)) {
        if (!a || before.contains(a->appid_int)) continue;
        arr.append(a->appid_int);
        if (names) names->append(a->nickname.isEmpty() ? a->appid : a->nickname);
    }
    return arr;
}

void handleQrLoginStart(ClientConnection *client, const QString &reqId)
{
    QPointer<ClientConnection> safe(client);

    QSet<int> before;
    for (const auto &a : std::as_const(m_accounts))
        if (a) before.insert(a->appid_int);

    QQBindLogin::instance().start(
        [safe, reqId, before](bool ok, const QString &sid, const QString &qrUrl, const QString &err) {
            if (!safe) return;
            if (!ok) {
                sendReply(safe, "qrLoginStart", false, QJsonValue(), reqId,
                          err.isEmpty() ? QStringLiteral("获取登录二维码失败") : err);
                return;
            }
            if (!g_qrBeforeAppids.contains(sid))
                g_qrBeforeAppids.insert(sid, before);

            QUrl img("https://api.2dcode.biz/v1/create-qr-code");
            QUrlQuery q;
            q.addQueryItem("data", qrUrl);
            q.addQueryItem("size", "320x320");
            img.setQuery(q);

            QJsonObject o;
            o["sid"]   = sid;
            o["qrUrl"] = qrUrl;                       // 二维码原始内容
            o["qrImg"] = QString::fromUtf8(img.toEncoded(QUrl::FullyEncoded));  // 直接可用的图片地址
            o["ttlSec"]= 120;                         // QQBindLogin 里 120s 过期
            sendReply(safe, "qrLoginStart", true, o, reqId, "请使用手机 QQ 扫码");
        });
}

void handleQrLoginCheck(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const QString sid = params.value("sid").toString();
    if (sid.isEmpty()) {
        sendReply(client, "qrLoginCheck", false, QJsonValue(), reqId, "缺少 sid");
        return;
    }

    QJsonObject o;
    o["sid"] = sid;

    QQBindLogin::Session s;
    if (QQBindLogin::instance().session(sid, &s)) {
        o["status"] = "waiting";                       // 会话还在 -> 等待扫码
        sendReply(client, "qrLoginCheck", true, o, reqId);
        return;
    }

    if (!g_qrBeforeAppids.contains(sid)) {
        o["status"] = "closed";                        // 已经上报过结果了
        sendReply(client, "qrLoginCheck", true, o, reqId);
        return;
    }
    const QSet<int> before = g_qrBeforeAppids.take(sid);

    QStringList names;
    const QJsonArray added = qrAddedAppids(before, &names);
    if (added.isEmpty()) {
        // 会话被清理但没多出账号 = 过期 / 解密失败 / 取消
        o["status"] = "expired";
        sendReply(client, "qrLoginCheck", true, o, reqId, "二维码已过期或登录失败，请重新获取");
        return;
    }

    o["status"] = "ok";
    o["added"]  = added;
    o["names"]  = QJsonArray::fromStringList(names);
    AppendEventLog(QString("[WebUI] 扫码登录成功：%1").arg(names.join(",")));
    sendReply(client, "qrLoginCheck", true, o, reqId,
              QString("登录成功：%1").arg(names.join(",")));
}

void handleQrLoginCancel(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const QString sid = params.value("sid").toString();
    if (!sid.isEmpty()) {
        QQBindLogin::instance().cancel(sid);
        g_qrBeforeAppids.remove(sid);
    }
    sendReply(client, "qrLoginCancel", true, QJsonValue(), reqId, "已取消");
}

// ---------------------------------------------------------------- 插件

QJsonObject pluginToJson(int idx, const PluginInfo &p)
{
    QJsonObject o;
    o["index"]       = idx;
    o["id"]          = p.id;
    o["name"]        = p.name;
    o["version"]     = p.version;
    o["versionInt"]  = p.version_int;
    o["author"]      = p.author;
    o["description"] = p.description;
    o["path"]        = p.path;
    o["icon"]        = p.icon;
    o["type"]        = p.type;
    o["typeName"]    = typeName(p.type);
    o["enabled"]     = p.enabled;
    o["uuid"]        = p.uuid;
    o["sendQuantity"]= p.SendQuantity;
    // 注意语义：PluginInfo::appid 里存的账号是"被禁用"的账号（黑名单），
    // 判定见 pluginpage.cpp 的 dispatch：contains(appid) 就 skip。
    QJsonArray ids;
    for (int a : p.appid) ids.append(a);
    o["disabledAccounts"] = ids;
    return o;
}

int resolvePluginIndex(const QJsonObject &params)
{
    if (params.contains("index")) {
        const int idx = params.value("index").toInt(-1);
        if (idx >= 0 && idx < m_pluginList.size()) return idx;
        return -1;
    }
    const QString key = params.value("id").toString();
    if (key.isEmpty()) return -1;
    for (int i = 0; i < m_pluginList.size(); ++i) {
        const PluginInfo &p = m_pluginList[i];
        if (p.id == key || p.name == key || p.uuid == key || p.path == key) return i;
    }
    return -1;
}

void handleGetPluginList(ClientConnection *client, const QString &reqId)
{
    QJsonArray arr;
    for (int i = 0; i < m_pluginList.size(); ++i) {
        arr.append(pluginToJson(i, m_pluginList[i]));
    }
    sendReply(client, "getPluginList", true, arr, reqId);
}

void handleSetPluginEnabled(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    if (!pluginPage) {
        sendReply(client, "setPluginEnabled", false, QJsonValue(), reqId, "pluginPage 未就绪");
        return;
    }
    const int idx = resolvePluginIndex(params);
    if (idx < 0) {
        sendReply(client, "setPluginEnabled", false, QJsonValue(), reqId, "插件不存在");
        return;
    }
    const bool want = params.value("enabled").toBool(true);
    PluginInfo &info = m_pluginList[idx];
    const bool wasEnabled = info.enabled;

    // 1) 先走框架原有的启停（会触发插件自己的 on_enable / on_disable 钩子，
    //    以及 32 位 / Node 子模块的通知）
    bool hookOk = true;
    if (want && !info.enabled)        hookOk = pluginPage->Enabled_Plugin(idx);
    else if (!want && info.enabled)   hookOk = pluginPage->disable_Plugin(info);

    // 2) 兜底把标志位对齐到请求：
    //    32 位 / Node 类型在子模块无响应时不会改动 enabled，而 dispatch_message
    //    只认这个标志位，所以必须强制对齐，否则"禁用"点了等于没点。
    if (info.enabled != want) info.enabled = want;

    // 3) 刷新桌面端界面（插件卡片 + 详情面板）——Reload_Plugin / uninstall_Plugin2
    //    内部都会刷新，只有 Enabled_Plugin / disable_Plugin 不会，这就是原来
    //    "webui 点了启停，桌面端还显示旧状态"的原因。
    pluginPage->updatePluginItemInUI(idx);

    pluginPage->savePlugins();

    AppendEventLog(QString("[WebUI] 插件 %1 %2 (type=%3 钩子=%4 标志 %5→%6)")
                       .arg(info.name)
                       .arg(want ? QStringLiteral("启用") : QStringLiteral("禁用"))
                       .arg(info.type)
                       .arg(hookOk ? QStringLiteral("ok") : QStringLiteral("失败"))
                       .arg(wasEnabled ? QStringLiteral("启用") : QStringLiteral("禁用"))
                       .arg(info.enabled ? QStringLiteral("启用") : QStringLiteral("禁用")));

    QJsonObject ret;
    ret["index"]   = idx;
    ret["enabled"] = info.enabled;
    ret["hookOk"]  = hookOk;
    sendReply(client, "setPluginEnabled", info.enabled == want, ret, reqId,
              QString("插件 %1 已%2").arg(info.name).arg(want ? QStringLiteral("启用") : QStringLiteral("禁用")));
}

void handleReloadPlugin(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    if (!pluginPage) {
        sendReply(client, "reloadPlugin", false, QJsonValue(), reqId, "pluginPage 未就绪");
        return;
    }
    const int idx = resolvePluginIndex(params);
    if (idx < 0) {
        sendReply(client, "reloadPlugin", false, QJsonValue(), reqId, "插件不存在");
        return;
    }
    const QString name = m_pluginList[idx].name;
    const bool ok = pluginPage->Reload_Plugin(idx);
    sendReply(client, "reloadPlugin", ok, QJsonValue(), reqId,
              ok ? QString("插件 %1 已重载").arg(name) : QString("插件 %1 重载失败").arg(name));
}

void handleUninstallPlugin(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    if (!pluginPage) {
        sendReply(client, "uninstallPlugin", false, QJsonValue(), reqId, "pluginPage 未就绪");
        return;
    }
    const int idx = resolvePluginIndex(params);
    if (idx < 0) {
        sendReply(client, "uninstallPlugin", false, QJsonValue(), reqId, "插件不存在");
        return;
    }
    const QString name = m_pluginList[idx].name;
    // uninstall_Plugin2：不弹确认框、不删磁盘文件，只从框架卸载
    const bool ok = pluginPage->uninstall_Plugin2(idx);
    sendReply(client, "uninstallPlugin", ok, QJsonValue(), reqId,
              ok ? QString("插件 %1 已从框架卸载").arg(name) : QString("插件 %1 卸载失败").arg(name));
}

// 针对某个账号启用/禁用插件。
// 语义与 pluginpage.cpp 的 onAccountCheckStateChanged 完全一致：
//   启用 → 把 appid 从"禁用列表"里移除；禁用 → 加进去。
void handleSetPluginAccount(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    if (!pluginPage) {
        sendReply(client, "setPluginAccount", false, QJsonValue(), reqId, "pluginPage 未就绪");
        return;
    }
    const int idx = resolvePluginIndex(params);
    if (idx < 0) {
        sendReply(client, "setPluginAccount", false, QJsonValue(), reqId, "插件不存在");
        return;
    }
    const int appid = params.value("appid").toInt();
    if (appid == 0) {
        sendReply(client, "setPluginAccount", false, QJsonValue(), reqId, "缺少 appid");
        return;
    }
    const bool enabled = params.value("enabled").toBool(true);

    PluginInfo &info = m_pluginList[idx];
    if (enabled) {
        info.appid.removeAll(appid);
    } else if (!info.appid.contains(appid)) {
        info.appid.append(appid);
    }

    // 同步给 32 位子模块（与桌面端勾选账号时一致），再刷新界面 + 落盘
    pluginPage->sendData32(11, info, joinIntListFast(info.appid, ","));
    pluginPage->updatePluginItemInUI(idx);
    pluginPage->savePlugins();

    QStringList offList;
    for (int a : std::as_const(info.appid)) offList << QString::number(a);
    AppendEventLog(QString("[WebUI] 插件 %1 对账号 %2 %3 (被禁用账号: %4)")
                       .arg(info.name).arg(appid)
                       .arg(enabled ? QStringLiteral("启用") : QStringLiteral("禁用"))
                       .arg(offList.isEmpty() ? QStringLiteral("无") : offList.join(",")));

    QJsonObject ret;
    ret["index"] = idx;
    ret["appid"] = appid;
    ret["enabled"] = enabled;
    QJsonArray arr;
    for (int a : std::as_const(info.appid)) arr.append(a);
    ret["disabledAccounts"] = arr;
    sendReply(client, "setPluginAccount", true, ret, reqId, "已更新");
}

// 插件目录里某个路径是否已经被框架加载
bool isPluginLoaded(const QString &path)
{
    const QString full = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString leaf = QFileInfo(path).fileName();
    for (const PluginInfo &p : std::as_const(m_pluginList)) {
        if (p.path.isEmpty()) continue;
        if (QDir::cleanPath(QFileInfo(p.path).absoluteFilePath()) == full) return true;
        if (!leaf.isEmpty() && QFileInfo(p.path).fileName() == leaf) return true;
    }
    return false;
}

// 扫描 plugin/ 与 plugins/（与 core/global.cpp 的 #扫描插件 用的是同一组目录）
QJsonArray scanPluginCandidates()
{
    QJsonArray out;
    const QStringList dirs = { "plugin", "plugins" };
    for (const QString &dirName : dirs) {
        QDir dir(dirName);
        if (!dir.exists()) continue;

        // 顶层原生动态库（Win .dll / macOS .dylib / Linux .so，见 global.h）
        const QStringList natives = dir.entryList(nativePluginFilters(), QDir::Files, QDir::Name);
        for (const QString &dll : natives) {
            const QString path = dirName + "/" + dll;
            QJsonObject o;
            o["dir"]      = dirName;
            o["name"]     = dll;
            o["path"]     = path;
            o["type"]     = 1;
            o["typeName"] = typeName(1);
            o["entry"]    = dll;
            o["loaded"]   = isPluginLoaded(path);
            out.append(o);
        }

        // 子目录：有 main.py / main.js 才算插件（优先 Python）
        const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &sub : subs) {
            const QString path  = dirName + "/" + sub;
            const bool hasPy    = QFile::exists(path + "/main.py");
            const bool hasJs    = QFile::exists(path + "/main.js");
            if (!hasPy && !hasJs) continue;

            const int type = hasPy ? 0 : 3;
            QJsonObject o;
            o["dir"]      = dirName;
            o["name"]     = sub;
            o["path"]     = path;
            o["type"]     = type;
            o["typeName"] = typeName(type);
            o["entry"]    = hasPy ? QStringLiteral("main.py") : QStringLiteral("main.js");
            o["loaded"]   = isPluginLoaded(path);
            out.append(o);
        }
    }
    return out;
}

void handleScanPluginFiles(ClientConnection *client, const QString &reqId)
{
    QJsonObject ret;
    ret["candidates"] = scanPluginCandidates();
    ret["baseDir"]    = QDir::currentPath();
    sendReply(client, "scanPluginFiles", true, ret, reqId);
}

void handleLoadPluginFile(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    if (!pluginPage) {
        sendReply(client, "loadPluginFile", false, QJsonValue(), reqId, "pluginPage 未就绪");
        return;
    }
    const QString path = params.value("path").toString().trimmed();
    if (path.isEmpty()) {
        sendReply(client, "loadPluginFile", false, QJsonValue(), reqId, "缺少 path");
        return;
    }

    QFileInfo info(path);
    if (!info.exists()) {
        sendReply(client, "loadPluginFile", false, QJsonValue(), reqId,
                  QString("路径不存在：%1").arg(path));
        return;
    }

    // 类型自动识别（与 #加载插件 指令一致）
    int type = -1;
    if (info.isFile() && isNativePluginSuffix(info.suffix())) {
        type = 1;                                   // 原生动态库（内部会自动降级 32 位）
    } else if (info.isDir()) {
        if (QFile::exists(path + "/main.py"))      type = 0;
        else if (QFile::exists(path + "/main.js")) type = 3;
        else {
            sendReply(client, "loadPluginFile", false, QJsonValue(), reqId,
                      QString("文件夹 %1 里没有 main.py 或 main.js").arg(path));
            return;
        }
    } else {
        sendReply(client, "loadPluginFile", false, QJsonValue(), reqId,
                  QString("既不是原生插件库文件（%1），也不是插件目录：%2")
                      .arg(nativePluginFilters().join(" / "), path));
        return;
    }

    QList<int> noDisabledAccounts;                  // 新加载的插件默认对所有账号启用
    const QString err = pluginPage->LoadPlugin(path, type, true, noDisabledAccounts);
    if (!err.isEmpty()) {
        sendReply(client, "loadPluginFile", false, QJsonValue(), reqId,
                  QString("加载失败：%1").arg(err));
        return;
    }
    pluginPage->savePlugins();
    AppendEventLog(QString("[WebUI] 加载插件 %1 (%2)").arg(path, typeName(type)));

    QJsonObject ret;
    ret["path"] = path;
    ret["type"] = type;
    ret["typeName"] = typeName(type);
    sendReply(client, "loadPluginFile", true, ret, reqId,
              QString("已加载 %1").arg(info.fileName()));
}

// ---------------------------------------------------------------- 插件市场

void handleGetPluginMarket(ClientConnection *client, const QString &reqId)
{
    QPointer<ClientConnection> safe(client);
    httpGet(QUrl(kMarketUrl), 0,
            [safe, reqId](bool ok, const QByteArray &raw, const QString &err) {
        if (!ok) {
            sendReply(safe, "getPluginMarket", false, QJsonValue(), reqId,
                      "获取插件市场失败：" + err);
            return;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(stripBom(raw), &pe);
        if (doc.isNull()) {
            sendReply(safe, "getPluginMarket", false, QJsonValue(), reqId,
                      "插件市场数据解析失败：" + pe.errorString());
            return;
        }

        QJsonArray list;
        if (doc.isArray()) {
            list = doc.array();
        } else if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.contains("code") && root.value("code").toInt(0) != 0) {
                sendReply(safe, "getPluginMarket", false, QJsonValue(), reqId,
                          "接口返回错误：" + root.value("message").toString());
                return;
            }
            list = root.value("data").toObject().value("list").toArray();
        }

        QJsonArray out;
        int n = 0;
        for (const QJsonValue &v : std::as_const(list)) {
            const QJsonObject item = v.toObject();
            QJsonObject o;
            const QString id = item.value("id").toString().isEmpty()
                                   ? item.value("name").toString()
                                   : item.value("id").toString();
            o["id"]          = id;
            o["name"]        = item.value("name").toString();
            o["icon"]        = item.value("icon").toString();
            o["remark"]      = item.value("remark").toString();
            o["homepage"]    = item.value("homepage").toString();
            o["author"]      = item.value("author").toString();
            o["versionCode"] = item.value("versionCode").toInt();
            o["versionName"] = item.value("versionName").toString();
            o["downloadUrl"] = item.value("downloadUrl").toString();
            o["type"]        = item.value("type").toString().isEmpty()
                                   ? QStringLiteral("未知")
                                   : item.value("type").toString();
            o["tags"]        = item.value("tags").toArray();

            bool installed = false;
            bool hasUpdate = false;
            int  localVer  = 0;
            QString localVerName;
            for (const PluginInfo &pi : std::as_const(m_pluginList)) {
                if (pi.name == o["name"].toString() || pi.id == id) {
                    installed = true;
                    localVer  = pi.version_int;
                    localVerName = pi.version;
                    break;
                }
            }
            if (installed) {
                hasUpdate = o["versionCode"].toInt() > localVer;
            }
            o["installed"]        = installed;
            o["hasUpdate"]        = hasUpdate;
            o["installedVersion"] = localVerName;
            out.append(o);
            ++n;
        }

        QJsonObject ret;
        ret["list"]  = out;
        ret["total"] = n;
        sendReply(safe, "getPluginMarket", true, ret, reqId);
    });
}

// 下载并安装市场插件
void handleInstallPlugin(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const QString name = params.value("name").toString().trimmed();
    const QString dlUrl = params.value("downloadUrl").toString().trimmed();
    const QString type = params.value("type").toString().trimmed();
    QString id = params.value("id").toString().trimmed();

    if (name.isEmpty() || dlUrl.isEmpty()) {
        sendReply(client, "installPlugin", false, QJsonValue(), reqId, "缺少 name 或 downloadUrl");
        return;
    }
    if (!pluginPage) {
        sendReply(client, "installPlugin", false, QJsonValue(), reqId, "pluginPage 未就绪");
        return;
    }

    const QString safeName = safeFileName(name);
    if (id.isEmpty()) id = safeName;

    QPointer<ClientConnection> safeClient(client);
    auto stage = [safeClient, reqId, name](const QString &s, int percent, const QString &m) {
        if (!safeClient) return;
        QJsonObject o;
        o["cmd"]      = "installPlugin";
        o["success"]  = true;
        o["progress"] = true;          // 过程消息：前端只更新进度条，不结束请求
        o["stage"]    = s;
        o["percent"]  = percent;
        o["name"]     = name;
        if (!m.isEmpty()) o["msg"] = m;
        if (!reqId.isEmpty()) o["reqId"] = reqId;
        safeClient->sendMessage(o);
    };

    // 1. 准备临时 zip
    const QString tmpMarketDir = QCoreApplication::applicationDirPath() + "/tmp/market";
    QDir().mkpath(tmpMarketDir);
    QString zipPath = tmpMarketDir + "/" + safeFileName(id) + ".zip";
    QFile::remove(zipPath);

    stage("download", 0, "开始下载");

    auto lastPct = std::make_shared<int>(-1);

    httpGet(QUrl(dlUrl), 0,
            [safeClient, reqId, name, type, safeName, zipPath, stage, lastPct]
            (bool ok, const QByteArray &data, const QString &err) {
        if (!ok) {
            sendReply(safeClient, "installPlugin", false, QJsonValue(), reqId,
                      "下载失败：" + err);
            return;
        }
        QFile out(zipPath);
        if (!out.open(QIODevice::WriteOnly)) {
            sendReply(safeClient, "installPlugin", false, QJsonValue(), reqId,
                      "无法写入临时文件：" + zipPath);
            return;
        }
        out.write(data);
        out.close();

        stage("extract", 90, "下载完成，正在解压");

        // 2. 定位解压工具（平台差异统一在 plugininstaller.h 里处理）
        QString zipErr;
        const QString sevenZip = PluginInstaller::resolveSevenZip(&zipErr);
        if (sevenZip.isEmpty()) {
            sendReply(safeClient, "installPlugin", false, QJsonValue(), reqId, zipErr);
            return;
        }

        const QString targetDir = QCoreApplication::applicationDirPath()
                                  + "/plugins/" + safeName + "/";
        QDir().mkpath(targetDir);

        QProcess *proc = new QProcess(qApp);
        proc->setProgram(sevenZip);
        proc->setArguments(QStringList() << "x" << zipPath << "-o" + targetDir << "-y" << "-aoa");

        QObject::connect(proc,
                         QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         qApp,
                         [safeClient, reqId, name, type, safeName, targetDir, zipPath,
                          proc, stage](int code, QProcess::ExitStatus status) {
            const QString stderrText = QString::fromLocal8Bit(proc->readAllStandardError());
            proc->deleteLater();
            QFile::remove(zipPath);

            if (code != 0 || status != QProcess::NormalExit) {
                QDir(targetDir).removeRecursively();
                sendReply(safeClient, "installPlugin", false, QJsonValue(), reqId,
                          "解压失败：" + stderrText);
                return;
            }

            stage("load", 95, "解压完成，正在加载插件");

            QString msg;
            // Python / JS 必须走会补依赖的入口（pip install -r requirements.txt / npm install），
            // 裸 LoadPlugin 不补依赖，带 requirements.txt / package.json 的插件会因缺包加载失败。
            // 且必须传**绝对目录**：这两个函数内部是 QFile::exists(dir + "/main.py") 和
            // setWorkingDirectory(dir)，相对路径会依赖进程的当前工作目录，不可靠。
            // 注意它们是异步的（依赖装完才真正加载），所以这里只能报"已开始安装"。
            if (type == "Python") {
                pluginPage->LoadPlugin_Python_pip(targetDir);
                msg = QString("插件 %1 已解压到 plugins/%2，正在后台安装依赖 (pip) 并加载")
                          .arg(name, safeName);
            } else if (type == "JS") {
                pluginPage->npmJSpk(targetDir);
                msg = QString("插件 %1 已解压到 plugins/%2，正在后台安装依赖 (npm) 并加载")
                          .arg(name, safeName);
            } else {
                msg = QString("插件 %1 已解压到 plugins/%2，DLL 类型需手动加载").arg(name, safeName);
            }

            AppendEventLog(QString("[WebUI] 安装插件 %1 (%2)").arg(name, type));
            stage("done", 100, msg);

            QJsonObject ret;
            ret["name"] = name;
            ret["type"] = type;
            ret["path"] = targetDir;
            sendReply(safeClient, "installPlugin", true, ret, reqId, msg);
        });

        proc->start();
    },
    [stage, lastPct](qint64 recv, qint64 total) {
        if (total <= 0) return;
        const int pct = int(recv * 90 / total);
        if (pct == *lastPct) return;
        *lastPct = pct;
        stage("download", pct, QString("下载中 %1/%2 KB")
                                   .arg(recv / 1024).arg(total / 1024));
    });
}

// ---------------------------------------------------------------- 系统概览

void handleGetSysInfo(ClientConnection *client, const QString &reqId)
{
    int onlineCount = 0;
    for (const auto &p : std::as_const(m_accounts)) {
        if (p && p->online) ++onlineCount;
    }
    int pluginEnabled = 0;
    for (const PluginInfo &p : std::as_const(m_pluginList)) {
        if (p.enabled) ++pluginEnabled;
    }

    const qint64 elapsedSec = runtimeSeconds();
    const double memUsed    = processMemMB();          // 进程工作集
    const double memTotal   = totalMemMB > 0 ? totalMemMB : 0;   // 机器总量
    int memPercent = 0;
    if (memTotal > 0)
        memPercent = qBound(0, static_cast<int>(memUsed / memTotal * 100.0), 100);

    QJsonObject o;
    o["accountCount"]     = m_accounts.size();
    o["accountOnline"]    = onlineCount;
    o["pluginCount"]      = m_pluginList.size();
    o["pluginEnabled"]    = pluginEnabled;
    o["wsPort"]           = g_config["webws_p"].toInt();   // WS 端口来自配置
    o["httpPort"]         = g_config["webhook_p"].toInt();
    o["runtime"]          = formatRuntime(elapsedSec * 1000);   // 已运行时长
    o["runtimeSec"]       = double(elapsedSec);
    o["runtimeMs"]        = double(elapsedSec * 1000);
    o["memMB"]            = memUsed;
    o["memTotalMB"]       = memTotal;
    o["memPercent"]       = memPercent;
    o["appid"]            = g_appid;
    o["exiting"]          = 框架退出;

    sendReply(client, "getSysInfo", true, o, reqId);
}

} // namespace

// ---------------------------------------------------------------- 入口

bool webuiAdminHandle(const QString &action,
                      const QJsonObject &params,
                      ClientConnection *client,
                      const QString &reqId)
{
    if (action == "getSysInfo")          { handleGetSysInfo(client, reqId);                         return true; }

    // 账号
    if (action == "getAccountList")      { handleGetAccountList(params, client, reqId);             return true; }
    if (action == "saveBot")             { handleSaveBot(params, client, reqId);                    return true; }
    if (action == "qrLoginStart")        { handleQrLoginStart(client, reqId);                       return true; }
    if (action == "qrLoginCheck")        { handleQrLoginCheck(params, client, reqId);                return true; }
    if (action == "qrLoginCancel")       { handleQrLoginCancel(params, client, reqId);               return true; }

    // 插件
    if (action == "getPluginList")       { handleGetPluginList(client, reqId);                      return true; }
    if (action == "setPluginEnabled")    { handleSetPluginEnabled(params, client, reqId);           return true; }
    if (action == "setPluginAccount")    { handleSetPluginAccount(params, client, reqId);           return true; }
    if (action == "reloadPlugin")        { handleReloadPlugin(params, client, reqId);               return true; }
    if (action == "uninstallPlugin")     { handleUninstallPlugin(params, client, reqId);            return true; }
    if (action == "scanPluginFiles")     { handleScanPluginFiles(client, reqId);                    return true; }
    if (action == "loadPluginFile")      { handleLoadPluginFile(params, client, reqId);             return true; }

    // 插件市场
    if (action == "getPluginMarket")     { handleGetPluginMarket(client, reqId);                    return true; }
    if (action == "installPlugin")       { handleInstallPlugin(params, client, reqId);              return true; }

    return false;
}
