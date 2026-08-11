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

#ifndef QQBOTCLIENT_H
#define QQBOTCLIENT_H

#include <QObject>
#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QTimer>
#include "AccountInfo.h"
#include <QColor>
#include <future>
#include <qnetworkreply.h>

struct logdb
{
    QString groupId;     // 群id / 子频道id / 私聊对方的id
    QString user;       // 发送人id (用户openid或member_openid) hex32字节
    QString msgId;      // 消息id
    QString msg;        // 消息内容 (已去除@前缀等)
    QString nickname;       // 发送人昵称
    QString replyTo;        // 引用回复的消息id (message_scene字段)
    int member_role=-1;     //0群主 1管理 2群成员
};


struct MessageEvent
{
    QString groupId;        // 群id / 子频道id / 私聊对方的id
    QString user;       // 发送人id (用户openid或member_openid)
    QString msgId;          // 消息id
    QString msg;        // 消息内容 (已去除@前缀等)
    qint64 seq = 0;         // 消息序号 (用于去重/过滤)
    int appid = 0;
    int user_int=0;
    int type = 0;           // 0群 1频道 2私聊 3频道私聊
    int subType=0;          //ai整的没啥用
    int callbackType = 0;   // 回调回应来源: 0群 1频道 2私聊 3频道私聊
    int member_role=-1;     //0群主 1管理 2群成员
    int bitmap=0;//群相关配置
    bool fullType = false;  // 全量标识 这条信息来自全量
    bool at_you=false;
    bool bot=false;         //true时 为机器人
    QString nickname;       // 发送人昵称
    QString groupname;       // 群昵称
    QString guildId;        // 频道id (仅频道消息有效)
    QString msgType;        // 原始事件类型字符串 (如 "GROUP_AT_MESSAGE_CREATE")
    QString extra;          // 附加信息 (图片等资源，可扩展)
    QString raw;        // 原始JSON (d对象)
    QString callbackId;     // 回调事件id (用于INTERACTION_CREATE)
    QString replyTo;        // 引用回复的消息id (message_scene字段)
    uint64_t log=0;
    QString toString() const;
};
struct MessageLogContext {
    QString openid;
    QString pname;
    QString jsonString;
    qint64 now_us;
    int index;
    int type;


};
using Callback = std::function<void(const QString&, QNetworkReply::NetworkError)>;

Q_DECLARE_METATYPE(MessageEvent)   // 这行必须加在结构体定义之后
class QQBotClient : public QObject
{
    Q_OBJECT
public:

    explicit QQBotClient(AccountInfo *info, QObject *parent = nullptr);
    ~QQBotClient();

    // 连接控制
    void start();       // 启动连接（如果已 online 则无效）
    void stop();        // 停止连接并清理

    bool isOnline() const { return m_info->online; }
    AccountInfo *m_info;                // 指向外部原始 AccountInfo
    int m_reconnectAttempts;
    QString onTextMessage(const QString &message);
    // 发送消息接口
    QString send_messages(int type, const QString &openid, QString &pname, QString &text, const QString &msgid=QString(),
                          bool is_wakeup=false, bool mode=false, int = 0, bool noref=false);
    QString send_messagesAsync(int type, const QString &openid,QString &pname, QString &text,
                                            const QString &msgid,bool is_wakeup=false,bool mode=false,int 发送类型=0,bool noref=false);
    QString send_messages(int type, const QString &openid, const QString &text, const QString &info,
                          const QJsonArray &prompt_keyboard,
                          const QString &message_reference, const QString &msgid,
                          bool is_wakeup, int seq_index, const MessageLogContext ctx, bool noref);

    QString send_messages_ark(int type, const QString &openid, QString &pname, const QJsonObject &ark,
                              const QString &msgid, bool is_wakeup=false, int seq_index=0,const MessageLogContext ctx = MessageLogContext());



    QString send_messages_markdown(int type, const QString &openid, const QString &markdown, const QJsonArray prompt_keyboard,
                                   const QJsonObject keyboard, const QString &message_reference,
                                   const QString &msgid, bool is_wakeup=false, int seq_index=0, const MessageLogContext ctx = MessageLogContext(), bool noref=false);


    QString send_messages_mb(int type, const QString &openid, const QString &markdown, const QJsonArray prompt_keyboard,
                             const QJsonObject keyboard, const QString &message_reference,
                             const QString &msgid, bool is_wakeup, int seq_index, const MessageLogContext ctx, bool noref);


    QString send_messages_pd(const QString &url, const QString &msgId, const QString &content, const QString &imagePath,
                             const QString &message_reference, int seq_index, const MessageLogContext ctx, bool noref);
    //上传富媒体(分片)
    QString uploadRichMediaA(int targetType, const QString& groupId,int fileType, const QString& filePath, bool &ok);
    QString uploadRichMediaB(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename, bool &ok);
    QString del_members (int type,const QString& group,const QString &user,bool add_blacklist = false,int delete_history_msg_days=0);
    //撤回
    QString delete_messages(int type, const QString &openid, const QString &msgid);
    void delete_messages_Async(int type, const QString &openid, const QString &msgid);
    //获取邀请链接
    QString get_members_list(const QString& group, int limit, int index);
    QString get_groups_members(const QString& group,const QString& user);
    QString generate_share_link(const QString& callback_data);
    QString get_groups_list(int limit, int index);

    QString get_users_list(int limit,int index);
    //回应回调
    QString respond_interaction(const QString &interaction_id, int code, const QString &data = QString());
    QString get_groups_info(const QString& group);
    QString get_groups_bot_state(const QString& group);
    QString set_mute(const QString& group,const QString &user,qint64 mute_seconds);

    QString setGroupRestrictChatSetting(const QString& groupOpenId,
                                                        const QString& memberOpenId,
                                                        int muteSeconds = 60);
    void setGroupRestrictChatSetting_Async(const QString& groupOpenId,
                                              const QString& memberOpenId,
                                              int muteSeconds, Callback callbacks);
    QString approveGroupJoinRequest(const QString& group,const QString& user,
                                                 bool op,const QString& joinRequestId,
                                                 const QString& rejectReason=QString(),bool addToBlacklist=false);
    void approveGroupJoinRequest_Async(const QString& group, const QString& user,
                                    bool op, const QString& joinRequestId,
                                    const QString& rejectReason=QString(), bool addToBlacklist=false, Callback callbacks = Callback());
    QString setGroupRestrictChatSetting(const QString& group, const QJsonArray &membersJson);
    QString getjoin_request_list(const QString& group, int limit=20, const QString &cursor=QString());
    QString getGroupRestrictChatSetting(const QString& group);



public slots:
    void onTextMessageReceived(const QString &message);

signals:
    void loginSuccess();

    void disconnected();
    void messageReceived(const QJsonObject &payload);
    void avatarDownloaded();


private slots:
    void onConnected();
    void onDisconnected();

    void onError(QAbstractSocket::SocketError error);
    void onHeartbeatTimeout();


private:
    // 网关和 token
    void parseMessageEvent(QJsonObject &payload,const QString &text);
    QString fetchGatewayUrl();
    bool refreshAccessToken();
    void initjgt(QJsonObject &json, const QJsonArray &prompt_keyboard, const QString &message_reference, const QString &msgid, bool is_wakeup, int logindex);
    QString send_Media(int type, const QString &openid, QString &pname, const QString &info, qint64 now_us,
                       const QString &msgid, bool is_wakeup, bool noref, MessageLogContext ctx);
    QString sendOneMedia(int type, const QString &openid, QString &pname, QString &text, qint64 now_us, const QString &msgid, bool is_wakeup, bool mode, int, bool noref, MessageLogContext ctx);
    QString uploadRichMedia(int targetType, const QString& groupId, int fileType, const QString& filePath, qint64& expireTime, QString &md5, bool &ok, QString &outurl);
    QString uploadRichMedia(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename,
                            qint64& expireTime,QString &md5, bool &ok, QString &outurl);
    QString uploadRichMedia_url(int targetType, const QString& openid,int fileType, const QString& fileurl,qint64& expireTime,bool &ok);
    void addmsglog(const QString &response, int index, const QString &pname, const QString &text, qint64 now_us, int type, const QString &openid);
    void bianl(int type, int log, QString &text, QJsonObject &keyboard, QJsonArray &prompt_keyboard, const QString &openid, QString &mb);
    // WebSocket 协议
    void sendIdentify();
    void sendHeartbeat();
    void startHeartbeatTimer(int intervalSec);
    void stopHeartbeatTimer();

    // 重连
    void scheduleReconnect(int delaySec = 3);
    void resetReconnectAttempts();
    void fetchSelfInfo();   // 获取机器人自身信息
    void downloadAvatar(const QString &url, const QString &savePath);
    QString _Post(const QString &url,const QJsonObject &json, int timeoutMs = 30000);
    QString _Post(const QString &url, const QByteArray &jsonData, const QString &ContentTypeHeader,int timeoutMs = 30000);
    QString _Get(const QString &url, int timeoutMs);

    QString PostSync(const QString &url, const QByteArray &jsonData, const QString &contentType, int timeoutMs);
    QString PostSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs);
    QString GetSync(const QString &url, const QString &contentType, int timeoutMs);
    QString PatchSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs) ;

    QString put(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs);
    std::future<QString> put2(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs);
    QString DeleteSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs);
    void DeleteAsync(const QString &url, const QJsonObject &jsonData, const QString &contentType);
    QString processImageTags(QString &text, int type, QString &info, int targetType, const QString &openid, QString &message_reference);



    // 异步 POST，自动处理 token 过期刷新和去重重试（递归实现）
    void PostAsync(const QString& url, const QJsonObject& json,
                   const QString& contentType, int timeoutMs,
                   Callback finalCallback);
    void doPost(const QString& url, const QJsonObject& json,
                         const QString& contentType, int timeoutMs,
                Callback finalCallback, int retryCount);
    void postRawAsync(const QString &url, const QByteArray &data,
                               const QHash<QString, QString> &headers, int timeoutMs,
                      Callback callback);
private:


    QWebSocket m_webSocket;
    QNetworkAccessManager m_nam;
    QTimer m_heartbeatTimer; //心跳
    QTimer m_reconnectTimer; //重连
    bool 代理=false;
    bool suo=false;
    QString m_accessToken;              // 运行时 token
    qint64 m_tokenExpireTime;           // 过期时间戳（秒）
    QString m_sessionId;
    qint64 m_seq;                       // 消息序号（用于心跳）
    bool m_isConnecting;

    int m_heartbeatIntervalSec;
    int m_invalidHeartbeatCount;
};

#endif // QQBOTCLIENT_H