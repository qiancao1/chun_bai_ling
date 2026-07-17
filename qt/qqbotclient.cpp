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
#include <QFile>
#include <QDir>

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
    m_info->err.clear();
    AppendEventLog(QString("尝试登录：%1(%2)").arg(m_info->nickname,m_info->appid), 0x0082FD);

    if (!refreshAccessToken()) {

        m_isConnecting = false;
        scheduleReconnect(10);
        return;
    }


    QString wsUrl = m_info->wsAddress;
    if (wsUrl.isEmpty()) {
        wsUrl = fetchGatewayUrl();
        if (wsUrl.isEmpty()) {

            m_isConnecting = false;
            scheduleReconnect(5);
            return;
        }
    }else{
        代理=1;
    }

    if(m_info->type==0)
    {
        QNetworkRequest request(wsUrl);

        request.setRawHeader("User-Agent", "QQBotPlugin/9.9.9 (Node/20.11.0; Linux;纯白铃铛/1.0.19)");
        m_webSocket.open(request);
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


}

void QQBotClient::stop()
{
    m_info->online = false;
    m_info->autoConnect=false;
    m_isConnecting = false;
    if(m_info->type==0){
        stopHeartbeatTimer();
        resetReconnectAttempts();
        if (m_webSocket.state() == QAbstractSocket::ConnectedState)
            m_webSocket.close();
    }

    m_invalidHeartbeatCount = 0;
    m_seq = 0;
    m_sessionId.clear();

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
        m_info->err+="access_token 为空 请设置access_token 后再次登录\n";
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
        m_info->err+="获取登录Ws错误: " + err.errorString()+"\n";
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
        m_info->err+="获取ws地址失败: " + errMsg+"\n";
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
        AppendEventLog("缺少 appid 或 secret，无法获取 AccessToken",0xff);
        m_info->err+="缺少 appid 或 secret，无法获取 AccessToken\n";
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
        QByteArray data = reply->readAll();
        AppendEventLog("刷新 token 网络错误: " + reply->errorString()+"\n错误信息"+data,0xff);
        m_info->err+="刷新 token 网络错误: " + reply->errorString()+"\n错误信息"+data;
        reply->deleteLater();
        return false;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        AppendEventLog("解析 token 响应失败: " + err.errorString()+"\n返回内容："+data,0xff);
         m_info->err+="解析 token 响应失败: " + err.errorString()+"\n返回内容："+data;
        return false;
    }

    QJsonObject obj = doc.object();
    QString newToken = obj.value("access_token").toString();
    if (newToken.isEmpty()) {
        QString errMsg = obj.value("message").toString();
        if (errMsg.isEmpty())
            errMsg = "appid 或 secret 错误（无具体返回信息）";
        AppendEventLog("获取 token 失败: " + errMsg,0xff);
        m_info->err+="获取 token 失败: " + errMsg;
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

    m_isConnecting = false;
    if (wasOnline)
        emit disconnected();
    scheduleReconnect(3);
}

void QQBotClient::onError(QAbstractSocket::SocketError error)
{
    AppendEventLog("WebSocket 错误: " + m_webSocket.errorString(),0xff);
}
void Messages(AccountInfo *info, MessageEvent &ev);
int mapTypeToTabIndex(int type);

void logMessageEvent(const QString &botName, MessageEvent &ev) {


    switch (ev.type) {
    case 0: // 群消息
        if(ev.subType>1)
            ev.msg = QString("%1").arg(ev.subType == 2 ? "群成员添加" : "群成员退群");
        break;
    case 1: // 频道消息
        break;
    case 2: // 私聊消息
        break;
    case 3: // 频道私聊消息
        break;
    case 4: // 群事件
        ev.msg = QString("[群事件] %1 操作者:%2")
                      .arg(ev.subType == 4 ? "被邀请进群" : "被踢出群", ev.user);
        break;
    case 5: // 好友事件

        ev.msg = QString("[好友事件] %1 用户:%2")
                      .arg(ev.subType == 6 ? "添加好友" : "删除好友", ev.user);
        break;
    case 6: // 频道成员事件
        ev.msg = QString("[频道成员事件] %1 频道:%2 用户:%3")
                      .arg(ev.subType == 1 ? "加入" : "离开", ev.guildId, ev.user);
        break;
    case 7: // 回调事件
        ev.msg = QString("[回调事件] 类型:%1 数据:%2").arg(ev.callbackType).arg(ev.msg);
        break;
    case 8: // 审核事件

        ev.msg = QString("[审核事件] %1 消息:%2 原因:%3")
                      .arg(ev.subType == 1 ? "通过" : "拒绝", ev.msgId, ev.msg);
        break;

    case 11: // 审核事件
        ev.msg = QString("[频道事件] %1 频道:%2 操作者:%3")
                      .arg(ev.subType == 1 ? "机器人被邀请进入" : "机器人被踢出频道", ev.groupId, ev.user);
        break;
    case 13: // 审核事件
        ev.msg = QString("[频道事件] %1 频道:%2 操作者:%3")
                      .arg(ev.subType == 1 ? "成员加入" : "成员离开", ev.groupId, ev.user);
        break;

    default:
        ev.msg = QString("[未处理事件] 类型:%1").arg(ev.msgType);
        break;
    }
}
void tiqfuj(const QJsonObject &d ,QString &msg){
    QString extraInfo;

    const QJsonArray attachments = d["attachments"].toArray();


    int i = 0;
    for (const QJsonValue &attVal : attachments) {
        QJsonObject att = attVal.toObject();
        QString contentType = att["content_type"].toString();
        QString filename = att["filename"].toString();
        int size = att["size"].toDouble();
        QString url = att["url"].toString();

        // 构建前缀部分（不变）
        QString prefix;
        if (contentType.contains("image")) {
            int height = att["height"].toDouble();
            int width = att["width"].toDouble();
            prefix = QString("[image,height=%1,width=%2,name=").arg(height).arg(width);
        } else if (contentType.contains("file")) {
            prefix = "[file,name=";
        } else if (contentType.contains("voice")) {
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
                    text = obj["text"].toString();
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
            QString extraInfo = prefix + filename +
                                QString(",type=%1,size=%2,url=%3]")
                                    .arg(contentType)
                                    .arg(size)
                                    .arg(url);
            msg+=extraInfo;
        }
        // 如果未找到，则不递增 i（与原逻辑一致）
    }

}

void QQBotClient::parseMessageEvent(QJsonObject &payload,const QString &text)
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
        if (ev.user.isEmpty()) ev.user = author.value("id").toString();
        QString role= author.value("member_role").toString();
        if(role == "owner")
            ev.member_role = 0;
        else if(role=="admin")
            ev.member_role = 1;
        else if(role == "member")
            ev.member_role = 2;
        ev.nickname = author.value("username").toString();
        ev.bot = author.value("bot").toBool();
        ev.msgId = d.value("id").toString();
        ev.msg = d.value("content").toString();

        if(m_info->unid.isEmpty())
        {
            const QJsonArray array = d["mentions"].toArray();
            for (const QJsonValue &a : array)
            {
                if(a.isObject())
                {
                    QJsonObject arronj = a.toObject();
                    if(!arronj["is_you"].toBool()) continue;
                    ev.at_you = true;
                    m_info->unid= arronj["id"].toString();
                }
                break;
            }
        }

        if(!m_info->unid.isEmpty() && ev.msg.contains(m_info->unid))
        {
            ev.at_you = true;
            ev.msg = subTextReplace(ev.msg,"<@"+m_info->unid+">","");
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
        if(ev.msg.contains(m_info->pduid))
        {
            ev.at_you = true;
            ev.msg = subTextReplace(ev.msg,"<@!"+m_info->pduid+">","");
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
        ev.type = 4; ev.subType = 4;
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("scene_param").toString();
        if (ev.user.isEmpty()) ev.user = d.value("op_member_openid").toString();
        ev.msgId = d.value("id").toString();
        ev.msg = ev.groupId;
        m_info->今日加群数量++;
    }
    else if (ev.msgType == "GROUP_DEL_ROBOT") { //被踢出群
        ev.type = 4; ev.subType = 5;
        ev.groupId = d.value("group_openid").toString();
        ev.user = d.value("op_member_openid").toString();
        ev.msgId = d.value("id").toString();
        m_info->今日退群数量++;
    }
    else if (ev.msgType == "FRIEND_ADD") { //好友增加
        ev.type = 5; ev.subType = 6;
        ev.user = d.value("openid").toString();
        ev.groupId = ev.user;
        ev.msgId = d.value("id").toString();
        m_info->今日好友数量++;

    }
    else if (ev.msgType == "FRIEND_DEL") { //好友删除
        ev.type = 5; ev.subType = 7;
        ev.user = d.value("openid").toString();
        ev.groupId = ev.user;
        ev.msgId = d.value("id").toString();
        m_info->今日删除好友数量++;
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

        if (ev.callbackType == 0)
        {
            ev.type = 1;
            ev.guildId = d.value("guild_id").toString();
            ev.groupId = d.value("channel_id").toString();
            ev.user = resolved.value("user_id").toString();
        }else if (ev.callbackType == 1) {
            ev.type = 0;
            ev.groupId = d.value("group_openid").toString();
            ev.user = d.value("group_member_openid").toString();
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
    tiqfuj(d,ev.msg);
    ev.appid = m_info->appid_int;
    ev.user_int=-1;

    if (g_botdb.contains(ev.appid) && ev.subType<=1)
        ev.user_int = g_botdb [ev.appid]->getOrUpdateUser(ev.user,ev.nickname);//先获取id  并且更新或读取id

    int tabIndex= mapTypeToTabIndex(ev.type);


    ev.msg = ev.msg.trimmed();
    logMessageEvent(m_info->nickname,ev);

    QString tiems=QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    Message mes{ev.user,ev.msg,false,tiems,"",ev.replyTo,ev.msgId};
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
    auto start = std::chrono::steady_clock::now();
    ev.log = g_logdb[tabIndex] ->appendLog(m_info->appid,ev.groupId,mes);
    auto end = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    mes.seq = ev.log;
    logPage->onNewLogAdded(tabIndex,ev.log,m_info->appid_int,ev.groupId,mes);
    if(ws_server) ws_server->broadcastMessage(mes,ev.appid,ev.type,ev.groupId);
    if(ev.type<=3)
    {
        m_info->message_received++;
        m_info->received++;
        if(ev.fullType && ev.type==0)
        {
            chatPage->addContact(0,ev,mes.name); //为对话聊天增加 新成员
        }
        else
        {
            chatPage->addContact(tabIndex,ev,mes.name); //为对话聊天增加 新成员
        }
    }


    if(ev.type ==4 && ev.subType==4 || ev.subType==5)
    {
        if(g_botdb.contains(ev.appid))
        {
            BotDB *db = g_botdb[ev.appid];
            if(ev.subType==4)
                db->addGroup(ev.groupId,QDateTime::currentSecsSinceEpoch()/60,ev.user_int,0);

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

    if (ev.type == 0 && ev.fullType)
    {
        if(shuaping(m_info,ev))
        {
            QString res = delete_messages(ev.type,ev.groupId,ev.msgId);
            qDebug() << res;

            return;
        }
    }

    if(m_info->pbbot && ev.bot) return;
    ev.msgId= "|"+QString::number(ev.log)+"|"+ev.msgId;
    d["content"] = ev.msg;
    d["id"] = ev.msgId;
    payload["d"] = d;
    payload["user_id"] = ev.user_int;
    payload["appid"]=ev.appid;
    payload["at_you"]=ev.at_you;
    payload["type"]=ev.type;
    ev.raw = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    //qDebug() << ev.groupId <<ev.user << ev.msg;
    Messages(m_info, ev);
    return ;
}

class ___tdxx : public QRunnable {
public:
    ___tdxx(QQBotClient *c, const QString &m) : m_client(c), m_msg(m) {
        setAutoDelete(true);
    }

    void run() override {

        QString msg = m_msg;
        m_client->onTextMessage(msg);

    }

private:
    QQBotClient *m_client;
    QString m_msg;
};

void QQBotClient::onTextMessageReceived(const QString &message) {
    auto *task = new ___tdxx(this, message);
    QThreadPool::globalInstance()->start(task);
}


typedef const char* (__cdecl *GetSignatureFunc)(const char*, const char*, const char*);
typedef void (__cdecl *FreeSignatureFunc)(const char*);

static GetSignatureFunc pGetSignature = nullptr;
static FreeSignatureFunc pFreeSignature = nullptr;

static bool loadSignatureDll() {
    if (pGetSignature && pFreeSignature) {
        return true; // 已加载
    }

    QLibrary lib("GetSignature.dll");
    if (!lib.load()) {
        qWarning() << "Failed to load GetSignature.dll:" << lib.errorString();
        return false;
    }

    pGetSignature = (GetSignatureFunc)lib.resolve("GetSignature");
    pFreeSignature = (FreeSignatureFunc)lib.resolve("FreeSignature");

    if (!pGetSignature || !pFreeSignature) {
        qWarning() << "Failed to resolve GetSignature or FreeSignature";
        return false;
    }

    return true;
}

QString webhook_sig(const QJsonObject &obj, const QString &secret) {
    // 1. 提取字段

    QJsonObject d = obj.value("d").toObject();
    QString plain_token = d.value("plain_token").toString();
    QString event_ts = d.value("event_ts").toString();

    if (plain_token.isEmpty() || event_ts.isEmpty()) {
        qWarning() << "Missing plain_token or event_ts";
        return QString();
    }

    // 2. 确保 DLL 已加载
    if (!loadSignatureDll()) {
        return QString();
    }

    // 3. 转换为 UTF-8 字符串（与 Python 的 encode('utf-8') 一致）
    QByteArray plain_token_utf8 = plain_token.toUtf8();
    QByteArray event_ts_utf8 = event_ts.toUtf8();
    QByteArray secret_utf8 = secret.toUtf8();

    // 4. 调用 GetSignature
    // 注意参数顺序：plain_token, event_ts, bot_secret
    const char* sig_cstr = pGetSignature(
        plain_token_utf8.constData(),
        event_ts_utf8.constData(),
        secret_utf8.constData()
        );

    if (!sig_cstr) {
        qWarning() << "GetSignature returned null";
        return QString();
    }

    // 5. 将结果转为 QString（假设签名是十六进制字符串，UTF-8 编码）
    QString signature = QString::fromUtf8(sig_cstr);

    // 6. 释放内存
    pFreeSignature(sig_cstr);


    QJsonObject res;
    res["plain_token"] = plain_token;
    res["signature"] = signature;
    QString text=  QJsonDocument(res).toJson(QJsonDocument::Compact);

    return text;
}
QString QQBotClient::onTextMessage(const QString &message)
{
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        AppendEventLog("收到非法 JSON: " + message.left(200),0xff);
        return QString();
    }
    QJsonObject obj = doc.object();
    int op = obj.value("op").toInt(-1);
    qint64 s = obj.value("s").toVariant().toLongLong();
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
        AppendEventLog("鉴权失败：可能订阅了不允许的事件或 token 无效" ,0xff);
        QMetaObject::invokeMethod(this, [=]() {
            stop();
            scheduleReconnect(10);
        });
        break;
    case 7: // Reconnect
        if(m_info->type==0){
            QMetaObject::invokeMethod(this, [=]() {
                stopHeartbeatTimer();
                m_webSocket.close();
            });
        }
        break;
    case 13: // webhook认证

        res = webhook_sig(obj,m_info->secret);

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

    identify["intents"] = m_info->wsIntents;//m_info->botqq.toInt()| (1 << ___aaa);//m_info->wsIntents | (1 << 0);
    //qDebug() <<  (m_info->botqq.toInt() | (1 << ___aaa)) << "|" <<___aaa;
    //___aaa++;
    identify["shard"] = QJsonArray{0, 1};

    QJsonObject payload;
    payload["op"] = 2;
    payload["d"] = identify;
    //"QQBotPlugin/9.9.9 (Node/20.11.0; Linux; 自定义显示名称/1.0.19)"
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
    if(!m_info->autoConnect) return;
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
QString QQBotClient::_Post(const QString &url, const QByteArray &jsonData, const QString &ContentTypeHeader,int timeoutMs)
{


    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader,ContentTypeHeader);
    request.setRawHeader("Authorization", QString("QQBot " + m_accessToken).toUtf8());
    request.setRawHeader("X-Union-Appid", m_info->appid.toUtf8());


    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, jsonData);


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
QString QQBotClient::_Get(const QString &url, int timeoutMs)
{


    // 2. 创建请求对象
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("QQBot " + m_accessToken).toUtf8());
    request.setRawHeader("X-Union-Appid", m_info->appid.toUtf8());

    // 3. 创建网络管理器（局部变量，在同步阻塞模式下安全）
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(request);

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