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

#ifndef ACCOUNTINFO_H
#define ACCOUNTINFO_H

#include <QString>
#include <QJsonObject>


#include <QByteArray>
#include <QCryptographicHash>
#include <QHostInfo>
#include <QSysInfo>
#include <QDir>
#include <qmutex.h>
#include <qtimer.h>

// WebSocket 默认订阅掩码（新建账号 / 没勾任何订阅事件时用这个）。
// 位 = GROUP_MEMBER(1<<24) + GROUP_AND_C2C_EVENT(1<<25) + INTERACTION(1<<26)
//    + DIRECT_MESSAGE(1<<12) + PUBLIC_GUILD_MESSAGES(1<<30) = 1191186432
// 注意：sendIdentify() 是原样把 wsIntents 发出去的，0 等于一个事件都不订阅 →
// 机器人连上线也收不到任何消息，所以「0」必须翻译成这个默认值。
constexpr int kDefaultWsIntents = 1191186432;

struct mdbtn {
    QStringList zl; //指令
    QStringList jzc;//禁止词
    QStringList hxc;//候选词
    QJsonObject btnjson;//按钮json
    int pplx=0; //匹配规则

};

struct zdywb {
    QStringList zl; //指令
    QStringList jzc;//禁止词
    QStringList thck;//替换词
    QStringList thcv;//替换词
    QString data;//直接替换词
    int pplx=0;
};
struct UserStat {
    QVector<quint32> buffer;
    quint8 head = 0;
    quint8 count = 0;
};
struct PendingGroupEvent {
    qint64 startTime;          // 第一个事件到达的时间（秒）
    QString msgid;
    QString gname;
    QList<QString> username;
    QList<QString> user;  // 收集的用户名
};
struct AccountStats {
    uint32_t minute_index;
    int appid;

    // 账号自身统计（来自 AccountInfo）
    int message_received;
    int message_sent;
    int 今日加群数量;
    int 今日退群数量;
    int 今日好友数量;
    int 今日删除好友数量;
    int 今日频道数量;
    int 今日退出频道数量;

    // 全系统统计（来自 BotDB 缓存）
    int active_users;      // m_userDailyMsg.size()
    int active_groups;     // m_groupDailyMsg.size()
};
struct AccountInfo {
    QString appid;
    QString StatT;
    QString pduid;
    QString unid;
    QString secret;
    QString botqq;  //可空 用于配置全量

    QString nickname;  //不需要用户添加
    QString avatarPath; //不需要用户添加

    QList<mdbtn> mdbtnlist;
    QList<zdywb> zdywblist;
    int dyindex=0;
    int appid_int=0;

    int type = 0;               // 0: WebSocket, 1: Webhook
    int message_received = 0;
    int message_sent = 0; //总发送

    int received = 0;
    int sent = 0; //当前登录以来

    int wsIntents = 0;          // 订阅事件的位掩码
    int received_day = 0;
    int sent_day = 0; //每日


    int 今日加群数量=0;
    int 今日退群数量=0;

    int 今日好友数量=0;
    int 今日删除好友数量=0;

    int 今日频道数量=0;
    int 今日退出频道数量=0;

    int 日计时变量=0;          // 上次清零那天的「儒略日」(QDate::toJulianDay)，不是几号


    int times =5;//检测时间
    int tiaoshu=5; //刷屏条数

    int tq_ychf=0; //退群延迟回复
    int tq_lq=0 ;

    int rq_ychf=0, rq_lq=0;
    // 发送后自动撤回(秒)：0 = 不撤。只在该群 bitmap 开了 BIT_AUTO_RECALL 时才由 addmsglog 登记
    int auto_recall=0;
    bool ychfwcl=false; //延迟回应未处理
    bool autoht=true;
    bool pbbot=true;
    bool autoConnect = false;
    bool online = false;
    bool sandbox = false;       // 勾选「连接沙盒」：走腾讯沙盒环境（具体使用见连接逻辑）
    bool cbl=true; // 群管指令？
    bool markdown=true;
    bool markdown_pd=false;
    bool markdown_pd_mb=false;
    bool e_bai=true;
    bool action_button=false;
    bool xiangliang=false; //向量数据库
    bool juece;
    qint64 startup_time = 0;
    QHash<int,UserStat> stat; //刷屏检测 绑定机器人的
    QString admin;
    QString welcomeMsg;         // 机器人入群 被添加时的欢迎词
    QString welcomeMsg2;         // 被添加好友

    QString fallbackReply;      // 指令未处理时的回应
    QString rqhy; //群成员加群提示文本
    QString bai_sr,bai_sc,bai_qy;
    QString err; //最后错误
    QString tqhy;
    QString help,caidan,emptyAt;
    QString apply;// 有人申请加群时
    QString jojnhf;//入群验证
    QString xxwb;
    QString ldsd;//看完广告提示词
    QString gxrz;//更新日志
    QString qiandao;//签到（普通文本 = 回复模板，+1000/天；#python 开头 = 脚本，返回值决定加多少铃铛）
    //=========AI
    QString Ai_nickname;           // 机器人名（显示在左侧列表）
    QList<QString> tools;
    QList<QString> fujia;
    QString model;              // 模型名称
    QString Embed_model;
    QString setting;            // 选中的全局设定名
    int context_len=128;            // 上下文
    int nSecondsNoReply = 0;    // N秒没回复
    int nMinutesNoReply = 0;    // N分钟没回复
    int delayReplySeconds = 6;  // 延迟回复(秒)
    int pplx=0;
    int 固定条数=0;
    int 触发概率=0;
    int 递增概率=0;
    int 模型开始下标=0;

    bool enableGroupChat = false;
    bool enableGroupPersonal = false;
    bool enablePrivateChat = false;

    bool enableChannel = false;
    bool enableChannelPersonal = false;
    bool atTrigger = false;

    bool enableImageRec = false;
    bool niren=false;

    QMutex pendingMutex;                          // 保护两个map
    QMap<QString, PendingGroupEvent> pendingJoin; // 入群缓存，key=群号
    QMutex pendingMutex2;
    QMap<QString, PendingGroupEvent> pendingLeave; // 退群缓存

    QString toJson() const;
    static void fromJson(const QJsonObject &obj,AccountInfo &info);

    // 跨天清零：「今日」相关计数器（今日收发 / 今日加群退群 / 今日好友 / 今日频道）
    // 到新的一天就归零。用儒略日比较（完整日期，不是几号），跨月、跨年都准。
    // 三处都会调：读取配置时、定时任务每分钟、首页刷新时 —— 程序连跑几天也能自己清零。
    // 返回 true 表示这次真的清零了。
    bool checkDailyReset();

};

// 签到配置的默认内容（新机器人 / 配置为空时填这段 py 示例；群管「签到」的 placeholder 也用它）
QString defaultQiandaoTemplate();

struct RoleSetting {
    QString name;
    QString content;
};
struct ToolConfig {
    bool enableAutoReply = false;
    bool enableScheduledTask = false;
    bool enableCustomLog = false;
    QList<QPair<QString, QString>> tableData; // 键值对
};
#endif // ACCOUNTINFO_H