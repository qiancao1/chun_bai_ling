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

#include "accountinfo.h"
#include <QJsonArray>
#include "jjm.h"

// ============================================================

QJsonObject AccountInfo::toJson() const {
    QJsonObject obj;
    obj["appid"] = appid;

    // 用机器特征 + appid 派生密钥，加密 secret
    QByteArray key = MachineKey::generateKey(appid);
    obj["secret"] = MachineKey::encrypt(secret, key);
    obj["ark"] = ark;
    obj["markdown"] = markdown;
    obj["botqq"] = botqq;
    obj["botsettext"] = botsettext;
    obj["nickname"] = nickname;
    obj["avatarPath"] = avatarPath;
    obj["wsAddress"] = wsAddress;
    obj["type"] = type;
    obj["message_received"] = message_received;
    obj["message_sent"] = message_sent;
    obj["autoConnect"] = autoConnect;
    obj["dyindex"] = dyindex;
    obj["welcomeMsg"] = welcomeMsg;

    obj["g_add"]=今日加群数量;
    obj["g_sub"]=今日退群数量;
    obj["f_add"]=今日好友数量;
    obj["f_sub"]=今日删除好友数量;
    obj["r_jis"]=日计时变量;
    obj["c_add"]=今日频道数量;
    obj["c_sub"]=今日退出频道数量;
    obj["fallbackReply"] = fallbackReply;
    obj["wsIntents"] = wsIntents;
    obj["Ainickname"] = Ai_nickname;
    obj["model"] = model;
    obj["setting"] = setting;
    obj["context"] = context_len;
    obj["nSecondsNoReply"] = nSecondsNoReply;
    obj["nMinutesNoReply"] = nMinutesNoReply;
    obj["delayReplySeconds"] = delayReplySeconds;
    obj["e_GroupChat"] = enableGroupChat;
    obj["e_GroupPersonal"] = enableGroupPersonal;
    obj["e_PrivateChat"] = enablePrivateChat;
    obj["nameTrigger"] = nameTrigger;
    obj["e_Channel"] = enableChannel;
    obj["atTrigger"] = atTrigger;
    obj["e_ChannelPersonal"] = enableChannelPersonal;
    obj["e_ImageRec"] = enableImageRec;
    obj["pplx"] = pplx;
    obj["niren"] = niren;
    obj["tools"] = QJsonArray::fromStringList(tools);
    obj["tcts"]=tcts;
    obj["fasjg"]=fasjg;
    obj["rqhy"]=rqhy;
    obj["jiam"]=1;
    return obj;
}

AccountInfo AccountInfo::fromJson(const QJsonObject &obj) {
    AccountInfo info;
    info.appid = obj["appid"].toString();
    info.appid_int = info.appid.toInt();
    QByteArray key;
    if(obj["jiam"].toInt()==0)
    {
        key = MachineKey::generateKey_old(info.appid);
    }else{
        key = MachineKey::generateKey(info.appid);
    }
    info.secret = MachineKey::decrypt(obj["secret"].toString(), key);
    info.ark = obj["ark"].toBool();
    info.markdown = obj["markdown"].toBool();
    info.botqq = obj["botqq"].toString();
    info.botsettext = obj["botsettext"].toString();
    info.nickname = obj["nickname"].toString();
    info.avatarPath = obj["avatarPath"].toString();
    info.wsAddress = obj["wsAddress"].toString();
    info.type = obj["type"].toInt();

    info.tcts = obj["tcts"].toString();
    info.fasjg = obj["fasjg"].toInt();
    info.rqhy = obj["rqhy"].toString();

    info.message_received = obj["message_received"].toInt();
    info.message_sent = obj["message_sent"].toInt();
    info.autoConnect = obj["autoConnect"].toBool();
    info.welcomeMsg = obj["welcomeMsg"].toString();



    info.今日加群数量 = obj["g_add"].toInt();
    info.今日退群数量 = obj["g_sub"].toInt();
    info.今日好友数量 = obj["f_add"].toInt();
    info.今日删除好友数量 = obj["f_sub"].toInt();
    info.日计时变量 = obj["r_jis"].toInt();
    info.今日频道数量= obj["c_add"].toInt();
    info.今日退出频道数量= obj["c_sub"].toInt();


    qint64 now = QDateTime::currentSecsSinceEpoch(); // 你已经有这个了
    QDateTime dt = QDateTime::fromSecsSinceEpoch(now);
    int day = dt.date().day();
    if(info.日计时变量!=day)
    {
        info.今日加群数量 = 0;
        info.今日退群数量 = 0;
        info.今日好友数量 = 0;
        info.今日删除好友数量 = 0;
        info.今日频道数量=0;
        info.今日退出频道数量=0;
        info.日计时变量=day;
    }
    info.fallbackReply = obj["fallbackReply"].toString();
    info.wsIntents = obj["wsIntents"].toInt();
    info.dyindex = obj["dyindex"].toInt();
    info.Ai_nickname = obj["Ainickname"].toString();
    info.niren = obj["niren"].toBool();
    info.model = obj["model"].toString();
    info.setting = obj["setting"].toString();
    info.context_len = obj["context"].toInt();
    if(info.context_len<5)
        info.context_len=5;
    info.nSecondsNoReply = obj["nSecondsNoReply"].toInt();
    info.nMinutesNoReply = obj["nMinutesNoReply"].toInt();
    info.delayReplySeconds = obj["delayReplySeconds"].toInt();
    info.enableGroupChat = obj["e_GroupChat"].toBool();
    info.enableGroupPersonal = obj["e_GroupPersonal"].toBool();
    info.enablePrivateChat = obj["e_PrivateChat"].toBool();
    info.nameTrigger = obj["nameTrigger"].toBool();
    info.enableChannel = obj["e_Channel"].toBool();
    info.atTrigger = obj["atTrigger"].toBool();
    info.enableChannelPersonal = obj["e_ChannelPersonal"].toBool();
    info.enableImageRec = obj["e_ImageRec"].toBool();
    info.pplx = obj["pplx"].toInt();
    QJsonArray arr = obj["tools"].toArray();
    info.tools.reserve(arr.size());
    for (const auto &value : std::as_const(arr)) {
        info.tools.append(value.toString());
    }
    return info;
}