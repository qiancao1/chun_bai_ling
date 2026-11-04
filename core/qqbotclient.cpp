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

#include "qqbotclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDateTime>

#include "global.h"
#include <QNetworkReply>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QHostInfo>
#include <QHostAddress>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QElapsedTimer>
#include <thread>
#include <openssl/evp.h>

#include <qwaitcondition.h>
#include <QFutureWatcher>
#include <netmanager.h>
#include <QThreadPool>


// 在文件头部包含 WinHttpClient 封装


bool shuaping(AccountInfo *info, const MessageEvent &ev);
bool is_server();
QHash<int, QQBotClient*> m_botClients;

QQBotClient::QQBotClient(AccountInfo *info, QObject *parent)
    : QObject(parent), m_info(info), m_isConnecting(false),
    m_reconnectAttempts(0), m_heartbeatIntervalSec(30),
    m_invalidHeartbeatCount(0), m_seq(0), m_tokenExpireTime(0)
{

    connect(&m_webSocket, &QWebSocket::connected, this, &QQBotClient::onConnected);
    connect(&m_webSocket, &QWebSocket::disconnected, this, &QQBotClient::onDisconnected);
    connect(&m_webSocket, &QWebSocket::textMessageReceived, this, &QQBotClient::onTextMessageReceived);
    connect(&m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &QQBotClient::onError);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &QQBotClient::onHeartbeatTimeout);
    m_reconnectTimer.setSingleShot(true);
    // 注意连的是 onReconnectTimerFired 而不是直接 start：
    // 到点必须"真的再试一次"，不能被 start() 第一行的 m_isConnecting 挡掉（见该函数注释）。
    connect(&m_reconnectTimer, &QTimer::timeout, this, &QQBotClient::onReconnectTimerFired);
    // 握手看门狗：open() 之后如果既没 connected 也没 disconnected（socket 还挂在
    // ClosingState、或 open 被静默忽略），m_isConnecting 会永远是 true，
    // 之后每次 start() 都在第一行 return，日志一行都不会有 —— 这个定时器就是兜这个底。
    m_handshakeTimer.setSingleShot(true);
    connect(&m_handshakeTimer, &QTimer::timeout, this, &QQBotClient::onHandshakeTimeout);

    // 自动撤回：把自己登记进**全局**撤回表 —— 全进程只有一个 1 秒定时器（recallTickAll），
    // 每个机器人不再各自挂一个定时器。定时器在第一次登记撤回时才启动，全部清空后停。
    // 这里不加锁：QQBotClient 只在主线程创建（CardWidget::getOrCreateClient）和销毁
    // （parent = CardWidget），而遍历也在主线程的定时器里跑 —— 表和定时器全是主线程私有。
    s_recallClients.append(this);
}

QQBotClient::~QQBotClient()
{
    // 摘表放在析构最前面（同样无需加锁：析构与遍历都在主线程，不会重叠）。
    s_recallClients.removeAll(this);
    // 停表只在主线程做（QTimer 跨线程 stop 不安全）。
    if (s_recallClients.isEmpty() && s_recallTimer
        && QCoreApplication::instance()
        && QThread::currentThread() == QCoreApplication::instance()->thread()) {
        s_recallTimer->stop();
        s_recallStarted.store(false, std::memory_order_release);
    }
    freeRecallChunks();
    stop();
}

// ==================== 发送后自动撤回 ====================
//
// 结构：每个机器人一份 m_recallChunk[120][16]（每块 16 条），全进程共用**一个**定时器。
//
//   · 槽 = 到期时刻的秒数 % 120。扫描时扫「上次处理到的那一秒 +1」到「此刻-1 秒」之间的槽 ——
//     正常每跳 1 个槽，不遍历全部待撤消息；卡顿过就把漏掉的几秒一起补上，不会漏。
//   · 扫的是「上一秒」的槽：添加侧按 [1,110] 秒算到期槽，跟「上一秒」永远差至少一个槽位，
//     所以写和扫绝不会落在同一块内存上 —— 这是写路径能彻底无锁的前提。代价只是晚 1 秒撤。
//   · 写：fetch_add 拿下标 → idx/16 定位第几块、idx%16 定位块内位置。块第一次用到才分配，
//     用原子 CAS 挂上去；块**只追加、永不搬移**，所以没有"扩容要把旧内存搬走"的问题，
//     也就不需要任何互斥。块数不够就再挂一块，等价于扩容，但老数据一个字节都不动。
//   · 撤：清空元素内容、块和位置都留着，扫完游标归零 —— 稳态下零分配、零释放。
//   · 同一个 id 可能被 addmsglog 记两次（媒体段 + 外层收尾），扫描时用局部 QSet 去重。
//
// 为什么定时器是全局的：几十个机器人各挂一个 1 秒 QTimer，等于每秒几十次定时器唤醒，
//   而绝大多数机器人根本没有待撤消息。改成一个全局表 + 一个定时器，一次遍历全搞定，
//   全部为空时直接停表。
//
// ⚠ 撤回用的 msgid 必须是**平台发消息回执里的 "id"**，不是插件传进来的 msgid ——
//   后者是「被回复的那条消息」的 id，拿它撤撤不掉自己发的消息。

// 全局撤回表与定时器（定义只能有一份，所以放 .cpp）
//   表只在主线程增删/遍历（见构造函数注释），所以不需要互斥。
QList<QQBotClient *> QQBotClient::s_recallClients;
QTimer              *QQBotClient::s_recallTimer = nullptr;
std::atomic<bool>    QQBotClient::s_recallStarted{false};
std::atomic<bool>    QQBotClient::s_recallPosted{false};

// 登记消息后拉起全局定时器。可在任意线程调用：QTimer 只能在它自己的线程（主线程）start，
// 不在主线程就投递一次 —— 用 s_recallPosted 保证「空闲→有活」这段最多投一次，不是每条都投。
void QQBotClient::recallEnsureTimer()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app) return;

    if (QThread::currentThread() == app->thread()) {
        if (!s_recallTimer) {
            // 定时器挂在 qApp 名下：进程退出时由 Qt 回收，不用手工 delete（也不会漏）。
            s_recallTimer = new QTimer(app);
            s_recallTimer->setInterval(1000);
            QObject::connect(s_recallTimer, &QTimer::timeout, app, []() { recallTickAll(); });
        }
        if (!s_recallTimer->isActive()) s_recallTimer->start();
        return;
    }

    s_recallStarted.store(true, std::memory_order_relaxed);
    if (!s_recallPosted.exchange(true)) {
        QMetaObject::invokeMethod(app, []() {
            s_recallPosted.store(false, std::memory_order_relaxed);
            if (s_recallStarted.load(std::memory_order_relaxed))
                recallEnsureTimer();
        }, Qt::QueuedConnection);
    }
}

// 全局定时器每秒一跳：遍历所有机器人，让有活的各扫自己的槽。
//   不加锁：表的增删（构造/析构）和这里的遍历全在主线程，单线程顺序执行，不存在并发。
//   遍历里只做内存操作 + 投递异步撤回请求（真正发网络是异步的，也不阻塞）。
void QQBotClient::recallTickAll()
{
    bool anyPending = false;
    for (QQBotClient *c : s_recallClients) {
        if (c->m_recallPending.load(std::memory_order_relaxed) <= 0) continue;
        c->recallTickSelf();
        if (c->m_recallPending.load(std::memory_order_relaxed) > 0) anyPending = true;
    }
    if (anyPending) return;

    // 全部清空 → 停表。停之前先把 started 落下，再**双检**一次：
    // 刚好在这一瞬间登记的消息，可能看到 started=true 就没去投递启动，这里补拉起来。
    if (s_recallTimer && s_recallTimer->isActive()) s_recallTimer->stop();
    s_recallStarted.store(false, std::memory_order_release);

    bool again = false;
    for (QQBotClient *c : s_recallClients) {
        if (c->m_recallPending.load(std::memory_order_relaxed) > 0) { again = true; break; }
    }
    if (again) {
        s_recallStarted.store(true, std::memory_order_relaxed);
        recallEnsureTimer();
    }
}

void QQBotClient::scheduleAutoRecall(int type, const QString &openid, const QString &msgid, int sec)
{
    if (sec <= 0 || msgid.isEmpty() || openid.isEmpty()) return;
    if (sec > kRecallMaxSec) sec = kRecallMaxSec;

    const int slot = int((QDateTime::currentSecsSinceEpoch() + sec) % kRecallSlots);
    const int idx  = m_recallCursor[slot].fetch_add(1, std::memory_order_relaxed);

    // 先记账、先拉表，再判断装不装得下：
    //   扫描时是按游标（used）整批减 pending 的，只要游标加了 1，pending 就必须也加 1，
    //   否则「这一秒超过 256 条被丢掉」时 pending 会被多减、变成负数，定时器会提前停掉、
    //   把别的槽里真正该撤的消息一起漏掉。丢掉的这条不写内容即可（到期那一秒扫到空位会跳过）。
    m_recallPending.fetch_add(1, std::memory_order_relaxed);
    // 拉起全局定时器（所有机器人共用一个）。任意线程都能登记，内部自己投递到主线程。
    // 每个「全部空闲→有活」的周期最多投一次，不是每条消息都投，主线程基本感觉不到。
    if (!s_recallStarted.exchange(true, std::memory_order_relaxed))
        recallEnsureTimer();

    const int ci = idx / kRecallChunk;
    if (ci >= kRecallMaxChunks) return;      // 这一秒超过 256 条（正常到不了）→ 这条不登记

    // 块第一次用到才分配。CAS 抢输了就扔掉自己那份，用别人挂上去的那块。
    std::atomic<RecallItem *> &cell = m_recallChunk[slot][ci];
    RecallItem *chunk = cell.load(std::memory_order_acquire);
    if (!chunk) {
        RecallItem *fresh = new RecallItem[kRecallChunk];
        RecallItem *expect = nullptr;
        if (cell.compare_exchange_strong(expect, fresh, std::memory_order_acq_rel)) {
            chunk = fresh;
        } else {
            delete[] fresh;
            chunk = expect;
        }
    }
    RecallItem &it = chunk[idx % kRecallChunk];
    it.type   = type;
    it.openid = openid;
    it.msgid  = msgid;
}

// 单个机器人的一跳（由全局定时器 recallTickAll 调用，只在主线程跑）：
// 从 m_recallLastSec（上次处理到的那一秒）接着往下扫到「此刻-1 秒」，
// 跨度几个槽就扫几个槽 —— 正常每跳跨度 1，只碰一个槽，O(1)。
//   · 主线程卡了几秒 → 跨度变大，把那几秒补上（但只在安全窗内，见下）；
//   · 首次启动 / 停太久 / 时钟回拨 / 卡顿超过安全窗 → **不补扫**，只把游标推到 now-1。
//     补扫一旦回看得太远，就会碰到「刚登记、还没到期」的槽（槽位是 %120 复用的），
//     变成把未来的消息立刻撤掉；所以宁可漏补，也绝不误撤。
void QQBotClient::recallTickSelf()
{
    const qint64 nowSec = QDateTime::currentSecsSinceEpoch();

    // 只在「上次扫到的那秒」离现在足够近时才补扫。
    //   为什么不能随便往回补：扫 s 这一秒 = 碰 s%120 这个槽，而**刚登记、还没到期**的消息
    //   其到期秒 (now+sec) 也可能和某个过去的秒落到同一个槽 —— 差满 120 就重合。
    //   设差 d 秒重合：sec = 120-d，而 sec 上限 110 → d ≥ 10 就必然撞上未来的消息，
    //   把还没到期的当成到期撤掉（首次启动 lastSec=0、跨度巨大时，会一次秒撤所有内容）。
    //   所以安全窗 = kRecallSlots - kRecallMaxSec（120-110 = 10 秒跨距，最多回看 9 个槽）。
    //   超出安全窗（首次 / 卡顿超过 9 秒 / 停表很久）就干脆不补：那段距离早就超出撤回时限，
    //   而且环形绕过一圈后那些槽已被后来的登记覆盖，补也补不回来。
    const qint64 gap = nowSec - m_recallLastSec;
    const qint64 fromSec = (m_recallLastSec > 0 && gap > 0 && gap <= kRecallSlots - kRecallMaxSec)
                           ? m_recallLastSec + 1
                           : nowSec;                      // = 让循环不执行（本跳不补扫）

    // pending == 0 ⇒ 所有槽都是空的（登记才 +1、扫到才 -used），整段循环跳过。
    const bool hasPending = m_recallPending.load(std::memory_order_relaxed) > 0;
    for (qint64 s = fromSec; hasPending && s <= nowSec - 1; ++s) {
        const int slot = int(s % kRecallSlots);
        const int used = m_recallCursor[slot].exchange(0, std::memory_order_acq_rel);
        if (used <= 0) continue;
        // 按「登记的条数」减，不是按真撤了几条：同一个 id 重复登记过就会多算，用 fired 减会永远减不完
        m_recallPending.fetch_sub(used, std::memory_order_relaxed);

        QSet<QString> seen;                   // 同一 id 只撤一次（重复登记来自媒体段 + 外层收尾）
        const int n = qMin(used, kRecallChunk * kRecallMaxChunks);
        for (int i = 0; i < n; ++i) {
            RecallItem *chunk = m_recallChunk[slot][i / kRecallChunk].load(std::memory_order_acquire);
            if (!chunk) continue;
            RecallItem &it = chunk[i % kRecallChunk];
            if (it.msgid.isEmpty()) continue;             // 空位，跳过
            const QString id = it.msgid;
            const QString op = it.openid;
            const int     tp = it.type;
            // 只清内容、不删块、不退容量：这一槽下次用到时直接在原位置上覆盖。
            it.msgid.clear();
            it.openid.clear();
            it.type = 0;
            if (seen.contains(id)) continue;
            seen.insert(id);
            // 异步撤回：传了非空回调 → 底层走 DeleteAsync，不占主线程等网络。
            // 结果直接丢：消息可能早已过期 / 被管理员删掉，撤回失败没有任何补救手段，也不值得刷日志。
            delete_messages(tp, op, id, [](const QString &, QNetworkReply::NetworkError) {});
        }
    }
    m_recallLastSec = nowSec - 1;
    // 启停全局定时器不在这里管 —— 由 recallTickAll 统一判断「所有机器人都空了」再停表。
}

void QQBotClient::freeRecallChunks()
{
    for (int s = 0; s < kRecallSlots; ++s)
        for (int c = 0; c < kRecallMaxChunks; ++c)
            delete[] m_recallChunk[s][c].exchange(nullptr, std::memory_order_relaxed);
}



void QQBotClient::start()
{
    // ⚠「僵尸在线」判定（就是"自动重连永远等不上、手动点登录一下就好"的根因之一）：
    //   m_info->online 只是个业务标志，TCP 被中间设备静默丢掉时它仍然是 true。
    //   以前这里直接 return，于是：
    //     · 重连定时器到点 → start() 第一行就被挡住 → 自动重连永久停摆；
    //     · 界面一直显示在线，看不到任何"断开"；
    //     · 手动点登录按钮能好，是因为它先 stop()（置 online=false）再 start()。
    //   现在先判断 WS 是否真的还连着，不连就把在线标志复位后再继续重连。
    if (m_info->online) {
        const bool wsAlive = (m_info->type != 0)
                          || (m_webSocket.state() == QAbstractSocket::ConnectedState);
        if (wsAlive)
            return;
        AppendEventLog(QString("%1 状态显示在线但连接实际已断，重置在线状态后重连").arg(m_info->nickname), 0xff);
        m_info->online = false;
        emit disconnected();
    }
    if (m_isConnecting)
        return;

    if (m_info->appid.isEmpty() || m_info->secret.isEmpty()) {
        AppendEventLog("缺少 appid 或 secret，无法获取 AccessToken",0xff);
        m_info->err+="缺少 appid 或 secret，无法获取 AccessToken\n";
        return ;
    }
    m_isConnecting = true;
    m_userWantsOnline = true;      // 用户/自动登录要这个机器人在线 → 掉线后允许自动重连
    m_info->err.clear();
    // 日志里把「这轮是复用本地 token 还是重新取 token」写清楚。
    // 「对面断开后一直连不上」现场，第一眼看这句就能判断本地 token 有没有刷新 ——
    // 之前这里只写「尝试登录」，token 到底是新的还是那条已经废了的，完全看不出来。
    {
        const qint64 now0 = QDateTime::currentSecsSinceEpoch();
        const bool reuse = !m_accessToken.isEmpty() && m_tokenExpireTime > now0;
        AppendEventLog(QString("尝试登录：%1(%2) %3")
                           .arg(m_info->nickname, m_info->appid,
                                reuse ? QString("token=复用(剩 %1 分钟)").arg((m_tokenExpireTime - now0) / 60)
                                      : QStringLiteral("token=重新获取")), 0x0082FD);
    }
    Callback2 calls = [this](){

        fetchGatewayUrl([this](const QString &wsUrl,QNetworkReply::NetworkError Neterr){

            QMetaObject::invokeMethod(this, [this,wsUrl]() {
                if (wsUrl.isEmpty()) {
                    m_isConnecting = false;
                    scheduleReconnect(5);

                    return;
                }
                if(m_info->type==0)
                {
                    // 上一次的连接可能还挂在 ConnectingState/ClosingState —— 那种状态上再 open()
                    // 是未定义行为（QWebSocket 会静默忽略这次 open，connected 永不触发，
                    // 只有看门狗能救回来）。这里先彻底丢掉旧 socket。
                    closeSocketSilently();

                    QNetworkRequest request(wsUrl);
                    request.setRawHeader("User-Agent", "QQBotPlugin/9.9.9 (Node/20.11.0; Linux; WorkBuddy/1.2.6)");
                    m_webSocket.open(request);
                    m_handshakeTimer.start(15000);
                }else{
                    if(is_server())
                    {
                        m_info->online=true;
                        fetchSelfInfo();
                    }else{
                        m_info->err+="未启动webhook服务器 请允许后再试试\n";
                        AppendEventLog("未启动webhook服务器 请允许后再试试");
                    }
                }
                emit avatarDownloaded(); //信号
                return  ;
            }, Qt::QueuedConnection);
        });
    };

    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_accessToken.isEmpty() && m_tokenExpireTime > now)
    {
        calls();
        return ;
    }


    QJsonObject payload;
    payload["appId"] = m_info->appid;
    payload["clientSecret"] = m_info->secret;
    Post("https://api.bot.qq.com/app/getAppAccessToken",payload,QString(),10000,[this,calls](const QString &resp,QNetworkReply::NetworkError neterr){
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8(), &err);

        if (err.error != QJsonParseError::NoError) {
            m_isConnecting = false;
            scheduleReconnect(10);
            AppendEventLog("解析 token 响应失败(无法获取AccessToken): " + err.errorString()+"\n返回内容："+resp,0xff);
            m_info->err+="解析 token 响应失败(无法获取AccessToken): " + err.errorString()+"\n返回内容："+resp;
            return ;
        }

        QJsonObject obj = doc.object();
        QString newToken = obj.value("access_token").toString();
        if (newToken.isEmpty()) {
            QString errMsg = obj.value("message").toString();
            if (errMsg.isEmpty())
                errMsg = "appid 或 secret 错误（无具体返回信息）";
            AppendEventLog("获取 token 失败: " + errMsg,0xff);
            m_info->err+="获取 token 失败: " + errMsg;
            m_isConnecting = false;
            scheduleReconnect(10);
            return ;
        }

        m_accessToken = newToken;
        m_gatewayFailStreak = 0;      // 换了新 token，连续失败计数从零开始
        //qDebug() << m_info->appid<<"|" << m_accessToken;
        // QQ 有时候把 expires_in 返回成数字、有时候是字符串：toString() 对数字给空串，
        // 再 toInt() 就是 0 → 过期时间算成「已过期 50 秒」→ 之后每 30 秒刷一次 token，
        // 白白撞 getAppAccessToken 的频率限制。这里两种都吃，解析不出来就按 7200 秒算。
        int expiresIn = obj.value("expires_in").toVariant().toInt();
        if (expiresIn <= 0)
            expiresIn = 7200;
        qint64 now = QDateTime::currentSecsSinceEpoch();
        m_tokenExpireTime = now + expiresIn-50;
        AppendEventLog(QString("%1 Token 刷新->%2")
                           .arg(m_info->nickname,QDateTime::fromSecsSinceEpoch(m_tokenExpireTime).toString()), Qt::darkGreen);
        calls();
    });



}

void QQBotClient::stop()
{

    m_invalidHeartbeatCount = 0;
    m_seq = 0;
    m_sessionId.clear();
    // ⚠ 这里原来是 `m_reconnectAttempts = 10;`（历史遗留，本意是"手动停了就别再排期"，
    //   那是靠计数拦的旧逻辑）。现在 scheduleReconnect 用计数算退避：
    //     3s << (attempts-1)  →  attempts=11 时是 3<<10=3072 秒 → 被 15 分钟封顶吃掉
    //   也就是说：**只要中途调过一次 stop()，下一次断线的"第一次重连"就不是 3 秒，而是 15 分钟**。
    //   而手动点登录按钮会把它清零，所以点一下就上去 —— 又一个"没重置"。
    //   停止就是停止，退避计数归零，下次重新从 3 秒开始。
    m_reconnectAttempts = 0;
    m_reconnectOverflow = 0;
    // 先把待触发的重连定时器停掉再把 autoConnect 置 false：
    // 现在 scheduleReconnect 不限次数了，光靠 m_reconnectAttempts 已经拦不住，
    // 而这个函数在 accinfo()==-1 时还会提前 return（那时 autoConnect 都还没改）。
    m_reconnectTimer.stop();
    m_handshakeTimer.stop();
    m_userWantsOnline = false;     // 用户/上层明确要停 → 之后不许再自动连回来
    int appid = m_info ->appid_int;
    if(accinfo(appid)==-1) return;

    m_info->online = false;
    m_info->autoConnect=false;
    m_isConnecting = false;
    if(m_info->type==0){
        stopHeartbeatTimer();
        resetReconnectAttempts();
        if (m_webSocket.state() == QAbstractSocket::ConnectedState)
            m_webSocket.close();
    }


    AppendEventLog(QString("机器人 %1 已停止").arg(m_info->appid),0xff);
}

// ---------- 网络请求 ----------
// 拉 WebSocket 网关地址。
//
// ⚠ 2026-11-01 改（原实现的问题）：
//   ① 只看 JSON 解析结果，网络错误 / HTTP 状态码 / 响应体全被丢掉 —— 腾讯网关在
//      401/429/5xx 时经常返回**空 body**，QJsonDocument 对空串固定报 "illegal value"，
//      于是所有失败都变成同一句「获取登录Ws错误: illegal value」，真正原因无从查起。
//   ② 服务端已经把 token 作废时，本地 m_tokenExpireTime 还没到，不清 token，
//      下一次重连还是拿同一个死 token 去撞 —— 撞满 5 次就永久停连（重启才好）。
//
// 现在按三类分别处理，并让日志能自证：
//   · 没收到任何 HTTP 响应（status=0）→ 网络不通，报 errorString；
//   · 401/403 → token 已失效，直接作废本地 token，下一轮 start() 必然重新取；
//   · 其它非 2xx / 非 JSON → 退避重试；连续 3 次还不行也作废本地 token 换一条路走。
// 注意：回调在 NetManager 的线程池线程里，所有状态改动都投回主线程执行。
void QQBotClient::fetchGatewayUrl(Callback calls)
{
    QString url;
    if(m_info->sandbox)
        url = "https://sandbox.api.bot.qq.com/gateway";
    else
        url = "https://api.bot.qq.com/gateway";

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    headers.insert("Content-Type", "application/json");

    NetManager::instance()->getAsyncEx(url, headers, 10000,
        [this, calls, url](const NetReplyInfo &r) {

        QMetaObject::invokeMethod(this, [this, calls, r, url]() {
            // 响应体只留前 160 字进日志：够定位（"{"code":11244,...}" / 一段 nginx 报错页），
            // 又不会把日志页刷爆。
            const QString head = r.body.left(160).simplified();

            auto fail = [&](const QString &why, bool killToken) {
                if (killToken) {
                    m_accessToken.clear();
                    m_tokenExpireTime = 0;      // 过期时间一起清，否则会残留一个"还没过期"的假象
                }
                m_gatewayFailStreak++;
                // 连续 3 次拿不到网关：多半不是网络抖动，而是本地这条 token 已经被服务端作废
                // （QQ 侧吊销 / 重置 secret / 另有一份程序用同一个 appid 取了新 token）。
                // 此时必须作废本地 token，否则每轮重连都是同一个死 token，永远恢复不了。
                if (!killToken && m_gatewayFailStreak >= 3 && !m_accessToken.isEmpty()) {
                    m_accessToken.clear();
                    m_tokenExpireTime = 0;
                    AppendEventLog(QString("%1 连续 %2 次拿不到网关地址，已作废本地 token，下一轮重新获取")
                                       .arg(m_info->nickname).arg(m_gatewayFailStreak), 0xff);
                }
                // 连 HTTP 响应都没有（连接被重置 / TLS 断 / 超时）或者收到空 body：
                // QNAM 池里那条 keep-alive / h2 连接可能已经废了，后面每个请求都复用它。
                // 不清掉的话换多少 token 都是同一个结果（这也是"重启程序就好"的原因）。
                //   第 1 次：只断空闲连接（轻）；
                //   第 2 次起：整套 QNAM 换掉 —— 等价于重启那一步，但不重启程序。
                if (r.httpStatus == 0 || r.body.trimmed().isEmpty()) {
                    if (m_gatewayFailStreak >= 2)
                        NetManager::instance()->resetHostPool(url);
                    else
                        NetManager::instance()->clearConnectionCache(url);
                }
                const QString msg = QString("%1（状态码=%2, 网络错误=%3%4）%5")
                                        .arg(why)
                                        .arg(r.httpStatus)
                                        .arg(int(r.error))
                                        .arg(r.errorString.isEmpty() ? QString()
                                                                     : ", " + r.errorString)
                                        .arg(head.isEmpty() ? QString() : " | 返回：" + head);
                AppendEventLog(msg, 0xff);
                m_info->err += msg + "\n";
                calls(QString(), QNetworkReply::NetworkError(QNetworkReply::ProtocolFailure));
            };

            // ① 鉴权被拒：token 已失效。必须换 token，否则重连多少次都是这一条。
            if (r.httpStatus == 401 || r.httpStatus == 403) {
                fail("获取登录Ws失败: token 已失效(401/403)", true);
                return;
            }
            // ② 连 HTTP 响应都没拿到：DNS / TLS / 连接被拒 / 10 秒超时被 abort
            if (r.httpStatus == 0) {
                fail("获取登录Ws失败: 网络不通", false);
                return;
            }
            // ③ 服务端抽风（429 限流 / 5xx）。退避重试即可，不要动 token。
            if (r.httpStatus < 200 || r.httpStatus >= 300) {
                fail(QString("获取登录Ws失败: 服务端返回 %1").arg(r.httpStatus), false);
                return;
            }

            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(r.body.toUtf8(), &err);
            if (err.error != QJsonParseError::NoError) {
                // 200 但 body 不是 JSON（空 body / 被中间层插了 HTML）
                fail("获取登录Ws失败: 响应不是 JSON(" + err.errorString() + ")", false);
                return;
            }

            QJsonObject obj = doc.object();
            QString wsUrl = obj.value("url").toString();
            if (wsUrl.isEmpty()) {
                QString errMsg = obj.value("message").toString();
                if (errMsg.contains("token", Qt::CaseInsensitive)) {
                    fail("获取登录Ws失败: token 疑似过期(" + errMsg + ")", true);
                    return;
                }
                if (errMsg.isEmpty())
                    errMsg = "未知错误，可能 token 无效或 appid 不正确";
                fail("获取ws地址失败: " + errMsg, false);
                return;
            }

            m_gatewayFailStreak = 0;      // 拿到地址了，连续失败计数清零
            calls(wsUrl, QNetworkReply::NetworkError());
        }, Qt::QueuedConnection);
    });
}

//主线程 专属
void QQBotClient::onRefreshReplyFinished()
{
    qint64 now = QDateTime::currentSecsSinceEpoch();

    if (!m_accessToken.isEmpty() && m_tokenExpireTime > now)
        return ;

    if (m_info->appid.isEmpty() || m_info->secret.isEmpty()) {
        AppendEventLog("缺少 appid 或 secret，无法获取 AccessToken",0xff);
        m_info->err+="缺少 appid 或 secret，无法获取 AccessToken\n";
        return ;
    }

    QJsonObject payload;
    payload["appId"] = m_info->appid;
    payload["clientSecret"] = m_info->secret;
    Post("https://api.bot.qq.com/app/getAppAccessToken",payload,QString(),10000,[this,now](const QString &resp,auto){
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8(), &err);

        if (err.error != QJsonParseError::NoError) {
            AppendEventLog("解析 token 响应失败: " + err.errorString()+"\n返回内容："+resp,0xff);
            m_info->err+="解析 token 响应失败: " + err.errorString()+"\n返回内容："+resp;
            return ;
        }

        QJsonObject obj = doc.object();
        QString newToken = obj.value("access_token").toString();
        if (newToken.isEmpty()) {
            QString errMsg = obj.value("message").toString();
            if (errMsg.isEmpty())
                errMsg = "appid 或 secret 错误（无具体返回信息）";
            AppendEventLog("获取 token 失败: " + errMsg,0xff);
            m_info->err+="获取 token 失败: " + errMsg;
            return ;
        }
        m_accessToken2=newToken;

        std::swap(m_accessToken2,m_accessToken); //防止多线程


        int expiresIn = obj.value("expires_in").toVariant().toInt();
        if (expiresIn <= 0)
            expiresIn = 7200;          // 解析不出来按标准有效期算，别算成"已过期"
        m_tokenExpireTime = now + expiresIn-50;
        AppendEventLog(QString("%1 Token 刷新->%2")
                           .arg(m_info->nickname,QDateTime::fromSecsSinceEpoch(m_tokenExpireTime).toString()), Qt::darkGreen);
        return ;
    });


}
// ---------- WebSocket 事件 ----------
void QQBotClient::onConnected()
{
    m_handshakeTimer.stop();          // 握手成功，看门狗撤掉
    // 每次连上都是全新的 identify（没有走 resume），上一次会话的 session_id / seq
    // 必须清掉：否则心跳会带着上一轮的旧序号（对服务端来说是个"未来序号"），
    // 很容易被判成非法会话（op=9）→ 刚连上就被踢。
    m_sessionId.clear();
    m_seq = 0;
    m_invalidHeartbeatCount = 0;
    sendIdentify();
}

void QQBotClient::onDisconnected()
{
    // 自己 abort 掉的旧 socket（start() 里换连接时）：不排重连，由调用方决定
    if (m_suppressDisconnect)
        return;

    m_handshakeTimer.stop();

    // 「对面因错误断开」的情况要看 closeCode，否则日志里只有一句"已断开"，永远查不清原因。
    // 1000=正常关闭；1005/1006=没带 code（对端直接断 TCP，这也是 30 分钟强断的常见形态）。
    // 4000 段是 QG 协议自己的错误码，见 https://bot.q.qq.com/wiki/develop/api-v2/dev-prepare/error-trace/websocket.html
    const int code = m_webSocket.closeCode();
    const QString reason = m_webSocket.closeReason();
    QString tip;
    switch (code) {
    case 4003:   // 未鉴权
    case 4004:   // 鉴权失败（token 无效/过期）
        m_accessToken.clear();          // 作废本地 token，下一轮 start() 重新取
        m_tokenExpireTime = 0;          // 过期时间一起清，别留下"还没过期"的假象
        tip = " | 鉴权失败，已作废本地 token";
        break;
    case 4006:   // 会话失效
    case 4009:   // 会话超时
        m_sessionId.clear();
        m_seq = 0;
        tip = " | 会话失效，下一轮重新握手";
        break;
    case 4008:   // 限流
        tip = " | 被服务端限流";
        break;
    default:
        break;
    }
    QString closeInfo;
    if (code > 0)
        closeInfo = QString(" code=%1").arg(code);
    if (!reason.isEmpty())
        closeInfo += "（" + reason.left(80) + "）";

    AppendEventLog(m_info->nickname+"WebSocket 已断开(30分钟会强行断开一次)" + closeInfo + tip, 0xff);
    stopHeartbeatTimer();
    bool wasOnline = m_info->online;
    m_info->online = false;

    m_isConnecting = false;
    if (wasOnline)
        emit disconnected();
    // 4008 被限流时别贴着 3 秒硬撞，退一步
    scheduleReconnect(code == 4008 ? 10 : 3);
}

// 握手看门狗触发：15 秒内既没连上也没断开（或断开信号没来），说明这条连接已经废了。
// 没有这个兜底的话 m_isConnecting 会永远为 true，start() 每次第一行就 return，日志一片空白。
void QQBotClient::onHandshakeTimeout()
{
    if (m_info->online || !m_isConnecting)
        return;
    AppendEventLog(QString("%1 连接网关超时(15秒无响应)，丢掉这次连接重来").arg(m_info->nickname), 0xff);
    m_isConnecting = false;
    closeSocketSilently();
    scheduleReconnect(5);
}

// 丢掉当前 socket，并且不让 onDisconnected 再排一次重连
void QQBotClient::closeSocketSilently()
{
    if (m_webSocket.state() == QAbstractSocket::UnconnectedState)
        return;
    m_suppressDisconnect = true;
    m_webSocket.abort();
    // 标志用 0ms 定时器复位而不是就地复位：abort() 的 disconnected 信号如果比返回
    // 慢一拍（走事件队列），就地复位就漏拦了，会把正在握手的新连接搅掉。
    QTimer::singleShot(0, this, [this]() { m_suppressDisconnect = false; });
}

void QQBotClient::onError(QAbstractSocket::SocketError error)
{
    AppendEventLog("WebSocket 错误: " + m_webSocket.errorString(),0xff);
}
void Messages(AccountInfo *info, MessageEvent &ev);
int mapTypeToTabIndex(int type);

static QString escapeTagValue(const QString &v)
{
    if (!v.contains(QLatin1Char(']'))) return v;   // 绝大多数消息走这条，不做多余拷贝
    QString s = v;
    s.replace(QLatin1Char(']'), QLatin1String("%5D"));
    return s;
}

void tiqfuj(const QJsonObject &d ,QString &msg){
    QString extraInfo;

    const QJsonArray attachments = d["attachments"].toArray();


    int i = 0;
    for (const QJsonValue &attVal : attachments) {
        QJsonObject att = attVal.toObject();
        QString contentType = att["content_type"].toString();
        QString filename = escapeTagValue(att["filename"].toString());   // 文件名可能带 ']'
        int size = att["size"].toDouble();
        QString url = att["url"].toString();

        // 构建前缀部分（不变）
        QString prefix,asr_refer_text;
        if (contentType.contains("image")) {
            int height = att["height"].toDouble();
            int width = att["width"].toDouble();
            prefix = QString("[image,height=%1,width=%2,name=").arg(height).arg(width);
        } else if (contentType.contains("file")) {
            prefix = "[file,name=";
        } else if (contentType.contains("voice")) {
            asr_refer_text = escapeTagValue(att["asr_refer_text"].toString());   // 识别文本同样可能带 ']'
            QString url2 = att["voice_wav_url"].toString();
            if(!url2.isEmpty())
            {
                url = url2;
            }
            prefix = "[audio,name=";
        } else if (contentType.contains("video")) {
            prefix = "[video,name=";
        } else {
            prefix = "[unknown,name=";
        }

        // ---- 高效提取 ext 中的 text ----
        QString text;
        // 构建当前 i 对应的标记前缀（固定格式，无需正则）
        QString markerPrefix = QString("<faceType=6,faceId=\"%1\",ext=\"").arg(i);
        int startPos = msg.indexOf(markerPrefix);
        if (startPos != -1) {
            int extStart = startPos + markerPrefix.length();           // ext 值的起始位置
            int extEnd = msg.indexOf("\">", extStart);             // 找到结束的 ">
            if (extEnd != -1) {
                QString extBase64 = msg.mid(extStart, extEnd - extStart);
                QByteArray decoded = QByteArray::fromBase64(extBase64.toUtf8());
                QJsonDocument doc = QJsonDocument::fromJson(decoded);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    text = escapeTagValue(obj["text"].toString());   // 表情说明同样可能带 ']'
                }


                QString extraInfo = prefix + filename +
                                    QString(",type=%1,text=%2,size=%3,url=%4]")
                                        .arg(contentType,text)
                                        .arg(size)
                                        .arg(url);


                msg.replace(startPos, extEnd + 2 - startPos, extraInfo);
                i++;  // 只有成功替换后才递增
            }else{
                QString extraInfo = prefix + filename +
                                    QString(",type=%1,size=%2,url=%3]")
                                        .arg(contentType)
                                        .arg(size)
                                        .arg(url);
                msg+=extraInfo;
            }
        }else{
            if(!asr_refer_text.isEmpty())
            {
                QString extraInfo = prefix + filename +
                                    QString(",type=%1,size=%2,text=%3,url=%4]")
                                        .arg(contentType)
                                        .arg(size).arg(asr_refer_text,url);

                msg+=extraInfo;
            }else{
                QString extraInfo = prefix + filename +
                                    QString(",type=%1,size=%2,url=%3]")
                                        .arg(contentType)
                                        .arg(size)
                                        .arg(url);
                msg+=extraInfo;
            }

        }
        // 如果未找到，则不递增 i（与原逻辑一致）
    }

}

#include <charconv>   // C++17

// 转义 JSON 字符串（返回转义后的内容，不含外层引号）
QString escapeJson(const QString& str) {
    QString escaped;
    escaped.reserve(str.size() * 2);
    for (QChar ch : str) {
        switch (ch.unicode()) {
        case '\\': escaped.append("\\\\"); break;
        case '"':  escaped.append("\\\""); break;
        case '\b': escaped.append("\\b"); break;
        case '\f': escaped.append("\\f"); break;
        case '\n': escaped.append("\\n"); break;
        case '\r': escaped.append("\\r"); break;
        case '\t': escaped.append("\\t"); break;
        default:
            if (ch.unicode() < 0x20) {
                escaped.append("\\u").append(QString::number(ch.unicode(), 16).rightJustified(4, '0'));
            } else {
                escaped.append(ch);
            }
            break;
        }
    }
    return escaped;
}

// 将整数值直接追加到 QString（零堆分配）
template<typename T>
void appendInt(QString& out, T value) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec == std::errc()) {
        out.append(QLatin1String(buf, static_cast<int>(ptr - buf)));
    } else {
        out.append(QString::number(value)); // fallback（实际不会发生）
    }
}

static constexpr qint64 kSignDefaultPoints = 1000;        // 非 py 模式：每天 +1000
static constexpr qint64 kSignMaxPoints     = 100000;   // 脚本返回值的上限（防手抖写成天文数字）

bool QQBotClient::handleSignIn(const MessageEvent &ev)
{
    // ---- 1) 配置 ----
    QString cfg = m_info->qiandao;
    const bool isPy = cfg.trimmed().startsWith(QLatin1String("#python"));
    // 空配置 = 静默模式：一天一次 +1000，但**一个字都不回**（连"今天已经签过"也不回）。
    // 要说话就在群管「签到」里填文案，或写 #python 脚本。
    const bool silent = cfg.trimmed().isEmpty();

    // ---- 2) openid → 16 字节（长度不对就别签，免得给错人加分）----
    const QByteArray openidBin = QByteArray::fromHex(ev.user.toUtf8());
    if (openidBin.size() < 8) {
        AppendEventLog("[签到] 事件里没有合法用户 openid，忽略（事件 " + ev.msgId + "）", 0xff);
        return false;
    }
    BotDB *db = g_botdb.value(ev.appid, nullptr);
    if (!db) {

        return true;
    }

    // ---- 3) 现状 ----
    SignRecord rec;                       // botdb.h 里的全局结构（同 UserRecord / GroupRecord2）
    db->getSignRecord(openidBin, rec);
    const qint64 today = QDate::currentDate().toJulianDay();
    const bool signedToday = (rec.day == quint32(today));
    const qint64 points = db->getPoints(openidBin);

    // 静默模式下今天已经签过：既不回复也不加分，直接走人
    if (silent && signedToday) return true;

    // ---- 4) 本次加多少 ----
    qint64 gain = 0;
    QString reply;                      // 脚本自定义文案（可空）
    if (isPy) {
        bool ok = false;
        gain = python_sign_code(cfg, ev, signedToday, int(rec.total), int(rec.streak),
                                points, &reply, &ok);
        if (!ok) {                      // 脚本出错：不加币、不记签到（详细报错已进日志，别刷群里）
            QString text = QStringLiteral("签到失败：签到脚本执行出错，请联系管理员查看日志");
            send_msgAsync(ev.type, ev.groupId, "[签到]", text, ev.msgId);
            return true;
        }
        if (gain > kSignMaxPoints) gain = kSignMaxPoints;
    } else {
        gain = signedToday ? 0 : kSignDefaultPoints;   // 普通文本：一天一次 +1000
    }

    // ---- 5) 加币 + 记签到（只有 gain > 0 才算签到成功）----
    qint64 balance = points;
    if (gain > 0) {
        bool addOk = false;
        const qint64 after = db->addPoints(openidBin, gain, &addOk);
        if (!addOk) {
            AppendEventLog("[签到] 积分写入失败：" + ev.user, 0xff);
            if (!silent) {
                QString text = QStringLiteral("签到失败：积分写入失败，请稍后再试");
                send_msgAsync(ev.type, ev.groupId, "[签到]", text, ev.msgId);
            }
            return true;
        }
        balance = after;

        if (!signedToday) {                              // 今天第一次才算一次签到
            const bool cont = (rec.day + 1u == quint32(today));   // 昨天签过 = 连续
            rec.day    = quint32(today);
            rec.total  = rec.total + 1;
            rec.streak = cont ? rec.streak + 1 : 1;
            db->putSignRecord(openidBin, rec);
        }
    }

    // ---- 6) 回复 ----
    if (silent) return true;          // 静默模式：加币已经做完了，一个字都不回

    QString text = isPy ? reply : (signedToday ? QString() : cfg);
    if (text.trimmed().isEmpty()) {
        if (gain > 0)         text = QStringLiteral("签到成功：+[获得余额]  总铃铛数：[余额]");
        else if (signedToday) text = QStringLiteral("今天已经签到过了，总铃铛数：[余额]");
        else                  text = QStringLiteral("签到未成功，总铃铛数：[余额]");
    }
    text.replace("[获得余额]", QString::number(gain));      // 本次获得
    text.replace("{获得余额}", QString::number(gain));
    text.replace("[余额]",     QString::number(balance));   // 总铃铛数
    text.replace("{余额}",     QString::number(balance));

    send_msgAsync(ev.type, ev.groupId, "[签到]", text, ev.msgId);
    return true;
}

//在编译状态下 每次执行 耗时0.1ms 100us 测试包括上一级解析耗时 所以当你卡顿时 真的不是这里原因 理论秒 1w次
void QQBotClient::parseMessageEvent(QJsonObject &payload,const QString &text)
{
    MessageEvent ev;
    ev.raw = text;
    ev.seq = payload.value("s").toVariant().toLongLong();
    ev.msgType = payload.value("t").toString();
    QJsonObject d = payload.value("d").toObject();

    ev.groupId = d.value("group_openid").toString();
    // 默认值
    ev.type = -1;
    ev.subType = 0;
    ev.fullType = 0;
    ev.callbackType = 0;
    ev.appid = m_info->appid_int;
    QJsonObject obj2 = d.value("message_scene").toObject();
    QJsonArray arr= obj2["ext"].toArray();
    if (!arr.isEmpty()) {
        ev.replyTo = arr[0].toString();
        ev.replyTo = "[ref," + ev.replyTo + "]";   // 即使是空字符串也会变成 "[ref,]"
    }

    // ========== 1. 消息类事件（已有） ==========
    if (ev.msgType == "GROUP_AT_MESSAGE_CREATE" || ev.msgType == "GROUP_MESSAGE_CREATE") {
        ev.type = 0;   // 群
        ev.fullType = (ev.msgType == "GROUP_MESSAGE_CREATE");

        QJsonObject author = d.value("author").toObject();
        ev.user = author.value("union_openid").toString();
        if (ev.user.isEmpty()) ev.user = author.value("id").toString();
        QString role= author.value("member_role").toString();
        if(role == "owner")
            ev.member_role = 3;
        else if(role=="admin")
            ev.member_role = 4;
        else
            ev.member_role = 6;
        ev.nickname = author.value("username").toString();
        ev.bot = author.value("bot").toBool();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();

        const QJsonArray array = d["mentions"].toArray();
        if(m_info->unid.isEmpty()){ //获取部分机器没有的unid
            for (const QJsonValue &a : array)
            {
                if(a.isObject())
                {
                    QJsonObject arronj = a.toObject();
                    if(!arronj["is_you"].toBool()) continue;
                    ev.at_you = true;
                    ev.bot_admin = (arronj["member_role"].toString() == "admin");
                    m_info->unid= arronj["id"].toString();
                }
                break;
            }
        }

        if(!m_info->unid.isEmpty() && ev.msg.contains(m_info->unid))
        {
            ev.at_you = true;
            ev.msg.remove("<@"+m_info->unid+">");

            for (const QJsonValue &a : array)//从这里获取机器人是不是管理员
            {
                if(a.isObject())
                {
                    QJsonObject arronj = a.toObject();
                    if(!arronj["is_you"].toBool()) continue;
                    ev.at_you = true;
                    ev.bot_admin = (arronj["member_role"].toString() == "admin");
                    m_info->unid= arronj["id"].toString();
                }
                break;
            }
        }

    }
    else if (ev.msgType == "C2C_MESSAGE_CREATE") {
        ev.type = 2;   // 私聊
        QJsonObject author = d.value("author").toObject();
        ev.user = author.value("union_openid").toString();
        if (ev.user.isEmpty()) ev.user = author.value("id").toString();
        ev.groupId = ev.user;
        ev.nickname = author.value("username").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();

        ev.member_role = 3;
        ev.at_you=true;
    }
    else if (ev.msgType == "AT_MESSAGE_CREATE" || ev.msgType == "MESSAGE_CREATE") {
        ev.type = 1;   // 频道
        ev.fullType = (ev.msgType == "MESSAGE_CREATE");
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        QJsonObject author = d.value("author").toObject();
        ev.user = author.value("union_openid").toString();
        if (ev.user.isEmpty()) ev.user = author.value("id").toString();
        ev.nickname = author.value("username").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();
        ev.member_role = 6;
        if(ev.msg.contains(m_info->pduid))
        {
            ev.at_you = true;
            ev.msg.remove("<@!"+m_info->pduid+">");

        }
    }
    else if (ev.msgType == "DIRECT_MESSAGE_CREATE") {
        ev.type = 3;   // 频道私聊
        ev.guildId = d.value("guild_id").toString();

        QJsonObject author = d.value("author").toObject();
        ev.user = author.value("union_openid").toString();
        ev.groupId = d.value("guild_id").toString();
        if (ev.user.isEmpty()) ev.user = ev.groupId;
        ev.nickname = author.value("username").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();
        ev.at_you=true;
    }
    // ========== 2. 群/好友管理事件 ==========
    else if (ev.msgType == "GROUP_ADD_ROBOT") {//被邀请进新群
        ev.type = 0; ev.subType = 4;

        ev.user = d.value("scene_param").toString();
        if (ev.user.isEmpty()) ev.user = d.value("op_member_openid").toString();
        ev.msg = "机器人被邀请加群";
        m_info->今日加群数量++;
    }
    else if (ev.msgType == "GROUP_DEL_ROBOT") { //被踢出群
        ev.type = 0; ev.subType = 5;
        ev.user = d.value("op_member_openid").toString();
        ev.msg = "机器人被移除出群聊";
        m_info->今日退群数量++;
    }
    else if (ev.msgType == "FRIEND_ADD") { //好友增加
        ev.type = 2; ev.subType = 6;
        ev.msg = "机器人被添加好友";
        m_info->今日好友数量++;

    }
    else if (ev.msgType == "FRIEND_DEL") { //好友删除
        ev.type = 2; ev.subType = 7;

        ev.msg = "机器人被删除好友";
        m_info->今日删除好友数量++;
    }
    else if (ev.msgType == "C2C_MSG_REJECT") {
        ev.type = 5; ev.subType = 8;
        ev.user = d.value("openid").toString();
        ev.msg="私聊主动消息被拒绝";
    }
    else if (ev.msgType == "C2C_MSG_RECEIVE") {
        ev.type = 5; ev.subType = 9;
        ev.user = d.value("openid").toString();
        ev.msg="私聊主动消息被同意";
    }

    // ========== 3. 频道 Guild 事件 ==========
    else if (ev.msgType == "GUILD_CREATE") {
        ev.type = 11; ev.subType = 1;
        ev.guildId = d.value("id").toString();
        ev.user = d.value("op_user_id").toString();
        ev.msg = d.value("name").toString();
        m_info->今日频道数量++;
    }
    else if (ev.msgType == "GUILD_UPDATE") {
        ev.type = 11; ev.subType = 2;
        ev.guildId = d.value("id").toString();
        ev.msg = d.value("name").toString();
    }
    else if (ev.msgType == "GUILD_DELETE") {
        ev.type = 11; ev.subType = 3;
        ev.guildId = d.value("id").toString();
        ev.user = d.value("op_user_id").toString();
        m_info->今日退出频道数量++;
    }
    // ========== 4. 子频道事件 ==========
    else if (ev.msgType == "CHANNEL_CREATE") {
        ev.type = 12; ev.subType = 1;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("id").toString();
        ev.msg = d.value("name").toString();
    }
    else if (ev.msgType == "CHANNEL_UPDATE") {
        ev.type = 12; ev.subType = 2;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("id").toString();
        ev.msg = d.value("name").toString();
    }
    else if (ev.msgType == "CHANNEL_DELETE") {
        ev.type = 12; ev.subType = 3;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("id").toString();
    }
    // ========== 5. 频道成员事件 ==========
    else if (ev.msgType == "GUILD_MEMBER_ADD") {
        ev.type = 13; ev.subType = 1;
        ev.guildId = d.value("guild_id").toString();
        ev.user = d.value("op_user_id").toString();
    }
    else if (ev.msgType == "GUILD_MEMBER_UPDATE") {
        ev.type = 13; ev.subType = 2;
        ev.guildId = d.value("guild_id").toString();
        ev.user = d.value("op_user_id").toString();
        ev.nickname = d.value("nick").toString();
    }
    else if (ev.msgType == "GUILD_MEMBER_REMOVE") {
        ev.type = 13; ev.subType = 3;
        ev.guildId = d.value("guild_id").toString();
        ev.user = d.value("op_user_id").toString();
    }

    // ========== 8. 互动回调事件 ==========
    else if (ev.msgType == "INTERACTION_CREATE") {
        ev.subType = 1;
        ev.callbackId = d.value("id").toString();
        ev.callbackType = d.value("chat_type").toInt();
        QJsonObject resolved = d.value("data").toObject().value("resolved").toObject();
        ev.msg = resolved.value("button_data").toString();
        int type = d.value("type").toInt(); //13为点赞

        if (ev.callbackType == 0)
        {
            ev.type = 1;
            ev.guildId = d.value("guild_id").toString();
            ev.groupId = d.value("channel_id").toString();
            ev.user = resolved.value("user_id").toString();
        }else if (ev.callbackType == 1) {
            ev.type = 0;

            ev.user = d.value("group_member_openid").toString();
        } else if (ev.callbackType == 2){
            ev.type = 2;
            ev.groupId = d.value("union_openid").toString();
            if(ev.groupId.isEmpty())
                ev.groupId = d.value("user_openid").toString();
            ev.user = ev.groupId;
        } //频道私聊没按钮
        if(type==13)
        {
            QString feedback_opt = resolved.value("feedback_opt").toString();
            if(feedback_opt=="REGENERATE")
            {
                int logindex = ev.msg.toInt();
                if(logindex >0)
                {
                    Message msg;
                    g_logdb[ev.type+1]->readLog(QString::number(ev.appid),ev.groupId,logindex,msg);
                    ev.msg = msg.msg;
                    ev.at_you = true;
                }
            }
            int checked =(resolved.value("checked").toInt()==1);
            if(feedback_opt=="LIKE")
            {
                ev.msg =QString("%1:%2").arg(checked?"消息点赞👍":"取消点赞👍",ev.msg) ;
            }
            if(feedback_opt=="UNLIKE")
            {
                ev.msg =QString("%1:%2").arg(checked?"消息差评👎":"取消差评👎",ev.msg) ;
            }
        }
    }
    /*

    else if (ev.msgType == "GROUP_MSG_REJECT") {
        ev.type = 4; ev.subType = 10;

        ev.user = d.value("op_member_openid").toString();
        ev.msg="群主动消息被拒绝(已弃用)";
    }
    else if (ev.msgType == "GROUP_MSG_RECEIVE") {
        ev.type = 4; ev.subType = 11;

        ev.user = d.value("op_member_openid").toString();
        ev.msg="群主动消息被同意(已弃用)";
    }
     // ========== 6. 消息删除事件 ==========
    else if (ev.msgType == "MESSAGE_DELETE") {
        ev.type = 14; ev.subType = 1;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.msgId = payload.value("id").toString();
        ev.user = d.value("op_user_id").toString();
    }
    else if (ev.msgType == "PUBLIC_MESSAGE_DELETE") {
        ev.type = 14; ev.subType = 2;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.msgId = payload.value("id").toString();
        ev.user = d.value("op_user_id").toString();
    }
    else if (ev.msgType == "DIRECT_MESSAGE_DELETE") {
        ev.type = 14; ev.subType = 3;
        ev.guildId = d.value("guild_id").toString();
        ev.msgId = payload.value("id").toString();
    }
    // ========== 7. 表情表态事件 ==========
    else if (ev.msgType == "MESSAGE_REACTION_ADD") {
        ev.type = 15; ev.subType = 1;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.msgId = d.value("message_id").toString();
        ev.user = d.value("user_id").toString();
        ev.msg = d.value("emoji").toObject().value("name").toString();
    }
    else if (ev.msgType == "MESSAGE_REACTION_REMOVE") {
        ev.type = 15; ev.subType = 2;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.msgId = d.value("message_id").toString();
        ev.user = d.value("user_id").toString();
        ev.msg = d.value("emoji").toObject().value("name").toString();
    }
    // ========== 9. 消息审核事件 ==========
    else if (ev.msgType == "MESSAGE_AUDIT_PASS") {
        ev.type = 8; ev.subType = 1;
        ev.msgId = d.value("message_id").toString();
    }
    else if (ev.msgType == "MESSAGE_AUDIT_REJECT") {
        ev.type = 8; ev.subType = 2;
        ev.msgId = d.value("message_id").toString();
        ev.msg = d.value("reason").toString();
    }
    // ========== 10. 论坛事件 ==========
    else if (ev.msgType == "FORUM_THREAD_CREATE") {
        ev.type = 16; ev.subType = 1;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("author_id").toString();
        ev.msg = d.value("thread_name").toString();
    }
    else if (ev.msgType == "FORUM_THREAD_UPDATE") {
        ev.type = 16; ev.subType = 2;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("author_id").toString();
    }
    else if (ev.msgType == "FORUM_THREAD_DELETE") {
        ev.type = 16; ev.subType = 3;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
    }
    else if (ev.msgType == "FORUM_POST_CREATE") {
        ev.type = 16; ev.subType = 4;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("author_id").toString();
        ev.msg = d.value("content").toString();
    }
    else if (ev.msgType == "FORUM_POST_DELETE") {
        ev.type = 16; ev.subType = 5;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
    }
    else if (ev.msgType == "FORUM_REPLY_CREATE") {
        ev.type = 16; ev.subType = 6;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("author_id").toString();
        ev.msg = d.value("content").toString();
    }
    else if (ev.msgType == "FORUM_REPLY_DELETE") {
        ev.type = 16; ev.subType = 7;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
    }
    else if (ev.msgType == "FORUM_PUBLISH_AUDIT_RESULT") {
        ev.type = 16; ev.subType = 8;
        ev.msgId = d.value("publish_id").toString();
        ev.msg = d.value("result").toString();
    }

    // ========== 11. 音频事件 ==========
    else if (ev.msgType == "AUDIO_START") {
        ev.type = 17; ev.subType = 1;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("user_id").toString();
    }
    else if (ev.msgType == "AUDIO_FINISH") {
        ev.type = 17; ev.subType = 2;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("user_id").toString();
    }
    else if (ev.msgType == "AUDIO_ON_MIC") {
        ev.type = 17; ev.subType = 3;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("user_id").toString();
    }
    else if (ev.msgType == "AUDIO_OFF_MIC") {
        ev.type = 17; ev.subType = 4;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.user = d.value("user_id").toString();
    }
    */
    else if (ev.msgType == "GROUP_MEMBER_ADD") {
        ev.type = 0; ev.subType = 2;
        ev.msg = "该成员加入本群...";
    }
    else if (ev.msgType == "GROUP_MEMBER_REMOVE") {
        ev.type = 0; ev.subType = 3;
        ev.msg = "群成员退群...";
    }
    else if (ev.msgType == "GROUP_JOIN_REQUEST") {
        ev.type = 18; ev.subType = 0;
        ev.user = d.value("member_openid").toString();
        ev.callbackId = d.value("join_request_id").toString();
        ev.nickname =d.value("username").toString();
        ev.user2 =d.value("invited_by").toString();
        if(!ev.user2.isEmpty())
            ev.subType = 1;
        QJsonObject o2=d["verify_info"].toObject();
        QJsonArray arr=o2["review_qa_list"].toArray();
        if(arr.size()>0)
        {
            QJsonObject o3 = arr.at(0).toObject();
            ev.extra ="问题："+ o3["question"].toString()+"\n答案："+o3["answer"].toString();
        }
        ev.msg ="【有人申请加群】 群：%1(%2) 用户:%3(%4)\n%5";
    }


    else {
        ev.type = 99;
        ev.extra = text;
        ev.msg="未解析事件:" + ev.msgType;
    }

    if(ev.msgId.isEmpty()) ev.msgId = payload.value("id").toString();
    if(ev.user.isEmpty()) ev.user = d.value("user_openid").toString();
    if(ev.groupId.isEmpty()) ev.groupId = ev.user;
    if(neizev(d,ev)) return; //真是代表被处理 没必要传递下去
    if(m_info->pbbot && ev.bot) return;

    if (ev.type == 0 && ev.subType == 2 && (ev.bitmap & BIT_RUQUN_YZ))
    {
        setGroupRestrictChatSetting(ev.groupId,ev.user,30*1440*60-1,[this,ev](const QString &resp,auto){
            if(resp=="{}")
            {
                auto *db = g_botdb[ev.appid];
                db->addJojiyzRecord(ev.groupId,ev.user_int);
                QString text = m_info->jojnhf;
                if(text.isEmpty())
                    text = QString("<@%1> 请在5分钟内点击下面按钮 完成验证 超时踢出").arg(ev.user);
                if(text.startsWith("#python")) text =python_code(text,ev);
                else{
                    text.replace("{ID}",ev.user);
                    text.replace("{群昵称}",ev.groupname);
                    if(text.contains("{头像}"))
                            text.replace("{头像}", QString(">![#24px #24px](https://thirdqq.qlogo.cn/qqapp/%1/%2/100)").arg(ev.appid).arg(ev.user));

                }
                text+=R"(#b:#{"keyboard":{"content":{"rows":[{"buttons":[{"action":{"data":"入群验证","permission":{"type":2},"type":1,"unsupport_tips":"不支持"},"id":"1","render_data":{"label":"点我验证","style":1,"visited_label":"点我验证"}}]},{"buttons":[{"action":{"data":"免验证 %1","permission":{"type":2},"type":2,"unsupport_tips":"不支持"},"id":"2","render_data":{"label":"免验证（需权限） ","style":1,"visited_label":"免验证"}}]}]}}}#b:#)";
                text=text.arg(ev.user);
                send_msgAsync(ev.type,ev.groupId,"[入群验证]",text,ev.msgId);
            }

        });

    }

    if(ev.msg=="广告积分")
    {
        QString text = "点击下发按钮查看广告 完成后可获得1000铃铛";
        if(ev.type==2){
            return;
            send_msgAsync(ev.type,ev.groupId,"[广告观看]",text,ev.msgId,ev.user,false,0,false,[this,ev](const QString &resp,auto){
                if(resp.contains("message")){
                    QString text = resp;
                    send_msgAsync(ev.type,ev.groupId,"[广告观看]",text,ev.msgId);
                }
            });
            return;
        }
        text = "当前广告积分 仅限私聊可用 请前往私聊 查看\n如果你仍然要查看群的 可以发送[广告积分2]()";
        send_msgAsync(ev.type,ev.groupId,"[广告观看]",text,ev.msgId);
        return ;
    }
    if(ev.msg=="广告积分2")
    {
        QString text = "点击下发按钮查看广告 完成后可获得1000铃铛";
        if(ev.type==2 || ev.type==0){
            send_msgAsync(ev.type,ev.groupId,"[广告观看]",text,ev.msgId,ev.user,false,0,false,[this,ev](const QString &resp,auto){
                if(resp.contains("message")){
                    QString text = resp;
                    send_msgAsync(ev.type,ev.groupId,"[广告观看]",text,ev.msgId);
                }
            });
            return;
        }
        text = "当前广告积分 仅限私聊可用 请前往私聊 查看\n如果你仍然要查看群的 可以发送[广告积分2]()";
        send_msgAsync(ev.type,ev.groupId,"[广告观看]",text,ev.msgId);
        return ;
    }

    if(ev.msg=="更新日志" && !m_info->gxrz.isEmpty())
    {
        send_msgAsync(ev.type,ev.groupId,"[私有指令]",m_info->gxrz,ev.msgId);
        return ;
    }
    // 签到：配置在「群管 → 签到」（普通文本 = 模板 +1000/天，或 #python 脚本自定义加多少）
    if(ev.msg=="签到") handleSignIn(ev);

    if(ev.msg=="菜单" && !m_info->caidan.isEmpty())
    {
        send_msgAsync(ev.type,ev.groupId,"[私有指令]",m_info->caidan,ev.msgId);
        return ;

    }else if(ev.msg =="帮助" || ev.msg =="help")
    {
        if(!m_info->help.isEmpty())
        {
            send_msgAsync(ev.type,ev.groupId,"[私有指令]",m_info->help,ev.msgId);
            return ;
        }
    }else if(ev.msg.isEmpty() && ev.at_you && !m_info->emptyAt.isEmpty())
    {
        send_msgAsync(ev.type,ev.groupId,"[私有指令]",m_info->emptyAt,ev.msgId);
        return ;
    }

    QString name=ev.nickname;
    if(强制审核昵称)
    {
        ev.nickname = ev.nickname2;
        if(ev.nickname.isEmpty()) ev.nickname = QString::number(ev.user_int);
    }
    if(ev.member_role==6){
        for (int i = 0; i < 20; ++i) { //循环20次 很快 不影响
            if (ev.qid[i] == ev.user_int) {
                ev.member_role =5;
                break;
            }
        }
    }

    d["content"] = ev.msg;
    d["id"] = ev.msgId;
    d["user_openid"] = ev.user;
    d["group_openid"]=ev.groupId;
    payload["d"] = d;
    payload["user_id"] = ev.user_int;
    payload["appid"]=ev.appid;

    payload["member_role"]=ev.member_role;
    payload["at_you"]=ev.at_you;
    payload["type"]=ev.type;
    payload["GroupName"]=ev.groupname;
    payload["username"]=ev.nickname2;
    ev.raw = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if(强制审核昵称)
    {
        QString oldname =escapeJson(name);
        if(!name.isEmpty())
            ev.raw.replace(oldname,escapeJson(ev.nickname));
    }
    if(logPage->wanzjson) logPage->onNewLogAdded(ev.raw);
    Messages(m_info, ev);
    if(m_info->autoht && ev.subType== 1 && ev.type<=3) respond_interaction(ev.callbackId,0);

    return ;//3736 ms
}


bool QQBotClient::neizev(const QJsonObject &d,MessageEvent &ev)
{
    if(!ev.fullType) ev.at_you=true; //

    if(m_info->admin.contains(ev.user)) ev.member_role=2;
    if(g_admin.contains(ev.user)) ev.member_role=1;

    tiqfuj(d,ev.msg);

    ev.user_int=-1;

    if (g_botdb.contains(ev.appid))
        ev.user_int = g_botdb [ev.appid]->getOrUpdateUser(this,ev);//先获取id  并且更新或读取id
    //qDebug() << "name2" << ev.nickname2;
    ev.bot_admin = ev.bitmap & BIT_ADMIN;
    int tabIndex= mapTypeToTabIndex(ev.type);
    ev.msg = ev.msg.trimmed();
    if (ev.msg.startsWith("/")) {
        ev.msg.remove(0, 1); // 从索引0开始，删除1个字符
    }
    // 每个事件对应的 msg（原 logMessageEvent，已内联到此处）
    if(ev.type == 18)
    {
        ev.msg = ev.msg.arg(ev.groupname,ev.groupId,ev.nickname).arg(ev.user_int).arg(ev.extra);
    }
    if(ev.type ==0)
    {
        if(ev.subType==5 || ev.subType==4){
            if(g_botdb.contains(ev.appid))
            {
                BotDB *db = g_botdb[ev.appid];
                if(ev.subType==4)
                {
                    QString name;
                    QString json =  get_groups_info(ev.groupId);
                    QJsonParseError err;
                    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
                    if (err.error == QJsonParseError::NoError) {
                        QJsonObject obj = doc.object();
                        name = obj["group_name"].toString();
                    }

                    db->addGroup(ev.groupId,QDateTime::currentSecsSinceEpoch()/60,ev.user_int,0,name);

                }
                else{
                    QMutexLocker locker(&chatPage->m_GMutex);
                    if(chatPage->全量群 .contains(ev.groupId))
                    {
                        chatPage->全量群.remove(ev.groupId);
                        QFile file("data/全量群.hash");
                        if (file.open(QIODevice::WriteOnly)) {
                            QDataStream out(&file);
                            out.setVersion(QDataStream::Qt_5_15);
                            out << chatPage->全量群;   // 直接序列化整个 QHash
                            file.close();
                        }
                    }
                    db->deleteGroup(ev.groupId);
                    tabIndex = 1;
                }
            }
        }
    }
    if(ev.type ==2)
    {
        if( ev.subType==6 || ev.subType==7){
            if(g_botdb.contains(ev.appid))
            {
                BotDB *db = g_botdb[ev.appid];
                if(ev.subType==6){
                    db->addFriend(ev.user_int,QDateTime::currentSecsSinceEpoch()/60);
                }
                else{
                    db->removeFriend(ev.user_int);
                    tabIndex = 3;
                }
            }
        }
    }
    QString tiems = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    Message mes{ev.user,ev.msg,false,tiems,"",ev.replyTo,ev.msgId};
    mes.Gname =ev.groupname;
    if(ev.type<4)
    {
        const auto& msg_array = d["msg_elements"].toArray();
        if (msg_array.size() > 0) {
            const QJsonObject &first_element = msg_array.at(0).toObject();
            if (!first_element.isEmpty()) {  // 确认是有效对象
                const auto& author_obj = first_element["author"].toObject();
                if (!author_obj.isEmpty()) {  // 确认 author 是对象且非空
                    mes.ref_name = author_obj["username"].toString();
                } else {
                    mes.ref_name.clear(); // 无 author 时清空，避免脏数据
                }
                mes.ref_msg = first_element["content"].toString(); // content 缺失则为空字符串
                tiqfuj(first_element,mes.ref_msg);
            }
        }
    }

    mes.Color_0 = Color_1;
    mes.name = QString("%1(%2)").arg(ev.nickname).arg(ev.user_int);

    ev.log = g_logdb[tabIndex] ->appendLog(m_info->appid,ev.groupId,mes);
    ev.msgId=  QStringLiteral("|%1|%2").arg(ev.log).arg(ev.msgId);

    mes.seq = ev.log;

    logPage->onNewLogAdded(tabIndex,ev.log,m_info->appid_int,ev.groupId,mes);
    if(ws_server) ws_server->broadcastMessage(mes,ev.appid,ev.type,ev.groupId);

    if(ev.type<=3)
    {

        if(ev.fullType && ev.type==0)
        {
            chatPage->addContact(0,ev,mes.name); //为对话聊天增加 新成员
        }
        else
        {
            chatPage->addContact(tabIndex,ev,mes.name); //为对话聊天增加 新成员
        }
    }

    if(ev.type==0 && ev.subType ==4  ){
        if(!m_info->welcomeMsg.isEmpty()){
            send_msgAsync(ev.type,ev.groupId,"[私有欢迎语]",m_info->welcomeMsg,ev.msgId);
            return true;
        }
    }else if( ev.type==2 && ev.subType ==6){
        if(!m_info->welcomeMsg2.isEmpty()){
            send_msgAsync(ev.type,ev.groupId,"[私有欢迎语]",m_info->welcomeMsg2,ev.msgId);
            return true;
        }
    }

    if (ev.type == 0 && (ev.bitmap & BIT_SHUA_P))
    {
        if(ev.bot_admin) {
            if(shuaping(m_info,ev))
            {
                setGroupRestrictChatSetting(ev.groupId,ev.user,60,
                                            [this,ev](const QString &resp, QNetworkReply::NetworkError err) {

                                                if(resp == "{}")
                                                {

                                                    QString text = "<@"+ev.user+"> 发送信息速度过快 疑似刷屏";
                                                    delete_messages(ev.type,ev.groupId,ev.msgId,[](auto, auto){ return; });
                                                    send_msgAsync(ev.type,ev.groupId,"[刷屏检测]",text,ev.msgId);
                                                }
                                            });
                return true;
            }
        }
    }
    if(pluginPage->handleAdRewardEvent(ev)) return true;

    if(ev.type ==18)
    {

        if(ev.bitmap & BIT_AUTO_JOJN)
        {
            auto *db = g_botdb[m_info->appid_int];
            GroupRecord2 gid;
            db->getGroupInfo(ev.groupId, gid);
            QString text = gid.autoref;
            if(text.isEmpty()){
                QQBotClient *client = m_botClients[m_info->appid_int];

                QString t2 = client->approveGroupJoinRequest(ev.groupId,ev.user,true,ev.callbackId);
                if(t2=="{}") return true;

            }else if(text.contains(","))
            {
                QStringList list = text.split(",");
                for(const auto & str : std::as_const(list))
                {
                    if(ev.extra.contains(str))
                    {
                        QQBotClient *client = m_botClients[m_info->appid_int];
                        QString t2 = client->approveGroupJoinRequest(ev.groupId,ev.user,true,ev.callbackId);
                        if(t2=="{}") return true;
                        break;
                    }
                }
            }else if(ev.extra.contains(text)){
                QQBotClient *client = m_botClients[m_info->appid_int];
                QString t2 = client->approveGroupJoinRequest(ev.groupId,ev.user,true,ev.callbackId);
                if(t2=="{}") return true;
            }

        }
        if(ev.bitmap & BIT_AUTO_JOJN_KG){
            if(!m_info->apply.isEmpty())
            {
                QString ret = m_info->apply;
                if(ret.startsWith("#python")) ret = python_code(ret,ev);
                if(m_botClients.contains(m_info->appid_int))
                {
                    QQBotClient *client = m_botClients[m_info->appid_int];
                    ret.replace("{ID}",ev.user);
                    ret.replace("{ReqId}",ev.callbackId);

                    ret.replace("{申请理由}",ev.msg);
                    ret.replace("{昵称}",ev.nickname);
                    ret.replace("{群昵称}",ev.groupname);
                    if(ret.contains("{头像}"))
                        ret.replace("{头像}",QString("![#48px #48px](https://thirdqq.qlogo.cn/qqapp/%1/%2/100)").arg(ev.appid).arg(ev.user));
                    client->send_msgAsync(ev.type,ev.groupId,"[入群申请]",ret,ev.msgId);
                    return true;
                }

            }else{
                QQBotClient *client = m_botClients[m_info->appid_int];
                QString ret = R"(有人申请加群但是未配置文案 点击下面按钮同意加群\n申请人：{ID}#b:#{"keyboard":{"content":{"rows":[{"buttons":[{"action":{"data":"同意加群 {ID} {ReqId}","permission":{"type":2},"type":2,"unsupport_tips":"不支持"},"id":"1","render_data":{"label":"同意","style":1,"visited_label":"按钮1"}}]}]}}}#b:#)";
                ret.replace("{ID}",ev.user);
                ret.replace("{ReqId}",ev.callbackId);
                client->send_msgAsync(ev.type,ev.groupId,"[入群申请]",ret,ev.msgId);
                return true;
            }
        }
    }
    return false;
}




// QElapsedTimer __t;
class ___tdxx : public QRunnable {
public:
    ___tdxx(QQBotClient *c, const QString &m) : m_client(c), m_msg(m) {
        setAutoDelete(true);
    }

    void run() override {
        QString msg = m_msg;
        m_client->onTextMessage(msg);
       // qDebug() << "线程池耗时 " << __t.nsecsElapsed() / 1e6 << " ms";
    }

private:
    QQBotClient *m_client;
    QString m_msg;
};

void QQBotClient::onTextMessageReceived(const QString &message) {
    //__t.start();
    auto *task = new ___tdxx(this, message);
    QThreadPool::globalInstance()->start(task);
}
QString webhook_sig(const QJsonObject &obj, const QString &secret) {
    QJsonObject d = obj.value("d").toObject();
    QString plain_token = d.value("plain_token").toString();
    QString event_ts = d.value("event_ts").toString();

    if (plain_token.isEmpty() || event_ts.isEmpty()) {
        qWarning() << "Missing plain_token or event_ts";
        return QString();
    }

    // 生成种子：重复 secret 直到 >= 32 字节，截取前 32 字节
    QByteArray seed = secret.toUtf8();
    while (seed.size() < 32) {
        seed.append(seed);
    }
    seed.truncate(32);

    // 创建 Ed25519 私钥
    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_ED25519,
        nullptr,
        reinterpret_cast<const unsigned char*>(seed.constData()),
        seed.size()
        );
    if (!pkey) {
        qWarning() << "创建 Ed25519 私钥失败";
        return QString();
    }

    // 拼接消息
    QByteArray message = event_ts.toUtf8() + plain_token.toUtf8();

    // 签名
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (!ctx) {
        qWarning() << "创建上下文失败";
        EVP_PKEY_free(pkey);
        return QString();
    }
    if (EVP_PKEY_sign_init(ctx) <= 0) {
        qWarning() << "初始化签名失败";
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        return QString();
    }

    unsigned char signature[64];
    size_t siglen = sizeof(signature);
    int ret = EVP_PKEY_sign(ctx,
                            signature,
                            &siglen,
                            reinterpret_cast<const unsigned char*>(message.constData()),
                            message.size());
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    if (ret != 1 || siglen != 64) {
        qWarning() << "签名失败";
        return QString();
    }

    QString signatureHex = QByteArray(
                               reinterpret_cast<const char*>(signature),
                               static_cast<int>(siglen)
                               ).toHex();

    QJsonObject res;
    res["plain_token"] = plain_token;
    res["signature"] = signatureHex;
    return QJsonDocument(res).toJson(QJsonDocument::Compact);
}


QString QQBotClient::onTextMessage(const QString &message)
{
    return onTextMessage(message.toUtf8());
}

QString QQBotClient::onTextMessage(const QByteArray &message)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message, &err);
    if (err.error != QJsonParseError::NoError) {
        AppendEventLog("收到非法 JSON: " + message.left(200),0xff);
        return QString();
    }
    //2012 ms
    QJsonObject obj = doc.object();
    int op = obj.value("op").toInt(-1);
    qint64 s = obj.value("s").toVariant().toLongLong();
    //if(s==m_seq) return QString();
    if (s > 0) m_seq = s;
    QString res;
    switch (op) {
    case 10: { // Hello
        int interval = obj.value("d").toObject().value("heartbeat_interval").toInt(30000);
        m_heartbeatIntervalSec = interval / 1000;
        QMetaObject::invokeMethod(this, [=]() {
            startHeartbeatTimer(m_heartbeatIntervalSec);
        });


        break;
    }
    case 11: // Heartbeat ACK
        m_invalidHeartbeatCount = 0;
        break;
    case 0: { // Dispatch
        QString eventType = obj.value("t").toString();
        if (eventType == "READY") {
            m_sessionId = obj.value("d").toObject().value("session_id").toString();
            m_info->online = true;
            m_isConnecting = false;
            QMetaObject::invokeMethod(this, [=]() {
                resetReconnectAttempts();
                fetchSelfInfo();
            });



            return res;
        } else if (eventType == "RESUMED") {
            m_info->online = true;
            m_isConnecting = false;
            AppendEventLog("会话恢复成功",0x594787);
            emit loginSuccess();
            return res;
        }
        parseMessageEvent(obj,message);
        break;
    }
    case 9: // Invalid Session
        // ⚠ 2026-11-01 改：以前这里是 stop() + scheduleReconnect(10)，而 stop() 会把
        //   m_info->autoConnect 置成 false，scheduleReconnect 第一行又恰好是
        //   「!autoConnect 就 return」—— 也就是说**一次 Invalid Session 就把账号永久打死**，
        //   之后没有任何重连、日志也不再出现，只能手动点登录。
        //   这正是「对面因错误断开就连不上」的形态：token/会话被服务端作废 → op=9 → 静默死亡。
        //   现在只做「清会话 + 作废 token + 重连」，绝不碰 autoConnect（那是用户意图）。
        m_info->err  = "鉴权失败：可能订阅了不允许的事件或 token 无效";
        AppendEventLog("鉴权失败：可能订阅了不允许的事件或 token 无效" ,0xff);
        m_accessToken.clear();      // 会话无效基本等于 token 已废，下一轮 start() 重新取
        m_tokenExpireTime = 0;
        m_sessionId.clear();
        m_seq = 0;
        QMetaObject::invokeMethod(this, [this]() {
            const bool wasOnline = m_info->online;
            m_info->online = false;
            m_isConnecting = false;
            stopHeartbeatTimer();
            if (m_reconnectAttempts > 3)
                m_reconnectAttempts = 3;     // 别让一次鉴权失败把退避直接顶到 60 秒
            if (wasOnline)
                emit disconnected();
            closeSocketSilently();
            scheduleReconnect(10);
        }, Qt::QueuedConnection);
        break;
    case 7: // Reconnect
        if(m_info->type==0){
            QMetaObject::invokeMethod(this, [=]() {
                stopHeartbeatTimer();
                m_webSocket.close();
            });
        }
        break;

    default:
        AppendEventLog(QString("未处理的 op=%1").arg(op) ,0xff);
        break;
    }
    return res;
}


int ___aaa=0;
// ---------- 发送协议包 ----------
void QQBotClient::sendIdentify()
{

    QJsonObject identify;
    identify["token"] = QString("QQBot %1").arg(m_accessToken);

    identify["intents"] = m_info->wsIntents;
    //if(m_info->botqq.toInt()<=128)
    //     m_info->botqq= QString::number(m_info->wsIntents);
    //identify["intents"] =m_info->botqq.toInt() | (1 << ___aaa);
    //qDebug() <<  (m_info->botqq.toInt() | (1 << ___aaa)) << "|" <<___aaa;
    //___aaa++;
    identify["shard"] = QJsonArray{0, 1};

    QJsonObject payload;
    payload["op"] = 2;
    payload["d"] = identify;
    //"QQBotPlugin/9.9.9 (Node/20.11.0; Linux; 自定义显示名称/1.0.19)"
    QString msg = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_webSocket.sendTextMessage(msg);


}

void QQBotClient::sendHeartbeat() //心跳
{
    if (!m_info->online && m_webSocket.state() != QAbstractSocket::ConnectedState)
        return;

    QJsonObject hb;
    hb["op"] = 1;
    if (m_seq != 0)
        hb["d"] = m_seq;

    QString msg = QJsonDocument(hb).toJson(QJsonDocument::Compact);
    m_webSocket.sendTextMessage(msg);
}

void QQBotClient::onHeartbeatTimeout()
{
    if (!m_info->online) return;
    sendHeartbeat();

    m_invalidHeartbeatCount++;
    // ⚠ 以前这里是「连续 10 次只打一行日志、然后计数清零」，既不重连也不把 online 置 false。
    //   对端被中间设备静默丢弃（NAT/防火墙超时、对端进程挂了但没发 FIN）时，
    //   QWebSocket 永远不会发 disconnected，于是：
    //     · online 一直是 true → start() 第一行挡死 → 自动重连永远不动；
    //     · 界面照旧显示在线；
    //     · 手动点登录按钮才恢复（它先 stop() 再 start()）。
    //   这就是"定时重连等不上、手动点一下就能登录、不用重启软件"的完整成因。
    //   现在收到 ACK 会清零；连续 3 次（≈90 秒）都没有 ACK 就判定连接已死，主动断开重连。
    if (m_invalidHeartbeatCount >= 3) {
        AppendEventLog(QString("%1 连续 %2 次心跳无响应，判定连接已死，主动断开重连")
                           .arg(m_info->nickname).arg(m_invalidHeartbeatCount), 0xff);
        m_invalidHeartbeatCount = 0;
        m_info->online = false;
        m_isConnecting = false;
        stopHeartbeatTimer();
        emit disconnected();
        // 用 silent 版断开：disconnected 信号已经被上面这个标志拦住不重复排期，
        // 重连由下面这一行自己排，保证一定会有下一次。
        closeSocketSilently();
        scheduleReconnect(3);
    }
}

void QQBotClient::startHeartbeatTimer(int intervalSec)
{
    stopHeartbeatTimer();
    if (intervalSec > 0) {
        m_heartbeatTimer.start(intervalSec * 1000);

    }
}

void QQBotClient::stopHeartbeatTimer()
{
    if (m_heartbeatTimer.isActive())
        m_heartbeatTimer.stop();
}

// ---------- 重连 ----------
//
// ⚠ 2026-11-01 改：原来「5 次就永久停手」（3s/10s/15s/20s/25s，总共 3 分半）。
//   对端偶尔抽风几分钟、或 token 刚被作废那几分钟，正好能把这 5 次全吃光，
//   之后就再也不重连，只能手动停止再启动 —— 这就是「偶尔被对面断开就连不上」。
//
// 现在的节奏（用户指定）：
//   · **不限次数**，一直重试到连上或用户手动停止；
//   · 间隔逐次翻倍：3 → 6 → 12 → 24 → 48 → 96 …（第一次仍然是 3 秒，抖一下就能回来）；
//   · 最长 15 分钟；到顶之后**每重试一次再多加 1 分钟**（900 → 960 → 1020 …），
//     避免长时间断线时一直贴着同一频率撞服务器。
void QQBotClient::scheduleReconnect(int delaySec)
{
    // 用户手动停止（autoConnect=false 且不是本次运行里手动点过登录）就真的不再连
    if(!m_info->autoConnect && !m_userWantsOnline) return;

    m_reconnectAttempts++;
    const int kMaxSec = 15 * 60;                          // 15 分钟封顶
    if (delaySec < 3) delaySec = 3;

    // 逐次翻倍；shift 限 10（×1024）防止乘爆
    const int shift = qBound(0, m_reconnectAttempts - 1, 10);
    qint64 wait = qint64(delaySec) << shift;

    if (wait >= kMaxSec) {
        // 已顶到 15 分钟：之后每多试一次 +1 分钟
        wait = kMaxSec + qint64(m_reconnectOverflow) * 60;
        m_reconnectOverflow++;
    }

    const int waitSec = int(qMin<qint64>(wait, 24 * 3600)); // 保险丝，别算出个天文数字
    if (waitSec != 3)
        AppendEventLog(QString("将在 %1 秒后进行第 %2 次重连...").arg(waitSec).arg(m_reconnectAttempts),0xD891BC);
    m_reconnectTimer.start(waitSec * 1000);
}

void QQBotClient::resetReconnectAttempts()
{
    m_reconnectAttempts = 0;
    m_reconnectOverflow = 0;
    m_reconnectTimer.stop();
}

// 重连定时器到点。
//
// ⚠ 为什么不能把 timeout 直接连到 start()：
//   start() 第一行是 `if (m_info->online || m_isConnecting) return;`。
//   只要上一轮停在 m_isConnecting == true（/gateway 的回调丢了、socket 半死不活、
//   abort 之后 disconnected 没来……），定时器到点就会被这一行直接吃掉，
//   而且**被吃掉的这一次不会再排下一次重连** → 自动重连从此永久停摆。
//
//   现象正是：界面一直离线，日志最后一句是「将在 N 秒后进行重连」，之后再没有任何动静；
//   而手动点一下登录按钮立刻就能连上（手动路径会把重连计数清零、并且重新走一遍 start）。
//   所以这里到点先把卡住的标志解开，保证这一次重试真的发得出去。
void QQBotClient::onReconnectTimerFired()
{
    if (m_info->online)
        return;                        // 已经在线了（可能刚被别的路径连上），别折腾

    if (m_isConnecting) {
        AppendEventLog(QString("%1 上一次连接没有收尾(状态卡在连接中)，强制重来").arg(m_info->nickname), 0xD891BC);
        m_isConnecting = false;
        m_handshakeTimer.stop();
        closeSocketSilently();
    }
    start();
}


QString QQBotClient::PostSync(const QString &url, const QByteArray &jsonData, const QString &contentType, int timeoutMs) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }
    std::future<QByteArray> future = NetManager::instance()->post(url, jsonData, headers, timeoutMs);
    QString resp = future.get();
    // token 刷新改由主线程定时任务（onRefreshReplyFinished）统一处理，这里不再救火重发
    return resp;

}

QString QQBotClient::PostSync(const QString &url, const QJsonObject &jsonData,
                              const QString &contentType, int timeoutMs) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }

    QJsonObject currentJson = jsonData;

    QByteArray jsonbyte = QJsonDocument(currentJson).toJson(QJsonDocument::Compact);
    std::future<QByteArray> future = NetManager::instance()->post(url, jsonbyte, headers, timeoutMs);
    QString resp = future.get();

    // token 刷新改由主线程定时任务（onRefreshReplyFinished）统一处理，这里不再救火重发

    if (url.contains("/messages") && resp.contains("被去重")) {

        currentJson["msg_seq"] = m_info->message_sent + m_info->message_received;
        jsonbyte = QJsonDocument(currentJson).toJson(QJsonDocument::Compact);
        future = NetManager::instance()->post(url, jsonbyte, headers, timeoutMs);
        resp = future.get();
        return resp;
    }
    return resp;
}

QString QQBotClient::Post(const QString &url, const QJsonObject &jsonData,
                              const QString &contentType, int timeoutMs,Callback finalCallback) {
    if(!finalCallback) return PostSync(url,jsonData,contentType,timeoutMs);
    doPost(url, jsonData, contentType, timeoutMs, finalCallback, 0);
    return QString();

}

QString QQBotClient::Get(const QString &url,const QString &contentType, int timeoutMs,Callback finalCallback) {
    if(!finalCallback) return GetSync(url,contentType,timeoutMs);
    GetAsync(url, contentType, timeoutMs, finalCallback);
    return QString();

}
void QQBotClient::PostAsync(const QString& url, const QJsonObject& json,
                            const QString& contentType, int timeoutMs,
                            Callback finalCallback) {
    // 启动递归重试，初始重试次数为 0
    doPost(url, json, contentType, timeoutMs, finalCallback, 0);
}

void QQBotClient::doPost(const QString& url, const QJsonObject& json,
                         const QString& contentType, int timeoutMs,
                         Callback finalCallback, int retryCount) {
    // 构建请求头
    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }

    QByteArray jsonData = QJsonDocument(json).toJson(QJsonDocument::Compact);
    int appid = m_info->appid_int;
    // 发起异步请求（回调丢线程池执行，不在网络线程也不在主线程）
    NetManager::instance()->postAsync(url, jsonData, headers, timeoutMs,
                                      [appid,url, json, contentType, timeoutMs, finalCallback, retryCount]
                                      (const QString& response, QNetworkReply::NetworkError error) {

                                        // token 刷新改由主线程定时任务（onRefreshReplyFinished）统一处理，
                                        // 这里不再救火刷新重发

                                        if (response.contains("被去重") && retryCount < 3) {

                                            if(json["noref"].toBool()) return;
                                            QJsonObject modifiedJson = json;
                                            QQBotClient *c = m_botClients.value(appid);   // 找不到返回 nullptr，不插入
                                            if (c) {
                                                QJsonObject modifiedJson = json;
                                                modifiedJson["msg_seq"] = c->m_info->message_sent + c->m_info->message_received;
                                                c->doPost(url, modifiedJson, contentType, timeoutMs, finalCallback, retryCount + 1);
                                            }
                                            return;
                                        }

                                        QString logResp = response;
                                        if (error != QNetworkReply::NoError) logResp += "\n[Network Error: " + QString::number(error) + "]";
                                        if(finalCallback)
                                            finalCallback(logResp, error);
                                    });
}

//频道用
void QQBotClient::postRawAsync(const QString &url, const QByteArray &data,
                               const QHash<QString, QString> &headers, int timeoutMs,
                               Callback callbacks) {
    NetManager::instance()->postAsync(url, data, headers, timeoutMs,
                                      [callbacks]
                                      (const QString &response, QNetworkReply::NetworkError error) {
                                          if (callbacks)
                                              callbacks(response, error);
                                      });
}


QString QQBotClient::GetSync(const QString &url, const QString &contentType, int timeoutMs) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }

    std::future<QByteArray> future = NetManager::instance()->get(url, headers, timeoutMs);
    return future.get();
}
void QQBotClient::GetAsync(const QString &url, const QString &contentType, int timeoutMs, Callback callbacks) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }
    NetManager::instance()->getAsync(url, headers, timeoutMs, callbacks);
}

QString QQBotClient::PatchSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }

    QByteArray jsonbyte = QJsonDocument(jsonData).toJson(QJsonDocument::Compact);
    std::future<QByteArray> future = NetManager::instance()->Patch(url,jsonbyte ,headers, timeoutMs);
    return future.get();
}
QString QQBotClient::put2(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs, Callback callbacks) {
    if(!callbacks) return put(url,data,contentType,timeoutMs);

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }

    NetManager::instance()->putAsync(url,data,headers,timeoutMs,callbacks);
    return QString();

}
QString QQBotClient::put(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }

    std::future<QByteArray> future = NetManager::instance()->put(url,data ,headers, timeoutMs);
    return future.get();
}
std::future<QByteArray> QQBotClient::put2(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }
    return NetManager::instance()->put(url,data ,headers, timeoutMs);
}


// ─────────────────────────────────────────────────────────────────────────────
// 内网探测（detectOptimalRegion）
//
// ⚠ 2026-10-30 改：以前这里是「20 次 QHostInfo::fromName 直接在 GUI 线程上跑」，
//   而调用点在 main.cpp 的 w->show() 之后、a.exec() 之前 —— 窗口只是被排进队列、
//   一次 paint 都还没跑。断网 / 防火墙或安全软件拦了 DNS 时，每次解析都要等满超时
//   （1~5 秒），20 次累计几十秒，主线程就死死卡在 exec() 前面：
//   进程在、窗口「载入不出来」、白屏假死。这就是那条现象的根因。
//
//   现在拆成两半：
//     · probeInnerRegionSync()：纯 DNS，不碰任何全局变量和控件，放后台线程跑；
//     · 结果用 QueuedConnection 投回 GUI 线程，再写 g_neiw / 打日志 ——
//       g_neiw 是给上传路径读的全局量，AppendEventLog 会动 logPage 和 g_logdb[0]，
//       这两个都只能在主线程写。
//   代价：探测完成的那几百毫秒/几秒里，g_neiw 还是空的 → 上传走公网域名（只是慢一点，
//   不会错），探测回来后自动切内网；和以前「探测完才进事件循环」相比没有功能损失。
// ─────────────────────────────────────────────────────────────────────────────
static QString probeInnerRegionSync()
{
    static const char *const kRegions[] = {
        "ap-beijing", "ap-nanjing", "ap-shanghai", "ap-guangzhou",
        "ap-chengdu", "ap-chongqing", "ap-shenzhen-fsi", "ap-shanghai-fsi", "ap-beijing-fsi",
        "ap-hongkong", "ap-singapore", "ap-jakarta", "ap-seoul", "ap-bangkok",
        "ap-tokyo", "me-saudi-arabia", "na-siliconvalley", "na-ashburn",
        "sa-saopaulo", "eu-frankfurt"
    };
    static const QString bucketPrefix = QStringLiteral("qqbot-file-upload-1251316161.cos.");

    for (const char *r : kRegions) {
        const QString region  = QString::fromLatin1(r);
        const QString domain  = bucketPrefix + region + QStringLiteral(".myqcloud.com");
        const QHostInfo info  = QHostInfo::fromName(domain);   // 阻塞；只在后台线程调
        if (info.error() != QHostInfo::NoError) continue;

        const auto addrs = info.addresses();
        for (const QHostAddress &addr : addrs) {
            if (addr.isInSubnet(QHostAddress::parseSubnet("10.0.0.0/8")) ||
                addr.isInSubnet(QHostAddress::parseSubnet("100.0.0.0/8")) ||
                addr.isInSubnet(QHostAddress::parseSubnet("169.254.0.0/16"))) {
                return region;                                 // 命中内网段
            }
        }
    }
    return QString();                                          // 空 = 不支持内网
}

void detectOptimalRegion() {
    std::thread([]() {
        // 量一下这 20 次解析到底花了多久：正常网络 100~600ms；如果被防火墙/安全软件
        // 丢了包，就要每个名字各等满一次超时，累计几十秒 —— 也就是「启动后窗口很久不出来」。
        QElapsedTimer timer;
        timer.start();
        const QString region = probeInnerRegionSync();
        const qint64  elapsedMs = timer.elapsed();

        // 回 GUI 线程再落状态 + 打日志（g_neiw / logPage 都只能在主线程动）
        QMetaObject::invokeMethod(qApp, [region, elapsedMs]() {
            if (框架退出) return;
            if (!region.isEmpty()) {
                g_neiw = region;
                AppendEventLog("检查到服务器支持直连腾讯内网 已经切换到 内网模式 上传 图片 视频 音频 文件将快速上传");
            } else {
                AppendEventLog("坏！当前服务器不支持内网模式 你可能需要购买腾讯云服务器 才能内网加速",0xff0000);
            }
            // 只在明显变慢时补一句，正常启动不刷屏；这条就是「是不是防火墙」的自证
            if (elapsedMs > 2000) {
                AppendEventLog(QString("内网探测耗时 %1 毫秒：DNS 解析很慢（基本是被防火墙/安全软件丢了包、"
                                       "只能等满超时），如果外部用户反馈「启动后窗口很久不出来」多半就是这条")
                                   .arg(elapsedMs), 0xff0000);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

QString QQBotClient::Delete(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs, Callback callbacks) {

    if(!callbacks) return DeleteSync(url,jsonData,contentType,timeoutMs);
    DeleteAsync(url,jsonData,contentType,timeoutMs,callbacks);
    return QString();
}
QString QQBotClient::DeleteSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }
    QByteArray jsonbyte = QJsonDocument(jsonData).toJson(QJsonDocument::Compact);
    std::future<QByteArray> future = NetManager::instance()->Delete(url, jsonbyte, headers, timeoutMs);
    return future.get();
}
void QQBotClient::DeleteAsync(const QString &url, const QJsonObject &jsonData, const QString &contentType ,int timeoutMs, Callback callbacks) {

    QHash<QString, QString> headers;
    headers.insert("X-Union-Appid", m_info->appid);
    headers.insert("Authorization", "QQBot " + m_accessToken);
    if (contentType.isEmpty()) {
        headers.insert("Content-Type", "application/json");
    } else {
        headers.insert("Content-Type", contentType);
    }
    QByteArray jsonbyte = QJsonDocument(jsonData).toJson(QJsonDocument::Compact);
    NetManager::instance()->Delete2(url, jsonbyte, headers,timeoutMs,callbacks);

}



void QQBotClient::fetchSelfInfo()
{
    if (!m_info->online) return;
    QUrl url("https://api.bot.qq.com/users/@me");
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QString("QQBot %1").arg(m_accessToken).toUtf8());
    request.setRawHeader("X-Union-Appid", m_info->appid.toUtf8());

    QNetworkReply *reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {

        QByteArray data = reply->readAll();
        reply->deleteLater();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull()) {
            return;
        }
        QJsonObject obj = doc.object();
        QString uid = obj.value("id").toString();
        QString nickname = obj.value("username").toString();
        QString avatarUrl = obj.value("avatar").toString();
        QString share_url = obj.value("share_url").toString();

        m_info->botqq= extractBetween(share_url,"robot_uin=","&");
        //qDebug() << obj;
        if (uid.isEmpty()) {
            m_info->nickname = "请检查ip白名单";
            AppendEventLog(QString::fromUtf8(data) ,0xff);
            return;
        }
        m_info->pduid = uid;
        m_info->unid = obj.value("union_openid").toString();
        m_info->nickname = nickname;

        for (int i = 0; i < robotListWidget->count(); ++i) {
            QListWidgetItem *item = robotListWidget->item(i);
            if (item->data(Qt::UserRole).toInt() == m_info->appid_int) {
                item->setText(nickname);               // 更新显示名称

                item->setData(Qt::UserRole, m_info->appid_int);
                break;
            }
        }

        if (!avatarUrl.isEmpty()) {
            QString avatarDir = QCoreApplication::applicationDirPath() + "/avatars/";
            QDir dir;
            if (!dir.exists(avatarDir))
                dir.mkpath(avatarDir);
            QString avatarPath = avatarDir + m_info->appid + ".png";
            downloadAvatar(avatarUrl, avatarPath);
            m_info->avatarPath = avatarPath;
        }
        if (g_cqev && m_info && g_cqev->appid == m_info->appid_int) {

            QString text = QString("重启成功 耗时%1秒...")
                               .arg(QDateTime::currentSecsSinceEpoch() - g_cqev->log);
            send_msgAsync(g_cqev->type, g_cqev->groupId, "[私有指令]", text, g_cqev->msgId);

            delete g_cqev;   // 释放内存
            g_cqev = nullptr;
        }
        m_info->autoConnect=true;
        emit loginSuccess(); //通知界面修改
    });
}

void QQBotClient::downloadAvatar(const QString &url, const QString &savePath)
{
    QNetworkRequest request(url);
    QNetworkReply *reply = m_nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, savePath]() {
        if (reply->error() == QNetworkReply::NoError) {
            QFile file(savePath);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(reply->readAll());
                file.close();
                emit avatarDownloaded();
            }
        }
        reply->deleteLater();
    });
}