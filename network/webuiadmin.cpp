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
//   设置 / 规则
//     getSysConfig        读取可在 WebUI 编辑的全局配置（data/config.json 白名单）
//     setSysConfig        写回配置并保存
//     getForbiddenWords   读取 data/forbidden_words.txt
//     setForbiddenWords   写回并重建 AC 自动机
//     getRuleFile         读取 data/*_rules.json（白名单）
//     setRuleFile         校验 JSON 后写回，并让框架重新加载规则

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

#include <algorithm>   // std::sort（黑名单排序）
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

// 规则类页面保存后要回调这些控件做「重新加载」，改动才会立刻生效。
// 注意：forbiddenwordpage.h 里有 #define QTextEdit PlaceholderTextEdit，
// 所以这一组头文件必须放在所有 Qt 头之后（本 TU 不使用 QTextEdit，安全）。
#include "forbiddenwordpage.h"
#include "keywordmatchconfigwidget.h"
#include "keywordpunishconfigwidget.h"
#include "textreplaceconfigwidget.h"
#include "botruleconfigwidget.h"

// 「Ai → 模型配置」里的 API 密钥在 data/model_config.json 里是加密存的，
// WebUI 读要解密、写要加密，所以这里跟着用同一套 MachineKey。
#include <QSysInfo>
#include "machinekey.h"

// 这两个全局指针定义在 mainwindow.cpp，core/global.h 里没有声明
extern TextReplaceConfigWidget *TextReplace;
extern BotRuleConfigWidget     *RuleConfigWidget;

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

            // 平台差异（**index 与 downloadUrl_linux 只有原生库 DLL / DLL32 才有**，
            // Python / JS 只有通用 downloadUrl）：downloadUrl = Windows 包 / 通用包，
            // downloadUrl_linux = 原生库的 Linux 包。按当前平台挑出真正要下载的那个地址
            // 给前端（旧前端只会读 downloadUrl），并把 index 与自动补好的平台标签一起传下去。
            {
                PluginInfo2 meta;
                meta.type             = o["type"].toString();
                meta.downloadUrl      = item.value("downloadUrl").toString();
                meta.downloadUrlLinux = item.value("downloadUrl_linux").toString();
                meta.index            = item.value("index").toString().trimmed();
                // index 只有原生库才有：没写时退回插件名当入口基名；
                // Python / JS 没有这个字段，保持空（entryFile 自然也是空）。
                if (meta.index.isEmpty() && PluginMarketMeta::isNativeType(meta.type))
                    meta.index = o["name"].toString();
                PluginMarketMeta::applyPlatformTags(meta);

                QJsonArray tagArr;
                for (const QString &t : std::as_const(meta.tags)) tagArr.append(t);

                o["index"]            = meta.index;
                o["downloadUrlWin"]   = meta.downloadUrl;
                o["downloadUrlLinux"] = meta.downloadUrlLinux;
                o["downloadUrl"]      = PluginMarketMeta::activeDownloadUrl(meta);
                o["entryFile"]        = PluginMarketMeta::entryFileName(meta.index);
                o["available"]        = PluginMarketMeta::availableOnThisPlatform(meta);
                o["tags"]             = tagArr;
            }

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

    // 市场列表里补齐 index（**只有原生库 DLL / DLL32 才有**这个字段）：前端旧版本不会传 index，
    // 而没有它就定位不到 纯白世界.dll / 纯白世界.so。查不到时退回插件名当基名。
    // Python / JS 的入口靠目录内容判定（main.py / main.js），不需要 index，保持空。
    QString index = params.value("index").toString().trimmed();
    if (index.isEmpty() && PluginMarketMeta::isNativeType(type)) {
        for (const PluginInfo2 &m : std::as_const(m_allPlugins)) {
            if ((!id.isEmpty() && m.id == id) || m.name == name) { index = m.index; break; }
        }
        if (index.isEmpty()) index = name;
    }

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
            [safeClient, reqId, name, type, safeName, index, zipPath, stage, lastPct]
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
                         [safeClient, reqId, name, type, safeName, index, targetDir, zipPath,
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
            } else if (PluginMarketMeta::isNativeType(type)) {
                // 原生库（DLL / DLL32，大小写不敏感）：按 index 定位当前平台的入口文件
                // （纯白世界 → 纯白世界.dll / 纯白世界.so）后直接加载，不再让用户手动装。
                QString entryErr;
                const QString entry = PluginInstaller::resolveNativeEntry(targetDir, index, &entryErr);
                if (entry.isEmpty()) {
                    msg = QString("插件 %1 已解压到 plugins/%2，但没找到入口文件：%3")
                              .arg(name, safeName, entryErr);
                } else {
                    QList<int> noDisabledAccounts;   // 新装的插件默认对所有账号启用
                    const int nativeType = (type.compare(QLatin1String("DLL32"), Qt::CaseInsensitive) == 0)
                                               ? 2 : 1;   // 2=DLL32（走 32 位桥接），1=x64 原生库
                    const QString loadErr = pluginPage->LoadPlugin(
                        entry, nativeType, true, noDisabledAccounts);
                    if (loadErr.isEmpty()) {
                        pluginPage->savePlugins();
                        msg = QString("插件 %1 已安装并加载（入口 %2）")
                                  .arg(name, entry.section('/', -1));
                    } else {
                        msg = QString("插件 %1 已解压到 plugins/%2，但加载失败：%3")
                                  .arg(name, safeName, loadErr);
                    }
                }
            } else {
                msg = QString("插件 %1 已解压到 plugins/%2，类型 %3 未知，需手动加载")
                          .arg(name, safeName, type);
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

// ---------------------------------------------------------------- 全局设置

// WebUI 里可编辑的全局配置（data/config.json 白名单）。
// 只列框架级、改了不会当场把 WebUI 自己弄死的项。
// restart = true 表示要重启框架才生效，界面上会标出来。
struct SysConfigItem {
    const char *key;
    const char *label;
    const char *type;      // int / bool / string
    bool        restart;
    const char *desc;
};

const SysConfigItem kSysConfigItems[] = {
    { "webws_p",         "WebSocket 端口", "int",    true,  "WebUI 长连接与聊天室用的端口" },
    { "webhook_p",       "HTTP 端口",      "int",    true,  "静态页（/webui）与图床上传用的端口" },
    { "SSL",             "启用 SSL",       "bool",   true,  "WebUI 走 https / wss" },
    { "logs",            "日志缓存条数",   "int",    false, "最多永久缓存多少条聊天记录" },
    { "xc_s",            "线程池线程数",   "int",    true,  "0 表示按 CPU 核心数自动" },
    { "ai_log_max",      "AI 日志条数",    "int",    false, "每个账号最多保留多少条 AI 日志" },
    { "admin",           "全局管理员",     "string", false, "空格分隔的管理员 openid" },
    { "ffmpeg",          "ffmpeg 路径",    "string", false, "ffmpeg 可执行文件所在目录" },
    { "local_server_ip", "本机对外 IP",    "string", false, "生成 WebUI / 图床链接时用的地址" },
    { "y_img",           "启用远程图床",   "bool",   false, "把图片交给远程服务处理" },
    { "y_port",          "远程图床地址",   "string", false, "远程图床的主机:端口" },
    { "SendType",        "聊天发送模式",   "int",    false, "聊天页默认的发送模式" },
};
const int kSysConfigCount = int(sizeof(kSysConfigItems) / sizeof(kSysConfigItems[0]));

bool jsonToBool(const QJsonValue &v)
{
    if (v.isBool()) return v.toBool();
    const QString s = v.toVariant().toString().trimmed().toLower();
    return s == "true" || s == "1" || s == "on" || s == "yes";
}

QJsonArray buildSysConfigItems()
{
    QJsonArray items;
    for (int i = 0; i < kSysConfigCount; ++i) {
        const SysConfigItem &it = kSysConfigItems[i];
        const QString key  = QString::fromUtf8(it.key);
        const QString type = QString::fromUtf8(it.type);
        // 注意用 value() 而不是 operator[] —— 后者会在键不存在时往 g_config 里塞一个 null
        const QJsonValue cur = g_config.value(key);

        QJsonObject o;
        o["key"]     = key;
        o["label"]   = QString::fromUtf8(it.label);
        o["type"]    = type;
        o["restart"] = it.restart;
        o["desc"]    = QString::fromUtf8(it.desc);
        if (type == QLatin1String("bool"))      o["value"] = cur.toBool(false);
        else if (type == QLatin1String("int"))  o["value"] = cur.toInt(0);
        else                                    o["value"] = cur.toString();
        items.append(o);
    }
    return items;
}

void handleGetSysConfig(ClientConnection *client, const QString &reqId)
{
    QJsonObject ret;
    ret["items"] = buildSysConfigItems();
    sendReply(client, "getSysConfig", true, ret, reqId);
}

void handleSetSysConfig(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const QJsonObject values = params.value("values").toObject();
    if (values.isEmpty()) {
        sendReply(client, "setSysConfig", false, QJsonValue(), reqId, "没有提交任何配置");
        return;
    }

    QStringList changed;
    for (int i = 0; i < kSysConfigCount; ++i) {
        const SysConfigItem &it = kSysConfigItems[i];
        const QString key = QString::fromUtf8(it.key);
        if (!values.contains(key)) continue;          // 只认白名单里的键
        const QJsonValue in = values.value(key);
        const QString type = QString::fromUtf8(it.type);

        if (type == QLatin1String("bool"))       g_config[key] = jsonToBool(in);
        else if (type == QLatin1String("int"))   g_config[key] = in.toVariant().toString().trimmed().isEmpty()
                                                                    ? 0
                                                                    : in.toVariant().toInt();
        else                                     g_config[key] = in.toVariant().toString();
        changed << key;
    }

    if (changed.isEmpty()) {
        sendReply(client, "setSysConfig", false, QJsonValue(), reqId,
                  "提交的配置项都不在白名单里，已忽略");
        return;
    }

    saveConfig();
    AppendEventLog(QString("[WebUI] 修改全局配置 %1").arg(changed.join(QLatin1Char(','))));

    QJsonObject ret;
    ret["items"] = buildSysConfigItems();
    sendReply(client, "setSysConfig", true, ret, reqId,
              QString("已保存 %1 项（端口 / SSL 这类改动要重启框架才生效）").arg(changed.size()));
}

// ---------------------------------------------------------------- 违禁词

QString forbiddenWordsPath() { return QStringLiteral("data/forbidden_words.txt"); }

int countNonEmptyLines(const QString &text)
{
    int n = 0;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        if (!l.trimmed().isEmpty()) ++n;
    }
    return n;
}

void handleGetForbiddenWords(ClientConnection *client, const QString &reqId)
{
    const QString path = forbiddenWordsPath();
    QString text;
    QFile f(path);
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) {
            sendReply(client, "getForbiddenWords", false, QJsonValue(), reqId,
                      QString("打不开 %1").arg(path));
            return;
        }
        text = QString::fromUtf8(f.readAll());
        f.close();
    }

    QJsonObject ret;
    ret["text"]  = text;
    ret["count"] = countNonEmptyLines(text);
    ret["path"]  = path;
    sendReply(client, "getForbiddenWords", true, ret, reqId);
}

void handleSetForbiddenWords(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const QString path = forbiddenWordsPath();
    const QString text = params.value("text").toString();
    if (text.size() > 2 * 1024 * 1024) {
        sendReply(client, "setForbiddenWords", false, QJsonValue(), reqId, "内容过大（超过 2MB），未保存");
        return;
    }

    // 去空行、去重后再写盘，和桌面端 loadFromDefaultFile 的口径保持一致
    QStringList words;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &l : lines) {
        const QString w = l.trimmed();
        if (!w.isEmpty() && !words.contains(w)) words.append(w);
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendReply(client, "setForbiddenWords", false, QJsonValue(), reqId, QString("写不了 %1").arg(path));
        return;
    }
    QByteArray body = words.join(QLatin1Char('\n')).toUtf8();
    if (!body.isEmpty()) body.append('\n');
    f.write(body);
    f.close();

    const bool reloaded = (forbidden != nullptr);
    if (reloaded) forbidden->webuiReload();

    AppendEventLog(QString("[WebUI] 更新违禁词 %1 条").arg(words.size()));

    QJsonObject ret;
    ret["count"]    = words.size();
    ret["reloaded"] = reloaded;
    ret["path"]     = path;
    sendReply(client, "setForbiddenWords", true, ret, reqId,
              reloaded ? QString("已保存 %1 条违禁词并重新加载").arg(words.size())
                       : QString("已保存 %1 条违禁词（框架未重载，重启后生效）").arg(words.size()));
}

// ---------------------------------------------------------------- 关键词 / 规则文件

struct RuleFileMeta {
    const char *id;         // WebUI 里用的名字
    const char *path;       // 相对框架目录
};

const RuleFileMeta kRuleFiles[] = {
    { "keyword_match",  "data/keyword_match_rules.json"  },
    { "keyword_punish", "data/keyword_punish_rules.json" },
    { "text_replace",   "data/text_replace_rules.json"   },
    { "bot_rules",      "data/bot_rules.json"            },
};
const int kRuleFileCount = int(sizeof(kRuleFiles) / sizeof(kRuleFiles[0]));

const RuleFileMeta *findRuleFile(const QString &id)
{
    for (int i = 0; i < kRuleFileCount; ++i) {
        if (id == QLatin1String(kRuleFiles[i].id)) return &kRuleFiles[i];
    }
    return nullptr;
}

// 让对应的控件重新读一遍文件，重建匹配器 / 规则表
void reloadRuleConsumer(const QString &id)
{
    if (id == QLatin1String("keyword_match") && keyword) {
        keyword->webuiReload();
    } else if (id == QLatin1String("keyword_punish") && keyword_Punish) {
        keyword_Punish->webuiReload();
    } else if (id == QLatin1String("text_replace") && TextReplace) {
        TextReplace->webuiReload();
    } else if (id == QLatin1String("bot_rules") && RuleConfigWidget) {
        RuleConfigWidget->webuiReload();
    }
}

void handleGetRuleFile(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const QString id = params.value("file").toString();
    const RuleFileMeta *meta = findRuleFile(id);
    if (!meta) {
        sendReply(client, "getRuleFile", false, QJsonValue(), reqId, QString("未知的规则文件：%1").arg(id));
        return;
    }
    const QString path = QString::fromUtf8(meta->path);

    QString text = QStringLiteral("{}");
    QFile f(path);
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) {
            sendReply(client, "getRuleFile", false, QJsonValue(), reqId, QString("打不开 %1").arg(path));
            return;
        }
        const QString raw = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!raw.isEmpty()) text = raw;
    }

    QJsonObject ret;
    ret["file"] = QString::fromUtf8(meta->id);
    ret["path"] = path;
    ret["text"] = text;
    ret["exists"] = QFile::exists(path);

    // 带 appid 时额外把这一个账号的规则数组单独拆出来：
    // 这几个规则文件顶层都是 { "<appid>": [ ... ] }，网页上要按账号编辑。
    if (params.contains("appid")) {
        const int appid = params.value("appid").toInt(0);
        QJsonArray rules;
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject()) {
            rules = doc.object().value(QString::number(appid)).toArray();
        }
        ret["appid"] = appid;
        ret["rules"] = rules;
    }
    sendReply(client, "getRuleFile", true, ret, reqId);
}

void handleSetRuleFile(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const QString id = params.value("file").toString();
    const RuleFileMeta *meta = findRuleFile(id);
    if (!meta) {
        sendReply(client, "setRuleFile", false, QJsonValue(), reqId, QString("未知的规则文件：%1").arg(id));
        return;
    }
    const QString path = QString::fromUtf8(meta->path);

    // ---- 按账号写入：只替换该 appid 那一份，其它账号原样保留 ----
    // 这几个文件是「一个文件装所有账号」的结构，整份覆盖式保存会互相踩，
    // 所以 WebUI 默认走这条路径。
    if (params.contains("appid") && params.value("rules").isArray()) {
        const int appid = params.value("appid").toInt(0);
        if (appid == 0) {
            sendReply(client, "setRuleFile", false, QJsonValue(), reqId, "appid 不能为 0，未保存");
            return;
        }

        QJsonObject root;
        QFile rf(path);
        if (rf.exists()) {
            if (!rf.open(QIODevice::ReadOnly)) {
                sendReply(client, "setRuleFile", false, QJsonValue(), reqId, QString("打不开 %1").arg(path));
                return;
            }
            const QByteArray raw = rf.readAll();
            rf.close();
            QJsonParseError rerr{};
            const QJsonDocument rdoc = QJsonDocument::fromJson(raw, &rerr);
            if (rerr.error == QJsonParseError::NoError && rdoc.isObject()) root = rdoc.object();
        }

        const QJsonArray rules = params.value("rules").toArray();
        const QString key = QString::number(appid);
        if (rules.isEmpty()) root.remove(key);   // 清空 = 顺手删掉这个账号的键
        else                 root[key] = rules;

        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile wf(path);
        if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            sendReply(client, "setRuleFile", false, QJsonValue(), reqId, QString("写不了 %1").arg(path));
            return;
        }
        wf.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        wf.close();

        reloadRuleConsumer(id);
        AppendEventLog(QString("[WebUI] 更新规则文件 %1 里账号 %2 的 %3 条规则")
                           .arg(path).arg(appid).arg(rules.size()));

        QJsonObject ret;
        ret["file"]  = id;
        ret["path"]  = path;
        ret["appid"] = appid;
        ret["count"] = rules.size();
        sendReply(client, "setRuleFile", true, ret, reqId,
                  QString("已保存账号 %1 的 %2 条规则并重新加载").arg(appid).arg(rules.size()));
        return;
    }

    const QString text = params.value("text").toString();

    if (text.size() > 8 * 1024 * 1024) {
        sendReply(client, "setRuleFile", false, QJsonValue(), reqId, "内容过大（超过 8MB），未保存");
        return;
    }

    // 先校验，别把坏内容写进去 —— 这个文件是框架运行时直接读的
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError) {
        sendReply(client, "setRuleFile", false, QJsonValue(), reqId,
                  QString("JSON 不合法：%1（位置 %2），未保存").arg(perr.errorString()).arg(perr.offset));
        return;
    }
    if (!doc.isObject()) {
        sendReply(client, "setRuleFile", false, QJsonValue(), reqId,
                  "JSON 顶层必须是对象（键为 appid），未保存");
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendReply(client, "setRuleFile", false, QJsonValue(), reqId, QString("写不了 %1").arg(path));
        return;
    }
    f.write(doc.toJson(QJsonDocument::Compact));
    f.close();

    reloadRuleConsumer(id);
    AppendEventLog(QString("[WebUI] 更新规则文件 %1").arg(path));

    QJsonObject ret;
    ret["file"] = id;
    ret["path"] = path;
    sendReply(client, "setRuleFile", true, ret, reqId, QString("已保存并重新加载 %1").arg(path));
}

// ---------------------------------------------------------------- 内置功能（按账号）

// 前端可能把数字传成字符串，统一兜一下。注意 QJsonValue::toInt() 对字符串恒为 0。
int jsonToInt(const QJsonValue &v)
{
    if (v.isDouble()) return v.toInt();
    const QString s = v.toVariant().toString().trimmed();
    return s.isEmpty() ? 0 : s.toInt();
}

// 一个可以由 WebUI 编辑的账号字段。
// key 直接用 AccountInfo 的成员名，方便和桌面端 ai/ 下的界面逐项对照。
// choices 以 '@' 开头表示「选项在 options 里动态取」（模型名 / 全局设定名）。
struct AcctField {
    const char *key;
    const char *label;
    const char *type;      // int / bool / string / strings
    const char *group;     // 分组，前端按组排版
    const char *desc;
    const char *choices;
    QJsonValue (*get)(const AccountInfo &a);
    void       (*set)(AccountInfo &a, const QJsonValue &v);
};

QJsonArray buildAcctFields(const AcctField *fields, int count,
                           const AccountInfo *a, const QJsonObject &options)
{
    QJsonArray arr;
    for (int i = 0; i < count; ++i) {
        const AcctField &f = fields[i];
        const QString key  = QString::fromUtf8(f.key);
        const QString type = QString::fromUtf8(f.type);

        QJsonObject o;
        o["key"]   = key;
        o["label"] = QString::fromUtf8(f.label);
        o["type"]  = type;
        o["group"] = QString::fromUtf8(f.group);
        o["desc"]  = QString::fromUtf8(f.desc);

        if (a)                                      o["value"] = f.get(*a);
        else if (type == QLatin1String("bool"))     o["value"] = false;
        else if (type == QLatin1String("int"))      o["value"] = 0;
        else if (type == QLatin1String("strings"))  o["value"] = QJsonArray();
        else                                        o["value"] = QString();

        const QString ch = QString::fromUtf8(f.choices);
        if (ch.startsWith(QLatin1Char('@'))) o["choices"] = options.value(ch.mid(1)).toArray();
        else if (!ch.isEmpty())              o["choices"] = QJsonArray::fromStringList(ch.split(QLatin1Char('|')));
        else                                 o["choices"] = QJsonArray();

        arr.append(o);
    }
    return arr;
}

// 只认白名单里的键，返回实际写入的项数
int applyAcctFields(const AcctField *fields, int count, AccountInfo &a,
                    const QJsonObject &values, QStringList &changed)
{
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const AcctField &f = fields[i];
        const QString key = QString::fromUtf8(f.key);
        if (!values.contains(key)) continue;
        f.set(a, values.value(key));
        changed << key;
        ++n;
    }
    return n;
}

// ---- 「内置功能 → 基础」（对应桌面端 ai/qunguan 那一页） ----
// 字段名 = AccountInfo 成员名；注意桌面端「条数 / 时长」两栏的绑定关系就是
// time_Edit→times、tiao_Edit→tiaoshu，这里保持一致。
const AcctField kBasicFields[] = {
    // 刷屏检测
    { "times", "刷屏检测条数", "int", "detect", "统计窗口内同一个人连发多少条就判定为刷屏", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.times); },
      [](AccountInfo &a, const QJsonValue &v) { a.times = jsonToInt(v); } },
    { "tiaoshu", "刷屏检测时长(秒)", "int", "detect", "上面那个统计窗口的长度", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.tiaoshu); },
      [](AccountInfo &a, const QJsonValue &v) { a.tiaoshu = jsonToInt(v); } },

    // 入群 / 退群
    { "rq_ychf", "入群发送延迟(秒)", "int", "delay", "成员入群后等多久再发欢迎语", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.rq_ychf); },
      [](AccountInfo &a, const QJsonValue &v) { a.rq_ychf = jsonToInt(v); } },
    { "rq_lq", "入群提醒 CD(秒)", "int", "delay", "同一个人入群提醒的最小间隔", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.rq_lq); },
      [](AccountInfo &a, const QJsonValue &v) { a.rq_lq = jsonToInt(v); } },
    { "tq_ychf", "退群发送延迟(秒)", "int", "delay", "成员退群后等多久再发提示", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.tq_ychf); },
      [](AccountInfo &a, const QJsonValue &v) { a.tq_ychf = jsonToInt(v); } },
    { "tq_lq", "退群提醒 CD(秒)", "int", "delay", "同一个人退群提醒的最小间隔", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.tq_lq); },
      [](AccountInfo &a, const QJsonValue &v) { a.tq_lq = jsonToInt(v); } },

    // 开关
    { "autoht", "自动回应回调", "bool", "switch", "收到需要回调的事件时自动回应", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.autoht); },
      [](AccountInfo &a, const QJsonValue &v) { a.autoht = jsonToBool(v); } },
    { "pbbot", "屏蔽机器人信息", "bool", "switch", "忽略其它机器人发出的消息", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.pbbot); },
      [](AccountInfo &a, const QJsonValue &v) { a.pbbot = jsonToBool(v); } },
    { "cbl", "启用纯白铃指令", "bool", "switch", "允许群里使用框架的内置指令", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.cbl); },
      [](AccountInfo &a, const QJsonValue &v) { a.cbl = jsonToBool(v); } },

    // 各类回复文本
    { "admin", "机器人管理", "string", "text", "群管理指令的回复", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.admin); },
      [](AccountInfo &a, const QJsonValue &v) { a.admin = v.toString(); } },
    { "caidan", "发送菜单", "string", "text", "菜单指令的回复内容", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.caidan); },
      [](AccountInfo &a, const QJsonValue &v) { a.caidan = v.toString(); } },
    { "help", "发送帮助", "string", "text", "帮助指令的回复内容", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.help); },
      [](AccountInfo &a, const QJsonValue &v) { a.help = v.toString(); } },
    { "emptyAt", "空艾特时", "string", "text", "只 @ 机器人、没带内容时回什么", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.emptyAt); },
      [](AccountInfo &a, const QJsonValue &v) { a.emptyAt = v.toString(); } },
    { "rqhy", "用户入群", "string", "text", "入群欢迎语，支持 py 代码与 {id} 等变量", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.rqhy); },
      [](AccountInfo &a, const QJsonValue &v) { a.rqhy = v.toString(); } },
    { "tqhy", "用户退群", "string", "text", "退群提示语，支持 py 代码与变量", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.tqhy); },
      [](AccountInfo &a, const QJsonValue &v) { a.tqhy = v.toString(); } },
    { "jojnhf", "加群验证", "string", "text", "入群验证的处理逻辑（py 或变量）", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.jojnhf); },
      [](AccountInfo &a, const QJsonValue &v) { a.jojnhf = v.toString(); } },
    { "xxwb", "信息尾巴", "string", "text", "每条回复后面追加的固定文本", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.xxwb); },
      [](AccountInfo &a, const QJsonValue &v) { a.xxwb = v.toString(); } },
    { "fallbackReply", "未命中指令", "string", "text", "没匹配到任何规则时的兜底回复", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.fallbackReply); },
      [](AccountInfo &a, const QJsonValue &v) { a.fallbackReply = v.toString(); } },
    { "welcomeMsg", "机器人入群", "string", "text", "机器人被拉进群时的欢迎语", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.welcomeMsg); },
      [](AccountInfo &a, const QJsonValue &v) { a.welcomeMsg = v.toString(); } },
    { "apply", "有人申请加群", "string", "text", "收到加群申请时的处理逻辑", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.apply); },
      [](AccountInfo &a, const QJsonValue &v) { a.apply = v.toString(); } },
};
const int kBasicFieldCount = int(sizeof(kBasicFields) / sizeof(kBasicFields[0]));

// ---- 「内置功能 → Ai」 ----
// 与桌面端 AiWidget::on_btnSaveRobot_clicked() 保存的是同一批字段，
// 这样网页改完直接写进 AccountInfo，桌面端界面切过去看到的就是新值。
const AcctField kAiFields[] = {
    { "Ai_nickname", "机器人昵称", "string", "ai_basic", "左侧列表里显示的名字", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.Ai_nickname); },
      [](AccountInfo &a, const QJsonValue &v) { a.Ai_nickname = v.toString(); } },
    { "model", "模型", "string", "ai_basic", "对话用的大模型", "@models",
      [](const AccountInfo &a) { return QJsonValue(a.model); },
      [](AccountInfo &a, const QJsonValue &v) { a.model = v.toString(); } },
    { "Embed_model", "向量模型", "string", "ai_basic", "向量记忆库用的模型", "@models",
      [](const AccountInfo &a) { return QJsonValue(a.Embed_model); },
      [](AccountInfo &a, const QJsonValue &v) { a.Embed_model = v.toString(); } },
    { "pplx", "匹配类型", "int", "ai_basic", "群内按什么规则决定要不要理这条消息",
      "不匹配昵称|信息包含|信息头",
      [](const AccountInfo &a) { return QJsonValue(a.pplx); },
      [](AccountInfo &a, const QJsonValue &v) { a.pplx = jsonToInt(v); } },
    { "setting", "全局设定", "string", "ai_basic", "选用哪一套人设设定", "@settings",
      [](const AccountInfo &a) { return QJsonValue(a.setting); },
      [](AccountInfo &a, const QJsonValue &v) { a.setting = v.toString(); } },
    { "context_len", "上下文条数", "int", "ai_basic", "带多少条历史消息给模型", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.context_len); },
      [](AccountInfo &a, const QJsonValue &v) { a.context_len = jsonToInt(v); } },
    { "delayReplySeconds", "延迟回复(秒)", "int", "ai_basic", "收到消息后先等几秒再回", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.delayReplySeconds); },
      [](AccountInfo &a, const QJsonValue &v) { a.delayReplySeconds = jsonToInt(v); } },
    { "nSecondsNoReply", "N 秒没回复", "int", "ai_basic", "0 表示关闭；大于 0 则安静这么久后主动搭话", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.nSecondsNoReply); },
      [](AccountInfo &a, const QJsonValue &v) { a.nSecondsNoReply = jsonToInt(v); } },
    { "nMinutesNoReply", "N 分钟没回复", "int", "ai_basic", "同上，按分钟计", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.nMinutesNoReply); },
      [](AccountInfo &a, const QJsonValue &v) { a.nMinutesNoReply = jsonToInt(v); } },

    { "enableGroupChat", "群聊", "bool", "ai_switch", "在群里启用", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.enableGroupChat); },
      [](AccountInfo &a, const QJsonValue &v) { a.enableGroupChat = jsonToBool(v); } },
    { "enableGroupPersonal", "群个人", "bool", "ai_switch", "群里的私聊式触发", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.enableGroupPersonal); },
      [](AccountInfo &a, const QJsonValue &v) { a.enableGroupPersonal = jsonToBool(v); } },
    { "enablePrivateChat", "私聊", "bool", "ai_switch", "在私聊里启用", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.enablePrivateChat); },
      [](AccountInfo &a, const QJsonValue &v) { a.enablePrivateChat = jsonToBool(v); } },
    { "enableChannel", "频道", "bool", "ai_switch", "在频道里启用", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.enableChannel); },
      [](AccountInfo &a, const QJsonValue &v) { a.enableChannel = jsonToBool(v); } },
    { "enableChannelPersonal", "频道个人", "bool", "ai_switch", "频道私聊场景", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.enableChannelPersonal); },
      [](AccountInfo &a, const QJsonValue &v) { a.enableChannelPersonal = jsonToBool(v); } },
    { "atTrigger", "艾特触发", "bool", "ai_switch", "只有被 @ 才回应", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.atTrigger); },
      [](AccountInfo &a, const QJsonValue &v) { a.atTrigger = jsonToBool(v); } },
    { "enableImageRec", "识图", "bool", "ai_switch", "把图片也送给模型", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.enableImageRec); },
      [](AccountInfo &a, const QJsonValue &v) { a.enableImageRec = jsonToBool(v); } },
    { "xiangliang", "向量记忆库", "bool", "ai_switch", "启用向量数据库做长期记忆", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.xiangliang); },
      [](AccountInfo &a, const QJsonValue &v) { a.xiangliang = jsonToBool(v); } },
    { "niren", "拟人", "bool", "ai_switch", "拟人模式（@咸鱼王）", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.niren); },
      [](AccountInfo &a, const QJsonValue &v) { a.niren = jsonToBool(v); } },
    { "juece", "群决策", "bool", "ai_switch", "由模型判断这条消息要不要回", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.juece); },
      [](AccountInfo &a, const QJsonValue &v) { a.juece = jsonToBool(v); } },

    { "e_bai", "白名单模式", "bool", "ai_white", "只回应白名单里的群 / 人", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.e_bai); },
      [](AccountInfo &a, const QJsonValue &v) { a.e_bai = jsonToBool(v); } },
    { "bai_qy", "白名单模式号", "string", "ai_white", "对应桌面端的「设置ai白名单模式」", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.bai_qy); },
      [](AccountInfo &a, const QJsonValue &v) { a.bai_qy = v.toString(); } },
    { "bai_sr", "添加白名单", "string", "ai_white", "要加进白名单的 ID", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.bai_sr); },
      [](AccountInfo &a, const QJsonValue &v) { a.bai_sr = v.toString(); } },
    { "bai_sc", "删除白名单", "string", "ai_white", "要从白名单删掉的 ID", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.bai_sc); },
      [](AccountInfo &a, const QJsonValue &v) { a.bai_sc = v.toString(); } },

    { "触发概率", "随机回复", "int", "ai_rand", "每条消息被理会的百分比概率", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.触发概率); },
      [](AccountInfo &a, const QJsonValue &v) { a.触发概率 = jsonToInt(v); } },
    { "递增概率", "递增概率", "int", "ai_rand", "被无视后每次多出来的概率", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.递增概率); },
      [](AccountInfo &a, const QJsonValue &v) { a.递增概率 = jsonToInt(v); } },
    { "固定条数", "固定条数", "int", "ai_rand", "连续回应多少条之后重新掷概率", nullptr,
      [](const AccountInfo &a) { return QJsonValue(a.固定条数); },
      [](AccountInfo &a, const QJsonValue &v) { a.固定条数 = jsonToInt(v); } },

    { "tools", "内置函数", "strings", "ai_tools",
      "勾选这个账号允许 AI 调用的函数（对应桌面端内置功能列表）", "@toolNames",
      // 注意：必须显式写成 QJsonValue，否则 lambda 推导出的是 QJsonArray，
      // 转换不出 QJsonValue(*)(const AccountInfo &) 这个函数指针类型。
      [](const AccountInfo &a) -> QJsonValue { return QJsonArray::fromStringList(a.tools); },
      [](AccountInfo &a, const QJsonValue &v) {
          a.tools.clear();
          const QJsonArray arr = v.toArray();
          for (const QJsonValue &t : arr) {
              const QString name = t.toString().trimmed();
              if (!name.isEmpty()) a.tools.append(name);
          }
      } },
};
const int kAiFieldCount = int(sizeof(kAiFields) / sizeof(kAiFields[0]));

// data/roles.json（模型 / 全局设定）与 data/functions.json（内置函数）里的可选值
QJsonObject buildAiOptions()
{
    QJsonObject o;
    QJsonArray models, settings, tools, toolNames;

    QFile rf(QStringLiteral("data/roles.json"));
    if (rf.exists() && rf.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(rf.readAll()).object();
        rf.close();
        for (const QJsonValue &v : root.value(QStringLiteral("models")).toArray()) {
            const QString name = v.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) models.append(name);
        }
        for (const QJsonValue &v : root.value(QStringLiteral("global_settings")).toArray()) {
            const QString name = v.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) settings.append(name);
        }
    }

    QFile ff(QStringLiteral("data/functions.json"));
    if (ff.exists() && ff.open(QIODevice::ReadOnly)) {
        // 先把数组落到具名变量上再遍历，别依赖临时 QJsonDocument 的生存期
        const QJsonArray funcs = QJsonDocument::fromJson(ff.readAll()).array();
        ff.close();
        for (const QJsonValue &v : funcs) {
            const QJsonObject fo = v.toObject();
            const QString fn = fo.value(QStringLiteral("funcName")).toString();
            if (fn.isEmpty()) continue;
            QJsonObject t;
            t["name"]   = fn;
            t["remark"] = fo.value(QStringLiteral("remark")).toString();
            tools.append(t);
            toolNames.append(fn);
        }
    }

    o["models"]    = models;
    o["settings"]  = settings;
    o["tools"]     = tools;
    o["toolNames"] = toolNames;
    return o;
}

AccountInfo *accountByAppid(int appid)
{
    const int idx = appid ? findAccountIndex(appid) : -1;
    return (idx < 0) ? nullptr : m_accounts[idx].get();
}

QString accountDisplayName(const AccountInfo &a)
{
    if (!a.nickname.isEmpty())   return a.nickname;
    if (!a.Ai_nickname.isEmpty())return a.Ai_nickname;
    if (!a.botqq.isEmpty())      return a.botqq;
    return a.appid;
}

// 基础 / Ai 两页共用一套「读 → 改 → 存」流程，这里抽一层
void replyAcctConfig(const QString &cmd, const AcctField *fields, int count,
                     const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const int appid = params.value("appid").toInt(0);
    const AccountInfo *a = accountByAppid(appid);

    QJsonObject ret;
    ret["appid"]  = appid;
    ret["exists"] = (a != nullptr);
    ret["name"]   = a ? accountDisplayName(*a) : QString();
    ret["fields"] = buildAcctFields(fields, count, a, buildAiOptions());
    sendReply(client, cmd, true, ret, reqId,
              a ? QString() : QStringLiteral("还没有选中账号"));
}

void saveAcctConfig(const QString &cmd, const AcctField *fields, int count,
                    const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const int appid = params.value("appid").toInt(0);
    AccountInfo *a = accountByAppid(appid);
    if (!a) {
        sendReply(client, cmd, false, QJsonValue(), reqId, "没找到这个账号");
        return;
    }
    if (!accountPage) {
        sendReply(client, cmd, false, QJsonValue(), reqId, "账号页未就绪，无法落盘");
        return;
    }

    QStringList changed;
    const int n = applyAcctFields(fields, count, *a, params.value("values").toObject(), changed);
    if (n == 0) {
        sendReply(client, cmd, false, QJsonValue(), reqId, "提交的字段都不在白名单里，已忽略");
        return;
    }

    accountPage->saveAccounts(a);
    AppendEventLog(QString("[WebUI] 更新账号 %1 的配置 %2").arg(appid).arg(changed.join(QLatin1Char(','))));

    QJsonObject ret;
    ret["appid"]   = appid;
    ret["changed"] = changed.size();
    ret["fields"]  = buildAcctFields(fields, count, a, buildAiOptions());
    sendReply(client, cmd, true, ret, reqId, QString("已保存 %1 项").arg(changed.size()));
}

void handleGetBasicConfig(const QJsonObject &p, ClientConnection *c, const QString &r)
{ replyAcctConfig("getBasicConfig", kBasicFields, kBasicFieldCount, p, c, r); }

void handleSetBasicConfig(const QJsonObject &p, ClientConnection *c, const QString &r)
{ saveAcctConfig("setBasicConfig", kBasicFields, kBasicFieldCount, p, c, r); }

void handleGetAiConfig(const QJsonObject &p, ClientConnection *c, const QString &r)
{ replyAcctConfig("getAiConfig", kAiFields, kAiFieldCount, p, c, r); }

void handleSetAiConfig(const QJsonObject &p, ClientConnection *c, const QString &r)
{ saveAcctConfig("setAiConfig", kAiFields, kAiFieldCount, p, c, r); }

// ---------------------------------------------------------------- 黑名单

// 黑名单文件是 QDataStream 序列化的 QHash<QString,QString>，网页端解析不了，
// 所以读写统一走 BlacklistPage 的桥接口。
void handleGetBlacklist(ClientConnection *client, const QString &reqId)
{
    if (!Black) {
        sendReply(client, "getBlacklist", false, QJsonValue(), reqId, "黑名单页未就绪");
        return;
    }

    QList<QPair<QString, QString>> list;
    const QHash<QString, QString> snap = Black->webuiSnapshot();
    list.reserve(snap.size());
    for (auto it = snap.constBegin(); it != snap.constEnd(); ++it) {
        list.append(qMakePair(it.key(), it.value()));
    }
    std::sort(list.begin(), list.end(),
              [](const QPair<QString, QString> &x, const QPair<QString, QString> &y) {
                  return x.first < y.first;
              });

    QJsonArray items;
    for (const auto &kv : std::as_const(list)) {
        QJsonObject o;
        o["id"]     = kv.first;
        o["remark"] = kv.second;
        items.append(o);
    }

    QJsonObject ret;
    ret["items"] = items;
    ret["count"] = items.size();
    sendReply(client, "getBlacklist", true, ret, reqId);
}

void handleSetBlacklist(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    if (!Black) {
        sendReply(client, "setBlacklist", false, QJsonValue(), reqId, "黑名单页未就绪");
        return;
    }

    const QJsonArray items = params.value("items").toArray();
    if (items.size() > 200000) {
        sendReply(client, "setBlacklist", false, QJsonValue(), reqId, "条目过多（超过 20 万），未保存");
        return;
    }

    QHash<QString, QString> data;
    int skipped = 0;
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString().trimmed();
        if (id.isEmpty()) { ++skipped; continue; }
        data.insert(id, o.value("remark").toString());
    }

    if (!Black->webuiApply(data)) {
        sendReply(client, "setBlacklist", false, QJsonValue(), reqId,
                  "写 data/blacklist.json 失败，改动已应用到界面但没能落盘");
        return;
    }

    AppendEventLog(QString("[WebUI] 更新黑名单 %1 条").arg(data.size()));

    QJsonObject ret;
    ret["count"]   = data.size();
    ret["skipped"] = skipped;
    sendReply(client, "setBlacklist", true, ret, reqId,
              skipped > 0 ? QString("已保存 %1 条（忽略了 %2 条空 ID）").arg(data.size()).arg(skipped)
                          : QString("已保存 %1 条黑名单").arg(data.size()));
}

// ---------------------------------------------------------------- Ai · 模型配置

QString modelConfigPath() { return QStringLiteral("data/model_config.json"); }

void handleGetModelConfig(ClientConnection *client, const QString &reqId)
{
    const QString path = modelConfigPath();

    QJsonObject root;
    QFile f(path);
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) {
            sendReply(client, "getModelConfig", false, QJsonValue(), reqId, QString("打不开 %1").arg(path));
            return;
        }
        root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }

    // 密钥在文件里是加密存的（跟桌面端 AiWidget::saveToFile2 用的是同一把机密钥）
    const QByteArray keyA = MachineKey::generateKey("000");

    QJsonArray interfaces;
    for (const QJsonValue &iv : root.value(QStringLiteral("interfaces")).toArray()) {
        const QJsonObject io = iv.toObject();
        QJsonObject o;
        o["remark"] = io.value(QStringLiteral("remark")).toString();
        o["url"]    = io.value(QStringLiteral("url")).toString();
        QJsonArray keys;
        for (const QJsonValue &kv : io.value(QStringLiteral("keys")).toArray()) {
            const QJsonObject ko = kv.toObject();
            QJsonObject k;
            k["key"]        = MachineKey::decrypt(ko.value(QStringLiteral("key")).toString(), keyA);
            k["usageCount"] = ko.value(QStringLiteral("usageCount")).toInt();
            k["lastUsed"]   = ko.value(QStringLiteral("lastUsed")).toString();
            keys.append(k);
        }
        o["keys"] = keys;
        interfaces.append(o);
    }

    QJsonArray models;
    for (const QJsonValue &mv : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject mo = mv.toObject();
        QJsonObject m;
        m["name"]              = mo.value(QStringLiteral("name")).toString();
        m["enabledInterfaces"] = mo.value(QStringLiteral("enabledInterfaces")).toArray();
        models.append(m);
    }

    QJsonObject ret;
    ret["interfaces"] = interfaces;
    ret["models"]     = models;
    ret["path"]       = path;
    ret["exists"]     = QFile::exists(path);
    sendReply(client, "getModelConfig", true, ret, reqId);
}

void handleSetModelConfig(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    if (!params.contains("interfaces") && !params.contains("models")) {
        sendReply(client, "setModelConfig", false, QJsonValue(), reqId, "没有提交任何内容，未保存");
        return;
    }

    const QByteArray keyA = MachineKey::generateKey("000");

    QJsonObject root;

    QJsonArray ia;
    for (const QJsonValue &iv : params.value("interfaces").toArray()) {
        const QJsonObject io = iv.toObject();
        QJsonObject o;
        o["remark"] = io.value("remark").toString();
        o["url"]    = io.value("url").toString();
        QJsonArray keys;
        for (const QJsonValue &kv : io.value("keys").toArray()) {
            const QJsonObject ko = kv.toObject();
            const QString plain = ko.value("key").toString();
            QJsonObject k;
            // 留着原样的密文没意义，空串也照存（桌面端本身就是空串起始）
            k["key"]        = MachineKey::encrypt(plain, keyA);
            k["usageCount"] = ko.value("usageCount").toInt();
            k["lastUsed"]   = ko.value("lastUsed").toString();
            keys.append(k);
        }
        o["keys"] = keys;
        ia.append(o);
    }
    root["interfaces"] = ia;

    QJsonArray ma;
    for (const QJsonValue &mv : params.value("models").toArray()) {
        const QJsonObject mo = mv.toObject();
        QJsonObject m;
        m["name"]              = mo.value("name").toString();
        m["enabledInterfaces"] = mo.value("enabledInterfaces").toArray();
        ma.append(m);
    }
    root["models"] = ma;

    const QString path = modelConfigPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendReply(client, "setModelConfig", false, QJsonValue(), reqId, QString("写不了 %1").arg(path));
        return;
    }
    f.write(QJsonDocument(root).toJson());
    f.close();

    const bool reloaded = (ai_ui != nullptr);
    if (reloaded) ai_ui->webuiReloadModelConfig();

    AppendEventLog(QString("[WebUI] 更新模型配置（%1 个接口 / %2 个模型）").arg(ia.size()).arg(ma.size()));

    QJsonObject ret;
    ret["interfaces"] = ia.size();
    ret["models"]     = ma.size();
    ret["reloaded"]   = reloaded;
    sendReply(client, "setModelConfig", true, ret, reqId,
              reloaded ? QString("已保存 %1 个接口、%2 个模型，模型下拉已刷新").arg(ia.size()).arg(ma.size())
                       : QString("已保存 %1 个接口、%2 个模型（框架未重载，重启后生效）").arg(ia.size()).arg(ma.size()));
}

// ---------------------------------------------------------------- Ai · 附加模型

QString fujiaPath() { return QStringLiteral("data/fujia.json"); }

// 公共的附加模型库：data/fujia.json 是个数组，注意模型字段在文件里叫 "mode"
QJsonArray readFujiaItems()
{
    QJsonArray items;
    QFile f(fujiaPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return items;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        QJsonObject it;
        it["name"] = o.value(QStringLiteral("name")).toString();
        it["role"] = o.value(QStringLiteral("role")).toString();
        it["mode"] = o.value(QStringLiteral("mode")).toString();
        items.append(it);
    }
    return items;
}

void handleGetFujia(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const int appid = params.value("appid").toInt(0);
    const AccountInfo *a = accountByAppid(appid);

    QJsonObject ret;
    ret["items"]   = readFujiaItems();
    ret["models"]  = buildAiOptions().value(QStringLiteral("models")).toArray();
    ret["appid"]   = appid;
    ret["exists"]  = (a != nullptr);
    ret["name"]    = a ? accountDisplayName(*a) : QString();
    ret["enabled"] = a ? QJsonArray::fromStringList(a->fujia) : QJsonArray();
    sendReply(client, "getFujia", true, ret, reqId,
              a ? QString() : QStringLiteral("还没有选中账号，仍可编辑公共的附加模型库"));
}

void handleSetFujia(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    const int appid = params.value("appid").toInt(0);
    AccountInfo *a = accountByAppid(appid);

    // 1) 公共库写回 data/fujia.json
    QJsonArray out;
    for (const QJsonValue &v : params.value("items").toArray()) {
        const QJsonObject o = v.toObject();
        const QString name = o.value("name").toString();
        if (name.trimmed().isEmpty()) continue;
        QJsonObject item;
        item["name"] = name;
        item["role"] = o.value("role").toString();
        item["mode"] = o.value("mode").toString();
        out.append(item);
    }

    const QString path = fujiaPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendReply(client, "setFujia", false, QJsonValue(), reqId, QString("写不了 %1").arg(path));
        return;
    }
    f.write(QJsonDocument(out).toJson(QJsonDocument::Indented));
    f.close();

    // 2) 这个账号勾了哪几个（桌面端是存在 AccountInfo::fujia 里的名字列表）
    bool accSaved = false;
    int enabledCount = 0;
    if (a && accountPage) {
        a->fujia.clear();
        for (const QJsonValue &v : params.value("enabled").toArray()) {
            const QString name = v.toString().trimmed();
            if (!name.isEmpty()) a->fujia.append(name);
        }
        accountPage->saveAccounts(a);
        enabledCount = a->fujia.size();
        accSaved = true;
    }

    const bool reloaded = (ai_ui != nullptr);
    if (reloaded) ai_ui->webuiReloadFujia();

    AppendEventLog(QString("[WebUI] 更新附加模型库 %1 条").arg(out.size()));

    QJsonObject ret;
    ret["count"]    = out.size();
    ret["enabled"]  = enabledCount;
    ret["accSaved"] = accSaved;
    ret["reloaded"] = reloaded;
    sendReply(client, "setFujia", true, ret, reqId,
              accSaved ? QString("已保存 %1 条附加模型，当前账号勾选 %2 项").arg(out.size()).arg(enabledCount)
                       : QString("已保存 %1 条附加模型（未选账号，勾选未保存）").arg(out.size()));
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

    // 设置 / 规则
    if (action == "getSysConfig")        { handleGetSysConfig(client, reqId);                       return true; }
    if (action == "setSysConfig")        { handleSetSysConfig(params, client, reqId);               return true; }
    if (action == "getForbiddenWords")   { handleGetForbiddenWords(client, reqId);                  return true; }
    if (action == "setForbiddenWords")   { handleSetForbiddenWords(params, client, reqId);          return true; }
    if (action == "getRuleFile")         { handleGetRuleFile(params, client, reqId);                return true; }
    if (action == "setRuleFile")         { handleSetRuleFile(params, client, reqId);                return true; }

    // 内置功能 · 基础 / Ai
    if (action == "getBasicConfig")      { handleGetBasicConfig(params, client, reqId);             return true; }
    if (action == "setBasicConfig")      { handleSetBasicConfig(params, client, reqId);             return true; }
    if (action == "getAiConfig")         { handleGetAiConfig(params, client, reqId);                return true; }
    if (action == "setAiConfig")         { handleSetAiConfig(params, client, reqId);                return true; }

    // 黑名单
    if (action == "getBlacklist")        { handleGetBlacklist(client, reqId);                       return true; }
    if (action == "setBlacklist")        { handleSetBlacklist(params, client, reqId);               return true; }

    // Ai · 模型配置 / 附加模型
    if (action == "getModelConfig")      { handleGetModelConfig(client, reqId);                     return true; }
    if (action == "setModelConfig")      { handleSetModelConfig(params, client, reqId);             return true; }
    if (action == "getFujia")            { handleGetFujia(params, client, reqId);                   return true; }
    if (action == "setFujia")            { handleSetFujia(params, client, reqId);                   return true; }

    return false;
}
