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

    obj["welcomeMsg"] = welcomeMsg;
    obj["fallbackReply"] = fallbackReply;
    obj["wsIntents"] = wsIntents;


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


    return info;
}