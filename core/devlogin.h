#pragma once
// ============================================================================
// QQ 开放平台扫码登录 + 机器人「连接方式」(WebSocket / Webhook) 切换
//
// 1) 登录：把 qrlogin2.py 那一整套链子搬到 C++：
//      GET  /lite/create_session            -> session_id
//      GET  xlogin(...)                     -> pt_login_sig cookie
//      GET  ptqrshow                        -> 二维码 PNG + qrsig cookie
//      GET  ptqrlogin   (每 2 秒轮询)        -> 0=成功 / 65=过期(重开) / 66,67=等
//      GET  <check_url>                     -> 落到 entity-picker，种 QQ 登录 cookie
//      POST /lite/write_login_state   (1)    -> Confirming
//      GET  /lite/poll                      -> {code, developer_id, redirect}
//      GET  <redirect>                      -> 种 b-token / qticket / quin ...（真正的 ck）
//    拿到 b-token / qticket 就算登录成功，登录态全部留在内存 cookie jar 里。
//
//    ⚠ 不再查开发者主体、不再发「同意授权」：实测选不选主体都能改机器人，
//      平台自己在 poll 里就把身份定好了（poll 返回里的 developer_id 只是参考）。
//      这样也少 2 个请求、少一次卡顿。
//
// 2) 切换：
//      POST https://bot.q.qq.com/cgi-bin/v2/bot_dev_setting/connect_info/modify
//      WS      -> {"bot_appid":appid,"action":3,"webhook_url":""}
//      Webhook -> {"bot_appid":appid,"action":1,"webhook_url":"http://ip:端口/webhook/appid"}
//
// ⚠ 安全约定（用户明确要求）：**cookie 只在内存里，绝不落地**。
//   每个开发者账号一份独立的 QNetworkAccessManager + 内存 cookie jar，
//   不写 cookie 文件、不进 AccountInfo / 配置、不开磁盘缓存，
//   进程退出 / 手动清登录态即全部丢弃。
//
// ⚠⚠ 多账号（这是本框架的常态）：**一份 ck 只属于一个开发者 QQ 号**。
//   机器人 A 和 B 可能属于不同的开放平台开发者账号，
//   拿 A 的 ck 去改 B 的 bot_appid 平台会直接拒（无权限）。
//   所以这里按「开发者 QQ 号（p_uin）」分桶存会话：
//     · m_sessions  : uin -> { 独立 QNAM + 自己的 cookie jar, 登录时刻 }
//     · m_appidUin  : bot_appid -> uin   （上次用哪个开发者的 ck 改成功过）
//   调用 modifyConnectInfo(appid) 时：
//     ① appid 绑定过且会话没过期 -> 用那一份；
//     ② 没绑定 -> 拿「最近登录的那份」先试一次（同一开发者下的多个机器人就不用重复扫码），
//        成功即自动绑定；失败（平台判无权限）由界面提示「换对应 QQ 号扫码」；
//     ③ 一份都没有 -> 返回"需要登录"，界面弹二维码。
//   登录成功后自动把「这次登录的 uin」绑定到发起登录的那个 appid 上。
//
// 为什么不用 NetManager：那个是全局共享 QNAM，既没有 cookie jar，
//   回调里也拿不到 Set-Cookie —— 这条链必须逐跳携带/接收 cookie；
//   而且它只有一份会话，做不到按开发者隔离。所以本类自带 NAM（每个会话一份）。
//
// ⚠ 接口会回 {"retcode":401,"msg":"miss ticket"} —— 开放平台登录态过期。
//   modifyConnectInfo 遇到 401 会先补一次 bot.q.qq.com 的控制台会话再重试一次，
//   仍失败就**只清掉这个开发者**的会话（不连累其他账号），把原始响应交给调用方弹框。
//
// ⚠ 调试：所有 HTTP 请求/响应都打日志（qDebug + 首页事件日志），格式为
//     「GET q.qq.com/lite/poll → 128 ms, HTTP 200, {json}」
//   —— 扫码成功后到底哪一跳慢、哪个 body 才是结果，看这个就知道。
// ============================================================================

#include <QObject>
#include <QString>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <functional>

class QUrl;

class DevLogin : public QObject
{
    // 故意不用 Q_OBJECT：本类不定义自己的信号/槽，全部走 std::function 回调，
    // 这样头文件不需要 moc（也就不必往 CMake 的 moc 列表里加东西）。
public:
    using QrCb    = std::function<void(const QByteArray &png)>;                 // 二维码图（过期重开时会再调一次）
    using StatusCb = std::function<void(const QString &text)>;                  // 轮询状态文案（已扫码待确认等）
    using LoginCb = std::function<void(bool ok, const QString &err)>;           // 登录结束（成功/失败/取消）
    using ApiCb   = std::function<void(bool ok, const QString &body, const QString &err)>;

    static DevLogin &instance();

    // 为某个机器人（botAppid）扫码登录；成功后自动把该 appid 绑到这次登录的开发者账号。
    void startLogin(int botAppid, LoginCb cb, QrCb qrCb, StatusCb statusCb = nullptr);
    // 不绑定具体机器人的登录（兼容用法）。
    void startLogin(LoginCb cb, QrCb qrCb);
    // 取消（用户关掉了二维码窗口）。会以 ok=false + "已取消" 回调一次。
    void cancelLogin();

    // 内存里是否有任意一个「未过期」的开发者会话。
    bool hasLoginSession() const;
    // 这个机器人能不能直接改：绑定的会话有效，或者有别的会话可以拿来试。
    bool hasSessionFor(int botAppid) const;
    // 该 appid 当前绑定 / 将使用的会话描述（给弹框显示："开发者 1234567"）。
    QString sessionLabelFor(int botAppid) const;

    // 清空全部开发者会话（登出所有）。
    void clearSession();
    // 只解绑某个机器人（不清会话本身）——「换号重扫」时用。
    void clearSessionForAppid(int botAppid);

    // 切换连接方式。action: 1=Webhook, 3=WebSocket。webhookUrl 在 WS 时传空串。
    // 没有可用会话时立刻 cb(false, "", "NEED_LOGIN")，界面据此弹二维码。
    void modifyConnectInfo(int botAppid, int action, const QString &webhookUrl, ApiCb cb);

private:
    struct Session;   // 定义在 .cpp：{ QNAM*, loginAtMs, uin, developerId }

    explicit DevLogin(QObject *parent = nullptr);

    // ---- 会话池 ----
    QNetworkAccessManager *createNam();                       // 新开一份 QNAM + 内存 jar
    Session *validSession(const QString &uin) const;          // 没过期的
    Session *pickSession(int botAppid) const;                 // 按 appid 绑定 / 最近登录
    void dropSession(const QString &uin);                     // 删掉某个开发者的会话
    qint64 nowMs() const;

    // ---- 登录链 ----
    void newCycle();          // 建会话 + 取二维码
    void fetchQr();
    void pollQr();            // ptqrlogin 轮询
    void followCheckUrl(const QString &url);
    void bindConfirming();
    // ⚠ 只发 Confirming(1) 时 /lite/poll 会一直回 code=3「已扫码，确认中」——
    //   平台还在等「同意授权」，所以必须再发一次 login_status=2（Agree）。
    //   而 Agree 的 developer_id **不能为空**（实测空串回 retcode 30001
    //   「developer_id 不能为空」）→ 所以 Confirming 之后先查主体列表取第一个 id。
    void fetchDevelopers();   // get_developers_for_login → 填 m_developerId → bindAgree()
    void bindAgree();         // write_login_state login_status=2（Agree，带真实 developer_id）
    void pollLite();
    void resumePoll();        // 立刻打一次 poll，之后交给 2 秒定时器
    void followRedirect(const QString &url);
    void finishLogin(bool ok, const QString &err);

    // ---- 小工具 ----
    void get(const QString &url, const QHash<QString, QString> &headers, int timeoutMs,
             std::function<void(bool ok, const QString &body, int status)> cb);
    void getRaw(const QString &url, const QHash<QString, QString> &headers, int timeoutMs,
                std::function<void(bool ok, const QByteArray &data, int status)> cb);
    void postJson(const QString &url, const QJsonObject &body, const QHash<QString, QString> &headers,
                  int timeoutMs, std::function<void(bool ok, const QString &resp, int status)> cb);
    void warmupConsole(std::function<void(bool)> cb);   // 补 bot.q.qq.com 控制台 cookie
    QString cookieValue(const QString &name) const;     // 从「当前会话」的 jar 里取
    QString cookieHeaderFor(const QUrl &url) const;     // 把 jar 里该 URL 的 cookie 拼成 Cookie 头
    void setStatus(const QString &text);                // 给界面的状态文案（可选回调）

    QTimer m_qrTimer;         // ptqrlogin 轮询（2s）
    QTimer m_liteTimer;       // /lite/poll 轮询（2s）

    LoginCb  m_loginCb;
    QrCb     m_qrCb;
    StatusCb m_statusCb;

    bool    m_busy      = false;   // 一次登录流程进行中
    bool    m_cancel    = false;
    bool    m_qrBusy    = false;   // ptqrlogin 请求在途，防重发
    bool    m_liteBusy  = false;
    quint64 m_gen       = 0;       // 流程代号：过期重开/重新登录后，旧回调自动失效
    qint64  m_deadline  = 0;       // 整体超时（15 分钟）
    qint64  m_confirmAt = 0;       // 手机点确认的时刻（算「扫完到成功」耗时，日志用）

    QHash<QString, Session *> m_sessions;   // uin -> 会话（uin 拿不到时用 "-"）
    QHash<int, QString>       m_appidUin;   // bot_appid -> uin
    QString m_lastUin;                      // 最近一次登录成功的 uin

    // 登录中的会话（登录成功后整体并入 m_sessions，再新建一份空的下次用）
    QNetworkAccessManager *m_pendingNam = nullptr;
    // 当前流程正在用的 NAM（所有请求都走它，保证 cookie 是这个开发者的）
    QNetworkAccessManager *m_curNam = nullptr;
    QString m_curUin;
    int     m_loginAppid = 0;   // 这次登录是为哪个机器人发起的（成功后绑定）

    QString m_sessionId;
    QString m_u1;             // 已 percent-encode 一次的回调地址
    QString m_loginSig;
    QString m_developerId;    // poll / get_developers_for_login 给的 developer_id（agree 要用）
    QString m_uinHint;        // ptuiCB 第 6 个参数（有的版本带 uin）
    int     m_qrToken  = 0;   // hash33(qrsig)
    int     m_litePoll = 0;
    int     m_qrPoll   = 0;   // ptqrlogin 已经发了几轮（日志用）
    int     m_qrBadCount = 0; // 连续「解析不出 ptuiCB」的次数（403/空 body/风控）
    int     m_agreeTries = 0;      // 发了多少次 Agree（最多 3 次，防止 poll 卡 code=3 死等）
    bool    m_devListTried = false;// 兜底时查过主体列表没有（查过就只重发 Agree，别反复查）
};
