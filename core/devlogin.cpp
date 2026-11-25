#include "devlogin.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <memory>

// 首页「事件日志」（core/global.cpp）。只前置声明，不 include 那个巨大的 global.h。
void AppendEventLog(const QString &msg, int color = 0);

namespace {
// 日志配色（跟 AppendEventLog 的用法一致）
constexpr int kLogGray  = 0x888888;
constexpr int kLogErr   = 0xE74C3C;
constexpr int kLogOk    = 0x27AE60;
}

// 扫码登录的调试日志：qDebug 给 Qt Creator 的「应用程序输出」看，
// AppendEventLog 给程序首页的事件日志看（用户能直接截图反馈）。
static void qcLog(const QString &msg, int color = kLogGray)
{
    qDebug().noquote() << "[扫码登录]" << msg;
    AppendEventLog(QStringLiteral("[扫码登录] ") + msg, color);
}

// ============================================================================
// 说明见 devlogin.h。本文件只关心两件事：
//   ① 复刻 qrlogin2.py 的 q.qq.com 扫码登录（cookie 全部留在内存 jar 里）
//   ② 用这份登录态去 bot.q.qq.com 改机器人连接方式
// ============================================================================

namespace {

// 与 python 版一致：ptlogin 用的 appid / daid（QQ 开放平台网页登录）
const char kPtAppid[] = "1600001541";
const char kDaId[]    = "750";

const char kUa[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/125.0.0.0 Safari/537.36";

const char kModifyUrl[] =
    "https://bot.q.qq.com/cgi-bin/v2/bot_dev_setting/connect_info/modify";

// 只给本类用的 cookie jar：把 allCookies() 提到 public，方便按名字查 qrsig / b-token。
// ⚠ 它只在内存里 —— QNetworkCookieJar 本身不落盘，我们也没有给它配任何磁盘缓存。
class MemCookieJar : public QNetworkCookieJar
{
public:
    explicit MemCookieJar(QObject *parent = nullptr) : QNetworkCookieJar(parent) {}
    // 两个都是 protected，提上来给 DevLogin 用（清登录态 / 按名字查票据）
    using QNetworkCookieJar::allCookies;
    using QNetworkCookieJar::setAllCookies;
};

// 与 python 的 hash33 等价（ptqrlogin 的 ptqrtoken）
int hash33(const QString &s)
{
    quint32 e = 0;
    for (const QChar ch : s)
        e += (e << 5) + static_cast<quint32>(ch.unicode());
    return static_cast<int>(e & 0x7FFFFFFF);
}

// python 的 requests.utils.quote(x, safe='')：保留字母数字和 -_.~，其余全部 %XX
QString enc(const QString &raw)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(raw));
}

// ⚠⚠ 这三个 URL 全部用「+」拼接，**绝不能**写成 QStringLiteral("...%1...").arg(x)：
//   传进来的 u1 本身是百分号编码过的（含 %3A / %2F / %26 / %3D …），arg() 会把里面
//   的 %3、%2 当成占位符**再替换一遍**。实测拼出来是
//     u1=https1795606427221A1149524457F1149524457Fq.qq.com…
//   （1795606427221 = 毫秒时间戳，1149524457 = ptqrtoken），服务器认不出 u1，
//   ptqrlogin 直接 403 + 空 body，界面表现就是「手机扫了仍停在等待扫码」。
//   同一个坑对 login_sig 也成立（里面含 %2A / %3D）。要加参数就继续用 + 拼。

// xlogin：只为让服务器下发 pt_login_sig cookie
QString xloginUrl(const QString &u1)
{
    return QStringLiteral("https://xui.ptlogin2.qq.com/cgi-bin/xlogin?appid=")
         + QString::fromLatin1(kPtAppid)
         + QStringLiteral("&daid=") + QString::fromLatin1(kDaId)
         + QStringLiteral("&target=self&style=22&hide_title_bar=1&hide_border=1&hide_title=1"
                          "&theme=10&s_url=")
         + u1;
}

// 二维码图片（返回 PNG 字节）+ qrsig cookie
QString ptqrshowUrl(const QString &u1)
{
    return QStringLiteral("https://ssl.ptlogin2.qq.com/ptqrshow?appid=")
         + QString::fromLatin1(kPtAppid)
         + QStringLiteral("&e=2&l=M&s=3&d=72&v=4&t=")
         + QString::number(QRandomGenerator::global()->generateDouble(), 'f', 17)
         + QStringLiteral("&daid=") + QString::fromLatin1(kDaId)
         + QStringLiteral("&pt_3rd_aid=0&u1=")
         + u1;
}

QString ptqrloginUrl(const QString &u1, int token, const QString &loginSig)
{
    return QStringLiteral("https://ssl.ptlogin2.qq.com/ptqrlogin?u1=")
         + u1
         + QStringLiteral("&ptqrtoken=") + QString::number(token)
         + QStringLiteral("&ptredirect=0&h=1&t=1&g=1&from_ui=1&ptlang=2052&action=0-0-")
         + QString::number(QDateTime::currentMSecsSinceEpoch())
         + QStringLiteral("&js_ver=25040111&js_type=1&login_sig=")
         + enc(loginSig)
         + QStringLiteral("&pt_uistyle=40&aid=") + QString::fromLatin1(kPtAppid)
         + QStringLiteral("&daid=") + QString::fromLatin1(kDaId)
         + QStringLiteral("&has_onekey=1");
}

// ptuiCB('0','0','https://...','0','登录成功！','') -> ["0","0","https://...","0","登录成功！",""]
QStringList parsePtuiCB(const QString &text)
{
    QStringList out;
    const QRegularExpression re(QStringLiteral("ptuiCB\\((.*)\\)"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return out;
    const QStringList parts = m.captured(1).split(QLatin1Char(','));
    for (const QString &p : parts) {
        QString v = p.trimmed();
        if (v.startsWith(QLatin1Char('\''))) v.remove(0, 1);
        if (v.endsWith(QLatin1Char('\'')))   v.chop(1);
        out << v;
    }
    return out;
}

// q.qq.com 那套 lite 接口要的公共头（与 python 的 qhdr 一致）
QHash<QString, QString> qHeaders()
{
    QHash<QString, QString> h;
    h.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
    h.insert(QStringLiteral("Accept"), QStringLiteral("application/json, text/plain, */*"));
    h.insert(QStringLiteral("Origin"), QStringLiteral("https://q.qq.com"));
    h.insert(QStringLiteral("Referer"),
             QStringLiteral("https://q.qq.com/qqbot/openclaw/entity-picker.html"));
    return h;
}

void applyCommonHeaders(QNetworkRequest &req, const QHash<QString, QString> &headers, bool isPost)
{
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    if (!headers.contains(QStringLiteral("User-Agent")))
        req.setRawHeader("User-Agent", kUa);
    if (!headers.contains(QStringLiteral("Accept")))
        req.setRawHeader("Accept", "*/*");
    if (!headers.contains(QStringLiteral("Accept-Language")))
        req.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
    if (isPost && !headers.contains(QStringLiteral("Content-Type")))
        req.setRawHeader("Content-Type", "application/json");
}

// ptqrlogin / ptqrshow 每 2 秒一次，只进 Qt Creator 输出，不刷首页事件日志
bool isChattyUrl(const QUrl &u)
{
    return u.path().contains(QStringLiteral("ptqrlogin"))
        || u.path().contains(QStringLiteral("ptqrshow"));
}

// ============================================================================
// 日志脱敏
//
// ⚠ 用户明确要求：**日志里不能出现开发者 id、登录账号(uin)、ck / 各类票据**。
//   这些东西本来只该活在内存 jar 里，一旦被截下来发出去就等于把账号交出去。
//   body 是 JSON 就按键名打码；不是 JSON（ptuiCB / HTML）就原样 —— 那些里面没有票据。
// ============================================================================

// 完全打码，只留长度：日志里绝不出现 id / uin / 票据的任何字符
QString maskVal(const QString &v)
{
    if (v.isEmpty())
        return QString();
    return QStringLiteral("***") + QString::number(v.size());
}

bool isSecretKey(const QString &key)
{
    static const QSet<QString> keys = {
        // 开发者身份
        QStringLiteral("developer_id"), QStringLiteral("developer_infos"),
        QStringLiteral("unverified_developer_infos"), QStringLiteral("id"), QStringLiteral("name"),
        // 登录账号 / QQ 票据
        QStringLiteral("uin"), QStringLiteral("p_uin"), QStringLiteral("quin"), QStringLiteral("quid"),
        QStringLiteral("skey"), QStringLiteral("p_skey"), QStringLiteral("pt4_token"),
        QStringLiteral("ticket"), QStringLiteral("b-token"), QStringLiteral("qticket"),
        QStringLiteral("qticket_lite"), QStringLiteral("token"), QStringLiteral("qrsig"),
        // 会话 / 回调（redirect 里可能带一次性票据）
        QStringLiteral("session_id"), QStringLiteral("login_sig"), QStringLiteral("pt_login_sig"),
        QStringLiteral("redirect"), QStringLiteral("check_url"), QStringLiteral("cookie")
    };
    return keys.contains(key.toLower());
}

QJsonValue maskJsonValue(const QJsonValue &v);

QJsonObject maskJsonObject(const QJsonObject &o)
{
    QJsonObject out;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (isSecretKey(it.key()) && it.value().isString())
            out.insert(it.key(), maskVal(it.value().toString()));
        else
            out.insert(it.key(), maskJsonValue(it.value()));
    }
    return out;
}

QJsonValue maskJsonValue(const QJsonValue &v)
{
    if (v.isObject()) return maskJsonObject(v.toObject());
    if (v.isArray()) {
        QJsonArray a = v.toArray();
        for (int i = 0; i < a.size(); ++i)
            a[i] = maskJsonValue(a.at(i));
        return a;
    }
    return v;
}

// 日志里能显示的 body：JSON 走脱敏，其余原样（纯文本里不含票据）
QString safeBody(const QByteArray &data)
{
    constexpr int kMax = 2000;
    const QByteArray head = data.left(kMax);

    // HTML 整页（bopen/callback、entity-picker 之类）：body 里是 CSP 的 sha256 清单 + 脚本，
    // 十几 KB 全是噪声，真正的票据在 Set-Cookie 响应头里、不在 body 里 —— 只记大小。
    const QByteArray lead = head.trimmed().left(10).toLower();
    if (lead.startsWith("<!doctype") || lead.startsWith("<html"))
        return QStringLiteral("(HTML 页面 %1 字节，票据在 Set-Cookie 里)").arg(data.size());

    const QJsonDocument doc = QJsonDocument::fromJson(head);
    QString shown;
    if (doc.isObject())
        shown = QString::fromUtf8(QJsonDocument(maskJsonObject(doc.object()))
                                      .toJson(QJsonDocument::Compact));
    else if (doc.isArray())
        shown = QString::fromUtf8(QJsonDocument(maskJsonValue(QJsonValue(doc.array())).toArray())
                                      .toJson(QJsonDocument::Compact));
    else
        shown = QString::fromUtf8(head).simplified();

    if (data.size() > kMax)
        shown += QStringLiteral(" …共 %1 字节").arg(data.size());
    return shown;
}

// 一跳一张诊断卡：host+路径尾段、耗时、HTTP 状态、body（已脱敏）。
// 用户反馈「扫码成功后要卡很久才确认成功」—— 靠这个能直接看出卡在哪一跳、
// 哪一跳的 body 才是真正「结果返回了」。
void logHttp(const QString &method, const QUrl &u, qint64 costMs, int status,
             const QByteArray &data, bool netFail)
{
    // 只取 host + 路径尾段，免得把一整串 query（u1 / login_sig / session_id）刷满屏幕
    QString path = u.path();
    if (path.size() > 44)
        path = QStringLiteral("…") + path.right(44);
    const QString tag = method + QLatin1Char(' ') + u.host() + path;

    QString body;
    if (netFail) {
        body = QStringLiteral("(网络错误，连响应都没收到)");
    } else if (data.isEmpty()) {
        body = QStringLiteral("(body 空)");
    } else if (data.contains('\0')) {
        body = QStringLiteral("(二进制 %1 字节)").arg(data.size());
    } else {
        body = safeBody(data);
    }

    const QString line = QStringLiteral("%1 → %2 ms, HTTP %3, %4")
                             .arg(tag).arg(costMs).arg(status).arg(body);
    qDebug().noquote() << "[扫码登录]" << line;
    if (!isChattyUrl(u)) {
        const bool good = !netFail && status >= 200 && status < 300;
        AppendEventLog(QStringLiteral("[扫码登录] ") + line, good ? kLogGray : kLogErr);
    }
}

} // namespace

// ============================================================================

DevLogin &DevLogin::instance()
{
    // 首次调用发生在 GUI 线程（账号页点按钮），QNAM 也就活在主线程 ——
    // 之后所有回调都在主线程执行，不需要跨线程保护。
    //
    // 故意 new 出来不 delete：静态对象会在 main 返回之后析构，
    // 那时 QApplication 已经没了，销毁 QNAM 容易在退出时炸一下 —— 泄漏这一次无所谓。
    static DevLogin *inst = new DevLogin;
    return *inst;
}

DevLogin::DevLogin(QObject *parent) : QObject(parent)
{
    // ⚠ 每个开发者账号一份独立的 QNAM + 内存 cookie jar（多账号框架：一份 ck 只能改
    //   它自己名下的机器人）。这里先建一份「登录中」的，登录成功后整体并入会话池。
    //   全程没有任何 cookie 落盘：不写文件、不放进 AccountInfo / g_config。
    m_pendingNam = createNam();

    m_qrTimer.setInterval(2000);      // ptqrlogin 轮询节奏，跟 python 的 time.sleep(2) 一致
    m_liteTimer.setInterval(2000);    // /lite/poll 节奏
    connect(&m_qrTimer, &QTimer::timeout, this, [this]() { pollQr(); });
    connect(&m_liteTimer, &QTimer::timeout, this, [this]() { pollLite(); });
}

// ---------------------------------------------------------------- 会话池 --
//
// 多账号框架的核心：一个开发者 QQ 号 = 一份 QNAM = 一个 cookie jar。
// 机器人 A / B 如果属于不同开发者，就必须用各自的 ck，混用平台直接拒。

struct DevLogin::Session
{
    QNetworkAccessManager *nam = nullptr;   // 这个开发者专属的 cookie 容器（内存）
    qint64  loginAtMs  = 0;
    QString uin;                            // p_uin（去掉前导 'o'）；拿不到时是 "-"
    QString developerId;                    // poll 返回的 developer_id（日志/排查用）
};

static const qint64 kSessionTtlMs = 30LL * 60 * 1000;   // 腾讯侧 ck 本来会过期，这里兜 30 分钟

qint64 DevLogin::nowMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

QNetworkAccessManager *DevLogin::createNam()
{
    auto *nam = new QNetworkAccessManager(this);
    // 手动跟随跳转风险太大（check_sig / redirect 两跳都要种 cookie），交给 Qt 自动跟随。
    nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    // 内存 jar：QNAM 会接管所有权（原来那个默认 jar 会被它自己释放）
    nam->setCookieJar(new MemCookieJar(nam));
    return nam;
}

DevLogin::Session *DevLogin::validSession(const QString &uin) const
{
    if (uin.isEmpty()) return nullptr;
    Session *s = m_sessions.value(uin, nullptr);
    if (!s || !s->nam || s->loginAtMs == 0) return nullptr;
    if (nowMs() - s->loginAtMs > kSessionTtlMs) return nullptr;   // 过期当没有，让界面重扫
    return s;
}

DevLogin::Session *DevLogin::pickSession(int botAppid) const
{
    // ① 这个机器人上次是用哪个开发者改成功的 —— 优先用它
    const QString bound = m_appidUin.value(botAppid);
    if (!bound.isEmpty()) {
        if (Session *s = validSession(bound)) return s;
    }

    // ② 没绑定（或那份过期了）：拿最近登录的那份试一次。
    //    同一开发者名下的多个机器人因此只需要扫一次码；猜错了平台会回无权限，
    //    界面再提示「换对应 QQ 号扫码」，不会写坏任何东西。
    Session *best = nullptr;
    for (Session *s : m_sessions) {
        if (!validSession(s->uin)) continue;
        if (!best || s->loginAtMs > best->loginAtMs) best = s;
    }
    return best;
}

void DevLogin::dropSession(const QString &uin)
{
    Session *s = m_sessions.take(uin);
    if (!s) return;
    if (s->nam) {
        if (m_curNam == s->nam) m_curNam = nullptr;   // 不能再往已经作废的会话发请求
        s->nam->deleteLater();                        // 不 delete：可能还有请求在路上
    }
    delete s;                                         // Session 只是个 POD 壳子
    if (m_curUin == uin) m_curUin.clear();
}

// ---------------------------------------------------------------- 登录入口 --

void DevLogin::startLogin(LoginCb cb, QrCb qrCb)
{
    startLogin(0, std::move(cb), std::move(qrCb), nullptr);
}

void DevLogin::startLogin(int botAppid, LoginCb cb, QrCb qrCb, StatusCb statusCb)
{
    if (m_busy) {
        if (cb) cb(false, QStringLiteral("上一个扫码登录还没结束"));
        return;
    }
    m_loginCb = std::move(cb);
    m_qrCb    = std::move(qrCb);
    m_statusCb = std::move(statusCb);
    m_busy    = true;
    m_cancel  = false;
    m_qrBusy  = false;
    m_liteBusy = false;
    m_qrBadCount = 0;
    m_developerId.clear();
    m_loginSig.clear();
    m_uinHint.clear();
    m_loginAppid = botAppid;      // 成功后把这个机器人绑到这次登录的开发者账号
    m_curUin.clear();
    m_litePoll = 0;
    m_confirmAt = 0;
    m_agreeTries = 0;
    m_devListTried = false;
    m_deadline = QDateTime::currentMSecsSinceEpoch() + 15LL * 60 * 1000;  // 15 分钟没扫就放弃

    // ⚠ 登录过程必须用「还没归属任何开发者」的那份 NAM：
    //   如果复用某个已有会话的 NAM，新登录会把别人的 cookie 覆盖掉。
    if (!m_pendingNam) m_pendingNam = createNam();
    m_curNam = m_pendingNam;

    qcLog(QStringLiteral("==== 开始扫码登录（目标 AppID %1）====")
              .arg(botAppid > 0 ? QString::number(botAppid) : QStringLiteral("未绑定")));
    newCycle();
}

void DevLogin::cancelLogin()
{
    if (!m_busy)
        return;
    m_cancel = true;
    finishLogin(false, QStringLiteral("已取消"));
}

void DevLogin::newCycle()
{
    const quint64 gen = ++m_gen;   // 过期重开/重新登录：让上一轮的迟到回调全部失效
    m_sessionId.clear();
    m_qrToken = 0;
    m_qrBadCount = 0;
    m_qrPoll = 0;

    // 1. 建 lite 会话
    get(QStringLiteral("https://q.qq.com/lite/create_session"),
        {{QStringLiteral("Referer"), QStringLiteral("https://q.qq.com/")}}, 15000,
        [this, gen](bool ok, const QString &body, int status) {
            if (m_cancel || gen != m_gen) return;
            if (!ok) {
                finishLogin(false, QStringLiteral("创建登录会话失败（HTTP %1）：%2")
                                       .arg(status).arg(body.left(160)));
                return;
            }

            const QJsonObject data = QJsonDocument::fromJson(body.toUtf8()).object()
                                         .value(QStringLiteral("data")).toObject();
            m_sessionId = data.value(QStringLiteral("session_id")).toString();
            if (m_sessionId.isEmpty()) {
                finishLogin(false, QStringLiteral("创建登录会话失败：%1").arg(body.left(200)));
                return;
            }
            qcLog(QStringLiteral("create_session 成功（session_id 长度 %1）").arg(m_sessionId.size()));

            const QString callback =
                QStringLiteral("https://q.qq.com/qqbot/openclaw/entity-picker.html"
                               "?session_id=%1&_wv=16777218").arg(m_sessionId);
            m_u1 = enc(callback);   // 只编码一次，后面所有 URL 直接拼这份

            // 2. xlogin：拿 pt_login_sig（ptqrlogin 要带）
            get(xloginUrl(m_u1),
                {{QStringLiteral("Referer"), QStringLiteral("https://q.qq.com/")}}, 15000,
                [this, gen](bool, const QString &, int) {
                    if (m_cancel || gen != m_gen) return;
                    m_loginSig = cookieValue(QStringLiteral("pt_login_sig"));
                    qcLog(QStringLiteral("xlogin 完成：pt_login_sig %1")
                              .arg(m_loginSig.isEmpty() ? QStringLiteral("没拿到（登录仍可继续）")
                                                        : QStringLiteral("长度 %1").arg(m_loginSig.size())));
                    fetchQr();
                });
        });
}

void DevLogin::fetchQr()
{
    if (m_cancel) return;
    const quint64 gen = m_gen;

    // 3. 二维码图片 + qrsig
    getRaw(ptqrshowUrl(m_u1),
           {{QStringLiteral("Referer"), QStringLiteral("https://xui.ptlogin2.qq.com/")}}, 15000,
           [this, gen](bool ok, const QByteArray &png, int status) {
               if (m_cancel || gen != m_gen) return;
               if (!ok || png.size() < 100) {
                   finishLogin(false, QStringLiteral("获取二维码失败（HTTP %1）").arg(status));
                   return;
               }
               const QString qrsig = cookieValue(QStringLiteral("qrsig"));
               if (qrsig.isEmpty()) {
                   finishLogin(false, QStringLiteral("获取二维码失败：没拿到 qrsig cookie"));
                   return;
               }
               m_qrToken = hash33(qrsig);
               m_qrBusy  = false;
               m_qrBadCount = 0;
               // ptqrlogin 少这个 cookie 服务器直接 403（实测），所以把「会不会带上」提前验一遍
               // ⚠ 只打长度/是否带上，不打印 qrsig、ptqrtoken 本身（都是会话凭证）
               qcLog(QStringLiteral("二维码 OK：%1 字节，qrsig 长度 %2，ptqrlogin 将携带 qrsig：%3")
                         .arg(png.size()).arg(qrsig.size())
                         .arg(cookieHeaderFor(QUrl(ptqrloginUrl(m_u1, m_qrToken, m_loginSig)))
                                  .contains(QStringLiteral("qrsig="))
                                  ? QStringLiteral("是")
                                  : QStringLiteral("否（会自动补 Cookie 头）")));
               if (m_qrCb) m_qrCb(png);          // UI 直接把这张 PNG 画出来
               m_deadline = QDateTime::currentMSecsSinceEpoch() + 15LL * 60 * 1000;
               m_qrTimer.start();
           });
}

void DevLogin::pollQr()
{
    if (m_cancel || !m_busy) { m_qrTimer.stop(); return; }
    if (m_qrBusy) return;                                  // 上一发还没回来
    if (QDateTime::currentMSecsSinceEpoch() > m_deadline) {
        m_qrTimer.stop();
        finishLogin(false, QStringLiteral("扫码登录超时，请重试"));
        return;
    }

    m_qrBusy = true;
    const quint64 gen = m_gen;
    const QString url = ptqrloginUrl(m_u1, m_qrToken, m_loginSig);
    ++m_qrPoll;

    // ⚠ 实测：ptqrlogin 不带 qrsig cookie 时服务器直接回 403 + 空 body（curl 也一样）。
    //   Qt 的 jar 理论上会自动带，所以默认交给它；**只有当 jar 按 URL 匹配不到 qrsig 时**
    //   才手动补一个 Cookie 头（手动设又会被 Qt 合并，容易出双 Cookie 头，所以不无脑设）。
    QHash<QString, QString> h;
    h.insert(QStringLiteral("Referer"), QStringLiteral("https://xui.ptlogin2.qq.com/"));
    h.insert(QStringLiteral("Accept"), QStringLiteral("*/*"));
    h.insert(QStringLiteral("Accept-Encoding"), QStringLiteral("identity"));   // 不吃压缩，body 原样
    const QString ck = cookieHeaderFor(QUrl(url));
    if (!ck.contains(QStringLiteral("qrsig="))) {
        const QString qrsig = cookieValue(QStringLiteral("qrsig"));
        if (!qrsig.isEmpty()) {
            h.insert(QStringLiteral("Cookie"),
                     (ck.isEmpty() ? QString() : ck + QStringLiteral("; "))
                         + QStringLiteral("qrsig=") + qrsig);
            qcLog(QStringLiteral("jar 按 URL 匹配不到 qrsig，手动补 Cookie 头"), kLogErr);
        } else {
            qcLog(QStringLiteral("⚠ jar 里根本没有 qrsig，ptqrlogin 大概会 403"), kLogErr);
        }
    }

    get(url, h, 15000,
        [this, gen, url](bool, const QString &body, int status) {
            m_qrBusy = false;
            if (m_cancel || gen != m_gen || !m_busy) return;

            const QStringList a = parsePtuiCB(body);
            const QString code  = a.value(0);

            // 前 2 轮 + 每 15 轮记一次，确认「轮询确实在发、服务器确实在答」
            if (m_qrPoll <= 2 || m_qrPoll % 15 == 0)
                qcLog(QStringLiteral("ptqrlogin 第 %1 轮：HTTP %2，code=%3，body %4 字节")
                          .arg(m_qrPoll).arg(status)
                          .arg(code.isEmpty() ? QStringLiteral("(解析不到)") : code)
                          .arg(body.size()));

            if (a.isEmpty()) {
                // 空 body / HTML / 风控页：以前是静默「继续下一轮」，结果界面永远等待扫码。
                // 记日志，并在连续 3 次后直接报错退出，别让用户干等 15 分钟。
                ++m_qrBadCount;
                if (m_qrBadCount <= 3 || m_qrBadCount % 30 == 0)
                    qcLog(QStringLiteral("ptqrlogin 异常（第 %1 次）：HTTP %2，body %3 字节：%4")
                              .arg(m_qrBadCount).arg(status).arg(body.size())
                              .arg(body.left(160)), kLogErr);
                // ⚠ 不打完整 URL（里面 u1 / ptqrtoken / login_sig 都是凭证），只打长度特征：
                //   u1 正常编码后 300 字符以上；若只有几十字符，说明被二次替换坏了。
                if (m_qrBadCount == 1)
                    qDebug().noquote() << "[扫码登录] ptqrlogin 参数诊断：u1 长度" << m_u1.size()
                                       << "，ptqrtoken 已生成" << (m_qrToken != 0)
                                       << "，login_sig 长度" << m_loginSig.size();

                if (m_qrBadCount >= 3) {
                    m_qrTimer.stop();
                    finishLogin(false,
                        QStringLiteral("扫码轮询被服务器拒绝（连续 3 次，HTTP %1，body %2 字节）。\n\n"
                                       "原始返回：%3\n\n"
                                       "多半是 cookie 没带上或被风控：试试关掉代理再点一次。")
                            .arg(status).arg(body.size()).arg(body.left(200)));
                }
                return;
            }
            m_qrBadCount = 0;

            if (code == QLatin1String("0")) {              // 扫了并确认
                m_qrTimer.stop();
                m_confirmAt = nowMs();
                // ptuiCB 第 6 个参数一般是 uin（有的版本是空串）—— 拿它当开发者的备用标识
                m_uinHint = a.value(5).trimmed();
                const QString checkUrl = a.value(2);
                qcLog(QStringLiteral("扫码确认成功（ptuiCB code=0），开始换取登录态 —— "
                                     "从这里开始计时，看后面哪一跳慢"), kLogOk);
                if (checkUrl.isEmpty()) {
                    finishLogin(false, QStringLiteral("登录回调地址为空"));
                    return;
                }
                followCheckUrl(checkUrl);
                return;
            }
            if (code == QLatin1String("65")) {             // 二维码过期 -> 重开一轮（会推新二维码）
                qcLog(QStringLiteral("二维码过期，重新获取"), kLogGray);
                m_qrTimer.stop();
                newCycle();
                return;
            }
            if (code == QLatin1String("67")) {             // 已扫码，服务器认证中
                setStatus(QStringLiteral("已扫码，正在等待平台确认…（手机点了确认后稍等几秒）"));
            } else if (code == QLatin1String("66")) {      // 未扫
                setStatus(QStringLiteral("等待扫码…（二维码过期会自动刷新）"));
            } else {
                qcLog(QStringLiteral("ptqrlogin 返回未知 code=%1，body：%2")
                          .arg(code, body.left(160)), kLogErr);
            }
            // 66=未扫 / 67=已扫待确认：继续下一轮
        });
}

void DevLogin::followCheckUrl(const QString &url)
{
    const quint64 gen = m_gen;
    // 4. check_sig 链：最终落到 entity-picker，QQ 登录 cookie 在这一步种下
    get(url, {{QStringLiteral("Referer"), QStringLiteral("https://xui.ptlogin2.qq.com/")}}, 15000,
        [this, gen](bool ok, const QString &body, int status) {
            if (m_cancel || gen != m_gen) return;
            if (!ok) {
                finishLogin(false, QStringLiteral("登录跳转失败（HTTP %1）：%2")
                                       .arg(status).arg(body.left(120)));
                return;
            }
            qcLog(QStringLiteral("check_sig 链完成，QQ 登录 cookie 已种下"));
            bindConfirming();
        });
}

void DevLogin::bindConfirming()
{
    const quint64 gen = m_gen;
    QJsonObject body;
    body[QStringLiteral("session_id")]   = m_sessionId;
    body[QStringLiteral("developer_id")] = QString();
    body[QStringLiteral("login_status")] = 1;      // Confirming

    postJson(QStringLiteral("https://q.qq.com/lite/write_login_state"), body, qHeaders(), 15000,
             [this, gen](bool ok, const QString &resp, int status) {
                 if (m_cancel || gen != m_gen) return;
                 if (!ok) {
                     finishLogin(false, QStringLiteral("登录状态提交失败（HTTP %1）：%2")
                                            .arg(status).arg(resp.left(160)));
                     return;
                 }
                 // ⚠ 只发 Confirming 是走不完的：实测 /lite/poll 会一直回
                 //   {"code":3,"message":"已扫码，确认中"}（手机早就点了确认也一样）——
                 //   平台在等「同意授权」，必须再发一次 login_status=2。
                 //   而且这一步**必须要 developer_id**（空串直接被拒：
                 //   {"msg":"developer_id 不能为空","retcode":30001}），所以先查主体列表。
                 qcLog(QStringLiteral("write_login_state(Confirming) OK，去拿 developer_id 做 Agree"));
                 fetchDevelopers();
             });
}

void DevLogin::bindAgree()
{
    if (m_agreeTries >= 3) { resumePoll(); return; }   // 发够 3 次了，别再刷
    if (m_developerId.isEmpty()) {
        // 没有 id 就别发（必被 30001 拒），交给 poll 自己判断
        qcLog(QStringLiteral("没有可用的 developer_id，跳过 Agree 直接轮询"), kLogErr);
        resumePoll();
        return;
    }
    ++m_agreeTries;

    const quint64 gen = m_gen;
    QJsonObject body;
    body[QStringLiteral("session_id")]   = m_sessionId;
    body[QStringLiteral("developer_id")] = m_developerId;   // 第一次是空串，兜底时才填
    body[QStringLiteral("login_status")] = 2;               // Agree（同意授权）

    postJson(QStringLiteral("https://q.qq.com/lite/write_login_state"), body, qHeaders(), 15000,
             [this, gen](bool ok, const QString &resp, int status) {
                 if (m_cancel || gen != m_gen) return;

                 // 空 developer_id 会被平台直接拒（HTTP 200 + retcode 30001「developer_id 不能为空」）
                 const int rc = QJsonDocument::fromJson(resp.toUtf8()).object()
                                    .value(QStringLiteral("retcode")).toInt(0);
                 if (!ok || rc != 0) {
                     qcLog(QStringLiteral("write_login_state(Agree) 被拒（HTTP %1, retcode %2）：%3")
                               .arg(status).arg(rc).arg(resp.left(160)), kLogErr);
                     // id 拿不到才去查列表；已经查过还失败就别死循环，交给 poll
                     if (!m_devListTried) fetchDevelopers();
                     else                 resumePoll();
                     return;
                 }
                 qcLog(QStringLiteral("write_login_state(Agree, developer_id=%1) OK，开始轮询 lite/poll")
                           .arg(m_developerId.isEmpty() ? QStringLiteral("空") : maskVal(m_developerId)));
                 resumePoll();
             });
}

// 查一次主体列表填 m_developerId，然后发 Agree。
// 这一步是必经的：Agree 的 developer_id 不能为空（空串平台回 retcode 30001）。
void DevLogin::fetchDevelopers()
{
    if (m_devListTried) {
        // 查过了还是没有可用 id（账号在平台上没开发者身份）：只能靠 poll 自己走完
        resumePoll();
        return;
    }
    m_devListTried = true;
    const quint64 gen = m_gen;

    postJson(QStringLiteral("https://q.qq.com/lite/get_developers_for_login"), QJsonObject(),
             qHeaders(), 15000,
             [this, gen](bool ok, const QString &resp, int status) {
                 if (m_cancel || gen != m_gen) return;
                 if (!ok) {
                     // 查不到不算致命：poll 仍可能自己走完
                     qcLog(QStringLiteral("get_developers_for_login 失败（HTTP %1）：%2 —— 先继续轮询")
                               .arg(status).arg(resp.left(160)), kLogErr);
                     resumePoll();
                     return;
                 }

                 const QJsonObject data = QJsonDocument::fromJson(resp.toUtf8()).object()
                                              .value(QStringLiteral("data")).toObject();
                 const QJsonArray infos = data.value(QStringLiteral("developer_infos")).toArray();
                 const QJsonArray unv   = data.value(QStringLiteral("unverified_developer_infos")).toArray();

                 // 取第一个（已认证优先）。实测选错主体也能改机器人 —— 平台不按它鉴权，
                 // 所以不弹框让用户选，挑一个完成 Agree 就行。
                 if (m_developerId.isEmpty()) {
                     if (!infos.isEmpty())
                         m_developerId = infos.first().toObject().value(QStringLiteral("id")).toString();
                     else if (!unv.isEmpty())
                         m_developerId = unv.first().toObject().value(QStringLiteral("id")).toString();
                 }

                 qcLog(QStringLiteral("get_developers_for_login：已认证 %1 / 未认证 %2，developer_id %3")
                           .arg(infos.size()).arg(unv.size())
                           .arg(m_developerId.isEmpty() ? QStringLiteral("没取到（跳过 Agree）")
                                                        : QStringLiteral("已取到")));
                 bindAgree();
             });
}

void DevLogin::resumePoll()
{
    // 先启动 2 秒定时器，再立刻打一次（顺序反过来的话，若这一发就直接结束流程，
    // 后面的 start() 会把已经停掉的定时器又拉起来）
    if (!m_liteTimer.isActive()) m_liteTimer.start();
    pollLite();
}

void DevLogin::pollLite()
{
    if (m_cancel || !m_busy) { m_liteTimer.stop(); return; }
    if (m_liteBusy) return;
    if (++m_litePoll > 40) {                       // 与 python 的 40 次上限一致
        m_liteTimer.stop();
        finishLogin(false, QStringLiteral("登录状态确认超时"));
        return;
    }

    m_liteBusy = true;
    const quint64 gen = m_gen;
    const QString url = QStringLiteral("https://q.qq.com/lite/poll?session_id=%1").arg(enc(m_sessionId));

    get(url, {{QStringLiteral("Referer"), QStringLiteral("https://q.qq.com/")}}, 15000,
        [this, gen](bool, const QString &body, int) {
            m_liteBusy = false;
            if (m_cancel || gen != m_gen || !m_busy) return;

            const QJsonObject j = QJsonDocument::fromJson(body.toUtf8()).object();
            const QJsonObject d = j.value(QStringLiteral("data")).toObject();
            const int code = d.value(QStringLiteral("code")).toInt(-1);
            const QString redirect = d.value(QStringLiteral("redirect")).toString();

            // poll 的返回里自带 developer_id —— 只拿来打日志（实测不选主体也能改机器人）
            const QString devId = d.value(QStringLiteral("developer_id")).toString();
            if (!devId.isEmpty())
                m_developerId = devId;

            if (j.value(QStringLiteral("retcode")).toInt() == 0 && code == 0) {
                m_liteTimer.stop();
                qcLog(QStringLiteral("lite/poll 返回完成（code=0），redirect %1")
                          .arg(redirect.isEmpty() ? QStringLiteral("为空") : QStringLiteral("有效")),
                      kLogOk);
                if (!redirect.isEmpty())
                    followRedirect(redirect);
                else
                    finishLogin(true, QString());
                return;
            }
            if (code == 2 || code == 4) {
                m_liteTimer.stop();
                finishLogin(false, QStringLiteral("登录被拒绝（code=%1）").arg(code));
                return;
            }

            // code=3「已扫码，确认中」：说明 Agree 没生效（没发成功 / 没拿到 developer_id）。
            // 若还没查过主体列表，这里补查一次（只查一次），别干等到 40 轮超时（80 秒）。
            if (code == 3 && !m_devListTried && m_litePoll >= 3) {
                qcLog(QStringLiteral("poll 卡在 code=3（已扫码，确认中）→ 补查主体列表再 Agree"),
                      kLogErr);
                fetchDevelopers();
                return;
            }

            // 其他：下一轮继续（前几次记一下，方便看卡在哪一步）
            if (m_litePoll <= 3 || m_litePoll % 10 == 0)
                qcLog(QStringLiteral("lite/poll 第 %1 次：retcode=%2 code=%3")
                          .arg(m_litePoll)
                          .arg(j.value(QStringLiteral("retcode")).toInt())
                          .arg(code));
            // 让界面也能看出「还在跑」而不是僵在「等待扫码」（这一阶段手机早就确认过了）
            setStatus(QStringLiteral("手机已确认，正在等平台下发登录票据…（第 %1 次）")
                          .arg(m_litePoll));
        });
}

void DevLogin::followRedirect(const QString &url)
{
    const quint64 gen = m_gen;
    // 5. 落 redirect：b-token / qticket / quin / quid 在这一步种进 jar
    get(url, {{QStringLiteral("Referer"), QStringLiteral("https://q.qq.com/")}}, 15000,
        [this, gen](bool ok, const QString &, int status) {
            if (m_cancel || gen != m_gen) return;
            if (!ok) {
                finishLogin(false, QStringLiteral("获取登录票据失败（HTTP %1）").arg(status));
                return;
            }
            const bool got = !cookieValue(QStringLiteral("b-token")).isEmpty()
                          || !cookieValue(QStringLiteral("qticket")).isEmpty()
                          || !cookieValue(QStringLiteral("qticket_lite")).isEmpty();
            if (!got) {
                finishLogin(false, QStringLiteral("登录完成但没拿到开放平台票据（b-token/qticket）"));
                return;
            }
            // 票据到手的证据：jar 里种下了哪些 cookie（只打名字，值一律不打）
            QStringList names;
            if (auto *jar = static_cast<MemCookieJar *>(m_curNam ? m_curNam->cookieJar() : nullptr)) {
                for (const QNetworkCookie &c : jar->allCookies()) {
                    const QString n = QString::fromUtf8(c.name());
                    if (n == QLatin1String("b-token") || n == QLatin1String("qticket")
                        || n == QLatin1String("qticket_lite") || n == QLatin1String("quin")
                        || n == QLatin1String("quid") || n == QLatin1String("p_uin"))
                        names << n;
                }
            }
            qcLog(QStringLiteral("票据已到手：%1")
                      .arg(names.isEmpty() ? QStringLiteral("（jar 里有票据但名单为空）")
                                           : names.join(QStringLiteral(" / "))),
                  kLogOk);
            finishLogin(true, QString());
        });
}

void DevLogin::finishLogin(bool ok, const QString &err)
{
    m_qrTimer.stop();
    m_liteTimer.stop();
    m_qrBusy = false;
    m_liteBusy = false;
    m_busy = false;

    if (ok) {
        // 这次登录到底是谁：优先 cookie 里的 p_uin（形如 o0123456789），
        // 退化用 ptuiCB 带回的 uin，都拿不到就归到 "-" 这一桶。
        QString uin = cookieValue(QStringLiteral("p_uin")).trimmed();
        if (uin.startsWith(QLatin1Char('o')))
            uin.remove(0, 1);
        if (uin.isEmpty())
            uin = m_uinHint;
        if (uin.isEmpty())
            uin = QStringLiteral("-");

        // 同一个开发者又扫了一次：旧会话换掉（新的 ck 更可信）
        Session *s = m_sessions.value(uin, nullptr);
        if (!s) {
            s = new Session;
            s->uin = uin;
        } else if (s->nam && s->nam != m_pendingNam) {
            s->nam->deleteLater();
        }
        s->nam         = m_pendingNam;
        s->loginAtMs   = nowMs();
        s->developerId = m_developerId;
        m_sessions.insert(uin, s);   // 新号建档 / 同号再扫就覆盖（uin 拿不到时存 "-"）

        m_pendingNam = nullptr;   // 下次登录再开一份干净的
        m_curNam     = s->nam;
        m_curUin     = uin;
        m_lastUin    = uin;

        // 这次是为哪个机器人登录的 → 直接绑定，之后它就用这份 ck
        if (m_loginAppid > 0)
            m_appidUin.insert(m_loginAppid, uin);
        m_loginAppid = 0;
    } else if (m_pendingNam) {
        // 取消/失败：把这份半成品 jar 清干净，免得下次扫码时带上别人的旧 cookie
        if (auto *jar = static_cast<MemCookieJar *>(m_pendingNam->cookieJar()))
            jar->setAllCookies(QList<QNetworkCookie>());
    }

    if (ok) {
        const qint64 total = (m_confirmAt > 0) ? (nowMs() - m_confirmAt) : -1;
        // ⚠ uin 属于账号信息，日志里只留掩码形态（连"登录的是哪个号"都不外泄）
        qcLog(QStringLiteral("==== 登录成功，开发者 uin=%1，从手机点确认到拿到 ck 共 %2 ms ====")
                  .arg(maskVal(m_curUin))
                  .arg(total >= 0 ? QString::number(total) : QStringLiteral("?")),
              kLogOk);
    } else {
        qcLog(QStringLiteral("==== 登录结束（失败/取消）：%1 ====").arg(err), kLogErr);
    }

    m_qrBadCount = 0;
    m_statusCb = nullptr;

    // 先清回调再调用：回调里可能立刻发起新一轮登录
    LoginCb cb = m_loginCb;
    m_loginCb = nullptr;
    m_qrCb    = nullptr;
    if (cb) cb(ok, err);
}

// ------------------------------------------------------------ 登录态 / 切换 --

bool DevLogin::hasLoginSession() const
{
    // 任意一个开发者会话还在就用得上（多账号框架下「有没有登录」不是全局概念）
    for (Session *s : m_sessions) {
        if (validSession(s->uin))
            return true;
    }
    return false;
}

bool DevLogin::hasSessionFor(int botAppid) const
{
    return pickSession(botAppid) != nullptr;
}

QString DevLogin::sessionLabelFor(int botAppid) const
{
    Session *s = pickSession(botAppid);
    if (!s)
        return QStringLiteral("（还没登录任何开发者账号）");
    QString u = s->uin;
    if (u == QLatin1String("-"))
        u = QStringLiteral("未知（接口没回 uin）");
    return QStringLiteral("开发者 %1").arg(u);
}

void DevLogin::clearSession()
{
    // 逐个摘掉：每个会话有自己的一份 QNAM / jar
    const QList<QString> keys = m_sessions.keys();
    for (const QString &k : keys)
        dropSession(k);

    m_appidUin.clear();
    m_lastUin.clear();
    m_curNam = nullptr;
    m_curUin.clear();

    if (m_pendingNam) {
        if (auto *jar = static_cast<MemCookieJar *>(m_pendingNam->cookieJar()))
            jar->setAllCookies(QList<QNetworkCookie>());
    }
}

void DevLogin::clearSessionForAppid(int botAppid)
{
    // 只解绑，不动会话本体：别的机器人可能还在用这个开发者的 ck。
    // （「换号重扫」由界面强制走一次登录，成功后会自动重新绑定。）
    m_appidUin.remove(botAppid);
}

void DevLogin::modifyConnectInfo(int botAppid, int action, const QString &webhookUrl, ApiCb cb)
{
    // ① 选会话：这个机器人绑定过的开发者 → 否则最近登录的那个（先试一次）
    Session *sess = pickSession(botAppid);
    if (!sess) {
        // 一份 ck 都没有：约定用 "NEED_LOGIN" 告诉界面去弹二维码
        if (cb) cb(false, QString(), QStringLiteral("NEED_LOGIN"));
        return;
    }
    m_curNam = sess->nam;
    m_curUin = sess->uin;
    const QString usedUin = sess->uin;   // 回调里 m_curUin 可能已经被改掉，先存一份

    struct Ctx { int tries = 0; };
    auto ctx  = std::make_shared<Ctx>();
    auto fire = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakFire = fire;   // 避免 lambda 自己持有自己

    *fire = [this, botAppid, action, webhookUrl, cb, ctx, weakFire, usedUin]() {
        QJsonObject body;
        body[QStringLiteral("bot_appid")]   = botAppid;
        body[QStringLiteral("action")]      = action;
        body[QStringLiteral("webhook_url")] = webhookUrl;

        QHash<QString, QString> h;
        h.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
        h.insert(QStringLiteral("Accept"), QStringLiteral("application/json, text/plain, */*"));
        h.insert(QStringLiteral("Origin"), QStringLiteral("https://bot.q.qq.com"));
        h.insert(QStringLiteral("Referer"), QStringLiteral("https://bot.q.qq.com/"));
        h.insert(QStringLiteral("User-Agent"), QString::fromLatin1(kUa));

        postJson(QString::fromLatin1(kModifyUrl), body, h, 20000,
                 [this, botAppid, cb, ctx, weakFire, usedUin](bool httpOk, const QString &resp, int status) {
                     // 不管 2xx 还是 4xx/5xx，body 都先解析一遍：
                     //   登录态过期时这台服务器是回 {"retcode":401,"msg":"miss ticket"}
                     //   （有时挂 HTTP 200，有时直接 401 —— 两种都按 401 处理）
                     const QJsonObject o = QJsonDocument::fromJson(resp.toUtf8()).object();
                     int code = 0;
                     if (o.contains(QStringLiteral("retcode")))
                         code = o.value(QStringLiteral("retcode")).toInt();
                     else if (o.contains(QStringLiteral("code")))
                         code = o.value(QStringLiteral("code")).toInt();
                     QString msg = o.value(QStringLiteral("msg")).toString();
                     if (msg.isEmpty())
                         msg = o.value(QStringLiteral("message")).toString();
                     if (code == 0 && !httpOk)
                         code = status;      // 非 2xx 且 body 里没说原因：拿状态码当错误码

                     const bool authFail = (code == 401) || (status == 401)
                                        || msg.contains(QStringLiteral("ticket"), Qt::CaseInsensitive);

                     if (authFail && ctx->tries == 0) {
                         ctx->tries++;
                         // 第一次撞 401：补一次 bot.q.qq.com 控制台会话 cookie（有些票据是
                         // 访问控制台首页时才下发的），然后原样重试一次。
                         warmupConsole([weakFire](bool) {
                             if (auto f = weakFire.lock()) (*f)();
                         });
                         return;
                     }

                     if (ctx->tries > 0 && authFail) {
                         // 补过票还是 401：**这个开发者**的登录态真过期了。
                         // 只摘他这一份，别把其它账号的 ck 一起清了（多账号框架）。
                         dropSession(usedUin);
                     }

                     const bool ok = httpOk && code == 0;
                     QString err;
                     if (ok) {
                         // 记下来：这个机器人属于这个开发者，下次直接用同一份 ck
                         m_appidUin.insert(botAppid, usedUin);
                     } else {
                         if (!msg.isEmpty())
                             err = msg;
                         else if (status > 0)
                             err = QStringLiteral("接口返回 HTTP %1（body 为空或不是 JSON）").arg(status);
                         else
                             err = QStringLiteral("网络错误：连响应都没收到");
                     }
                     if (cb) cb(ok, resp, err);
                 });
    };

    (*fire)();
}

void DevLogin::warmupConsole(std::function<void(bool)> cb)
{
    get(QStringLiteral("https://bot.q.qq.com/"),
        {{QStringLiteral("Referer"), QStringLiteral("https://q.qq.com/")}}, 15000,
        [cb](bool ok, const QString &, int) {
            if (cb) cb(ok);
        });
}

// ------------------------------------------------------------------ 工具 --

void DevLogin::get(const QString &url, const QHash<QString, QString> &headers, int timeoutMs,
                   std::function<void(bool, const QString &, int)> cb)
{
    getRaw(url, headers, timeoutMs,
           [cb](bool ok, const QByteArray &data, int status) {
               if (cb) cb(ok, QString::fromUtf8(data), status);
           });
}

void DevLogin::getRaw(const QString &url, const QHash<QString, QString> &headers, int timeoutMs,
                      std::function<void(bool, const QByteArray &, int)> cb)
{
    const QUrl u(url);
    QNetworkRequest req{u};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    applyCommonHeaders(req, headers, false);
    req.setTransferTimeout(timeoutMs);

    if (!m_curNam) {          // 会话已被摘掉（401 后 dropSession）：当网络失败处理
        logHttp(QStringLiteral("GET"), u, 0, 0, QByteArray(), true);
        if (cb) cb(false, QByteArray(), 0);
        return;
    }
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply *reply = m_curNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb, u, t0]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        // ⚠ 只有 2xx 才算成功：非 200 一律当失败（401/403/429/5xx 都可能是 WAF / 登录态问题），
        //   但 body 和状态码仍然原样交回去，调用方才能把真正的报错内容弹给用户。
        //   （NetManager 那三个 *Async 只给 body + error，没有状态码 —— 这条链要判 401，
        //     所以这里用自带的 QNAM，能拿到 HttpStatusCodeAttribute。）
        const bool netErr = (reply->error() != QNetworkReply::NoError) && status == 0;
        const bool ok = !netErr && status >= 200 && status < 300;
        //logHttp(QStringLiteral("GET"), u, QDateTime::currentMSecsSinceEpoch() - t0,
        //        status, data, netErr);
        reply->deleteLater();
        if (cb) cb(ok, data, status);
    });
}

void DevLogin::postJson(const QString &url, const QJsonObject &body,
                        const QHash<QString, QString> &headers, int timeoutMs,
                        std::function<void(bool, const QString &, int)> cb)
{
    const QUrl u(url);
    QNetworkRequest req{u};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    applyCommonHeaders(req, headers, true);
    req.setTransferTimeout(timeoutMs);

    if (!m_curNam) {
        logHttp(QStringLiteral("POST"), u, 0, 0, QByteArray(), true);
        if (cb) cb(false, QString(), 0);
        return;
    }
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply *reply = m_curNam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, cb, u, t0]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        // 同 getRaw：只有 2xx 算成功，但 body 一定交回去（401 的 JSON 报错就在这里）
        const bool netErr = (reply->error() != QNetworkReply::NoError) && status == 0;
        const bool ok = !netErr && status >= 200 && status < 300;
        //logHttp(QStringLiteral("POST"), u, QDateTime::currentMSecsSinceEpoch() - t0,
        //        status, data, netErr);
        reply->deleteLater();
        if (cb) cb(ok, QString::fromUtf8(data), status);
    });
}

QString DevLogin::cookieValue(const QString &name) const
{
    // 只查「当前流程正在用的那份」jar —— 多账号下绝不能跨开发者读 cookie
    if (!m_curNam) return QString();
    auto *jar = static_cast<MemCookieJar *>(m_curNam->cookieJar());
    if (!jar) return QString();
    const QByteArray n = name.toUtf8();
    const QList<QNetworkCookie> all = jar->allCookies();
    for (const QNetworkCookie &c : all) {
        if (c.name() == n)
            return QString::fromUtf8(c.value());
    }
    return QString();
}

QString DevLogin::cookieHeaderFor(const QUrl &url) const
{
    if (!m_curNam) return QString();
    auto *jar = static_cast<MemCookieJar *>(m_curNam->cookieJar());
    if (!jar) return QString();

    // cookiesForUrl 走的是 RFC 域名/路径/secure 匹配（跟 QNAM 自动带 cookie 是同一逻辑），
    // 所以这里的返回值 = 「Qt 实际会发出去的那些 cookie」，用来诊断 403 很直观。
    QStringList parts;
    const QList<QNetworkCookie> cs = jar->cookiesForUrl(url);
    for (const QNetworkCookie &c : cs)
        parts << QStringLiteral("%1=%2").arg(QString::fromUtf8(c.name()),
                                             QString::fromUtf8(c.value()));
    return parts.join(QStringLiteral("; "));
}

void DevLogin::setStatus(const QString &text)
{
    if (m_statusCb) m_statusCb(text);
}
