#ifndef QQBOTCLIENT_H
#define QQBOTCLIENT_H

#include <QObject>
#include <QWebSocket>
#include <QNetworkAccessManager>
#include <QTimer>
#include "AccountInfo.h"
#include <QColor>

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
    bool fullType = false;  // 全量标识 这条信息来自全量
    bool at_you=false;
    QString nickname;       // 发送人昵称
    QString guildId;        // 频道id (仅频道消息有效)
    QString msgType;        // 原始事件类型字符串 (如 "GROUP_AT_MESSAGE_CREATE")
    QString extra;          // 附加信息 (图片等资源，可扩展)
    QString raw;        // 原始JSON (d对象)
    QString callbackId;     // 回调事件id (用于INTERACTION_CREATE)
    QString replyTo;        // 引用回复的消息id (message_scene字段)
    int log=0;
    // 辅助函数：将结构体转为可读字符串 (调试用)
    QString toString() const;
};

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

    // 发送消息接口
    QString send_messages(int type, const QString &openid, QString &pname, QString &text, const QString &msgid=QString(),
                          bool is_wakeup=false, bool mode=false);
    QString send_messages(int type, const QString &openid, const QString &text, const QString &info,
                          const QJsonArray &prompt_keyboard,
                          const QString &message_reference, const QString &msgid,
                          bool is_wakeup);
    QString send_messages_ark(int type, const QString &openid, QString &pname, const QJsonObject &ark,
                              const QString &msgid, bool is_wakeup=false);
    QString send_messages_ark(int type,const QString &openid,const QJsonObject &ark,const QJsonArray prompt_keyboard,
                              const QString &msgid,bool is_wakeup=false);

    QString send_messages_markdown(int type,const QString &openid,const QString &markdown,const QJsonArray prompt_keyboard,
                                   const QJsonObject keyboard,const QString &message_reference,
                                   const QString &msgid,bool is_wakeup=false);
    //上传富媒体(分片)
    QString uploadRichMediaA(int targetType, const QString& groupId,int fileType, const QString& filePath, bool &ok);
    QString uploadRichMediaB(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename, bool &ok);

    //撤回
    QString delete_messages(int type, const QString &openid, const QString &msgid);
    //获取邀请链接
    QString generate_share_link(const QString& callback_data);
    //回应回调
    QString respond_interaction(const QString &interaction_id, int code, const QString &data = QString());


signals:
    void loginSuccess();
    void loginFailed(const QString &reason);
    void disconnected();
    void messageReceived(const QJsonObject &payload);
    void avatarDownloaded();


private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onError(QAbstractSocket::SocketError error);
    void onHeartbeatTimeout();


private:
    // 网关和 token
    QString fetchGatewayUrl();
    bool refreshAccessToken();
    void initjgt(QJsonObject &json,const QJsonArray &prompt_keyboard,const QString &message_reference, const QString &msgid, bool is_wakeup);
    QString send_Media(int type, const QString &openid, QString &pname, const QString &info,  qint64 now_us,const QString &msgid, bool is_wakeup);
    QString sendOneMedia(int type, const QString &openid, QString &pname, QString &text, qint64 now_us, const QString &msgid, bool is_wakeup);
    QString uploadRichMedia(int targetType, const QString& groupId, int fileType, const QString& filePath, qint64& expireTime, QString &md5, bool &ok);
    QString uploadRichMedia(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename,
                            qint64& expireTime,QString &md5, bool &ok);
    QString uploadRichMedia_url(int targetType, const QString& openid,int fileType, const QString& fileurl,qint64& expireTime,bool &ok);
    void addmsglog(QString &response, int index, QString &pname, const QString &text, qint64 now_us, int type, QString &msgid, const QString &openid);
    void bianl(int type, int log, QString &text, QJsonObject &keyboard, QJsonArray &prompt_keyboard);
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
    QString processImageTags(QString &text, int type, QString &info, int targetType, const QString &openid, QString &message_reference);

private:


    QWebSocket m_webSocket;
    QNetworkAccessManager m_nam;
    QTimer m_heartbeatTimer;
    QTimer m_reconnectTimer;
    bool 代理=false;
    QString m_accessToken;              // 运行时 token
    qint64 m_tokenExpireTime;           // 过期时间戳（秒）
    QString m_sessionId;
    qint64 m_seq;                       // 消息序号（用于心跳）
    bool m_isConnecting;

    int m_heartbeatIntervalSec;
    int m_invalidHeartbeatCount;
};

#endif // QQBOTCLIENT_H