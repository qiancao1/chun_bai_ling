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

#include "botdb.h"
#include <QString>
#include <QJsonObject>


#include <QByteArray>
#include <QCryptographicHash>
#include <QHostInfo>
#include <QSysInfo>
#include <QDir>

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

struct AccountInfo {
    QString appid;

    QString pduid;
    QString unid;
    QString secret;
    QString botqq;  //可空 用于配置全量
    QString botsettext; //QQ有个回调设置 打开后会显示这个文本
    QString nickname;  //不需要用户添加
    QString avatarPath; //不需要用户添加
    QString wsAddress;          // WebSocket 地址（默认空 然后是空将连接腾讯的）
    QList<mdbtn> mdbtn;
    QList<zdywb> zdywb;
    int dyindex=0;
    int appid_int=0;
    int type = 0;               // 0: WebSocket, 1: Webhook
    int message_received = 0;
    int message_sent = 0;
    int received = 0;
    int sent = 0;
    int wsIntents = 0;          // 订阅事件的位掩码
    int 今日加群数量=0;
    int 今日退群数量=0;
    bool autoConnect = false;
    bool online = false;
    bool ark=false;
    bool markdown=false;

    qint64 startup_time = 0;

    QString welcomeMsg;         // 被添加时的欢迎词
    QString fallbackReply;      // 指令未处理时的回应

    //=========AI
    QString Ai_nickname;           // 机器人名（显示在左侧列表）
    QList<QString> tools;
    QString model;              // 模型名称
    QString setting;            // 选中的全局设定名
    int context_len=6;            // 上下文
    int nSecondsNoReply = 0;    // N秒没回复
    int nMinutesNoReply = 0;    // N分钟没回复
    int delayReplySeconds = 8;  // 延迟回复(秒)
    int pplx=0;
    int 模型开始下标=0;

    bool enableGroupChat = false;
    bool enableGroupPersonal = false;
    bool enablePrivateChat = false;
    bool nameTrigger = false;
    bool enableChannel = false;
    bool enableChannelPersonal = false;
    bool atTrigger = false;

    bool enableImageRec = false;
    bool niren=false;

    QJsonObject toJson() const;
    static AccountInfo fromJson(const QJsonObject &obj);
};
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
#endif // JJM_H