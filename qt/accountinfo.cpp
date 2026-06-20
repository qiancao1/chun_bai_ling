#include "accountinfo.h"
#include <QJsonArray>


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
    obj["tools"] = QJsonArray::fromStringList(tools);
    return obj;
}

AccountInfo AccountInfo::fromJson(const QJsonObject &obj) {
    AccountInfo info;
    info.appid = obj["appid"].toString();
    info.appid_int = info.appid.toInt();

    // 用相同的机器特征 + appid 派生密钥，解密 secret
    QByteArray key = MachineKey::generateKey(info.appid);
    info.secret = MachineKey::decrypt(obj["secret"].toString(), key);
    info.ark = obj["ark"].toBool();
    info.markdown = obj["markdown"].toBool();
    info.botqq = obj["botqq"].toString();
    info.botsettext = obj["botsettext"].toString();
    info.nickname = obj["nickname"].toString();
    info.avatarPath = obj["avatarPath"].toString();
    info.wsAddress = obj["wsAddress"].toString();
    info.type = obj["type"].toInt();
    info.message_received = obj["message_received"].toInt();
    info.message_sent = obj["message_sent"].toInt();
    info.autoConnect = obj["autoConnect"].toBool();
    info.welcomeMsg = obj["welcomeMsg"].toString();
    info.fallbackReply = obj["fallbackReply"].toString();
    info.wsIntents = obj["wsIntents"].toInt();
    info.dyindex = obj["dyindex"].toInt();
    info.Ai_nickname = obj["Ainickname"].toString();

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