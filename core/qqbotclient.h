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
#include "accountinfo.h"
#include <QColor>
#include <future>
#include <memory>
#include <qnetworkreply.h>
using Callback = std::function<void(const QString&, QNetworkReply::NetworkError)>;

// 「上传富媒体」异步链的统一回调（同步版靠出参 ok / expireTime / md5 / outurl 回值，异步版改走这里）
//   result     ：成功 = file_info；失败 = 平台错误文本（原样发给用户即可）
//   expireTime ：链接过期时间（秒）；URL 直传那条没有 raw_url，恒为 0
//   md5        ：本地文件上传的真实 md5；URL 直传为空
//   ok         ：成败
//   outurl     ：加过时间戳的 raw_url（只有走 prepare/finish/files 的本地文件才有）
using MediaUploadDone = std::function<void(const QString &result, qint64 expireTime,
                                           const QString &md5, bool ok, const QString &outurl)>;
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
    QString user2;   //事件可能用到
    QString nickname;       // 发送人昵称
    QString nickname2;       // 安全昵称 经过审核的昵称 可能是空
    QString groupname;       // 群昵称
    QString guildId;        // 频道id (仅频道消息有效)
    QString msgType;        // 原始事件类型字符串 (如 "GROUP_AT_MESSAGE_CREATE")
    QString extra;          // 附加信息 (图片等资源，可扩展)
    QString raw;        // 原始JSON (d对象)
    QString callbackId;     // 回调事件id (用于INTERACTION_CREATE)
    QString replyTo;        // 引用回复的消息id (message_scene字段)
    uint32_t qid[20];
    uint64_t log=0;
    qint64 seq = 0;         // 消息序号 (用于去重/过滤)
    int appid = 0;
    int user_int=0;
    int type = 0;           // 0群 1频道 2私聊 3频道私聊
    int subType=0;          //ai整的没啥用
    int callbackType = 0;   // 回调回应来源: 0群 1频道 2私聊 3频道私聊
    int member_role=-1;     //0群主 1管理 2群成员
    int admin_role=0;     //0无权限 1机器人管理 2后台管理
    int bitmap=0;//群相关配置
    bool fullType = false;  // 全量标识 这条信息来自全量
    bool at_you=false;
    bool bot=false;         //true时 为机器人
    bool bot_admin = false; //true时 机器人是管理员 不是就不要多管闲事了
    bool op=false; //本条消息是否被处理

    QString toString() const;
};
struct MessageLogContext {
    QString openid;
    QString pname;
    QString jsonString;
    Callback cb;
    qint64 now_us;
    int index;
    int type;


};
struct CosPutPoolEntry {
    QString presignedUrl;   // 可重复 put 的 cos 链接
    QString uploadId;       // upload_prepare 返回的 upload_id
    int     partIndex = 0;  // 分片 index（100K 申请只有 1 片）
    qint64  expireAt = 0;     // 链接诞生时刻 + 55 分钟（毫秒），cos 实际 60 分钟
    QString fileInfo;         // 首次 /files 返回的 file_info —— 同 upload_id 固定不变，复用时直接用
    QString rawUrl;           // 首次 /files 返回的原始 raw_url（不带时间戳，出参时再加工）
    int     infoFileType = -1; // fileInfo 是哪类 fileType 上传后拿到的（捷径防跨类型误用）
};
using Callback2 = std::function<void()>;

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
    QString onTextMessage(const QByteArray &message);
    // 发送消息接口

    QString send_msgAsync(int type, const QString &openid, const QString &pname, QString &text,
                          const QString &msgid, bool is_wakeup=false, bool mode=false, int sendType=0, bool noref=false, Callback cb=Callback());

    QString send_messages(int type, const QString &openid, const QString &pname, QString &text, const QString &msgid=QString(),
                          bool is_wakeup=false, bool mode=false, int sendType= 0, bool noref=false);
    QString send_messagesAsync(int type, const QString &openid,const QString &pname, QString &text,
                                            const QString &msgid,bool is_wakeup=false,bool mode=false,int sendType=0,bool noref=false,Callback cb=Callback());

    QString send_messagesAsync2(int type, const QString &openid, const QString &pname, QString &text,
                                const QString &msgid, bool is_wakeup, bool mode, int sendType, bool noref, const QString &mb2,
                                const QJsonArray &prompt_keyboard, const QJsonValue &keyboard,Callback cb=Callback());

    QString send_messages(int type, const QString &openid, const QString &text, const QString &info,
                          const QJsonArray &prompt_keyboard,
                          const QString &message_reference, const QString &msgid,
                          bool is_wakeup, int seq_index, const MessageLogContext ctx, bool noref);

    QString send_messages_ark(int type, const QString &openid,const QString &pname, const QJsonObject &ark,
                              const QString &msgid, bool is_wakeup=false, int seq_index=0,const MessageLogContext ctx = MessageLogContext());



    QString send_messages_markdown(int type, const QString &openid, const QString &markdown, const QJsonArray &prompt_keyboard,
                                   const QJsonValue &keyboard, const QString &message_reference,
                                   const QString &msgid, bool is_wakeup=false, int seq_index=0, const MessageLogContext ctx = MessageLogContext(), bool noref=false);


    QString send_messages_mb(int type, const QString &openid, const QString &markdown, const QJsonArray &prompt_keyboard,
                             const QJsonValue &keyboard, const QString &message_reference,
                             const QString &msgid, bool is_wakeup, int seq_index, const MessageLogContext ctx, bool noref);


    QString send_messages_pd(const QString &url, const QString &msgId, const QString &content, const QString &imagePath,
                             const QString &message_reference, int seq_index, const MessageLogContext ctx, bool noref);
    //上传富媒体(分片)
    QString uploadRichMediaA(int targetType, const QString& groupId,int fileType, const QString& filePath, bool &ok);
    QString uploadRichMediaB(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename, bool &ok);
    QString del_members (const QString& group, const QString &user_list, bool add_blacklist = false, Callback callbacks=Callback());

    //获取黑名单
    QString get_member_blacklist (const QString& group, const QString &cursor, Callback callbacks=Callback());
    QString member_blacklist (const QString& group,const QString &user_list,bool op,Callback callbacks=Callback()) ;
    //撤回
    QString delete_messages(int type, const QString &openid, const QString &msgid,Callback callbacks=Callback());
    //获取邀请链接
    QString get_members_list(const QString& group, const QString &cursor, Callback callbacks=Callback());
    QString get_groups_members(const QString& group,const QString& user,Callback callbacks=Callback());
    QString generate_share_link(const QString& callback_data,Callback callbacks=Callback());
    QString get_groups_list(const QString &cursor, Callback callbacks=Callback());

    QString get_users_list(const QString &cursor, Callback callbacks=Callback());
    //回应回调
    QString respond_interaction(const QString &interaction_id, int code, const QString &data = QString(), Callback callbacks = Callback());
    QString get_groups_info(const QString& group,Callback callbacks=Callback());
    QString get_groups_bot_state(const QString& group,Callback callbacks=Callback());
    QString set_mute(const QString& group, const QString &user, qint64 mute_seconds);
    void onRefreshReplyFinished();




    QString getjoin_request_list(const QString& group, int limit=20, const QString &cursor=QString(),Callback callbacks=Callback());
    QString getGroupRestrictChatSetting(const QString& group,Callback callbacks=Callback());

    //支持异步api


    QString setGroupRestrictChatSetting(const QString& groupOpenId,const QString& memberOpenId,int muteSeconds, Callback callbacks = Callback());
    QString setGroupRestrictChatSetting(const QString& group, const QJsonArray &membersJson, Callback callbacks = Callback());
    QString approveGroupJoinRequest(const QString& group, const QString& user,bool op, const QString& joinRequestId,
                                    const QString& rejectReason=QString(), bool addToBlacklist=false, Callback callbacks = Callback());

    QString getMenu(Callback callbacks);
    QString updateMenu(const QJsonObject& menuData, Callback callbacks);


    // ==================== 指令面板接口 ====================
    QString createPanel(const QJsonObject& panelData, Callback callbacks);
    QString listPanels(const QString& scope, int limit, const QString& cursor, Callback callbacks);
    QString getPanel(const QString& panelId, Callback callbacks);
    QString updatePanel(const QString& panelId, const QJsonObject& panelData, Callback callbacks);
    QString deletePanel(const QString& panelId, Callback callbacks);
    QString updatePanelTarget(const QString& panelId, const QJsonObject& targetData, Callback callbacks);
void parseMessageEvent(QJsonObject &payload,const QString &text);

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

    void fetchGatewayUrl(Callback calls);
    // token 刷新已改由主线程定时任务 onRefreshReplyFinished() 统一处理
    void initjgt(QJsonObject &json, const QJsonArray &prompt_keyboard, const QString &message_reference, const QString &msgid, bool is_wakeup, int logindex);
    QString send_Media(int type, const QString &openid, const QString &pname, const QString &info, qint64 now_us,
                       const QString &msgid, bool is_wakeup, bool noref, MessageLogContext ctx);
    // ctx 只读：函数内部仅取 ctx.openid / ctx.cb，并按值转发给 send_Media。
    // 必须是 const 引用 —— 否则调用方传临时对象（如 send_messages 传 MessageLogContext()）
    // 在 GCC/Clang 下报 “cannot bind non-const lvalue reference to an rvalue”，
    // MSVC 把这个当语言扩展放过了，所以 Windows 能过、Linux 不能。
    QString sendOneMedia(int type, const QString &openid, const QString &pname, QString &text, qint64 now_us, const QString &msgid, bool is_wakeup, bool mode, int, bool noref, const MessageLogContext &ctx);
    // sendOneMedia 的**纯回调版**：整条链没有任何阻塞点，也不新建线程（给 send_messagesAsync 用）。
    //   无 [file|audio|video,...] 媒体标签 → 直接同步回调（绝大多数发送走这条）；
    //   有媒体标签 → md5/缓存 → ffmpeg 探测/转码/切段（QProcess 异步，进程退出才回调）
    //     → 上传（PostAsync / 回调式 put）→ send_Media，逐条标签推进，最后才回调 onDone。
    // ⚠ 只做「发媒体」这一件事：文本摘除后的正文发送仍由调用方在 onDone 里继续（见 send_messagesAsync）。
    // onDone(textOut, mediaResp)：textOut = 摘掉媒体标签后的文本；mediaResp = 本条链最后一次 send_Media 的返回
    //   （纯媒体、正文被吃光时非空；否则为空串）。text 按值传入，链内改动不影响调用方。
    void sendOneMediaAsync(int type, const QString &openid, const QString &pname, QString text, qint64 now_us,
                           const QString &msgid, bool is_wakeup, bool mode, int sendType, bool noref,
                           const MessageLogContext &ctx,
                           std::function<void(const QString &, const QString &)> onDone);

    // ── 上传的异步回调版（与同名同步函数逐段对应；把 PostSync / put 换成 PostAsync / 回调式 put）──
    // 递归一律写成「成员函数 + shared_ptr 状态」，**不用**自引用的 std::function（那会形成引用环永不释放）。
    void uploadRichMedia_urlAsync(int targetType, const QString &openid, int fileType,
                                  const QString &fileurl, MediaUploadDone onDone);
    void uploadSmallVideoAsync(int targetType, const QString &openid, const QByteArray &data,
                               const QString &filename, const QString &md5, const QString &sha1,
                               const QString &md5_10m, MediaUploadDone onDone);
    void uploadRichMediaAsync(int targetType, const QString &openid, int fileType,
                              const QByteArray &data, const QString &filename, MediaUploadDone onDone);
    // uploadRichMediaPoolA 的异步版：URL → 注册链接；本地文件 → 读进内存后交给上面的异步上传器。
    // ⚠ 出参与同步版**对齐**：成功时 result 是整串 "[<类型>,path=<file_info>,md5=<md5>,Time=<过期秒>]"
    //   （底层三个上传器只回裸 file_info，由这里补包装）；失败时不包装，原样透传平台报错文本。
    //   两个下游都只认这个格式：send_Media 要 extractBetween(info,"path=",",")，媒体缓存要 ",Time="。
    void uploadRichMediaPoolAAsync(int targetType, const QString &openid, int fileType,
                                   const QString &filePath, bool usePool, MediaUploadDone onDone);

    // 状态结构体的定义在 api.cpp，这里只前向声明
    struct FilesRegJob;      // 通用「POST /files 直到拿到 file_info」重试器
    struct RichUploadJob;    // 本地文件多分片上传
    struct SmallVideoJob;    // ≤80M 视频快传
    // 通用 /files 注册重试器（同步版里那几处「重试 10 次」的循环全归它）
    void filesRegStep(std::shared_ptr<FilesRegJob> job);
    void richUploadReport(std::shared_ptr<RichUploadJob> job, const QString &result,
                          qint64 expireTime, bool ok, const QString &outurl);
    void richUploadPrepareDone(std::shared_ptr<RichUploadJob> job, const QString &response);
    void richUploadPutNext(std::shared_ptr<RichUploadJob> job);
    void richUploadFinishAll(std::shared_ptr<RichUploadJob> job);
    void richUploadFinishAllFiles(std::shared_ptr<RichUploadJob> job);
    void smallVideoReport(std::shared_ptr<SmallVideoJob> job, const QString &result,
                          qint64 expireTime, bool ok, const QString &outurl);
    void smallVideoPrepareDone(std::shared_ptr<SmallVideoJob> job, const QString &response);
    void smallVideoSubmit(std::shared_ptr<SmallVideoJob> job);

    // ── 媒体标签异步链（三个状态结构体的定义在 api.cpp，这里只前向声明，实现细节不外露）──
    struct MediaSendChain;
    struct MediaTagCtx;
    struct MediaSegJob;
    void mediaChainStep(std::shared_ptr<MediaSendChain> st);
    void mediaChainFinishTag(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc);
    void mediaChainUploadAndSend(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc);
    void mediaChainSegLoop(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc,
                           std::shared_ptr<MediaSegJob> job);
    void mediaRemoteAudioFallback(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc,
                                  const QString &url, const QString &errInfo);
    QString uploadRichMedia(int targetType, const QString& groupId, int fileType, const QString& filePath, qint64& expireTime, QString &md5, bool &ok, QString &outurl);
    QString uploadRichMedia(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename,
                            qint64& expireTime,QString &md5, bool &ok, QString &outurl);
    QString uploadRichMedia_url(int targetType, const QString& openid,int fileType, const QString& fileurl,qint64& expireTime,bool &ok);
    //小视频(≤80M)快速上传：prepare 固定申请 1K → 整段写入 put 链接 → 按真实数据提交 →
    //files 注册（视频处理可能「富媒体文件上传超时」，循环重试 10 次）
    QString uploadSmallVideo(int targetType, const QString& openid, const QByteArray& data, const QString& filename,
                             qint64& expireTime, const QString& md5, const QString& sha1, const QString& md5_10m,
                             bool& ok, QString& outurl);
    //复用 cos put 链接的快速上传（put 链接池，55 分钟超时 / 发送完成即回池无 CD / raw_url 加时间戳防缓存）
    //usePool=false 时只用「100K 申请 + 整文件直传」的快速路径，不入池（给音视频/文件用）；targetType==4 回退原始上传
    QString uploadRichMediaPool(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename,
                                qint64& expireTime,QString &md5, bool &ok, QString &outurl, bool usePool=true);
    //uploadRichMediaA 的池子版：入参/返回格式与 A 完全一致（[type,path=...,md5=...,Time=...]），可直接换调用点
    QString uploadRichMediaPoolA(int targetType, const QString& openid,int fileType, const QString& filePath, bool &ok, bool usePool=true);
    //uploadRichMediaPool 的纯回调版：prepare → put → finish → files 全链路异步（零线程零阻塞），图片热路径专用
    //onDone 在 NetManager 线程池线程触发（不可在里面阻塞），成败都会回调；出参语义与阻塞版一致：
    //result=返回串（成功=file_info，失败=错误信息/空）、ok 标记成败、outurl=加时间戳的 raw_url
    void uploadRichMediaPoolAsync(int targetType, const QString& openid, int fileType,
                                  const QByteArray& data, const QString& filename, bool usePool,
                                  std::function<void(const QString &result, qint64 expireTime,
                                                     const QString &md5, bool ok, const QString &outurl)> onDone);
    // pname 传值：插件侧直接把**发起方 uuid**当 pname 传（不带 '['）→ 内部先用 pluginPnameText()
    // 还原成展示标签 "[插件名|%1ms]" 再显示；带 '[' 的普通标签原样用。
    void addmsglog(const QString &response, int index, QString pname, const QString &text, qint64 now_us, int type, const QString &openid);
    void bianl(int type, int log, QString &text, QJsonValue &keyboard, QJsonArray &prompt_keyboard, const QString &openid, QString &mb);
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





    QString Post(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs, Callback finalCallback=Callback());
    QString Get(const QString &url,const QString &contentType, int timeoutMs,Callback finalCallback=Callback());


    void GetAsync(const QString &url, const QString &contentType, int timeoutMs, Callback callbacks=Callback()) ;
    QString GetSync(const QString &url, const QString &contentType, int timeoutMs);
    QString PatchSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs) ;
    QString put2(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs, Callback callbacks) ;
    QString put(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs);
    std::future<QByteArray> put2(const QString &url, const QByteArray &data, const QString &contentType, int timeoutMs);

    QString Delete(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs, Callback callbacks);
    QString DeleteSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs=30000);
    void DeleteAsync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs=30000, Callback callbacks=Callback());
    QString processImageTags(QString &text, int type, QString &info, int targetType, const QString &openid, QString &message_reference);
    //processImageTags 纯回调版（堵塞版不受影响）：type==1 热路径零线程——
    //阶段1（同步微秒级）解析+缓存判断+读文件 → 待上传项走 uploadimgCb 回调链（不占线程）
    //→ 原子计数聚合 → 最后回调所在线程替换文本并回调 onDone(text, info, message_reference)。
    //仅两类罕见路径临时起线程：uploadimg 全失败走阻塞备用上传；type==0/2 阻塞型上传单线程包装。
    //onDone 里可直接接着违禁词过滤+发消息（备用线程 park 的池条目会在收尾时 re-park 到
    //onDone 线程，发送走带 ctx 的 PostAsync，回池由发送回调负责）。
    void processImageTagsAsync(const QString &text, int type, int targetType, const QString &openid,
                               std::function<void(const QString &text, const QString &info,
                                                  const QString &message_reference)> onDone);


    QString PostSync(const QString &url, const QByteArray &jsonData, const QString &contentType, int timeoutMs);
    QString PostSync(const QString &url, const QJsonObject &jsonData, const QString &contentType, int timeoutMs);
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

    QString m_accessToken,m_accessToken2;              // 运行时 token
    qint64 m_tokenExpireTime;           // 过期时间戳（秒）
    QString m_sessionId;
    qint64 m_seq;                       // 消息序号（用于心跳）
    bool m_isConnecting;

    int m_heartbeatIntervalSec;
    int m_invalidHeartbeatCount;
};

#endif // QQBOTCLIENT_H