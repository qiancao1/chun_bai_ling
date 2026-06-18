#include "QQBotClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDateTime>
#include <QEventLoop>
#include "global.h"
#include <QNetworkReply>
#include <QCoreApplication>
#include "eventtask.h"
#include <QFile>
#include <QDir>

// 在文件头部包含 WinHttpClient 封装




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
    connect(&m_reconnectTimer, &QTimer::timeout, this, &QQBotClient::start);
}

QQBotClient::~QQBotClient()
{
    stop();
}

void QQBotClient::start()
{
    if (m_info->online || m_isConnecting)
        return;
    m_isConnecting = true;
    AppendEventLog(QString("正在启动机器人 %1 ...").arg(m_info->appid), 0x0082FD);

    if (!refreshAccessToken()) {
        emit loginFailed("获取 access_token 失败，请检查 appid/secret 或网络");
        m_isConnecting = false;
        scheduleReconnect(10);
        return;
    }


    QString wsUrl = m_info->wsAddress;
    if (wsUrl.isEmpty()) {
        wsUrl = fetchGatewayUrl();
        if (wsUrl.isEmpty()) {
            emit loginFailed("无法获取网关地址，可能是 token 无效或网络问题");
            AppendEventLog("获取网关地址失败，token 可能无效", 0xff);
            m_isConnecting = false;
            scheduleReconnect(5);
            return;
        }
    }else{
        代理=1;
    }

    // 第三步：连接 WebSocket
    QUrl url(wsUrl);
    if (!url.isValid()) {
        emit loginFailed("无效的 WebSocket 地址: " + wsUrl);
        m_isConnecting = false;
        return;
    }
    m_webSocket.open(url);

}

void QQBotClient::stop()
{

    stopHeartbeatTimer();
    if (m_webSocket.state() == QAbstractSocket::ConnectedState)
        m_webSocket.close();
    m_info->online = false;
    m_info->autoConnect=false;
    m_isConnecting = false;
    m_invalidHeartbeatCount = 0;
    m_seq = 0;
    m_sessionId.clear();
    resetReconnectAttempts();
    m_reconnectAttempts = 10;
    AppendEventLog(QString("机器人 %1 已停止").arg(m_info->appid),0xff);
}

// ---------- 网络请求 ----------
QString QQBotClient::fetchGatewayUrl()
{
    if (!m_info->wsAddress.isEmpty())
        return m_info->wsAddress;

    if (m_accessToken.isEmpty()) {
        AppendEventLog("无法获取网关：access_token 为空",0xff);
        return QString();
    }

    QUrl url("https://api.sgroup.qq.com/gateway");
    QNetworkRequest request(url);

    request.setRawHeader("Authorization", QString("QQBot %1").arg(m_accessToken).toUtf8());
    request.setRawHeader("X-Union-Appid", m_info->appid.toUtf8());

    QNetworkReply *reply = m_nam.get(request);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QByteArray data = reply->readAll();
    reply->deleteLater();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        AppendEventLog("获取登录Ws错误: " + err.errorString(),0xff);
        return QString();
    }

    QJsonObject obj = doc.object();
    QString wsUrl = obj.value("url").toString();
    if (wsUrl.isEmpty()) {

        QString errMsg = obj.value("message").toString();
        if(errMsg.startsWith("token"))
            m_accessToken.clear();
        if (errMsg.isEmpty())
            errMsg = "未知错误，可能 token 无效或 appid 不正确";
        AppendEventLog("获取ws地址失败: " + errMsg,0xff);
    }
    return wsUrl;
}

bool QQBotClient::refreshAccessToken()
{
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_accessToken.isEmpty() && m_tokenExpireTime > now + 60)
        return true;


    // 动态获取 token
    if (m_info->appid.isEmpty() || m_info->secret.isEmpty()) {
        AppendEventLog("缺少 appid 或 secret，无法获取 token",0xff);
        return false;
    }

    QUrl url("https://bots.qq.com/app/getAppAccessToken");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject payload;
    payload["appId"] = m_info->appid;
    payload["clientSecret"] = m_info->secret;
    QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_nam.post(request, body);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        AppendEventLog("刷新 token 网络错误: " + reply->errorString(),0xff);
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        AppendEventLog("解析 token 响应失败: " + err.errorString(),0xff);
        return false;
    }

    QJsonObject obj = doc.object();
    QString newToken = obj.value("access_token").toString();
    if (newToken.isEmpty()) {
        QString errMsg = obj.value("message").toString();
        if (errMsg.isEmpty())
            errMsg = "appid 或 secret 错误（无具体返回信息）";
        AppendEventLog("获取 token 失败: " + errMsg,0xff);
        return false;
    }

    m_accessToken = newToken;
    int expiresIn = obj.value("expires_in").toInt(7200);
    m_tokenExpireTime = now + expiresIn - 60;
    AppendEventLog(QString("Token 刷新成功，有效期至 %1")
                  .arg(QDateTime::fromSecsSinceEpoch(m_tokenExpireTime).toString()), Qt::darkGreen);
    return true;
}

// ---------- WebSocket 事件 ----------
void QQBotClient::onConnected()
{
    sendIdentify();
}

void QQBotClient::onDisconnected()
{
    AppendEventLog("WebSocket 已断开", 0xff);
    stopHeartbeatTimer();
    bool wasOnline = m_info->online;
    m_info->online = false;
    m_info->autoConnect=false;
    m_isConnecting = false;
    if (wasOnline)
        emit disconnected();
    scheduleReconnect(3);
}

void QQBotClient::onError(QAbstractSocket::SocketError error)
{
    AppendEventLog("WebSocket 错误: " + m_webSocket.errorString(),0xff);
}
void Message(AccountInfo *info,const MessageEvent &ev);
int mapTypeToTabIndex(int type);
void parseMessageEvent(QJsonObject &payload,const QString &text, QQBotClient *client)
{
    MessageEvent ev;
    ev.raw = text;
    ev.seq = payload.value("s").toVariant().toLongLong();
    ev.msgType = payload.value("t").toString();
    QJsonObject d = payload.value("d").toObject();


    // 默认值
    ev.type = -1;
    ev.subType = 0;
    ev.fullType = 0;
    ev.callbackType = 0;

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
        ev.groupId = d.value("group_openid").toString();
        QJsonObject author = d.value("author").toObject();
        ev.user = author.value("union_openid").toString();
        QString role= author.value("member_role").toString();
        if(role == "owner")
            ev.member_role = 0;
        else if(role=="admin")
            ev.member_role = 1;
        else if(role == "member")
            ev.member_role = 2;
        ev.nickname = author.value("username").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();

        if(client->m_info->unid.isEmpty())
        {
            const QJsonArray array = d["mentions"].toArray();
            for (const QJsonValue &a : array)
            {
                if(!a["at_you"].toBool()) continue;
                ev.at_you = true;
                client->m_info->unid= a["id"].toString();
                break;
            }
        }
        if(ev.msg.contains(client->m_info->unid))
        {
            ev.at_you = true;
            ev.msg = subTextReplace(ev.msg,"<@"+client->m_info->unid+">","");
        }

    }
    else if (ev.msgType == "C2C_MESSAGE_CREATE") {
        ev.type = 2;   // 私聊
        QJsonObject author = d.value("author").toObject();
        ev.user = author.value("union_openid").toString();
        ev.groupId = ev.user;
        ev.nickname = author.value("username").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();
        ev.replyTo = d.value("message_scene").toString();
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
        if(ev.msg.contains(client->m_info->pduid))
        {
            ev.at_you = true;
            ev.msg = subTextReplace(ev.msg,"<@!"+client->m_info->pduid+">","");
        }
    }
    else if (ev.msgType == "DIRECT_MESSAGE_CREATE") {
        ev.type = 3;   // 频道私聊
        ev.guildId = d.value("guild_id").toString();

        QJsonObject author = d.value("author").toObject();
        ev.user = author.value("union_openid").toString();
        ev.groupId = author.value("id").toString();
        if (ev.user.isEmpty()) ev.user = ev.groupId;
        ev.nickname = author.value("username").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();
        ev.at_you=true;
    }
    // ========== 2. 群/好友管理事件 ==========
    else if (ev.msgType == "GROUP_ADD_ROBOT") {//被邀请进新群
        ev.type = 4; ev.subType = 4;
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("scene_param").toString();
        if (ev.user.isEmpty()) ev.user = d.value("op_member_openid").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = ev.groupId;

    }
    else if (ev.msgType == "GROUP_DEL_ROBOT") { //被踢出群
        ev.type = 4; ev.subType = 5;
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("op_member_openid").toString();
        ev.msgId = d.value("id").toString();
    }
    else if (ev.msgType == "FRIEND_ADD") { //好友增加
        ev.type = 5; ev.subType = 6;
        ev.user = d.value("openid").toString();
        ev.groupId = ev.user;
        ev.msgId = d.value("id").toString();
    }
    else if (ev.msgType == "FRIEND_DEL") { //好友删除
        ev.type = 5; ev.subType = 7;
        ev.user = d.value("openid").toString();
        ev.groupId = ev.user;
        ev.msgId = d.value("id").toString();
    }
    else if (ev.msgType == "C2C_MSG_REJECT") {
        ev.type = 5; ev.subType = 8;
        ev.user = d.value("openid").toString();
    }
    else if (ev.msgType == "C2C_MSG_RECEIVE") {
        ev.type = 5; ev.subType = 9;
        ev.user = d.value("openid").toString();
    }
    else if (ev.msgType == "GROUP_MSG_REJECT") {
        ev.type = 4; ev.subType = 10;
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("op_member_openid").toString();
    }
    else if (ev.msgType == "GROUP_MSG_RECEIVE") {
        ev.type = 4; ev.subType = 11;
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("op_member_openid").toString();
    }
    // ========== 3. 频道 Guild 事件 ==========
    else if (ev.msgType == "GUILD_CREATE") {
        ev.type = 11; ev.subType = 1;
        ev.guildId = d.value("id").toString();
        ev.user = d.value("op_user_id").toString();
        ev.msg = d.value("name").toString();
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
    // ========== 6. 消息删除事件 ==========
    else if (ev.msgType == "MESSAGE_DELETE") {
        ev.type = 14; ev.subType = 1;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.msgId = d.value("id").toString();
        ev.user = d.value("op_user_id").toString();
    }
    else if (ev.msgType == "PUBLIC_MESSAGE_DELETE") {
        ev.type = 14; ev.subType = 2;
        ev.guildId = d.value("guild_id").toString();
        ev.groupId = d.value("channel_id").toString();
        ev.msgId = d.value("id").toString();
        ev.user = d.value("op_user_id").toString();
    }
    else if (ev.msgType == "DIRECT_MESSAGE_DELETE") {
        ev.type = 14; ev.subType = 3;
        ev.guildId = d.value("guild_id").toString();
        ev.msgId = d.value("id").toString();
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
    // ========== 8. 互动回调事件 ==========
    else if (ev.msgType == "INTERACTION_CREATE") {
        ev.subType = 1;
        ev.callbackId = d.value("id").toString();
        ev.callbackType = d.value("chat_type").toInt();
        ev.msgId = payload.value("id").toString();
        QJsonObject resolved = d.value("data").toObject().value("resolved").toObject();
        ev.msg = resolved.value("button_data").toString();
        ev.user = resolved.value("user_id").toString();
        if (ev.callbackType == 0)
        {
            ev.type = 0;
            ev.groupId = d.value("group_openid").toString();
        }else if (ev.callbackType == 1) {
            ev.type = 1;
            ev.guildId = d.value("guild_id").toString();
            ev.groupId = d.value("channel_id").toString();
        } else if (ev.callbackType == 2){
            ev.type = 2;
            ev.groupId = d.value("user_openid").toString();
        } //频道私聊没按钮
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
    else if (ev.msgType == "GROUP_MEMBER_ADD") {
        ev.type = 0; ev.subType = 2;
        ev.msgId=payload["id"].toString();
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("member_openid").toString();
    }
    else if (ev.msgType == "GROUP_MEMBER_REMOVE") {
        ev.type = 0; ev.subType = 3;
        ev.msgId=payload["id"].toString();
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("member_openid").toString();
    }
    // ========== 12. 未识别事件 ==========
    else {
        ev.type = 99;
        ev.extra = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
        qDebug() << "Unhandled event type:" << ev.msgType;
    }
    if(!ev.fullType) ev.at_you=true; //
    // 解析附件信息（图片、文件、语音、视频等）
    QString extraInfo;

    const QJsonArray attachments = d["attachments"].toArray();
    int i=0;
    for (const QJsonValue &attVal : attachments) {
        QJsonObject att = attVal.toObject();
        QString contentType = att["content_type"].toString();
        QString filename = att["filename"].toString();
        int size = att["size"].toDouble();
        QString url = att["url"].toString();

        if (contentType.contains("image")) {
            int height = att["height"].toDouble();
            int width = att["width"].toDouble();
            extraInfo += QString("[image,height=%1,width=%2,name=").arg(height).arg(width);
        } else if (contentType.contains("file")) {
            extraInfo += "[file,name=";
        } else if (contentType.contains("voice")) {
            extraInfo += "[audio,name=";
        } else if (contentType.contains("video")) {
            extraInfo += "[video,name=";
        } else {
            extraInfo += "[unknown,name=";
        }


        extraInfo += filename + QString(",type=%1,size=%2,url=%3]").arg(contentType).arg(size).arg(url);
        QString text=QString(R"(<faceType=6,faceId="%1",ext="eyJ0ZXh0IjoiIn0=">)").arg(i);
        int index = ev.msg.indexOf(text);
        if (index != -1) {
            ev.msg.replace(index, text.length(), extraInfo);
            extraInfo=QString();
            i++;
        }
    }
    ev.appid = client->m_info->appid_int;
    if (g_botdb.contains(ev.appid))
        ev.user_int = g_botdb[ev.appid]->getOrUpdateUser(ev.user,ev.nickname);//先获取id  并且更新或读取id
    int tabIndex= mapTypeToTabIndex(ev.type);
    if (!extraInfo.isEmpty()) {
        ev.msg += extraInfo;   // 若已有其他 extra 内容，可改为 ev.extra += extraInfo;
    }
    if(ev.type<=3)
    {
        client->m_info->message_received++;
        client->m_info->received++;
        if(ev.fullType && ev.type==0)
        {
            chatPage->addContact(0,ev.appid,ev.groupId,ev.user,ev.nickname,ev.msg,ev.msgId,ev.replyTo); //为对话聊天增加 新成员
        }
        else
        {
            chatPage->addContact(tabIndex,ev.appid,ev.groupId,ev.user,ev.nickname,ev.msg,ev.msgId,ev.replyTo); //为对话聊天增加 新成员
        }
    }

    ev.msg = ev.msg.trimmed();
    if(ev.type ==4 && ev.subType==4 || ev.subType==5)
    {
        if(g_botdb.contains(ev.appid))
        {
            BotDB *db = g_botdb[ev.appid];
            if(ev.subType==4)
                db->addGroup(ev.groupId,QDateTime::currentSecsSinceEpoch()/60,ev.user_int);

            else
                db->deleteGroup(ev.groupId);
        }
    }
    if(ev.type ==5 && ev.subType==6 || ev.subType==7)
    {
        if(g_botdb.contains(ev.appid))
        {
            BotDB *db = g_botdb[ev.appid];
            if(ev.subType==6)
                db->addFriend(ev.user_int,QDateTime::currentSecsSinceEpoch()/60);
            else
                db->removeFriend(ev.user_int);
        }
    }
    if(m_logStore[0].capacity()>=1000)
        ev.log = m_logStore[tabIndex].allocate();
    else
        ev.log=-1;
    ev.msgId= "|"+QString::number(ev.log)+"|"+ev.msgId;
    d["content"] = ev.msg;
    d["id"] = ev.msgId;
    payload["d"] = d;
    payload["user_id"] = ev.user_int;
    payload["appid"]=ev.appid;
    payload["at_you"]=ev.at_you;
    payload["type"]=ev.type;
    ev.raw = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    qDebug() << ev.raw;
    EventTask *task = new EventTask(std::move(ev), [client, info = client->m_info](const MessageEvent &event) {

        Message(info, event);

    });
    QThreadPool::globalInstance()->start(task);

    return ;
}


void QQBotClient::onTextMessageReceived(const QString &message)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        AppendEventLog("收到非法 JSON: " + message.left(200),0xff);
        return;
    }
    QJsonObject obj = doc.object();
    int op = obj.value("op").toInt(-1);
    qint64 s = obj.value("s").toVariant().toLongLong();
    if (s > 0) m_seq = s;

    switch (op) {
    case 10: { // Hello
        int interval = obj.value("d").toObject().value("heartbeat_interval").toInt(30000);
        m_heartbeatIntervalSec = interval / 1000;
        startHeartbeatTimer(m_heartbeatIntervalSec);
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
            resetReconnectAttempts();
            fetchSelfInfo();


            return;
        } else if (eventType == "RESUMED") {
            m_info->online = true;
            m_isConnecting = false;
            AppendEventLog("会话恢复成功",0x594787);
            emit loginSuccess();
            return;
        }
        parseMessageEvent(obj,message,this);
        break;
    }
    case 9: // Invalid Session
        AppendEventLog("鉴权失败：可能订阅了不允许的事件或 token 无效" ,0xff);
        stop();
        scheduleReconnect(10);
        break;
    case 7: // Reconnect
        stopHeartbeatTimer();
        m_webSocket.close();
        break;
    default:
        AppendEventLog(QString("未处理的 op=%1").arg(op) ,0xff);
        break;
    }
}


int ___aaa=0;
// ---------- 发送协议包 ----------
void QQBotClient::sendIdentify()
{

    QJsonObject identify;
    identify["token"] = QString("QQBot %1").arg(m_accessToken);

    identify["intents"] = m_info->wsIntents;//m_info->botqq.toInt()| (1 << ___aaa);//m_info->wsIntents | (1 << 0);
    //qDebug() <<  (m_info->botqq.toInt() | (1 << ___aaa)) << "|" <<___aaa;
    //___aaa++;
    identify["shard"] = QJsonArray{0, 1};

    QJsonObject payload;
    payload["op"] = 2;
    payload["d"] = identify;

    QString msg = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_webSocket.sendTextMessage(msg);
    if(代理)
    {
        m_info->online = true;
        m_isConnecting = false;
        resetReconnectAttempts();
        fetchSelfInfo();

    }

}

void QQBotClient::sendHeartbeat() //心跳
{
    if (!m_info->online && m_webSocket.state() != QAbstractSocket::ConnectedState)
        return;

    QJsonObject hb;
    hb["op"] = 1;
    if (m_seq != 0)
        hb["d"] = m_seq;
    else
        hb["d"] = QJsonValue();

    QString msg = QJsonDocument(hb).toJson(QJsonDocument::Compact);
    m_webSocket.sendTextMessage(msg);
}

void QQBotClient::onHeartbeatTimeout()
{
    if (!m_info->online) return;
    sendHeartbeat();
    if(代理) return;
    m_invalidHeartbeatCount++;
    if (m_invalidHeartbeatCount >= 3) {
        AppendEventLog("连续3次心跳无响应，主动断开重连" ,0xff);
        m_webSocket.close();
    }
}

void QQBotClient::startHeartbeatTimer(int intervalSec)
{
    stopHeartbeatTimer();
    if (intervalSec > 0) {
        m_heartbeatTimer.start(intervalSec * 1000);
        AppendEventLog(QString("心跳定时器已启动，间隔 %1 秒").arg(intervalSec),0x28BF74);
    }
}

void QQBotClient::stopHeartbeatTimer()
{
    if (m_heartbeatTimer.isActive())
        m_heartbeatTimer.stop();
}

// ---------- 重连 ----------
void QQBotClient::scheduleReconnect(int delaySec)
{
    if (m_reconnectAttempts >= 5) {
        AppendEventLog("重连次数已达上限，停止自动重连" ,0xff);
        return;
    }
    m_reconnectAttempts++;
    int wait = delaySec * m_reconnectAttempts;
    AppendEventLog(QString("将在 %1 秒后进行第 %2 次重连...").arg(wait).arg(m_reconnectAttempts),0xD891BC);
    m_reconnectTimer.start(wait * 1000);
}

void QQBotClient::resetReconnectAttempts()
{
    m_reconnectAttempts = 0;
    m_reconnectTimer.stop();
}


QString QQBotClient::_Post(const QString &url, const QJsonObject &json, int timeoutMs)
{
    // 1. 准备 JSON 数据
    QByteArray jsonData = QJsonDocument(json).toJson(QJsonDocument::Compact);

    // 2. 创建请求对象
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("QQBot " + m_accessToken).toUtf8());
    request.setRawHeader("X-Union-Appid", m_info->appid.toUtf8());

    // 3. 创建网络管理器（局部变量，在同步阻塞模式下安全）
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, jsonData);

    // 4. 同步等待：事件循环 + 超时定时器
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();   // 阻塞直到请求完成或超时

    // 5. 处理结果
    QString response= QString::fromUtf8(reply->readAll());

    reply->deleteLater();
    return response;
}
void QQBotClient::fetchSelfInfo()
{
    if (!m_info->online) return;
    QUrl url("https://api.sgroup.qq.com/users/@me");
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
        if (uid.isEmpty()) {
            m_info->nickname = "请检查ip白名单";
            AppendEventLog(QString::fromUtf8(data) ,0xff);
            return;
        }
        m_info->pduid = uid;
        m_info->unid = obj.value("union_openid").toString();
        m_info->nickname = nickname;
        if (!avatarUrl.isEmpty()) {
            QString avatarDir = QCoreApplication::applicationDirPath() + "/avatars/";
            QDir dir;
            if (!dir.exists(avatarDir))
                dir.mkpath(avatarDir);
            QString avatarPath = avatarDir + m_info->appid + ".png";
            downloadAvatar(avatarUrl, avatarPath);
            m_info->avatarPath = avatarPath;
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