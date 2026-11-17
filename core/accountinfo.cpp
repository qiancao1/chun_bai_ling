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
#include <QDate>
#include <qjsondocument.h>
#include "machinekey.h"
// ============================================================

QString AccountInfo::toJson() const {
    QJsonObject obj;
    obj["appid"] = appid;

    // 用机器特征 + appid 派生密钥，加密 secret
    QByteArray key = MachineKey::generateKey(appid);
    obj["secret"] = MachineKey::encrypt(secret, key);

    obj["markdown"] = markdown;
    obj["markdown_pd"]= markdown_pd;
    obj["markdown_pd"]= markdown_pd_mb;
    obj["botqq"] = botqq;

    obj["nickname"] = nickname;
    obj["avatarPath"] = avatarPath;

    obj["type"] = type;
    obj["message_received"] = message_received;
    obj["message_sent"] = message_sent;
    obj["received_day"] = received_day;
    obj["sent_day"] = sent_day;
    obj["ad_total"] = ad_watched_total;   // 广告看完：累计（启动以来 ad_watched 不落盘）
    obj["ad_day"] = ad_watched_day;       // 广告看完：今日
    obj["autoConnect"] = autoConnect;
    obj["dyindex"] = dyindex;
    obj["welcomeMsg"] = welcomeMsg;
    obj["welcomeMsg2"] = welcomeMsg2;
    obj["xxwb"] = xxwb;
    obj["autoht"] = autoht;
    obj["g_add"]=今日加群数量;
    obj["g_sub"]=今日退群数量;
    obj["f_add"]=今日好友数量;
    obj["f_sub"]=今日删除好友数量;
    obj["r_jis"]=日计时变量;
    obj["sign_today"]=签到今日人数;   // 今日签到人数（跨天由 sign_day 判定归零）
    obj["sign_day"]=签到今日日期;     // 上面那个数属于哪天（儒略日）
    obj["c_add"]=今日频道数量;
    obj["c_sub"]=今日退出频道数量;
    obj["fallbackReply"] = fallbackReply;
    obj["wsIntents"] = wsIntents;
    obj["Ainickname"] = Ai_nickname;
    obj["model"] = model;
    obj["juece_model"] = juece_model;
    obj["setting"] = setting;
    obj["context"] = context_len;
    obj["nSecondsNoReply"] = nSecondsNoReply;
    obj["nMinutesNoReply"] = nMinutesNoReply;
    obj["delayReplySeconds"] = delayReplySeconds;
    obj["e_GroupChat"] = enableGroupChat;
    obj["e_GroupPersonal"] = enableGroupPersonal;
    obj["e_PrivateChat"] = enablePrivateChat;
    obj["action_button"]= action_button;
    obj["ychfwcl"] = ychfwcl;
    obj["e_Channel"] = enableChannel;
    obj["atTrigger"] = atTrigger;
    obj["e_ChannelPersonal"] = enableChannelPersonal;
    obj["e_ImageRec"] = enableImageRec;
    obj["sandbox"] = sandbox;
    obj["pplx"] = pplx;
    obj["niren"] = niren;
    obj["tools"] = QJsonArray::fromStringList(tools);
    obj["fujia"] = QJsonArray::fromStringList(fujia);

    obj["rqhy"]=rqhy;
    obj["apply"] = apply;
    obj["jojnhf"] = jojnhf;
    obj["jiam"]=1;
    obj["a_gdts"]=固定条数;
    obj["a_cfgl"]=触发概率;
    obj["a_dzgl"]=递增概率;
    obj["e_bai"]=e_bai;
    obj["bai_sr"]=bai_sr;
    obj["bai_sc"]=bai_sc;
    obj["bai_qy"]=bai_qy;
    obj["times"] = times;
    obj["ldsd"]=ldsd;
    obj["gxrz"]=gxrz;
    obj["qiandao"]=qiandao;
    obj["default_buttons"]=default_buttons;
    obj["tiaoshu"]=tiaoshu;
    obj["pbbot"]=pbbot;
    obj["admin"]=admin;
    obj["help"]=help;
    obj["caidan"]=caidan;
    obj["koat"]=emptyAt;
    obj["cbl"]=cbl;
    obj["xiangliang"]=xiangliang;
    obj["Embed_model"]=Embed_model;
    obj["juece"]=juece;
    obj["rq_ychf"]=rq_ychf;
    obj["tq_lq"]=tq_lq;
    obj["rq_lq"]=rq_lq;
    obj["tq_ychf"]=tq_ychf;
    obj["auto_recall"]=auto_recall;
    obj["recall_prob"]=recall_prob;
    obj["tqhy"]=tqhy;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// 签到配置的默认内容：一份能直接跑的 py 示例（群管「签到」框点开就能看到、改几个数字就能用）
QString defaultQiandaoTemplate()
{
    return QString::fromUtf8(R"(#python
# ↑ 第一行必须是 #python 框架才当代码跑；删掉这行或改成普通文本 = 回复模板模式（一天一次 +1000）

def on_sign(msg, signed_today, total, streak, points):
    #   msg           事件：msg.user / msg.groupId / msg.nickname / msg.appid / msg.msg ...
    #   signed_today  今天是否已经签到（True / False）
    #   total         总签到次数（不含这一次）
    #   streak        连续签到次数（不含这一次）
    #   points        当前铃铛数（不含这一次）
    #   返回值 > 0 = 本次加多少铃铛（同时记一次签到）；返回 0 = 不加、不记
    if signed_today:
        return 0, "今天已经签到过啦，总铃铛数：[余额]"

    gain = 1500 if streak >= 6 else 1000      # 连续 7 天以上每天多给 500
    return gain, "签到成功：+[获得余额]   总铃铛数：[余额]"

# ---- 说明 ----
# 函数名认这些，任选其一：on_sign / sign / qiandao / 签到 / checkin / main
# 文案变量：[获得余额] 本次获得   [余额] 当前铃铛总数
# 不想用函数、只写顶层常量也行：__sign_points__ = 1000  /  __result__ = "固定文案"
# 脚本只编译、只执行顶层一次，之后每次只调用函数（所以很快）：
#   → msg / 次数 / 余额只能从参数拿，顶层没有这些名字；顶层定义的全局变量会跨调用保留
# 可用变量 [总签到] [连续签到] [今日签到] [获得余额] [余额] <-非py代码也可用
#   [今日签到] = 今天已签到的人数（含本次）
)");
}

// 「默认按钮」输入整理：允许整段粘消息里那套 #b:#{...}#b:#，顺手去掉 BOM 和首尾空白。
// 只整理不校验 —— 校验分两处：UI 保存时给用户报错、解析时（defaultButtonKeyboard）写错就当没配。
QString defaultButtonJsonText(const QString &raw)
{
    QString s = raw;
    s.remove(QChar(0xFEFF));                 // 从网页 / 记事本复制常带 BOM
    const int a = s.indexOf("#b:#");
    if (a >= 0) {
        // 结尾标记取「最后一个」：按钮文案里万一也写了 #b:# 时不会被提前截断
        const int b = s.lastIndexOf("#b:#");
        s = (b > a) ? s.mid(a + 4, b - a - 4)      // 完整的 #b:#...#b:#
                    : s.mid(a + 4);                // 只写了开头一个标记也认
    }
    return s.trimmed();
}

QJsonValue AccountInfo::defaultButtonKeyboard() const
{
    QMutexLocker locker(&btnCacheMutex);
    // 文本没变 = 直接吃缓存（按钮是每条消息都要走的热路径，不能每次 fromJson）
    if (btnCacheReady && btnCacheSrc == default_buttons)
        return btnCacheVal;

    btnCacheSrc   = default_buttons;
    btnCacheReady = true;
    btnCacheVal   = QJsonValue(QJsonValue::Undefined);

    const QString src = defaultButtonJsonText(default_buttons);
    if (src.isEmpty())
        return btnCacheVal;

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(src.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return btnCacheVal;                  // 写错了就当没配（发送端按「无默认按钮」处理）

    // 各种写法都收：{"keyboard":{...}} / {"content":{...}} / {"rows":[...]} /
    // {"buttons":[...]} / 直接一个数组 —— 具体怎么摆平由 send_messages_markdown / _mb 负责。
    if (doc.isObject())     btnCacheVal = doc.object();
    else if (doc.isArray()) btnCacheVal = doc.array();
    return btnCacheVal;
}


void AccountInfo::fromJson(const QJsonObject &obj,AccountInfo &info) {

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

    info.markdown = obj["markdown"].toBool(true);
    info.markdown_pd = obj["markdown_pd"].toBool();
    info.markdown_pd_mb = obj["markdown_pd"].toBool();
    info.botqq = obj["botqq"].toString();

    info.nickname = obj["nickname"].toString();
    info.avatarPath = obj["avatarPath"].toString();

    info.admin = obj["admin"].toString();
    info.type = obj["type"].toInt();
    info.juece =obj["juece"].toBool();
    info.autoht =obj["autoht"].toBool(true);//自动回调

    info.rqhy = obj["rqhy"].toString();
    info.help=obj["help"].toString();
    info.caidan=obj["caidan"].toString();
    info.emptyAt=obj["koat"].toString();
    info.apply=obj["apply"].toString();
    info.jojnhf=obj["jojnhf"].toString();
    info.xxwb=obj["xxwb"].toString();
    info.tq_lq = obj["tq_lq"].toInt();
    info.rq_lq = obj["rq_lq"].toInt();
    info.tq_ychf = obj["tq_ychf"].toInt();
    info.tqhy = obj["tqhy"].toString();
    info.rq_ychf=obj["rq_ychf"].toInt();
    info.auto_recall = obj["auto_recall"].toInt();
    info.recall_prob = qBound(0, obj["recall_prob"].toInt(), 100);
    info.ychfwcl = obj["ychfwcl"].toBool(true);
    info.message_received = obj["message_received"].toInt();
    info.message_sent = obj["message_sent"].toInt();

    info.received_day = obj["received_day"].toInt();
    info.sent_day = obj["sent_day"].toInt();
    info.ad_watched_total = obj["ad_total"].toInt();
    info.ad_watched_day = obj["ad_day"].toInt();
    info.autoConnect = obj["autoConnect"].toBool();
    info.welcomeMsg = obj["welcomeMsg"].toString();
    info.welcomeMsg2 = obj["welcomeMsg2"].toString();
    info.Embed_model = obj["Embed_model"].toString();


    info.今日加群数量 = obj["g_add"].toInt();
    info.今日退群数量 = obj["g_sub"].toInt();
    info.今日好友数量 = obj["f_add"].toInt();
    info.今日删除好友数量 = obj["f_sub"].toInt();
    info.日计时变量 = obj["r_jis"].toInt();
    info.今日频道数量= obj["c_add"].toInt();
    info.今日退出频道数量= obj["c_sub"].toInt();
    info.固定条数 = obj["a_gdts"].toInt();
    info.触发概率= obj["a_cfgl"].toInt();
    info.递增概率= obj["a_dzgl"].toInt();


    info.times = obj["times"].toInt(5);
    info.xiangliang=obj["xiangliang"].toBool(); //向量开关
    info.cbl=obj["cbl"].toBool(true);
    info.sandbox=obj["sandbox"].toBool();
    info.pbbot =obj["pbbot"].toBool(true);
    info.tiaoshu = obj["tiaoshu"].toInt(5);

    //ai白名单
    info.e_bai = obj["e_bai"].toBool();
    info.bai_sr = obj["bai_sr"].toString();
    info.bai_sc = obj["bai_sc"].toString();
    info.bai_qy = obj["bai_qy"].toString();
    info.ldsd = obj["ldsd"].toString("广告看完啦，+1000 铃铛，当前余额 [余额] 铃铛");
    info.gxrz = obj["gxrz"].toString();
    // 签到配置：空就是空（= 静默签到：一天一次 +1000，不回复任何消息）。
    // 示例不写进配置，只作为「群管 → 签到」输入框的灰色 placeholder 展示。
    info.qiandao = obj["qiandao"].toString();
    // 默认按钮：存原文本，这里是配置唯一入口 → 顺手把解析缓存预热掉（之后每条消息都走缓存）
    info.default_buttons = obj["default_buttons"].toString();
    info.defaultButtonKeyboard();
    // 「今日」计数器跨天清零（今日收发 / 今日加群退群 / 今日好友 / 今日频道）
    info.checkDailyReset();
    // 今日签到人数：自己的日期字段判（checkDailyReset 里也清，但程序常驻跨天不一定跑得到那里）
    info.签到今日人数 = obj["sign_today"].toInt();
    info.签到今日日期 = obj["sign_day"].toInt();
    {
        const int todayJ = QDate::currentDate().toJulianDay();
        if (info.签到今日日期 != todayJ) {   // 换天了 / 老配置没这个字段 → 从 0 重新数
            info.签到今日日期 = todayJ;
            info.签到今日人数 = 0;
        }
    }
    info.fallbackReply = obj["fallbackReply"].toString();
    info.wsIntents = obj["wsIntents"].toInt();
    info.dyindex = obj["dyindex"].toInt();
    info.Ai_nickname = obj["Ainickname"].toString();
    info.niren = obj["niren"].toBool();
    info.model = obj["model"].toString();
    info.juece_model = obj["juece_model"].toString();
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
    info.action_button = obj["action_button"].toBool();
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

    QJsonArray arr2 = obj["fujia"].toArray();
    info.fujia.reserve(arr2.size());
    for (const auto &value : std::as_const(arr2)) {
        info.fujia.append(value.toString());
    }


}

// 跨天清零。
// ⚠ 日计时变量 存的是 **儒略日**（完整日期），不是「几号」：
//    以前存 day()（1~31），上月 5 号跑到本月 5 号会被判成"同一天"而不清零；
//    而且两处清零逻辑（这里 + api.cpp 的 botlist）各清各的字段 ——
//    api.cpp 那边只清今日加群/好友/频道、还把日计时变量改成当天，
//    结果下次启动走到这里时日期"相等"直接跳过，received_day/sent_day 就再也不清零，
//    首页的「今日收发」越跑越大，看着跟累计一样。现在统一走这个函数。
bool AccountInfo::checkDailyReset()
{
    const int today = QDate::currentDate().toJulianDay();
    if (日计时变量 == today)
        return false;

    日计时变量 = today;
    今日加群数量 = 0;
    今日退群数量 = 0;
    今日好友数量 = 0;
    今日删除好友数量 = 0;
    今日频道数量 = 0;
    今日退出频道数量 = 0;
    received_day = 0;      // 今日接收
    sent_day = 0;          // 今日发送
    ad_watched_day = 0;    // 今日广告看完次数
    签到今日人数 = 0;      // 今日签到人数（下一次签到会重新 +1）
    签到今日日期 = today;
    return true;
}