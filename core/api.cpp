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


#include "apiprocessor.h"
#include "netmanager.h"
#include "qqbotclient.h"
#include <QRandomGenerator>
#include <qwaitcondition.h>
#include <string>
#include "global.h"
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtMath>
#include <QNetworkReply>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QMutex>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <thread>
#include <memory>
#include <functional>

const int OUTLOG = 1; //输出日志
const int API_ID_SEND_MESSAGES    = 2; //发送消息
const int API_ID_SEND_MESSAGES_ARK = 3; //发送卡片
const int API_ID_DELETE_MESSAGES  = 4; //撤回消息
const int API_ID_GENERATE_SHARE_LINK = 5; //获取邀请链接
const int API_ID_RESPOND_INTERACTION = 6; //响应回调
const int API_ID_BOT_LIST = 7; //获取机器人列表
const int API_ID_GET_OPENID = 8; //获取用户openid
const int API_ID_GET_USER_NAME=9; //获取用户昵称
const int API_ID_PYTHON_HTTP=10; //弃用
const int API_ID_GET_USER_ID=11;

const int API_ID_HTMLIMG1=12; //html制图
const int API_ID_HTMLIMG2=13;
const int API_ID_DS=14; //添加定时
const int API_ID_AI=15; //调用内部AI
const int API_ID_GET_MEMBER=16;//获取群成员消息
const int API_ID_GET_MEMBER_LIST=17; //获取群成员列表
const int API_ID_GET_groups_info=18;//获取群消息
const int API_ID_GET_groups_bot_state=19; //获取机器人群内状态

const int API_ID_SET_JOIN_REQUEST=20; //处理加群请求
const int API_ID_GET_JOIN_REQUEST_LIST=21 ;//获取加群列表
const int API_ID_SET_MUTE_G=22; //禁言某人
const int API_ID_GET_MUTE_LIST_G=23; //获取禁言列表

const int API_ID_REMOV_MEMBER =24;//批量移除成员
const int API_ID_GET_GROUP_BLCKLIST=25; //获取群黑名单列表
const int API_ID_GROUP_BLCKLIST=26; //修改黑名单列表



void DelFileSync_Cnb();
QString renderInThread(const QString &htmlContent,int width = 400) ;
inline QString toQString(const char* s) {
    return s ? QString::fromUtf8(s) : QString();
}

inline int toInt(const char* s) {
    return s ? std::atoi(s) : 0;
}

inline bool toBool(const char* s) {
    if (!s) return false;
    QString str = QString::fromUtf8(s).trimmed().toLower();
    return str == "1" || str == "true";
}

inline QJsonArray toJsonArray(const char* s) {
    if (!s) return QJsonArray();
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(s));
    if (doc.isArray()) return doc.array();

    return QJsonArray();
}

inline QJsonObject toJsonObject(const char* s) {
    if (!s) return QJsonObject();
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(s));
    if (doc.isObject()) return doc.object();

    return QJsonObject();
}

inline QByteArray toByteArray(const char* s) {
    if (!s) return QByteArray();
    return QByteArray(s, std::strlen(s));
}

QString formatDuration(qint64 seconds) {
    const qint64 DAY_SECS = 86400;
    qint64 days = seconds / DAY_SECS;
    qint64 remainder = seconds % DAY_SECS;
    qint64 hours = remainder / 3600;
    qint64 minutes = (remainder % 3600) / 60;
    qint64 secs = remainder % 60;

    return QString("%1天%2时%3分%4秒")
        .arg(days)
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

QString convertMdLinksKeepHttp(const QString &input)
{
    static QRegularExpression re(R"((?<!!)\[([^\]]*?)\]\(([^\)]*?)\))");
    QRegularExpressionMatchIterator it = re.globalMatch(input);

    QString output;
    int lastIndex = 0;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int start = match.capturedStart();
        int end = match.capturedEnd();

        // 添加匹配之前的普通文本
        output.append(QStringView(input).mid(lastIndex, start - lastIndex));

        QString text = match.captured(1);   // 方括号内文字
        QString url = match.captured(2);    // 圆括号内地址

        // 检查 url 是否以 http:// 或 https:// 开头（不区分大小写）
        QString lowerUrl = url.toLower();
        bool isHttpLink = lowerUrl.startsWith("http://") || lowerUrl.startsWith("https://");

        if (isHttpLink) {
            // 保持原样
            output.append(match.captured(0));
        } else {
            // 只保留方括号内的文字
            output.append(text);
        }

        lastIndex = end;
    }

    // 添加剩余文本
    output.append(QStringView(input).mid(lastIndex));

    return output;
}

QString convertMarkdownLinksToXml(const QString &input)
{
    // 修改正则：前面不能有感叹号（排除图片格式）
    QRegularExpression re(R"((?<!!)\[([^\]]*?)\]\(([^\)]*?)\))");
    QRegularExpressionMatchIterator it = re.globalMatch(input);

    QString output;
    int lastIndex = 0;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int start = match.capturedStart();
        int end = match.capturedEnd();

        output.append(QStringView(input).mid(lastIndex, start - lastIndex));

        QString showText = match.captured(1);
        QString url = match.captured(2);

        QString lowerUrl = url.toLower();
        bool shouldConvert = !(lowerUrl.startsWith("http://") ||
                               lowerUrl.startsWith("https://") ||
                               lowerUrl.startsWith("mqqapi://") ||
                               lowerUrl.startsWith("qagent://")) ;

        if (shouldConvert) {
            QString encodedUrl = QString::fromUtf8(QUrl::toPercentEncoding(url));
            showText.replace("%", "%25");

            encodedUrl.replace("\\","\\\\");
            encodedUrl.replace("\"","\\\"");
            if(encodedUrl.isEmpty())
                encodedUrl=showText;
            if(url.size()>95)
            {
                QString xmlTag = QString("[%1](qagent://aio/inlinecmd?command=%2)")
                .arg(showText,encodedUrl);
                //qDebug() <<xmlTag;
                output.append(xmlTag);
            }else{


                QString xmlTag = QString("<qqbot-cmd-input text=\"%1\" show=\"%2\" reference=\"false\" />")
                                 .arg(encodedUrl, showText);
                output.append(xmlTag);
            }

        } else {
            output.append(match.captured(0));
        }

        lastIndex = end;
    }

    output.append(QStringView(input).mid(lastIndex));
    return output;
}
QString botlist()
{
    qint64 now = QDateTime::currentSecsSinceEpoch();
    QDateTime dt = QDateTime::fromSecsSinceEpoch(now);
    int day = dt.date().day();

    QJsonArray array;
    for(auto &info : m_accounts)
    {
        if (!info) continue;
        if(info->appid_int==0) continue;
        QJsonObject obj;
        obj["appid"] = info->appid_int;
        obj["name"] = info->nickname;
        obj["qq"]=info->botqq;
        obj["avatarPath"] = info->avatarPath;
        obj["total_received"] = info->message_received;//累计
        obj["total_sent"]=info->message_sent;
        obj["received"] = info->received;//当前运行
        obj["sent"]=info->sent;
        obj["online"] = info->online;
        obj["id"] = info->pduid; //频道id
        obj["union_openid"]=info->unid;   //QQid
        obj["time"] = formatDuration(now-info->startup_time);
        obj["admin"] = info->admin;
        if(g_botdb.contains(info->appid_int)){
            auto *db = g_botdb[info->appid_int];

            obj["dau"] = db->m_userDailyMsg.size();
            obj["group_dau"] = db->m_groupDailyMsg.size();
        }else{
            obj["dau"] = 0;
            obj["group_dau"] = 0;
        }

        if(info->日计时变量!=day)
        {
            info->今日加群数量 = 0;
            info->今日退群数量 = 0;
            info->今日好友数量 = 0;
            info->今日删除好友数量 = 0;
            info->日计时变量 = day;
            info->今日频道数量=0;
            info->今日退出频道数量=0;
        }
        obj["today_join_count"] = info->今日加群数量;       // 今日加群
        obj["today_leave_count"] = info->今日退群数量;       // 今日退群
        obj["today_friend_count"] = info->今日好友数量;      // 今日新增好友
        obj["today_del_friend_count"] = info->今日删除好友数量; // 今日删除好友
        obj["today_channel_join_count"] = info->今日频道数量;    // 今日加入频道
        obj["today_channel_leave_count"] = info->今日退出频道数量; // 今日退出频道
        array.append(obj);

    }
    return QJsonDocument(array).toJson();
}


#include <QImageReader>
#include <QImage>
#include <QBuffer>

bool calculateFileMD5AndSize(const QString &filePath, QString &md5, int &width, int &height)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        //qWarning() << "无法打开文件:" << filePath;
        return false;
    }

    // 1. 一次性读取整个文件到内存
    QByteArray fileData = file.readAll();
    file.close();

    if (fileData.isEmpty()) {
        //qWarning() << "文件为空或读取失败:" << filePath;
        return false;
    }

    // 2. 计算 MD5（直接对 fileData 做哈希）
    QByteArray md5Result = QCryptographicHash::hash(fileData, QCryptographicHash::Md5);
    md5 = QString::fromLatin1(md5Result.toHex());
    if(width!=0 || height!=0) return true;
    // 3. 用 QImageReader 从内存数据中读取宽高（仅解析头部）
    QBuffer buffer(&fileData);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    if (!reader.canRead()) {
        //qWarning() << "无法识别图片格式:" << filePath;
        // 宽高保留默认值，但可以返回 false 表示图片格式无效
        return true;
    }
    QSize size = reader.size();
    if (size.isEmpty()) {
        //qWarning() << "无法获取图片尺寸:" << filePath;
        return false;
    }
    width = size.width();
    height = size.height();

    return true;
}


// 主回调函数
const char* myCallbackA(const char* uuid, int apiId, int appid, const char* _1, const char* _2,
                       const char* _3, const char* _4, const char* _5,
                       const char* _6, const char* _7, const char* _8)
{
    py::gil_scoped_release release;
    return myCallback(uuid,apiId,appid,_1,_2,_3,_4,_5,_6,_7,_8);
}
const char* myCallback(const char* uuid, int apiId, int appid, const char* _1, const char* _2,
                       const char* _3, const char* _4, const char* _5,
                       const char* _6, const char* _7, const char* _8) {
    static std::string result="{}"; //静态
    result="{}"; //初始化
    //qDebug() << "apiid:"<< apiId << " appid:"<< appid << " _1:" << _1 << "_2" <<_2 << "_3"<<_3 << "_4"<<_4 << "_5"<<_6 << "_7"<<_7 ;
    if (apiId == 10000) {
        miaomiao32 = 0;
        return result.c_str();
    }
    if (apiId == 10001) {//32位异常
        QString text = toQString(_1);
        if (_2 == nullptr || strlen(_2) == 0) {
            AppendEventLog(text);
           return result.c_str();
        }

        AppendEventLog(text,toInt(_2));
        return result.c_str();
    }
    if(apiId==10002)
    {
        botnomsg(appid,toInt(_1),toQString(_2),toQString(_3));
        return result.c_str();
    }

    QString pname;
    int pluginindex=0;
    if(strcmp(uuid, g_keyuuid2) != 0)
    {
        for(int i=0;i<m_pluginList.size();i++)
        {
            if(m_pluginList[i].uuid!=uuid) continue;
            pluginindex=i;
            if(apiId==OUTLOG)
                pname = "["+m_pluginList[i].name+"]";
            else if(apiId ==API_ID_SEND_MESSAGES || apiId == API_ID_SEND_MESSAGES_ARK)
                pname = "["+m_pluginList[i].name+"|%1ms]";
            else
                pname = "[]";
            break;
        }
        if(apiId==API_ID_AI)
        {
            result = "无权限调用内置Ai 请等待授权添加？";
            return result.c_str();//这里
        }
    }else{
        pname = "[关键词匹配|%1ms]";
    }
    if(pname.isEmpty()) return result.c_str();
    QQBotClient *client=nullptr;
    if(apiId!=OUTLOG && apiId!=API_ID_BOT_LIST && apiId!=API_ID_PYTHON_HTTP && apiId!=API_ID_HTMLIMG1 && apiId!=API_ID_HTMLIMG2 && apiId!=API_ID_AI)
    {
        bool ok=false;
        for(int i=0;i<m_accounts.size();i++)
        {
            if(m_accounts[i]->appid_int!=appid) continue;
            if(!m_accounts[i]->online)
            {
                result = "{\"msg\":\"bot不在线\"}";
                AppendEventLog(QString("插件：%1 发送消息：%2 时bot不在线 无法发送消息 目标appid:%3").arg(pname,toQString(_3),QString::number(appid)));
                return result.c_str();
            }
            ok = true;
            if(m_botClients.contains(appid))
            {
                client = m_botClients[appid];
                break;
            }
            result = "{\"msg\":\"client没找到 代表机器人未登录 一般来说online 是 false 这里不会执行\"}";
            return result.c_str();
        }
        if(!ok)
        {
            AppendEventLog(QString("插件：%1 发送消息：%2 时指定appid不可用 无法发送消息 目标appid:%3").arg(pname,toQString(_3),QString::number(appid)));
            return result.c_str();
        }
    }


    switch (apiId) {
    case OUTLOG: {
        QString text = pname+toQString(_1);
        if (_2 == nullptr || strlen(_2) == 0) {
            AppendEventLog(text);
            break;
        }

        AppendEventLog(text,toInt(_2));
        break;
    }
    case API_ID_SEND_MESSAGES: {
        m_pluginList[pluginindex].SendQuantity++;
        int type = toInt(_1);
        QString openid = toQString(_2);
        QString text =toQString(_3);

        QString msgid = toQString(_4);
        bool is_wakeup = toBool(_5);
        QString ret;
        if(toBool(_6))
            ret = client->send_msgAsync(type, openid,pname, text,msgid, is_wakeup);
        else
            ret = client->send_messages(type, openid,pname, text,msgid, is_wakeup);
        result = ret.toStdString();
        break;
    }
    case API_ID_SEND_MESSAGES_ARK: {
        m_pluginList[pluginindex].SendQuantity++;
        int type = toInt(_1);
        QString openid = toQString(_2);
        QJsonObject ark = toJsonObject(_3);
        QString msgid = toQString(_4);
        bool is_wakeup = toBool(_5);
        QString ret = client->send_messages_ark(type, openid,pname, ark, msgid, is_wakeup);
        result = ret.toStdString();
        break;
    }
    case API_ID_DELETE_MESSAGES: {
        int type = toInt(_1);
        QString openid = toQString(_2);
        QString msgid = toQString(_3);

        QString ret = client->delete_messages(type, openid, msgid);
        result = ret.toStdString();
        break;
    }
    case API_ID_GENERATE_SHARE_LINK: {
        QString callback_data = toQString(_1);
        QString ret = client->generate_share_link(callback_data);
        result = ret.toStdString();
        break;
    }
    case API_ID_RESPOND_INTERACTION: {
        QString interaction_id = toQString(_1);
        int code = toInt(_2);
        QString data = toQString(_3);
        QString ret = client->respond_interaction(interaction_id, code, data);
        result = ret.toStdString();
        break;
    }
    case API_ID_BOT_LIST:{
        QString ret = botlist();
        result = ret.toStdString();
        break;
    }
    case API_ID_GET_OPENID: {
        if(!g_botdb.contains(appid))
        {
            result = "";
            break;
        }
        if(!g_botdb.contains(appid)) break ;
        BotDB *db = g_botdb[appid];
        QString user;
        db->getOpenIdBySeqId(toInt(_1),user);
        result =user.toStdString();
        break;
    }
    case API_ID_GET_USER_NAME: {

        if(!g_botdb.contains(appid))
        {
            result = "";
            break;
        }

        QString text = toQString(_1);
        if(!g_botdb.contains(appid)) break ;

        BotDB *db = g_botdb[appid];

        if(text.size()==32)
        {
            QString name;
            db->getOrUpdateUser(text,name);
            result = name.toStdString();
        }else{
            UserRecord user{};
            db->getUserBySeqId(text.toInt(),user);
            result =user.nickname;
        }
        break;
    }
    case API_ID_PYTHON_HTTP: {

        result = "弃用..";
        break;
    }
    case API_ID_GET_USER_ID: {
        if(!g_botdb.contains(appid))
        {
            result = "";
            break;
        }
        BotDB *db = g_botdb[appid];
        QString name;
        uint32_t id = db->getOrUpdateUser(toQString(_1),name);
        result = id;
        break;
    }
    case API_ID_HTMLIMG1: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="HTMLIMG1参数1为空";
            break;
        }
        result = renderInThread(text,toInt((_2))).toStdString();
        break;
    }
    case API_ID_HTMLIMG2: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="HTMLIMG2参数1为空";
            break;
        }
        result = ScreenA->captureHtmlSync(text,toInt(_2),toInt(_3),toInt(_4)).toStdString();
        break;
    }
    case API_ID_DS: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="添加定时 参数1 备注为空";
            break;
        }
        QString text2 = toQString(_2);
        if(text2.isEmpty())
        {
            result ="添加定时 参数3 定时时间为空";
            break;
        }
        QString text3 = toQString(_4);
        if(text3.isEmpty())
        {
            result ="添加定时 参数3 python代码为空";
            break;
        }
        result = schedule->add_byAi(text,appid,text2,toInt(_3),text3).toStdString();
        break;
    }
    case API_ID_AI: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="添加定时 参数1 模型不能是空";
            break;
        }
        QString text2 = toQString(_2);
        if(text2.isEmpty())
        {
            result ="添加定时 参数2 提交AI 内容不能是空";
            break;
        }
        result = ai_ui->Ai_post(text,text2,toInt(_3)).toStdString();
        break;
    }
    case API_ID_GET_MEMBER: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取用户信息 参数1 群id不能是空";
            break;
        }
        QString text2 = toQString(_2);
        if(text2.isEmpty())
        {
            result ="获取用户信息 参数2 用户id 内容不能是空";
            break;
        }
        result = client->get_groups_members(text,text2).toStdString();
        break;
    }
    case API_ID_GET_groups_info: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取用户信息 参数1 群id不能是空";
            break;
        }

        result = client->get_groups_info(text).toStdString();
        break;
    }
    case API_ID_GET_groups_bot_state: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取机器人状态 参数1 群id不能是空";
            break;
        }

        result = client->get_groups_bot_state(text).toStdString();
        break;
    }
    case API_ID_GET_MEMBER_LIST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取群成员列表 参数1 群id不能是空";
            break;
        }
        QString cursor = toQString(_2);

        result = client->get_members_list(text, cursor).toStdString();
        break;
    }
    case API_ID_SET_JOIN_REQUEST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="处理加群 参数1 群id不能是空";
            break;
        }
        QString user = toQString(_2);
        if(user.isEmpty())
        {
            result ="处理加群 参数2 用户ID不能是空";
            break;
        }
        bool op = toBool(_3);
        QString id = toQString(_4);
        QString reject = toQString(_5);
        bool bilack = toBool(_6);
        result = client->approveGroupJoinRequest(text,user,op,id,reject,bilack).toStdString();
        break;
    }
    case API_ID_SET_MUTE_G: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="设置禁言 参数1 群id不能是空";
            break;
        }
        QString user = toQString(_2);
        if(user.isEmpty())
        {
            result ="设置禁言 参数2 JSON不能是空";
            break;
        }
        QJsonDocument doc = QJsonDocument::fromJson(user.toUtf8());
        if (doc.isNull() || !doc.isArray()) {

            result ="设置禁言 参数2 json无法解析";
            break  ;
        }
        QJsonArray membersArray = doc.array();
        result = client->setGroupRestrictChatSetting(text,membersArray).toStdString();
        break;
    }
    case API_ID_GET_MUTE_LIST_G: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取禁言列表 参数1 群id不能是空";
            break;
        }
        result = client->getGroupRestrictChatSetting(text).toStdString();
        break;
    }
    case API_ID_GET_JOIN_REQUEST_LIST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取加群列表 参数1 群id不能是空";
            break;
        }
        result = client->getjoin_request_list(text).toStdString();
        break;
    }
    case API_ID_GET_GROUP_BLCKLIST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取群黑名单列表 参数1 群id不能是空";
            break;
        }
        QString cursor = toQString(_1);

        result = client->get_member_blacklist(text,cursor).toStdString();
        break;
    }
    case API_ID_GROUP_BLCKLIST: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="设置群黑名单列表 参数1 群id不能是空";
            break;
        }
        QString user_list = toQString(_1);
        bool op = toBool(_3);
        result = client->member_blacklist(text,user_list,op).toStdString();
        break;
    }
    case API_ID_REMOV_MEMBER: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="移除群成员 参数1 群id不能是空";
            break;
        }
        QString user_list = toQString(_1);
        if(user_list.isEmpty())
        {
            result ="移除群成员 参数2 设置的用户不能是空";
            break;
        }
        bool add_blacklist = toBool(_3);
        result = client->del_members(text,user_list,add_blacklist).toStdString();
        break;
    }

    default:
        result = R"({"error":"Unknown apiId"})";
        break;
    }

    return result.c_str();
}





QString get_url(int type,const QString &openid,const QString &text = QString(),const QString &text2 = QString())
{
    QString url;
    if(type==0) url = "https://api.bot.qq.com/v2/groups/" + openid;
    else if(type==1) url = "https://api.bot.qq.com/channels/" + openid;
    else if(type==2) url = "https://api.bot.qq.com/v2/users/" + openid;
    else url = "https://api.bot.qq.com/dms/" + openid;
    if(!text.isEmpty()) url +="/" + text;
    if(!text2.isEmpty()) url +="/" + text2;
    return url;
}




static bool extractParamValue(QStringView params, const QString &key, QString &value) {
    int pos = 0;
    const int len = params.size();
    while (pos < len) {
        // 跳过空格
        while (pos < len && params[pos].isSpace()) ++pos;
        if (pos >= len) break;

        // 检查 key 是否匹配
        bool keyMatch = true;
        for (int i = 0; i < key.size(); ++i) {
            if (pos + i >= len || params[pos + i].toLower() != key[i].toLower()) {
                keyMatch = false;
                break;
            }
        }
        if (!keyMatch) {
            // 不匹配，跳至下一个逗号
            while (pos < len && params[pos] != ',') ++pos;
            if (pos < len && params[pos] == ',') ++pos;
            continue;
        }

        // key 匹配，跳到等号
        pos += key.size();
        while (pos < len && params[pos].isSpace()) ++pos;
        if (pos >= len || params[pos] != '=') {
            // 格式错误，跳过
            while (pos < len && params[pos] != ',') ++pos;
            if (pos < len && params[pos] == ',') ++pos;
            continue;
        }
        ++pos; // 跳过 '='

        // 跳过等号后的空格
        while (pos < len && params[pos].isSpace()) ++pos;
        if (pos >= len) break;

        // 提取 value，直到逗号或结尾
        int valueStart = pos;
        while (pos < len && params[pos] != ',') ++pos;
        int valueLen = pos - valueStart;
        // 去除 value 尾部的空格
        while (valueLen > 0 && params[valueStart + valueLen - 1].isSpace()) --valueLen;

        if (valueLen > 0) {
            value = params.mid(valueStart, valueLen).toString();
        } else {
            value.clear();
        }
        return true;
    }
    return false;
}

struct ImageInfo {
    QString urlOrPath;
    int x = 0;
    int y = 0;
};

static ImageInfo parseImageTagContent(QStringView tagContent) {
    ImageInfo info;
    // 去掉开头的 "image" 和可能的逗号、空格
    int start = 0;
    while (start < tagContent.size() && tagContent[start].isSpace()) ++start;
    if (start < tagContent.size() && tagContent[start].toLower() == 'i') {
        // 跳过 "image" 单词
        if (tagContent.size() >= start + 5 &&
            tagContent.mid(start, 5).compare(QLatin1String("image"), Qt::CaseInsensitive) == 0) {
            start += 5;
        }
    }
    // 跳过后面的空白和逗号
    while (start < tagContent.size() && (tagContent[start].isSpace() || tagContent[start] == ',')) ++start;
    if (start >= tagContent.size()) return info;

    QStringView params = tagContent.mid(start);
    // 提取 url 或 path
    if (!extractParamValue(params, QStringLiteral("url"), info.urlOrPath)) {
        extractParamValue(params, QStringLiteral("path"), info.urlOrPath);
    }
    QString xStr, yStr;
    if (extractParamValue(params, QStringLiteral("x"), xStr)) info.x = xStr.toInt();
    if (extractParamValue(params, QStringLiteral("y"), yStr)) info.y = yStr.toInt();

    return info;
}



void get_ref(QString &text,QString &message_reference)
{

    int refStart = text.indexOf(QLatin1String("[ref,"), 0, Qt::CaseInsensitive);
    if (refStart != -1) {
        int refEnd = text.indexOf(']', refStart);
        if (refEnd != -1) {
            QStringView tagContent = QStringView(text).mid(refStart + 5, refEnd - refStart - 5);
            int idxPos = tagContent.indexOf(QLatin1String("msg_idx="), 0, Qt::CaseInsensitive);
            if (idxPos != -1) {
                int valStart = idxPos + 8;
                int valEnd = tagContent.size();
                int commaPos = tagContent.indexOf(',', valStart);
                if (commaPos != -1) valEnd = commaPos;
                while (valEnd > valStart && tagContent[valEnd - 1].isSpace()) --valEnd;
                if (valEnd > valStart) {
                    message_reference = tagContent.mid(valStart, valEnd - valStart).toString();
                } else {
                    message_reference.clear();
                }
            } else {
                message_reference.clear();
            }
            text.remove(refStart, refEnd - refStart + 1);
        }
    }

}
std::future<QString> uploadimg(const QString &filePath);
void uploadimgCb(const QString &filePath, std::function<void(QString)> onDone);

QString QQBotClient::processImageTags(QString &text, int type, QString &info,
                                      int targetType, const QString &openid,
                                      QString &message_reference)
{
    get_ref(text, message_reference);
    static const QRegularExpression mdImgRe(R"(!\[([^\]]*)\]\(([^)]*)\))");
    static const QRegularExpression sizeRe(R"(#(\d+)px)");
    // ---------- 1. 定义统一的图片标签结构 ----------
    struct ImgTag {
        int start;          // 起始位置
        int length;         // 原始长度

        // 用于最终替换的宽高（优先使用用户指定，否则使用文件读取）
        int width;
        int height;

        // 扩展字段（仅对 Markdown 图片有效）
        bool isMdImg = false;          // 是否来自 ![]()
        bool needPadding = false;      // 是否需要补尺寸（用户未指定任何尺寸）
        QString alt;                   // 修正后的完整 alt（已补全或保持原样）
        QString coreText;              // 去除所有尺寸标记后的纯文本（用于 needPadding=true 时拼接）
        int userWidth = 0;             // 用户指定的宽度（若有）
        int userHeight = 0;            // 用户指定的高度（若有）
        bool hasUserSize = false;      // 用户是否指定了至少一个尺寸

        QString url;                   // 图片路径或 URL
    };
    QList<ImgTag> allTags;

    // ---------- 2. 解析旧标签 [image] ----------
    int searchFrom = 0;
    while (true) {
        int imgStart = text.indexOf(QLatin1String("[image"), searchFrom, Qt::CaseInsensitive);
        if (imgStart == -1) break;

        int imgEnd = imgStart + 1;
        int bracketDepth = 1;
        while (imgEnd < text.size() && bracketDepth > 0) {
            if (text[imgEnd] == '[') bracketDepth++;
            else if (text[imgEnd] == ']') bracketDepth--;
            ++imgEnd;
        }
        if (bracketDepth != 0) break;

        int tagLen = imgEnd - imgStart;
        int contentStart = imgStart + 6; // "[image"
        while (contentStart < imgEnd - 1 && (text[contentStart].isSpace() || text[contentStart] == ','))
            ++contentStart;
        int contentLen = tagLen - (contentStart - imgStart) - 1;
        if (contentLen < 0) contentLen = 0;
        QStringView tagContentView = QStringView(text).mid(contentStart, contentLen);

        ImageInfo imgInfo = parseImageTagContent(tagContentView);
        if (!imgInfo.urlOrPath.isEmpty()) {
            ImgTag tag;
            tag.start = imgStart;
            tag.length = tagLen;
            tag.url = imgInfo.urlOrPath;
            tag.width = imgInfo.x;
            tag.height = imgInfo.y;
            tag.isMdImg = false;            // 旧标签
            allTags.append(tag);
        }
        searchFrom = imgEnd;
    }

    // ---------- 3. 解析 Markdown 图片标签 ![]() ----------

    QRegularExpressionMatchIterator it = mdImgRe.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString alt = match.captured(1).trimmed();
        QString url = match.captured(2).trimmed();
        if (url.isEmpty()) continue;

        // 提取所有尺寸标记 #数字px

        QRegularExpressionMatchIterator sizeIt = sizeRe.globalMatch(alt);
        QList<int> sizes;
        while (sizeIt.hasNext()) {
            QRegularExpressionMatch sizeMatch = sizeIt.next();
            sizes.append(sizeMatch.captured(1).toInt());
        }
        int count = sizes.size();

        // 提取核心文本（去除所有尺寸标记）
        QString coreText = alt;
        coreText.remove(sizeRe);

        // 确定 needPadding、修正后的 alt、用户尺寸
        bool needPadding = false;
        QString modifiedAlt = alt;
        int userWidth = 0, userHeight = 0;
        bool hasUserSize = false;

        if (count == 0) {
            needPadding = true;            // 无尺寸 → 需要补
            hasUserSize = false;
            // modifiedAlt 保持原样（无尺寸）
        } else if (count == 1) {
            // 只有宽度 → 立即补高度 #0px
            modifiedAlt = alt.trimmed() + " #0px";
            needPadding = false;
            userWidth = sizes[0];
            userHeight = 0;
            hasUserSize = true;
        } else { // count >= 2
            // 已有完整尺寸，不变
            needPadding = false;
            userWidth = sizes[0];
            userHeight = sizes[1];
            hasUserSize = true;
            // modifiedAlt 保持原样
        }

        ImgTag tag;
        tag.start = match.capturedStart();
        tag.length = match.capturedLength();
        tag.url = url;
        tag.isMdImg = true;
        tag.alt = modifiedAlt;
        tag.coreText = coreText;
        tag.needPadding = needPadding;
        tag.userWidth = userWidth;
        tag.userHeight = userHeight;
        tag.hasUserSize = hasUserSize;
        // 当前宽高先设为用户指定值（后续可能被文件读取覆盖，但会恢复）
        tag.width = userWidth;
        tag.height = userHeight;

        allTags.append(tag);
    }

    // ---------- 4. 若没有任何图片标签，处理其他 Markdown 链接后返回 ----------
    if (allTags.isEmpty()) {
        if (type == 0 || type == 2)
            text = convertMdLinksKeepHttp(text);
        else
            text = convertMarkdownLinksToXml(text);
        return text;
    }

    // ---------- 5. 按起始位置从后往前排序 ----------
    std::sort(allTags.begin(), allTags.end(),
              [](const ImgTag &a, const ImgTag &b) { return a.start > b.start; });



    // ========== 定义 ReplaceInfo 结构体（放在循环外） ==========
    struct ReplaceInfo {
        int start;
        int length;
        QString newUrl;
        bool isMdImg;
        QString coreText;
        QString alt;
        int width;
        int height;
        bool needPadding;
        QString fileMd5;          // 用于缓存写入
        QString originalUrl;      // 原始路径（上传失败时回退）
    };

    // ========== 如果 type == 1，使用并发上传 ==========
    if (type == 1) {
        // ---------- 替换信息结构体 ----------
        struct ReplaceInfo {
            int start;
            int length;
            QString newUrl;
            bool isMdImg;
            QString coreText;
            QString alt;
            int width;
            int height;
            bool needPadding;
            QString fileMd5;
            QString originalUrl;
            QByteArray fileData; // 为了备用上传保存数据
        };

        QList<ReplaceInfo> replacements;
        std::vector<std::pair<int, std::future<QString>>> uploadFutures;

        // ---------- 遍历所有标签 ----------
        for (int idx = 0; idx < allTags.size(); ++idx) {
            ImgTag &tag = allTags[idx];
            QString newUrl = tag.url;
            bool isHttp = newUrl.startsWith("http://", Qt::CaseInsensitive) ||
                          newUrl.startsWith("https://", Qt::CaseInsensitive);

            if (!isHttp && !newUrl.isEmpty()) {
                QString fileMd5;
                if (!calculateFileMD5AndSize(newUrl, fileMd5, tag.width, tag.height))
                    continue;

                if (tag.isMdImg && tag.hasUserSize) {
                    tag.width = tag.userWidth;
                    tag.height = tag.userHeight;
                }

                // 缓存检查
                QString cacheKey = m_info->appid + ":imageB_" + fileMd5;
                bool cacheValid = false;
                QString cachedUrl;
                if (cache_db && !fileMd5.isEmpty()) {
                    QString cached = cache_db->get(cacheKey);
                    if (!cached.isEmpty()) {
                        int sepIdx = cached.lastIndexOf("||||");
                        if (sepIdx != -1) {
                            qint64 expireTime = cached.left(sepIdx).toLongLong();
                            cachedUrl = cached.mid(sepIdx + 4);
                            if (QDateTime::currentSecsSinceEpoch() < expireTime)
                                cacheValid = true;
                        }
                    }
                }

                // 读取文件数据（用于备用上传）
                QByteArray fileData;
                if (!cacheValid) {
                    QFile file(newUrl);
                    if (file.open(QIODevice::ReadOnly)) {
                        fileData = file.readAll();
                        file.close();
                    }
                }

                int replaceIdx = replacements.size();
                replacements.append({
                    tag.start,
                    tag.length,
                    cacheValid ? cachedUrl : QString(),
                    tag.isMdImg,
                    tag.coreText,
                    tag.alt,
                    tag.width,
                    tag.height,
                    tag.needPadding,
                    fileMd5,
                    newUrl,
                    fileData // 保存数据
                });

                if (!cacheValid) {
                    // 需要上传：调用 uploadimg 并保存 future
                    std::future<QString> future = uploadimg(newUrl); // 假设 openid 为空或由其他逻辑提供
                    uploadFutures.emplace_back(replaceIdx, std::move(future));

                }
            } else {
                // HTTP 链接或空路径
                replacements.append({
                    tag.start,
                    tag.length,
                    newUrl,
                    tag.isMdImg,
                    tag.coreText,
                    tag.alt,
                    tag.width,
                    tag.height,
                    tag.needPadding,
                    QString(),
                    newUrl,
                    QByteArray()
                });
            }
        }

        // ---------- 等待所有上传任务完成并获取结果 ----------
        for (auto &pair : uploadFutures) {
            int replaceIdx = pair.first;
            std::future<QString> &future = pair.second;
            QString uploadedUrl;
            try {
                uploadedUrl = future.get();
            } catch (const std::exception &e) {
                //qWarning() << "uploadimg exception:" << e.what();
                uploadedUrl = QString();
            }

            // 如果 uploadimg 返回空，则尝试备用富媒体上传（堵塞）
            if (uploadedUrl.isEmpty() && !replacements[replaceIdx].fileData.isEmpty()) {

                qint64 expireTime = 0;
                QString md5;
                bool ok = false;
                uploadRichMediaPool(targetType, openid, 1, replacements[replaceIdx].fileData, QString(), // filename 可以为空，走图片池子复用
                                    expireTime, md5, ok, uploadedUrl, /*usePool=*/true);
                 if (ok) uploadedUrl += "&response-content-type=image%2Fpng";
            }

            // 更新 replacements 中的 newUrl
            if (!uploadedUrl.isEmpty()) {
                replacements[replaceIdx].newUrl = uploadedUrl;
                if (cache_db) {
                    QString fileMd5 = replacements[replaceIdx].fileMd5;
                    QString cacheKey = m_info->appid + ":imageB_" + fileMd5;
                    qint64 expire = QDateTime::currentSecsSinceEpoch() + 1430 * 60;
                    cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(uploadedUrl));
                }
            } else {
                // 上传失败，保留原路径
                replacements[replaceIdx].newUrl = replacements[replaceIdx].originalUrl;
            }
        }

        // ---------- 统一替换 text ----------
        std::sort(replacements.begin(), replacements.end(),
                  [](const ReplaceInfo &a, const ReplaceInfo &b) {
                      return a.start > b.start;
                  });

        for (const ReplaceInfo &ri : std::as_const(replacements)) {
            if (ri.newUrl.isEmpty()) {
                text.replace(ri.start, ri.length, QString());
                continue;
            }
            QString markdownImg;
            if (ri.isMdImg) {
                if (ri.needPadding) {
                    int w = (ri.width > 0) ? ri.width : 0;
                    int h = (ri.height > 0) ? ri.height : 0;
                    if (w > 0 || h > 0)
                        markdownImg = QString("![%1 #%2px #%3px](%4)").arg(ri.coreText).arg(w).arg(h).arg(ri.newUrl);
                    else
                        markdownImg = QString("![%1 #1000px #0px](%2)").arg(ri.coreText, ri.newUrl);
                } else {
                    markdownImg = QString("![%1](%2)").arg(ri.alt, ri.newUrl);
                }
            } else {
                int w = (ri.width > 0) ? ri.width : 1000;
                int h = ri.height;
                if (h > 0)
                    markdownImg = QString("![#%1px #%2px](%3)").arg(w).arg(h).arg(ri.newUrl);
                else
                    markdownImg = QString("![#1000px #0px](%2)").arg(ri.newUrl);
            }
            text.replace(ri.start, ri.length, markdownImg);
        }
    }

    else{

        bool firstProcessed = false;  // 用于 type==0 只处理第一个标签
        bool neiwang=false;//测试内网是否可用
        for (int idx = 0; idx < allTags.size(); ++idx) {
        ImgTag &tag = allTags[idx];
        QString newUrl = tag.url;
        bool isHttp = newUrl.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
                      newUrl.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);

        // ---------- 仅对本地非 HTTP 路径执行上传 ----------
        if (!isHttp && !newUrl.isEmpty()) {
            QString fileMd5;
            // 计算 MD5 并获取文件实际宽高（会写入 tag.width / tag.height）
            if (!calculateFileMD5AndSize(newUrl, fileMd5, tag.width, tag.height))
                continue;

            // 如果是 Markdown 图片且用户指定了尺寸，则恢复为用户指定的值
            if (tag.isMdImg && tag.hasUserSize) {
                tag.width = tag.userWidth;
                tag.height = tag.userHeight;
            }

            // 根据 type 进行上传和缓存
            if (type == 0) {
                // ========== 类型 0：富媒体上传，只处理第一个标签 ==========
                if (!firstProcessed) {
                    firstProcessed = true;

                    QString cacheKey ="imageA_" + fileMd5;
                    bool cacheValid = false;
                    QString cachedUrl;

                    if (cache_db && !fileMd5.isEmpty()) {
                        QString cached = cache_db->get(cacheKey);
                        if (!cached.isEmpty()) {
                            int sepIdx = cached.lastIndexOf("||||");
                            if (sepIdx != -1) {
                                qint64 expireTime = cached.left(sepIdx).toLongLong();
                                cachedUrl = cached.mid(sepIdx + 4);
                                if (QDateTime::currentSecsSinceEpoch() < expireTime)
                                    cacheValid = true;
                            }
                        }
                    }

                    if (cacheValid) {
                        newUrl = cachedUrl;
                    } else {
                        bool ok = false;



                        // type==0 用 file_info 发送（每次都要新的）→ 每次重新上传，不入池；只享受 100K 直传快速路径
                        QString fileInfo = uploadRichMediaPoolA(targetType, openid, 1, newUrl, ok, /*usePool=*/false);

                        if (ok) {
                            QString path = extractBetween(fileInfo, "path=", ",");
                            if (!path.isEmpty()) {
                                newUrl = path;
                                qint64 expire = QDateTime::currentSecsSinceEpoch() + 1440 * 60;
                                cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(newUrl));
                            } else {
                                newUrl = tag.url;
                            }
                        } else {
                            newUrl = tag.url;
                        }
                    }
                    info = newUrl;
                }
                // 类型 0：所有图片标签均删除
                text.replace(tag.start, tag.length, QString());
            }
            else if (type == 2) {
                info = newUrl;
                text.replace(tag.start, tag.length, QString());
            }    
        }
        else {
            // ---------- HTTP 链接（或空路径）不上传，仅替换 ----------
            if (type == 0 || type == 2) {
                info = newUrl;
                text.replace(tag.start, tag.length, QString());
            } else if (type == 1) {
                // HTTP 链接，保留原有内容，但也要遵循 Markdown 图片的 alt 规则
                QString markdownImg;
                if (tag.isMdImg) {
                    if (tag.needPadding) {
                        // 无法获取尺寸，只保留核心文本（不加尺寸）
                        markdownImg = QStringLiteral("![%1](%2)").arg(tag.coreText,newUrl);
                    } else {
                        markdownImg = QStringLiteral("![%1](%2)").arg(tag.alt,newUrl);
                    }
                } else {
                    // 旧 [image] 标签，保持原逻辑（默认宽度 1000）
                    int w = (tag.width > 0) ? tag.width : 1000;
                    int h = tag.height;
                    if (h > 0)
                        markdownImg = QStringLiteral("![#%1px #%2px](%3)").arg(w).arg(h).arg(newUrl);
                    else
                        markdownImg = QStringLiteral("![#%1px #0px](%2)").arg(w).arg(newUrl);
                }
                text.replace(tag.start, tag.length, markdownImg);
            }
        }
    }
    }
    // ---------- 6. 处理其他 Markdown 链接 ----------

    if (type == 0 || type == 2) 
        text = convertMdLinksKeepHttp(text);
    else
        text = convertMarkdownLinksToXml(text);

    return text;

}
static QMutex g_cosPutPoolMutex;
static QList<CosPutPoolEntry> g_cosPutPool;

static const qint64 COS_POOL_TTL_MS   = 58 * 60 * 1000;  // 55 分钟超时删除

static const int    COS_POOL_MAX      = 10000;


// 生成一张随机像素的小 PNG：合法图片（能过 file_type=1 的内容类型检查），
// 像素随机 → 每次内容不同，避免缓存命中。池子新建链接的占位用。
QByteArray makePlaceholderPng()
{
    QImage img(8, 8, QImage::Format_RGB32);
    quint64 seed = QDateTime::currentMSecsSinceEpoch();
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            img.setPixel(x, y, qRgb((seed >> 33) & 0xFF, (seed >> 41) & 0xFF, (seed >> 49) & 0xFF));
        }
    }
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return buf.data();
}

// 从池子取一条可用链接。O(1) 无遍历：回池 append 即保序，队头永远最早入池——
// 只看队头：过期就弹出（55 分钟超时惰性清理），没过期直接取走。
// 取到返回 true 并移出池子。
bool takeCosPutEntry(CosPutPoolEntry &out)
{
    QMutexLocker locker(&g_cosPutPoolMutex);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    while (!g_cosPutPool.isEmpty() && nowMs >= g_cosPutPool.first().expireAt)
        g_cosPutPool.removeFirst();    // 队头过期 → 丢弃（55 分钟超时，惰性清理）
    if (g_cosPutPool.isEmpty()) return false;
    out = g_cosPutPool.takeFirst();
    return true;
}

// 取走的链接先暂存在这里（put 已完成，等这条消息发送完成）
// thread_local：一条消息的「上传图片 → 发送消息」在同一线程内完成，天然按消息隔离
static thread_local QList<CosPutPoolEntry> t_cosPutPending;

// put 成功后暂存（不入池），等消息发送完再回池
void parkCosPutEntry(const CosPutPoolEntry &entry)
{
    t_cosPutPending.append(entry);
}


// 把一组链接回池：立即可复用（无 CD）。跨线程安全（池有锁）。
void flushCosPutList(QList<CosPutPoolEntry> list)
{
    if (list.isEmpty()) return;
    QMutexLocker locker(&g_cosPutPoolMutex);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const CosPutPoolEntry &e : list) {
        if (nowMs >= e.expireAt) continue;              // 已过期直接丢弃
        if (g_cosPutPool.size() >= COS_POOL_MAX) break;
        g_cosPutPool.append(e);
    }
}

// 取走当前线程暂存的全部条目（转移所有权），交给异步发送回调持有
QList<CosPutPoolEntry> takeCosPutPending()
{
    QList<CosPutPoolEntry> out;
    out.swap(t_cosPutPending);
    return out;
}




// 消息发送完成后统一回池（同步路径用）
void flushCosPutPending()
{
    flushCosPutList(takeCosPutPending());
}

// RAII：发送函数退出时（无论发送成败）把本线程 pending 的链接全部回池
struct CosPutPendingGuard {
    ~CosPutPendingGuard() { flushCosPutPending(); }
};

// raw_url 加时间戳：重复链接有缓存，加时间戳才会实时读取 cos 文件
QString bustRawUrlCache(const QString &rawUrl)
{
    if (rawUrl.isEmpty()) return rawUrl;
    QString sep = rawUrl.contains('?') ? "&" : "?";
    return rawUrl + sep + "t=" + QString::number(QDateTime::currentMSecsSinceEpoch());
}




// processImageTags 纯回调版（堵塞版不动；send_messagesAsync / send_messagesAsync2 接线用）：
// 热路径（type==1 md 图片）全程零线程等待：
//   阶段1（调用线程同步执行，微秒级）：解析标签 + md5 + 缓存命中判断 + 读文件；
//     无本地待上传（纯 http / 全缓存命中）→ 直接完成，零线程零阻塞
//   阶段2：每个待上传项走 uploadimgCb（uploadimg 的纯回调版，内部回调链，不占线程），
//     原子计数聚合并发回调（回调在 NetManager 线程池线程触发）
//   阶段3：最后一个回调所在线程统一替换文本 + convert 链接 → onDone
// 备用上传也全回调：uploadimg 全渠道失败且有 fileData → uploadRichMediaPoolAsync
//   （prepare→put→finish→files 全异步，零线程）；其回调线程 park 的池条目经
//   takeCosPutPending 收进共享状态，完成时统一 re-park 到 onDone 线程 ——
//   「发送后回池」闭环不依赖特定线程
// 仅 type!=1（非 md 图片，低频）保留单线程包装（processImageTags 阻塞本体）
// 全部中间状态挂 shared_ptr，this 销毁安全（每个 touchpoint 用 appid 重查 m_botClients）。
void QQBotClient::processImageTagsAsync(const QString &textIn, int type, int targetType,
                                        const QString &openid,
                                        std::function<void(const QString &, const QString &, const QString &)> onDone)
{
    if (!onDone) return;
    const int appid = m_info->appid_int;

    // ---------- type 0/2：上传是阻塞型，保留单线程包装 ----------
    if (type != 1) {
        std::thread([this, appid, textIn, type, targetType, openid, onDone]() mutable {
            QQBotClient *c = m_botClients.value(appid);   // 防 this 已销毁（与 doPost 同款保护）
            if (!c) return;

            QString text = textIn;
            QString info, message_reference;
            c->processImageTags(text, type, info, targetType, openid, message_reference);
            if (m_botClients.value(appid) != c) return;   // 回调前再确认一次
            onDone(text, info, message_reference);
        }).detach();
        return;
    }

    // ---------- 共享状态 ----------
    struct ReplaceInfo {   // 字段与堵塞版一致
        int start;
        int length;
        QString newUrl;
        bool isMdImg;
        QString coreText;
        QString alt;
        int width;
        int height;
        bool needPadding;
        QString fileMd5;
        QString originalUrl;
        QByteArray fileData;
    };
    struct AsyncState {
        QMutex mutex;
        QString text;
        QString message_reference;
        QList<ReplaceInfo> replacements;
        std::atomic<int> pending{0};      // 未完成的待上传项（原子：回调并发聚合）
        std::atomic<bool> phase1Done{false};
        bool completed = false;           // 防重复收尾（mutex 内访问）
        QList<CosPutPoolEntry> parked;    // 备用线程 park 的池条目
        QElapsedTimer timer;              // 上传总耗时（日志页勾选「图片上传统计」时输出）
        std::atomic<int> uploadCount{0};  // 实际发起上传的图片数（缓存命中不算）
    };
    auto st = std::make_shared<AsyncState>();
    st->text = textIn;
    st->timer.start();

    // ---------- 收尾：替换文本 + re-park + convert + onDone ----------
    auto completeAll = [appid, st, onDone]() {
        QQBotClient *c = m_botClients.value(appid);
        if (!c) return;

        QString text, message_reference;
        {
            QMutexLocker locker(&st->mutex);
            if (st->completed) return;
            st->completed = true;

            QList<ReplaceInfo> reps = st->replacements;
            std::sort(reps.begin(), reps.end(),
                      [](const ReplaceInfo &a, const ReplaceInfo &b) { return a.start > b.start; });

            text = st->text;
            for (const ReplaceInfo &ri : std::as_const(reps)) {
                if (ri.newUrl.isEmpty()) {
                    text.replace(ri.start, ri.length, QString());
                    continue;
                }
                QString markdownImg;
                if (ri.isMdImg) {
                    if (ri.needPadding) {
                        int w = (ri.width > 0) ? ri.width : 0;
                        int h = (ri.height > 0) ? ri.height : 0;
                        if (w > 0 || h > 0)
                            markdownImg = QString("![%1 #%2px #%3px](%4)").arg(ri.coreText).arg(w).arg(h).arg(ri.newUrl);
                        else
                            markdownImg = QString("![%1 #1000px #0px](%2)").arg(ri.coreText, ri.newUrl);
                    } else {
                        markdownImg = QString("![%1](%2)").arg(ri.alt, ri.newUrl);
                    }
                } else {
                    int w = (ri.width > 0) ? ri.width : 1000;
                    int h = ri.height;
                    if (h > 0)
                        markdownImg = QString("![#%1px #%2px](%3)").arg(w).arg(h).arg(ri.newUrl);
                    else
                        markdownImg = QString("![#1000px #0px](%2)").arg(ri.newUrl);
                }
                text.replace(ri.start, ri.length, markdownImg);
            }
            message_reference = st->message_reference;

            // 备用线程 park 的池条目统一转到本线程 thread_local，发送时取走、发送回调里回池
            for (const CosPutPoolEntry &e : std::as_const(st->parked))
                parkCosPutEntry(e);
            st->parked.clear();
        }

        // 图片上传统计（日志页勾选「图片上传统计」时输出，临时开关不落盘）
        if (logPage && logPage->imgStat && st->uploadCount.load() > 0)
            AppendEventLog(QString("图片上传：%1 张，耗时 %2ms")
                               .arg(st->uploadCount.load()).arg(st->timer.elapsed()));

        if (m_botClients.value(appid) != c) return;   // onDone 前再确认一次
        text = convertMarkdownLinksToXml(text);       // 与堵塞版 type==1 收尾一致
        onDone(text, QString(), message_reference);
    };

    // ---------- 聚合：计数减一，归零且阶段1已收口才收尾 ----------
    auto finishOne = [completeAll, st]() {
        if (st->pending.fetch_sub(1) == 1 && st->phase1Done.load())
            completeAll();
    };

    // ---------- 单项结果：空且备选数据在 → 全异步备用上传（uploadRichMediaPoolAsync）；否则写缓存 ----------
    auto handleResult = [appid, st, targetType, openid, finishOne](int replaceIdx, QString uploadedUrl) {
        QQBotClient *c = m_botClients.value(appid);

        bool needFallback = false;
        {
            QMutexLocker locker(&st->mutex);
            needFallback = uploadedUrl.isEmpty() && !st->replacements[replaceIdx].fileData.isEmpty();
        }
        if (needFallback) {
            QByteArray data;
            {
                QMutexLocker locker(&st->mutex);
                data = st->replacements[replaceIdx].fileData;
            }
            QQBotClient *c3 = m_botClients.value(appid);
            if (!c3) { finishOne(); return; }
            c3->uploadRichMediaPoolAsync(targetType, openid, 1, data, QString(), // filename 可以为空，走图片池子复用
                                         /*usePool=*/true,
                [appid, st, replaceIdx, finishOne](const QString &result, qint64, const QString &,
                                                   bool ok, const QString &outurl) {
                    QQBotClient *c2 = m_botClients.value(appid);
                    QString url = outurl;
                    if (ok) url += "&response-content-type=image%2Fpng";

                    {
                        QMutexLocker locker(&st->mutex);
                        // 本回调线程 park 的池条目收进共享状态（completeAll 时统一 re-park 到 onDone 线程）
                        st->parked.append(takeCosPutPending());
                        ReplaceInfo &ri = st->replacements[replaceIdx];
                        if (ok && !url.isEmpty()) {
                            ri.newUrl = url;
                            if (c2 && cache_db) {
                                QString cacheKey = c2->m_info->appid + ":imageB_" + ri.fileMd5;
                                qint64 expire = QDateTime::currentSecsSinceEpoch() + 1430 * 60;
                                cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(url));
                            }
                        } else {
                            ri.newUrl = ri.originalUrl;
                        }
                    }
                    finishOne();
                });
            return;
        }

        // 正常结果：写缓存 + 设置 newUrl
        {
            QMutexLocker locker(&st->mutex);
            ReplaceInfo &ri = st->replacements[replaceIdx];
            if (!uploadedUrl.isEmpty()) {
                ri.newUrl = uploadedUrl;
                if (c && cache_db) {
                    QString cacheKey = c->m_info->appid + ":imageB_" + ri.fileMd5;
                    qint64 expire = QDateTime::currentSecsSinceEpoch() + 1430 * 60;
                    cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(uploadedUrl));
                }
            } else {
                // 上传失败，保留原路径
                ri.newUrl = ri.originalUrl;
            }
        }
        finishOne();
    };

    // ---------- 阶段1：解析 + 缓存 + 整理（调用线程同步执行） ----------
    QString &text = st->text;
    get_ref(text, st->message_reference);
    static const QRegularExpression mdImgRe(R"(!\[([^\]]*)\]\(([^)]*)\))");
    static const QRegularExpression sizeRe(R"(#(\d+)px)");

    struct ImgTag {
        int start;
        int length;
        int width;
        int height;
        bool isMdImg = false;
        bool needPadding = false;
        QString alt;
        QString coreText;
        int userWidth = 0;
        int userHeight = 0;
        bool hasUserSize = false;
        QString url;
    };
    QList<ImgTag> allTags;

    // 解析旧标签 [image]
    int searchFrom = 0;
    while (true) {
        int imgStart = text.indexOf(QLatin1String("[image"), searchFrom, Qt::CaseInsensitive);
        if (imgStart == -1) break;

        int imgEnd = imgStart + 1;
        int bracketDepth = 1;
        while (imgEnd < text.size() && bracketDepth > 0) {
            if (text[imgEnd] == '[') bracketDepth++;
            else if (text[imgEnd] == ']') bracketDepth--;
            ++imgEnd;
        }
        if (bracketDepth != 0) break;

        int tagLen = imgEnd - imgStart;
        int contentStart = imgStart + 6;
        while (contentStart < imgEnd - 1 && (text[contentStart].isSpace() || text[contentStart] == ','))
            ++contentStart;
        int contentLen = tagLen - (contentStart - imgStart) - 1;
        if (contentLen < 0) contentLen = 0;
        QStringView tagContentView = QStringView(text).mid(contentStart, contentLen);

        ImageInfo imgInfo = parseImageTagContent(tagContentView);
        if (!imgInfo.urlOrPath.isEmpty()) {
            ImgTag tag;
            tag.start = imgStart;
            tag.length = tagLen;
            tag.url = imgInfo.urlOrPath;
            tag.width = imgInfo.x;
            tag.height = imgInfo.y;
            tag.isMdImg = false;
            allTags.append(tag);
        }
        searchFrom = imgEnd;
    }

    // 解析 Markdown 图片标签 ![]()
    QRegularExpressionMatchIterator it = mdImgRe.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString alt = match.captured(1).trimmed();
        QString url = match.captured(2).trimmed();
        if (url.isEmpty()) continue;

        QRegularExpressionMatchIterator sizeIt = sizeRe.globalMatch(alt);
        QList<int> sizes;
        while (sizeIt.hasNext()) {
            QRegularExpressionMatch sizeMatch = sizeIt.next();
            sizes.append(sizeMatch.captured(1).toInt());
        }
        int count = sizes.size();

        QString coreText = alt;
        coreText.remove(sizeRe);

        bool needPadding = false;
        QString modifiedAlt = alt;
        int userWidth = 0, userHeight = 0;
        bool hasUserSize = false;

        if (count == 0) {
            needPadding = true;
            hasUserSize = false;
        } else if (count == 1) {
            modifiedAlt = alt.trimmed() + " #0px";
            needPadding = false;
            userWidth = sizes[0];
            userHeight = 0;
            hasUserSize = true;
        } else {
            needPadding = false;
            userWidth = sizes[0];
            userHeight = sizes[1];
            hasUserSize = true;
        }

        ImgTag tag;
        tag.start = match.capturedStart();
        tag.length = match.capturedLength();
        tag.url = url;
        tag.isMdImg = true;
        tag.alt = modifiedAlt;
        tag.coreText = coreText;
        tag.needPadding = needPadding;
        tag.userWidth = userWidth;
        tag.userHeight = userHeight;
        tag.hasUserSize = hasUserSize;
        tag.width = userWidth;
        tag.height = userHeight;

        allTags.append(tag);
    }

    // 没有任何图片标签：只处理其他 Markdown 链接后直接完成（同步、零线程）
    if (allTags.isEmpty()) {
        text = convertMarkdownLinksToXml(text);
        onDone(text, QString(), st->message_reference);
        return;
    }

    std::sort(allTags.begin(), allTags.end(),
              [](const ImgTag &a, const ImgTag &b) { return a.start > b.start; });

    // 遍历所有标签：缓存命中直接落位；未命中发起 uploadimgCb（纯回调，不占线程）
    for (int idx = 0; idx < allTags.size(); ++idx) {
        ImgTag &tag = allTags[idx];
        QString newUrl = tag.url;
        bool isHttp = newUrl.startsWith("http://", Qt::CaseInsensitive) ||
                      newUrl.startsWith("https://", Qt::CaseInsensitive);

        if (!isHttp && !newUrl.isEmpty()) {
            QString fileMd5;
            if (!calculateFileMD5AndSize(newUrl, fileMd5, tag.width, tag.height))
                continue;

            if (tag.isMdImg && tag.hasUserSize) {
                tag.width = tag.userWidth;
                tag.height = tag.userHeight;
            }

            // 缓存检查
            QString cacheKey = m_info->appid + ":imageB_" + fileMd5;
            bool cacheValid = false;
            QString cachedUrl;
            if (cache_db && !fileMd5.isEmpty()) {
                QString cached = cache_db->get(cacheKey);
                if (!cached.isEmpty()) {
                    int sepIdx = cached.lastIndexOf("||||");
                    if (sepIdx != -1) {
                        qint64 expireTime = cached.left(sepIdx).toLongLong();
                        cachedUrl = cached.mid(sepIdx + 4);
                        if (QDateTime::currentSecsSinceEpoch() < expireTime)
                            cacheValid = true;
                    }
                }
            }

            // 读取文件数据（用于备用上传）
            QByteArray fileData;
            if (!cacheValid) {
                QFile file(newUrl);
                if (file.open(QIODevice::ReadOnly)) {
                    fileData = file.readAll();
                    file.close();
                }
            }

            int replaceIdx = st->replacements.size();
            st->replacements.append({
                tag.start,
                tag.length,
                cacheValid ? cachedUrl : QString(),
                tag.isMdImg,
                tag.coreText,
                tag.alt,
                tag.width,
                tag.height,
                tag.needPadding,
                fileMd5,
                newUrl,
                fileData
            });

            if (!cacheValid) {
                // 需要上传：先计数再发起（发起可能同步重入回调，保证计数完整）
                ++st->pending;
                ++st->uploadCount;   // 上传统计（日志页勾选时输出）
                uploadimgCb(newUrl, [st, handleResult, replaceIdx](QString uploadedUrl) {
                    handleResult(replaceIdx, uploadedUrl);
                });
            }
        } else {
            // HTTP 链接或空路径
            st->replacements.append({
                tag.start,
                tag.length,
                newUrl,
                tag.isMdImg,
                tag.coreText,
                tag.alt,
                tag.width,
                tag.height,
                tag.needPadding,
                QString(),
                newUrl,
                QByteArray()
            });
        }
    }

    // 阶段1收口：无待上传项（全缓存命中）→ 同步收尾；有 → 最后一个回调收尾
    st->phase1Done = true;
    if (st->pending.load() == 0)
        completeAll();
}

// 参考 uploadRichMedia(QByteArray 版)，但走 put 链接池复用
// usePool=false：音视频/文件——实测复用链接服务端报「文件格式不对」，不做优化，
// 直接回退原始分片上传（uploadRichMedia），保险
QString QQBotClient::uploadRichMediaPool(int targetType, const QString& openid,int fileType,
                                         const QByteArray& data,const QString &filename,
                                         qint64& expireTime,QString &md5,bool &ok,QString &outurl,
                                         bool usePool) {

    ok=false;
    outurl.clear();
    expireTime = 0;
    if(data.isEmpty()) return QString();

    // targetType==4 不支持 100K 申请/池子流程（会报错）；
    // 音视频/文件（usePool=false）复用链接实测报文件格式不对 → 都回退原始分片上传方法
    if (targetType == 4 || !usePool) {
        return uploadRichMedia(targetType, openid, fileType, data, filename, expireTime, md5, ok, outurl);
    }

    // ---- md5/sha1/md5_10m 一律用原文件的真实值（申请大小才固定 100K）----
    qint64 fileSize = data.size();
    QCryptographicHash md5Hash(QCryptographicHash::Md5);
    md5Hash.addData(data);
    md5 = md5Hash.result().toHex();
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(data);
    QString sha1 = sha1Hash.result().toHex();
    QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
    md5_10mHash.addData(data.left(10 * 1024 * 1024));
    QString md5_10m = md5_10mHash.result().toHex();

    CosPutPoolEntry entry;
    bool haveEntry = false;
    bool fromPool = false;
    bool freshLink = false;   // 新建链接：1KB 占位内容已注册 file_info/raw_url，真实内容随后复写

    // ---- 1. 从池子取一条可复用的 put 链接（取用即移出，顺带清理过期条目）----
    fromPool = takeCosPutEntry(entry);
    haveEntry = fromPool;

    // ---- 2. 池子没有 → 修改版 upload_prepare：file_size 固定 100K 申请 put 链接 ----
    if (!haveEntry) {
        const qint64 APPLY_SIZE = 100 * 1024;    // 申请 100K，cos 实际可传任意大小
        QJsonObject prepJson;
        // 注册声明 file_type=1（图片）：某些群「禁止非管理员上传文件」，类型 4 注册的
        // file_info 发消息会被拦 → 占位改用随机生成的小 PNG（合法图片，过类型 1 内容检查），
        // 注册后真实图片靠 put 复写（COS 层无类型概念）
        prepJson["file_type"] = 1;
        prepJson["file_name"] = "text.bin";
        prepJson["file_size"] = APPLY_SIZE;
        prepJson["md5"]       = md5;
        prepJson["sha1"]      = sha1;
        prepJson["md5_10m"]   = md5_10m;
        QString prepUrl = get_url(targetType, openid, "upload_prepare");
        QString response = PostSync(prepUrl, prepJson, QString(), 30000);
        if (response.isEmpty()) return QString();

        QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
        if (respDoc.isNull()) return QString();
        QJsonObject respObj = respDoc.object();
        entry.uploadId = respObj["upload_id"].toString();
        if (entry.uploadId.isEmpty()) return response; // 错误信息

        QJsonArray parts = respObj["parts"].toArray();
        if (parts.isEmpty()) return QString("upload_prepare 未返回分片");
        QJsonObject part = parts[0].toObject();
        entry.partIndex    = part["index"].toInt();
        entry.presignedUrl = part["presigned_url"].toString();
        if (entry.presignedUrl.isEmpty()) return QString("upload_prepare 未返回 presigned_url");
        entry.expireAt = QDateTime::currentMSecsSinceEpoch() + COS_POOL_TTL_MS;  // 55 分钟超时

        // ---- 新建链接（md 图片路径）：先传随机小 PNG 占位快速注册 file_info/raw_url，----
        // 真实内容随后一次 put 复写 —— 耗时的 files 放在小占位对象上，真实图片只承担一次 put
        if (usePool) {
            // 占位内容：随机像素 PNG（每次不同），合法图片 + 避免缓存命中
            const QByteArray placeholder = makePlaceholderPng();
            QCryptographicHash phHash(QCryptographicHash::Md5);
            phHash.addData(placeholder);
            const QString phMd5 = phHash.result().toHex();

            try {
                put(entry.presignedUrl, placeholder, "application/octet-stream", 30000);
            } catch (const std::exception &) {
                return QString("put 占位失败(新建链接)");
            }

            // finish 按占位内容提交（block_size/md5 = 实际 put 的数据）
            QJsonObject phFin;
            phFin["upload_id"]  = entry.uploadId;
            phFin["part_index"] = entry.partIndex;
            phFin["block_size"] = placeholder.size();
            phFin["md5"]        = phMd5;
            qDebug() << PostSync(get_url(targetType, openid, "upload_part_finish"), phFin, QString(), 30000);

            // files 注册出 file_info / raw_url
            QJsonObject phFiles;
            phFiles["upload_id"] = entry.uploadId;
            QString phResp = PostSync(get_url(targetType, openid, "files"), phFiles, QString(), 30000);
            QJsonDocument phDoc = QJsonDocument::fromJson(phResp.toUtf8());
            QJsonObject phObj = phDoc.object();
            entry.fileInfo = phObj["file_info"].toString();
            entry.rawUrl   = phObj["raw_url"].toString();
            if (entry.fileInfo.isEmpty() || entry.rawUrl.isEmpty())
                return phResp;   // 注册失败，链接作废
            entry.infoFileType = fileType;   // 记「使用类型」（注册类型同为 1 图片）：md 图片捷径靠它防跨类型误用
            freshLink = true;
        }
    }

    // ---- 3. 把完整文件 put 到 cos（申请 100K 但实际可传任意大小）----
    // 链接此刻已不在池中：失败 → 直接丢弃（不还池）；成功 → 最后统一还池
    bool putOk = false;
    int retry = 0;
    int currentTimeout = 30000;
    while (retry < 3 && !putOk) {
        try {
            put(entry.presignedUrl, data, "application/octet-stream", currentTimeout);
            putOk = true;
        } catch (const std::exception &) {
            retry++;
            if (retry < 3) {
                QThread::msleep(1000 * (1 << (retry - 1)));
                currentTimeout += 10000;
            }
        }
    }
    if (!putOk) {
        if (freshLink)
            parkCosPutEntry(entry);   // 占位链接已注册有效，留着给下一个调用者复写
        return QString("put 文件到 cos 失败(重试3次)");
    }

    // ---- 3.5 复用捷径（md 图片）：put 覆盖 cos 内容即完成 —— 省掉 finish + files 两个 API，
    // 直接用已注册的 raw_url 加时间戳（链接实时反映本次 put 的数据）----
    // fromPool = 池里取的；freshLink = 新建链接（1KB 占位注册后复写）。infoFileType 防跨类型误用。
    if ((fromPool || freshLink) && usePool && !entry.fileInfo.isEmpty() && entry.infoFileType == fileType) {
        outurl = bustRawUrlCache(entry.rawUrl);
        expireTime = entry.expireAt / 1000;   // 剩余有效期（秒）
        ok = true;
        parkCosPutEntry(entry);               // 暂存，等消息发送完成后回池（无 CD）
        return entry.fileInfo;
    }

    // ---- 4. 提交 put 成功（upload_part_finish，按实际 put 的数据提交）----
    // （捷径未命中才会走到这：fileInfo 缺失等兜底全流程）
    {
        QJsonObject finishJson;
        finishJson["upload_id"]  = entry.uploadId;
        finishJson["part_index"] = entry.partIndex;
        finishJson["block_size"] = fileSize;
        finishJson["md5"]        = md5;
        QString finishUrl = get_url(targetType, openid, "upload_part_finish");
        qDebug() << PostSync(finishUrl, finishJson, QString(), 30000);
    }

    // ---- 5. 执行 /files 拿 file_info ----
    QJsonObject filesJson;
    filesJson["upload_id"] = entry.uploadId;
    QString filesUrl = get_url(targetType, openid, "files");
    QString filesResp = PostSync(filesUrl, filesJson, QString(), 30000);
    if (filesResp.isEmpty()) return QString();

    QJsonDocument filesRespDoc = QJsonDocument::fromJson(filesResp.toUtf8());
    if (filesRespDoc.isNull()) return QString();
    QJsonObject filesObj = filesRespDoc.object();
    QString file_info = filesObj["file_info"].toString();
    if (file_info.isEmpty()) {
        return filesResp; // 错误信息（链接已移出池，自然丢弃）
    }

    // ---- 6. 成功：链接暂存（存下 file_info/raw_url），消息发送完成后回池即可复用 ----
    entry.fileInfo = file_info;
    entry.rawUrl   = filesObj["raw_url"].toString();
    entry.infoFileType = fileType;
    parkCosPutEntry(entry);

    // raw_url 加时间戳，避免重复链接命中缓存
    outurl = bustRawUrlCache(filesObj["raw_url"].toString());
    expireTime = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
    ok = true;
    return file_info;
}

// uploadRichMediaPool 的纯回调版：prepare → put → finish → files 全链路异步，零线程零阻塞。
// 与阻塞版逻辑逐行对应（取池/复用捷径/暂存回池机制完全一致），仅把三个 PostSync 换成 PostAsync、
// 阻塞 put 换成 put2（NetManager::putAsync，已具备 cos 内网直连）。
// 回调在 NetManager 线程池线程触发（无事件循环）→ put 重试为立即重试，不能用 QTimer 退避。
void QQBotClient::uploadRichMediaPoolAsync(int targetType, const QString &openid, int fileType,
                                           const QByteArray &data, const QString &filename, bool usePool,
                                           std::function<void(const QString &result, qint64 expireTime,
                                                              const QString &md5, bool ok, const QString &outurl)> onDone)
{
    const int appid = m_info->appid_int;
    if (!onDone) return;
    if (data.isEmpty()) { onDone(QString(), 0, QString(), false, QString()); return; }

    // targetType==4 不支持 100K 申请/池子流程；音视频/文件（usePool=false）复用实测报文件格式不对
    // → 都回退原始分片上传（阻塞型，罕见路径临时线程兜底）
    if (targetType == 4 || !usePool) {
        std::thread([this, appid, targetType, openid, fileType, data, filename, onDone]() mutable {
            QQBotClient *c = m_botClients.value(appid);
            if (!c) return;
            qint64 expireTime = 0; QString md5; bool ok = false; QString outurl;
            QString result = c->uploadRichMedia(targetType, openid, fileType, data, filename, expireTime, md5, ok, outurl);
            if (m_botClients.value(appid) != c) return;
            onDone(result, expireTime, md5, ok, outurl);
        }).detach();
        return;
    }

    // ---- 共享状态（全链路异步，跨线程存活）----
    struct PoolAsyncState {
        CosPutPoolEntry entry;
        bool fromPool = false;
        bool usePool = false;
        int targetType = 0;
        QString openid;
        QByteArray data;
        qint64 fileSize = 0;
        int fileType = 0;
        QString md5, sha1, md5_10m;
        int retry = 0;
        int currentTimeout = 30000;
    };
    auto stp = std::make_shared<PoolAsyncState>();
    stp->usePool = usePool;
    stp->targetType = targetType;
    stp->openid = openid;
    stp->data = data;
    stp->fileSize = data.size();
    stp->fileType = fileType;

    // ---- 同步部分（调用线程，微秒级）：md5/sha1 用原文件真实值 + 取池 ----
    QCryptographicHash md5Hash(QCryptographicHash::Md5);
    md5Hash.addData(data);
    stp->md5 = md5Hash.result().toHex();
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(data);
    stp->sha1 = sha1Hash.result().toHex();
    QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
    md5_10mHash.addData(data.left(10 * 1024 * 1024));
    stp->md5_10m = md5_10mHash.result().toHex();

    stp->fromPool = usePool ? takeCosPutEntry(stp->entry) : false;

    auto fail = [stp, appid, onDone](const QString &msg) {
        if (!m_botClients.value(appid)) return;
        onDone(msg, 0, stp->md5, false, QString());
    };

    // ---- 4/5. finish → files（put 成功后）----
    auto afterPut = [this, appid, stp, onDone, fail]() {
        QQBotClient *c = m_botClients.value(appid);
        if (!c) return;

        // ---- 3.5 复用捷径（md 图片）：put 覆盖 cos 内容即完成 —— 省掉 finish + files 两个 API，
        // 直接用首次返回的 raw_url 加时间戳（链接实时反映本次 put 的数据）----
        if (stp->fromPool && !stp->entry.fileInfo.isEmpty() && stp->entry.infoFileType == stp->fileType) {
            QString outurl = bustRawUrlCache(stp->entry.rawUrl);
            parkCosPutEntry(stp->entry);   // 暂存在本回调线程，等消息发送完成后回池（无 CD）
            onDone(stp->entry.fileInfo, stp->entry.expireAt / 1000, stp->md5, true, outurl);
            return;
        }

        QJsonObject finishJson;
        finishJson["upload_id"]  = stp->entry.uploadId;
        finishJson["part_index"] = stp->entry.partIndex;
        finishJson["block_size"] = stp->fileSize;
        finishJson["md5"]        = stp->md5;
        QString finishUrl = get_url(stp->targetType, stp->openid, "upload_part_finish");
        c->PostAsync(finishUrl, finishJson, QString(), 30000,
            [this, appid, stp, onDone, fail](const QString &, QNetworkReply::NetworkError) {
                QQBotClient *c2 = m_botClients.value(appid);
                if (!c2) return;

                QJsonObject filesJson;
                filesJson["upload_id"] = stp->entry.uploadId;
                QString filesUrl = get_url(stp->targetType, stp->openid, "files");
                c2->PostAsync(filesUrl, filesJson, QString(), 30000,
                    [this, appid, stp, onDone, fail](const QString &filesResp, QNetworkReply::NetworkError) {
                        QQBotClient *c3 = m_botClients.value(appid);
                        if (!c3) return;
                        if (filesResp.isEmpty()) { fail(QString()); return; }
                        QJsonDocument doc = QJsonDocument::fromJson(filesResp.toUtf8());
                        if (doc.isNull()) { fail(QString()); return; }
                        QJsonObject filesObj = doc.object();
                        QString fileInfo = filesObj["file_info"].toString();
                        if (fileInfo.isEmpty()) { fail(filesResp); return; }   // 链接已移出池，自然丢弃

                        // ---- 6. 成功：链接暂存（存下 file_info/raw_url），消息发送完成后回池 ----
                        if (stp->usePool) {
                            stp->entry.fileInfo = fileInfo;
                            stp->entry.rawUrl   = filesObj["raw_url"].toString();
                            stp->entry.infoFileType = stp->fileType;   // 记使用类型，与阻塞版捷径防跨类型一致
                            parkCosPutEntry(stp->entry);   // 暂存在本回调线程，onDone 同线程可 takeCosPutPending 取走
                        }
                        QString outurl = bustRawUrlCache(filesObj["raw_url"].toString());
                        qint64 expire = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
                        onDone(fileInfo, expire, stp->md5, true, outurl);
                    });
            });
    };

    // ---- 3. put 到 cos（put2 = NetManager::putAsync，含 cos 内网直连），3 次重试 ----
    auto tryPut = std::make_shared<std::function<void()>>();
    *tryPut = [this, appid, stp, afterPut, fail, tryPut]() {
        QQBotClient *c = m_botClients.value(appid);
        if (!c) return;
        c->put2(stp->entry.presignedUrl, stp->data, "application/octet-stream", stp->currentTimeout,
                [appid, stp, afterPut, fail, tryPut](const QString &, QNetworkReply::NetworkError err) {
                    if (err == QNetworkReply::NoError) { afterPut(); return; }
                    stp->retry++;
                    if (stp->retry < 3) {
                        stp->currentTimeout += 10000;
                        if (m_botClients.value(appid)) (*tryPut)();   // 立即重试（回调线程无事件循环）
                    } else {
                        fail(QString("put 文件到 cos 失败(重试3次)"));
                    }
                });
    };

    // ---- 2. 池子没有 → upload_prepare（file_size 固定 100K，md5 用真实值）----
    if (stp->fromPool) {
        (*tryPut)();
        return;
    }
    QQBotClient *c = m_botClients.value(appid);
    if (!c) return;
    const qint64 APPLY_SIZE = 100 * 1024;    // 申请 100K，cos 实际可传任意大小
    QJsonObject prepJson;
    prepJson["file_type"] = fileType;
    prepJson["file_name"] = filename;
    prepJson["file_size"] = APPLY_SIZE;
    prepJson["md5"]       = stp->md5;
    prepJson["sha1"]      = stp->sha1;
    prepJson["md5_10m"]   = stp->md5_10m;
    QString prepUrl = get_url(targetType, openid, "upload_prepare");
    c->PostAsync(prepUrl, prepJson, QString(), 30000,
        [this, appid, stp, tryPut, fail](const QString &resp, QNetworkReply::NetworkError) {
            QQBotClient *c2 = m_botClients.value(appid);
            if (!c2) return;
            if (resp.isEmpty()) { fail(QString()); return; }
            QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
            if (doc.isNull()) { fail(QString()); return; }
            QJsonObject obj = doc.object();
            stp->entry.uploadId = obj["upload_id"].toString();
            if (stp->entry.uploadId.isEmpty()) { fail(resp); return; }
            QJsonArray parts = obj["parts"].toArray();
            if (parts.isEmpty()) { fail(QString("upload_prepare 未返回分片")); return; }
            QJsonObject part = parts[0].toObject();
            stp->entry.partIndex    = part["index"].toInt();
            stp->entry.presignedUrl = part["presigned_url"].toString();
            if (stp->entry.presignedUrl.isEmpty()) { fail(QString("upload_prepare 未返回 presigned_url")); return; }
            stp->entry.expireAt = QDateTime::currentMSecsSinceEpoch() + COS_POOL_TTL_MS;  // 55 分钟超时
            (*tryPut)();
        });
}


// ---------------------------------------------------------------------------
// 定位 ffmpeg 可执行文件 —— 音频链路的三个调用点共用（转 m4a / 只读探时长 / m4a 流复制切段）
//
// 查找顺序（第一个命中就用），三条路都命中不了时返回裸名字、交给 QProcess 走 PATH：
//   ① 设置界面里配的目录（g_config["ffmpeg"]，Windows 默认 "ffmpeg/"）
//   ② 主程序自己所在目录 —— **随包携带的那份放这儿**
//      Windows：和 qiancao.exe 同目录的 ffmpeg.exe
//      Linux  ：和 AppRun 里 $HERE/qiancao 同目录的 ffmpeg
//      ⚠ Linux 的 AppRun 只设 LD_LIBRARY_PATH / PYTHONHOME / PYTHONPATH，**没有改 PATH**，
//        而安装目录是 $HOME/qiancao（不在 PATH 里）—— 所以裸名字 "ffmpeg" 找不到
//        Gitee 补全包里的那份，必须靠这条命中。
//   ③ 裸名字 → 系统 PATH：用户自己 apt/dnf install ffmpeg 的场景，保持原行为不变
//
// ⚠ ② 命中的文件如果丢了执行位（从 tar 包里解出来常见），这里顺手补一下 ——
//   否则 QProcess 会以 "Permission denied" 静默失败，日志里只看到「ffmpeg 启动失败」。
// ---------------------------------------------------------------------------
static QString findFfmpegPath()
{
#ifdef Q_OS_WIN
    const QString kExe = QStringLiteral("ffmpeg.exe");
#else
    const QString kExe = QStringLiteral("ffmpeg");
#endif

    // ① 设置里配的目录
    if (!ffmpegdiv.isEmpty()) {
        const QString p = QDir(ffmpegdiv).filePath(kExe);
        if (QFileInfo(p).isFile())
            return p;
    }

    // ② 主程序同目录（AppImage / 安装目录 / exe 旁边）
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QString beside = QDir(appDir).filePath(kExe);
        const QFileInfo bi(beside);
        if (bi.isFile()) {
#ifndef Q_OS_WIN
            if (!bi.isExecutable())
                QFile::setPermissions(beside, bi.permissions()
                                      | QFileDevice::ExeOwner
                                      | QFileDevice::ExeGroup
                                      | QFileDevice::ExeOther);
#endif
            return beside;
        }
    }

    // ③ 交给系统 PATH
    return kExe;
}

// 音频转换产物的统一路径：**源文件同目录下的 tmp/ 子目录**，文件名沿用源名再补 .m4a
//   → /x/y/歌曲.mp3  ⇒  /x/y/tmp/歌曲.mp3.m4a
// 为什么不放源文件旁边（老写法 srcFilePath + ".m4a"）：
//   ui/chatpage.cpp 的「选择音频」文件对话框过滤器里有 *.m4a，产物放在同目录会被一起枚举出来，
//   用户每转一次就多看见一个中间产物。放进子目录后 QFileDialog 不展开子目录，就看不见了。
//   （文件名保留原后缀，顺带保证不同源文件不会互相覆盖）
// ⚠ 「产物路径」这个约定只有这一个出处：转换、复用检查、切段三处都必须走它，
//   否则复用失效（每次都重转），或者二次拼接出 <源>.m4a.m4a。
// ⚠ 返回空串 = 「这条链路不适用」：source 是 http URL（远程音频直传）或路径为空。
// ⚠ 这些产物**不做清理**（2026-10-03 拍板）：源音频可能在任意目录，程序压根不知道用户的音频文件
//   都放在哪，更不该替用户去删东西；`tmp/` 攒多了让用户自己删。别自作聪明加清理逻辑。
static QString audioM4aPathFor(const QString &srcFilePath)
{
    if (srcFilePath.isEmpty()
        || srcFilePath.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        return {};
    const QFileInfo fi(srcFilePath);
    return fi.absoluteDir().filePath(QStringLiteral("tmp/") + fi.fileName()
                                     + QStringLiteral(".m4a"));
}

QString convertAudioToSilk(const QString &srcFilePath)
{
    if (!QFile::exists(srcFilePath)) {
        //qWarning() << "源文件不存在:" << srcFilePath;
        return {};
    }

    // 去掉“小于1MB直接返回”的捷径（防止视频体积小但无音频的情况）
    // 无论大小，都走转换流程，确保输出格式统一

    QString ffmpegPath = findFfmpegPath();

    // 产物放 <源目录>/tmp/<源名>.m4a（见 audioM4aPathFor 的注释）
    const QString outputFilePath = audioM4aPathFor(srcFilePath);
    if (outputFilePath.isEmpty())          // 非本地文件（http URL）→ 不适用，原样发
        return srcFilePath;

    // ⚠ ffmpeg **不会**自己创建输出目录（实测：`Error opening output nodir/out.m4a:
    //   No such file or directory`，退出码 127，目录也不会被建出来）。少了这句 mkpath，
    //   就会静默走到下面的「exitCode() != 0 → 返回源路径」分支，表现成「音频怎么都压不下来」。
    if (!QDir().mkpath(QFileInfo(outputFilePath).absolutePath())) {
        AppendEventLog("音频转换目录创建失败: " + QFileInfo(outputFilePath).absolutePath());
        return srcFilePath;
    }

    QStringList ffmpegArgs = {
        "-y",                      // 覆盖输出
        "-i", srcFilePath,         // 输入（支持视频/音频）
        "-map", "0:a:0?",          // 【核心】明确取第一个音频轨，若无音频则跳过不报错
        "-vn",                     // 剔除视频画面
        "-c:a", "aac",             // 音频编码AAC
        "-b:a", "32k",             // 码率
        "-ar", "24000",            // 采样率
        "-ac", "1",                // 单声道
        outputFilePath
    };

    QProcess ffmpeg;
    ffmpeg.start(ffmpegPath, ffmpegArgs);

    if (!ffmpeg.waitForStarted()) {
        AppendEventLog("ffmpeg 启动失败");
        return srcFilePath;
    }

    // ⚠ 2026-10-02：转码现在全靠这一步（不再有进程内兜底），超时给宽一些 ——
    //   超时会被当作「转换失败」按原文件发送，而超长音频原样发大概率被 QQ 的 4:59 上限拒绝。
    if (!ffmpeg.waitForFinished(120000)) {
        AppendEventLog("ffmpeg 超时");
        ffmpeg.kill();
        return srcFilePath;
    }

    // 检查执行结果
    if (ffmpeg.exitCode() != 0) {
        QString err = ffmpeg.readAllStandardError();
        // 如果是“没有音频流”，这不是错误，按原文件返回即可
        if (!err.contains("Output file does not contain any stream")) {
            AppendEventLog("ffmpeg 转换失败:" + err);
        }
        return srcFilePath;
    }

    // 成功且生成文件
    return outputFilePath;
}


static const int AUDIO_SEG_MAX_SEC = 298;

static const qint64 kAudioSkipConvertBytes = 1024 * 1024;


static bool isCompactAudioContainer(const QString &suffix)
{
    static const QStringList kList = {
        QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("amr"),
        QStringLiteral("silk"), QStringLiteral("ogg"), QStringLiteral("aac"),
    };
    return kList.contains(suffix.toLower());
}

// 明确的视频后缀 —— 不可能是纯音频，直接判，连文件都不用打开（省一次 ffmpeg）
static bool hasDefiniteVideoExtension(const QString &suffix)
{
    static const QStringList kList = {
        QStringLiteral("avi"),  QStringLiteral("mkv"),  QStringLiteral("flv"),
        QStringLiteral("webm"), QStringLiteral("ts"),   QStringLiteral("m2ts"),
        QStringLiteral("mts"),  QStringLiteral("wmv"),  QStringLiteral("asf"),
        QStringLiteral("rm"),   QStringLiteral("rmvb"), QStringLiteral("mpg"),
        QStringLiteral("mpeg"), QStringLiteral("vob"),  QStringLiteral("ogv"),
        QStringLiteral("m4v"),  QStringLiteral("f4v"),
    };
    return kList.contains(suffix.toLower());
}

// 一次 `ffmpeg -i` 同时拿到「时长」与「容器里有没有视频轨」—— 两者都在 stderr 的媒体信息里。
// ⚠ 不给输出文件时 ffmpeg 会以非 0 退出，但信息照常打印完，不影响解析。
// 只对「体积够小、后缀又看不出是视频」的文件调用，所以这点进程开销只落在少数路径上。
struct MediaProbe
{
    double durationSec = -1;    // <0 = 未知（调用方按「不超长」处理）
    bool   hasVideo    = false;
};

static MediaProbe probeMediaInfo(const QString &filePath)
{
    MediaProbe info;
    QProcess p;
    p.start(findFfmpegPath(), {"-i", filePath});
    if (!p.waitForStarted())
        return info;
    if (!p.waitForFinished(10000)) {
        p.kill();
        return info;
    }
    const QString err = QString::fromLocal8Bit(p.readAllStandardError());

    static QRegularExpression reDur("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)");
    const auto m = reDur.match(err);
    if (m.hasMatch())
        info.durationSec = m.captured(1).toInt() * 3600
                         + m.captured(2).toInt() * 60
                         + m.captured(3).toDouble();

    // 形如 "  Stream #0:0[0x1](und): Video: h264 (High), yuv420p, ..."
    // （音频轨是 "Audio:"、字幕轨是 "Subtitle:"，不会误命中）
    static QRegularExpression reVid(QStringLiteral("Stream #\\d+:\\d+.*:\\s*Video:"));
    info.hasVideo = reVid.match(err).hasMatch();
    return info;
}

static double probeAudioDurationSec(const QString &filePath);   // 定义在下面，audioCanSendAsIs 要用

// 这个音频是否可以直接原样发送（已是小体积容器，或本身就不大）
// ⚠ 视频例外：视频当音频发时必须先提取音轨转码，哪怕只有几百 KB 也不能原样直传 ——
//   否则发给 file_type=3 的是个视频容器而不是语音。
static bool audioCanSendAsIs(const QString &path)
{
    QString p = path;
    const int cut = p.indexOf(QLatin1Char('?'));   // URL 可能带 query，别把它算进后缀
    if (cut >= 0) p.truncate(cut);
    const QFileInfo fi(p);

    const qint64 sz = fi.size();                   // 不存在 / URL → 0，不会误判成「小」
    if (sz <= 0 || sz >= kAudioSkipConvertBytes)
        return false;                              // 体积偏大 → 交给转换流程决定要不要切段

    // 体积够小（<1MB）才值得往下判：
    //   ① 紧凑容器（opus/m4a/amr/silk/ogg/aac）：体积小不代表时长短（低码率能拖很久），
    //      所以仍然探一次时长，但不必判视频轨 —— 这些容器里不可能有画面
    //   ② 明确的视频后缀：必须先提音轨转码 → 不探内容，直接进转换
    //   ③ 其他（mp3/wav/flac/mp4/mov/3gp…）：探「有没有视频轨」+「有没有超长」
    //      （.mp4/.mov/.3gp 光看后缀分不出音视频，m4a 也是 mp4 容器）
    // ⚠ SILK 是 QQ 自己的语音格式，ffmpeg 完全不认（连时长都读不出来）→ 探了也是白搭
    //   一次进程启动。语音消息本身都很短，直接原样发。
    if (fi.suffix().compare(QLatin1String("silk"), Qt::CaseInsensitive) == 0)
        return true;

    if (isCompactAudioContainer(fi.suffix()))
        return probeAudioDurationSec(p) <= AUDIO_SEG_MAX_SEC;
    if (hasDefiniteVideoExtension(fi.suffix()))
        return false;

    const MediaProbe mi = probeMediaInfo(p);
    return !mi.hasVideo && mi.durationSec <= AUDIO_SEG_MAX_SEC;
}

// 探测媒体时长（秒）；失败返回 -1。用于「转换产物要不要切段」以及上面的超长判断。
static double probeAudioDurationSec(const QString &filePath)
{
    return probeMediaInfo(filePath).durationSec;
}

// 把 m4a 按 segSec 秒流复制切段（不重编码），段文件名 = 原名去 .m4a + _seg000.m4a ...
// 已有同名段文件则直接复用；失败返回空表
static QStringList splitM4aSegments(const QString &m4aPath, int segSec)
{
    if (!m4aPath.endsWith(".m4a"))
        return {};                       // 只切转换产物；转换失败兜底的原文件不动
    const QString base = m4aPath.left(m4aPath.size() - 4);
    const QString filter = QFileInfo(base).fileName() + "_seg*.m4a";

    QDir dir = QFileInfo(m4aPath).absoluteDir();
    QStringList out;
    const QStringList exist = dir.entryList(QStringList{filter}, QDir::Files, QDir::Name);
    if (!exist.isEmpty()) {              // 之前切过，直接复用
        for (const QString &f : exist)
            out << dir.filePath(f);
        return out;
    }

    QString ffmpegPath = findFfmpegPath();
    QProcess p;
    p.start(ffmpegPath, {
        "-y", "-i", m4aPath,
        "-c", "copy",                    // AAC 直接流复制，秒级完成
        "-f", "segment",
        "-segment_time", QString::number(segSec),
        "-reset_timestamps", "1",
        base + "_seg%03d.m4a"
    });
    if (!p.waitForStarted())
        return {};
    if (!p.waitForFinished(60000)) {
        p.kill();
        return {};
    }
    if (p.exitCode() != 0)
        return {};

    const QStringList files = dir.entryList(QStringList{filter}, QDir::Files, QDir::Name);
    if (files.size() < 2)
        return {};                       // 只切出 1 段没意义，按失败算
    for (const QString &f : files)
        out << dir.filePath(f);
    return out;
}

// 远程音频（URL 直传被平台按时长拒绝时）允许下载到本地的体积上限
static const qint64 kRemoteAudioMaxBytes = 50LL * 1024 * 1024;   // 50MB

// 异步下载远程文件到本机临时目录，完成后回调本地路径（超限 / 失败回调空串）。
//   · 边下边判体积：Content-Length 或已收字节一旦超过 maxBytes 立刻中止，
//     **不落盘**（满足「先检查文件大小，超 50MB 就不下载」）
//   · 文件名 = qiancao_remote_<url 的 md5 前 16 位><后缀>：同一 URL 复用同一份，
//     顺带复用它转好的 .m4a 与已切好的分段，不会每次重下重转
//   · 不阻塞调用线程：下载在 NetManager 网络线程，落盘在回调线程（线程池）
static void downloadRemoteAudioToTempAsync(const QString &url, qint64 maxBytes,
                                           std::function<void(const QString &localPath)> onDone)
{
    // 走 NetManager 的连接池（原地 new QNAM + QEventLoop 会让 reply->deleteLater()
    // 永远等不到事件循环去处理，每下漏一份）。
    NetManager::instance()->downloadAsync(
        url, maxBytes, 120000,
        [url, onDone](const QByteArray &data, bool tooBig, QNetworkReply::NetworkError) {
            if (tooBig) {
                AppendEventLog(QStringLiteral("远程音频超过 50MB，放弃下载：") + url);
                onDone(QString());
                return;
            }
            if (data.isEmpty()) {
                AppendEventLog(QStringLiteral("远程音频下载失败：") + url);
                onDone(QString());
                return;
            }

            // 后缀沿用 URL 上的（ffmpeg 主要看内容，这只是让产物名好认一点）
            QString suffix;
            const QString pathPart = QUrl(url).path();
            const int dot = pathPart.lastIndexOf(QLatin1Char('.'));
            if (dot >= 0 && pathPart.size() - dot <= 6) {
                const QString s = pathPart.mid(dot + 1).toLower();
                if (s.size() >= 2 && s.size() <= 5
                    && QRegularExpression(QStringLiteral("^[a-z0-9]+$")).match(s).hasMatch())
                    suffix = QStringLiteral(".") + s;
            }

            const QString hash = QString::fromLatin1(
                QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
            const QString localPath = QDir(QDir::tempPath())
                .filePath(QStringLiteral("qiancao_remote_") + hash + suffix);

            QFile f(localPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                AppendEventLog(QStringLiteral("远程音频落盘失败：") + localPath);
                onDone(QString());
                return;
            }
            f.write(data);
            f.close();
            onDone(localPath);
        });
}

QString QQBotClient::sendOneMedia(int type, const QString &openid,const QString &pname,QString &text,qint64 now_us,
                                  const QString &msgid,bool is_wakeup,bool mode,int 发送类型,bool noref,const MessageLogContext &ctx)
{
    // 匹配短标签或全名标签：f/file, a/audio, v/video, flie(笔误)
    static QRegularExpression re(R"(\[(f(?:ile)?|a(?:udio)?|v(?:ideo)?|flie)\s*,\s*([^\]]+)\])",
                          QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = re.globalMatch(text);

    struct MatchInfo {
        int start;
        int length;
        QString type;   // "f", "a", "v", "flie", etc.
        QString params;
    };
    QList<MatchInfo> matches;

    // 第一步：收集所有匹配位置和原始信息
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        matches.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()),
                        m.captured(1).toLower(), m.captured(2)});
    }
    QString response;
    // 第二步：从后往前处理（删除时不影响前面的索引）
    for (int i = matches.size() - 1; i >= 0; --i) {
        const MatchInfo &info = matches[i];
        QString rawType = info.type;

        // 规范化类型
        QString mediaType;
        if (rawType == "f" || rawType == "file") mediaType = "file";
        else if (rawType == "a" || rawType == "audio") mediaType = "audio";
        else if (rawType == "v" || rawType == "video") mediaType = "video";
        else if (rawType == "flie") mediaType = "file";   // 常见拼写错误
        else continue;
        static QRegularExpression pathRe(R"(path\s*=\s*([^,\]]+))");
        static QRegularExpression urlRe(R"(url\s*=\s*([^,\]]+))");

        QString filePath = pathRe.match(info.params).captured(1).trimmed();
        QString fileUrl  = urlRe.match(info.params).captured(1).trimmed();

        if (filePath.isEmpty() && fileUrl.isEmpty()) {
            text.remove(info.start, info.length);
            continue;
        }
        if(!fileUrl.isEmpty() && fileUrl.startsWith("http"))
        {
            filePath=fileUrl;
        }
        bool needUpload = true;
        QStringList audioSegs;   // 音频超长切段列表；空 = 单文件直传
        QString fileInfo,fileMd5;
        int fileType = 1;
        if (mediaType == "video") fileType = 2;
        else if (mediaType == "audio") fileType = 3;
        else if (mediaType == "file") fileType = 4;
        if(!filePath.startsWith("http"))
        {
            int w=0,h=0;
            calculateFileMD5AndSize(filePath,fileMd5,w,h);


            if (cache_db && !fileMd5.isEmpty()) {
                QString cacheKey = QString("%1_%2").arg(mediaType,fileMd5);
                QString cached = cache_db->get(cacheKey);
                if (!cached.isEmpty()) {
                    //qDebug() <<cached;
                    int timeIdx = cached.lastIndexOf(",Time=");

                    if (timeIdx != -1) {
                        // ⚠️ 缓存里存的是 uploadRichMediaA 返回的整串 "[audio,path=...,md5=...,Time=1790086656]"，
                        // 末尾还有个 ']'。toLongLong() 要求整串都是数字，遇到 ']' 会整体转换失败并返回 0，
                        // 结果 QDateTime::currentSecsSinceEpoch() < 0 恒为假 → 缓存永远判过期 → 每次都重传。
                        QString expireStr = cached.mid(timeIdx + 6);
                        const int rbIdx = expireStr.indexOf(']');
                        if (rbIdx >= 0) expireStr.truncate(rbIdx);   // 切掉 "]"
                        bool okNum = false;
                        qint64 expire = expireStr.trimmed().toLongLong(&okNum);
                        // qDebug() << cached<<"|" << timeIdx <<"|"<< expire;
                        if (okNum && QDateTime::currentSecsSinceEpoch() < expire) {
                            // 补回被 left() 一并切掉的收尾 ']'，与「未命中缓存」时 uploadRichMediaA 的格式一致
                            fileInfo = cached.left(timeIdx) + "]";
                            needUpload = false;
                        }
                    } else {
                        fileInfo = cached;
                        needUpload = false;
                    }
                }
            }
            if(needUpload && fileType==3)
            {
                needUpload=true;

                // 源文件路径单独留一份：产物路径一律由「源」推导。
                // （filePath 下面会被替换成产物路径，再拿它去推就会拼出 <源>.m4a.m4a）
                const QString srcPath = filePath;
                // 产物路径：<源目录>/tmp/<源名>.m4a；空串 = 非本地文件（http URL），整条转换链路不适用
                const QString m4aPath = audioM4aPathFor(srcPath);

                // 之前 ffmpeg 转好的产物还在 → 直接复用，不必再重转一份
                if (!m4aPath.isEmpty() && QFile::exists(m4aPath))
                    filePath = m4aPath;

                // 已经是小体积容器（opus/m4a/amr…）或本身就不大 → 原样发，转换纯属浪费算力。
                // 只有这种「看着够小」的才值得花一次只读探测：小容器也可能是超长低码率音频。
                // （大文件走短路求值，根本不探，直接进转换）
                // audioCanSendAsIs() 内部已经判过「容器里有没有视频轨」和「时长有没有超 4:59」
                const bool sendAsIs = audioCanSendAsIs(filePath);

                if (!sendAsIs) {
                    // ① 已经是 .m4a（多半就是上次转好的产物）→ 直接流复制切段：无损、秒级、不重编码
                    if (filePath.endsWith(QStringLiteral(".m4a"), Qt::CaseInsensitive))
                        audioSegs = splitM4aSegments(filePath, AUDIO_SEG_MAX_SEC);

                    // ⚠ 只对本地文件走转码/切段：m4aPath 为空 = 源是 http URL，
                    //   那种情况直接原样上传（远程音频我们本地没有文件可转）
                    if (audioSegs.isEmpty() && !m4aPath.isEmpty()) {
                        // ② 转码：ffmpeg → 32kbps 单声道 .m4a。
                        //    码率/采样率/声道数与原来的进程内 Opus 完全一致，产物体积也就基本不变
                        //    （QQ 只校验时长和大小，不校验容器格式）。
                        if (QFile::exists(m4aPath)) {
                            filePath = m4aPath;                  // 已有产物（复用检查那步之外再兜一次）
                        } else {
                            const QString converted = convertAudioToSilk(srcPath);
                            // 转换失败时它返回源路径本身；为空（理论上不会）就保持原样发
                            if (!converted.isEmpty())
                                filePath = converted;
                        }

                        // 超长音频 → 切成多段，走下面的循环逐段上传发送
                        // （splitM4aSegments 只对 .m4a 生效：转换失败时 filePath 还是原文件，
                        //   它会直接返回空 = 原样发，不会把路径改坏）
                        if (probeAudioDurationSec(filePath) > AUDIO_SEG_MAX_SEC)
                            audioSegs = splitM4aSegments(filePath, AUDIO_SEG_MAX_SEC);
                    }
                }
            }

        }
        bool ok = true;
        if (needUpload) {
            // 音频超长时 audioSegs 是各分段路径；否则就是 filePath 本身
            const QStringList upPaths = audioSegs.isEmpty() ? QStringList{filePath} : audioSegs;
            for (const QString &upPath : upPaths) {
                ok = true;
                qint64 expireTime = 0;
                QString md5;
                QString uploadedUrl;
                /*
                if (g_cnb.e) {
                    uploadedUrl = uploadFileSync(upPath);
                }
                // 2. COS
                if (uploadedUrl.isEmpty() && g_cos.e) {
                    uploadedUrl = uploadFileSync_cos(upPath);
                }
                */
                if(uploadedUrl.isEmpty())
                {
                    uploadedUrl = upPath;
                }
                // 音视频/文件：100K 申请 + 整文件直传的快速路径，不入池（池子只给图片用）
                fileInfo = uploadRichMediaPoolA(type, openid, fileType, uploadedUrl, ok, /*usePool=*/false);
                if(fileType==3 && fileInfo.contains("40093013") && uploadedUrl.startsWith("http"))//上传音频时长超过限制
                {
                    // ── 远程音频（URL 直传）超时长 ──
                    // QQ 对 URL 直传的音频是在服务端做时长校验的，超 4:59 就回 40093013。
                    // 处理：先看体积（超 50MB 直接不下载）→ 拉回本地 → 转成 32k 单声道 m4a
                    //       → 按 AUDIO_SEG_MAX_SEC 切成 4:58 的分段逐条发。
                    // 整条链路丢进回调异步做（下载在网络线程，转码/上传在回调线程），
                    // 不占住当前 worker 线程；任一步失败就把平台原报错当文本发出去。
                    downloadRemoteAudioToTempAsync(uploadedUrl, kRemoteAudioMaxBytes,
                        [this, type, openid, pname, fileType, msgid, is_wakeup, mode, 发送类型, noref, now_us, ctx, fileInfo]
                        (const QString &localFile) {
                            // 失败兜底：把平台返回的报错内容原样发出去
                            auto sendOriginError = [&]() {
                                QString errInfo = fileInfo;   // 非 const：send_* 收的是 QString&
                                if (ctx.openid.isEmpty())
                                    send_messages(type,openid,pname,errInfo,msgid,is_wakeup,mode,发送类型,noref);
                                else
                                    send_msgAsync(type,openid,pname,errInfo,msgid,is_wakeup,mode,发送类型,noref,ctx.cb);
                            };

                            if (localFile.isEmpty()) {        // 超 50MB / 下载失败（日志已在下载里记过）
                                sendOriginError();
                                return;
                            }

                            // 产物路径一律由「源」推导（见 audioM4aPathFor 的注释）
                            QString usePath = audioM4aPathFor(localFile);
                            if (usePath.isEmpty() || !QFile::exists(usePath))
                                usePath = convertAudioToSilk(localFile);   // 失败时它返回源路径本身

                            QStringList segs;
                            const double durSec = probeAudioDurationSec(usePath);
                            if (durSec > AUDIO_SEG_MAX_SEC)
                                segs = splitM4aSegments(usePath, AUDIO_SEG_MAX_SEC);
                            else if (durSec > 0)
                                segs = QStringList{ usePath };   // 本地这份其实没超长 → 直接重发一次

                            if (segs.isEmpty()) {
                                AppendEventLog(QStringLiteral("远程音频超时长处理失败（无法切段），按原内容输出：")
                                               + fileInfo);
                                sendOriginError();
                                return;
                            }

                            for (const QString &seg : segs) {
                                bool okSeg = true;
                                QString segInfo = uploadRichMediaPoolA(type, openid, fileType, seg, okSeg, /*usePool=*/false);   // 非 const：send_messages/send_msgAsync 收的是 QString&
                                if (!okSeg) {
                                    if (ctx.openid.isEmpty())
                                        send_messages(type,openid,pname,segInfo,msgid,is_wakeup,mode,发送类型,noref);
                                    else
                                        send_msgAsync(type,openid,pname,segInfo,msgid,is_wakeup,mode,发送类型,noref,ctx.cb);
                                } else if (!segInfo.isEmpty()) {
                                    send_Media(type, openid,pname, segInfo,now_us, msgid,is_wakeup,noref,ctx);
                                }
                            }
                        });
                    text.remove(info.start, info.length);   // 标签照样要从待发文本里摘掉
                    continue;                               // 下载/转码/发送都在回调里做了
                }
                if(!ok)
                {
                    if(ctx.openid.isEmpty())
                        send_messages(type,openid,pname,fileInfo,msgid,is_wakeup,mode,发送类型,noref);
                    else
                        send_msgAsync(type,openid,pname,fileInfo,msgid,is_wakeup,mode,发送类型,noref,ctx.cb);
                }else if (!fileInfo.isEmpty() && cache_db && !fileMd5.isEmpty() && audioSegs.isEmpty()) { //发的链接是没有md5的；分段结果不进缓存
                    cache_db->put(QString("%1_%2").arg(mediaType,fileMd5), fileInfo);
                }

                if (ok && !fileInfo.isEmpty()) {

                    response = send_Media(type, openid,pname, fileInfo,now_us, msgid,is_wakeup,noref,ctx); // 增加 fileType 参数
                }
            }
        }else{
            if (!fileInfo.isEmpty()) {

                response = send_Media(type, openid,pname, fileInfo,now_us, msgid,is_wakeup,noref,ctx); // 增加 fileType 参数
            }
        }
        qDebug () << text;
        text.remove(info.start, info.length);
        qDebug() <<text;
    }

    return response;
}



void QQBotClient::initjgt(QJsonObject &json,const QJsonArray &prompt_keyboard,const QString &message_reference, const QString &msgid, bool is_wakeup,int logindex)
{
    if (!message_reference.isEmpty()) {
        QJsonObject refObj;
        refObj["message_id"] = message_reference;
        refObj["ignore_get_message_error"] = false;
        json["message_reference"] = refObj;
    }
    if(logindex!=1)
         json["msg_seq"] = m_info->message_sent;


    if (!is_wakeup) {
        if (msgid.contains("INTERACTION") || msgid.contains("FRIEND_ADD") || msgid.contains("GROUP_MEMBER") || msgid.startsWith("GROUP_JOIN_REQUEST")) //GROUP_MEMBER_ADD
            json["event_id"] = msgid;
        else
            json["msg_id"] = msgid;
    } else {
        json["is_wakeup"] = is_wakeup;
    }
    if (!prompt_keyboard.isEmpty()) {
        json["prompt_keyboard"] = QJsonObject{
            {"msg", QJsonObject{
                            {"rows", QJsonArray{
                                         QJsonObject{{"buttons", prompt_keyboard}}
                                     }}
                        }}
        };
    }
    if(logPage->wanzjson) logPage->onNewLogAdded(QJsonDocument(json).toJson());
}


QJsonObject parseLabelsToKeyboard(const QString &labelsText) {
    QJsonArray rowsArray;

    //qDebug() << labelsText;
    const QStringList lines = labelsText.split('\r', Qt::SkipEmptyParts);
    for (const QString &line : lines) {

        QRegularExpression re(R"(\[([^\]]*)\])");
        QRegularExpressionMatchIterator it = re.globalMatch(line);

        QJsonArray buttonsArray;
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString content = match.captured(1).trimmed(); // 去掉首尾空格

            // 按逗号分割字段（最多9个字段，索引0~8）
            QStringList fields = content.split(',');
            while (fields.size() < 9) fields.append(QString()); // 补足空字段


            QString label = fields[0].trimmed();
            QString actionData = fields[1].trimmed();
            int actionType = fields[2].trimmed().isEmpty() ? 2 : fields[2].trimmed().toInt();
            bool enter = (fields[3].trimmed() == "1");   // 立即发送
            bool reply = (fields[4].trimmed() == "1");   // 引用
            int style = fields[5].trimmed().isEmpty() ? 1 : fields[5].trimmed().toInt();
            QString modalContent = fields[6].trimmed();
            QString modalConfirm = fields[7].trimmed();
            QString modalCancel = fields[8].trimmed();

            // 跳过标题为空的按钮（可选）
            if (label.isEmpty()) continue;

            // ---------- 构建按钮 JSON（参考 ButtonData::toJson） ----------
            QJsonObject buttonObj;

            // render_data
            QJsonObject renderData;
            renderData["label"] = label;
            renderData["visited_label"] = label;   // 与原逻辑一致，通常相同
            if (style == 9999) {
                QJsonObject styleObj;
                styleObj["font_size"] = "small";
                renderData["style"] = styleObj;
            } else {
                renderData["style"] = style;
            }
            buttonObj["render_data"] = renderData;

            // action
            QJsonObject action;
            action["type"] = actionType;
            action["data"] = actionData;
            action["unsupport_tips"] = "当前版本不支持该按钮";
            if (reply) action["reply"] = reply;   // 引用
            if (enter) action["enter"] = enter;   // 立即发送
            // anchor 默认不设置

            // permission (默认所有人可用)
            QJsonObject permission;
            permission["type"] =2;
            action["permission"] = permission;

            // modal（仅当弹出内容非空时添加）
            if (!modalContent.isEmpty()) {
                QJsonObject modal;
                modal["content"] = modalContent;
                if (!modalConfirm.isEmpty()) modal["confirm_text"] = modalConfirm;
                if (!modalCancel.isEmpty()) modal["cancel_text"] = modalCancel;
                action["modal"] = modal;
            }

            // subscribe_data 本例暂不处理
            buttonObj["action"] = action;

            buttonsArray.append(buttonObj);
        }

        if (!buttonsArray.isEmpty()) {
            QJsonObject rowObj;
            rowObj["buttons"] = buttonsArray;
            rowsArray.append(rowObj);
        }
    }

    QJsonObject result;
    result["rows"] = rowsArray;
    return result;
}
void QQBotClient::bianl(int type,int log, QString &text,QJsonValue  &keyboard,QJsonArray &prompt_keyboard,const QString &openid,QString &mb)
{
    QString keyboard_data = extractBetween(text,"#b:#","#b:#");
    if(!keyboard_data.isEmpty())
        text=replaceBetweenAll(text,"#b:#","#b:#","");
    mb = extractBetween(text,"#mb:#","#mb:#");
    if(!mb.isEmpty())
        text=replaceBetweenAll(text,"#mb:#","#mb:#","");
    int index = mapTypeToTabIndex(type);

    Message log2;
    g_logdb[index]->readLog(m_info->appid,openid,log,log2);
    const QList<mdbtn> &bts = m_info->mdbtnlist;
    for (const mdbtn &bt : bts)
    {
        bool isok=false;
        for(int i=0;i< bt.zl.size();++i)
        {
            switch (bt.pplx) {
            case 0:
                if(QString::compare(log2.msg, bt.zl[i], Qt::CaseInsensitive) != 0) continue; //判断等于
                break;
            case 1:
                if(!log2.msg.startsWith(bt.zl[i],Qt::CaseInsensitive)) continue; //判断头部
                break;
            case 2:
                if(!log2.msg.contains(bt.zl[i],Qt::CaseInsensitive)) continue; //判断包含
                break;
            case 3:
                if(!text.contains(bt.zl[i],Qt::CaseInsensitive)) continue;  //判断text 包含
                break;
            default:
                continue;
            }
            bool ok=false;
            for(int i2=0;i2< bt.jzc.size();++i2)
            {
                if(text.contains(bt.jzc[i2]))
                {
                    ok=true;
                    break;
                }
            }
            if(ok) continue;

            int len = bt.hxc.size();
            if (len > 64) len = 64;                 // 最多只考虑前 64 个（与易语言一致）
            int want = qMin(len, 3);                // 最多取 3 个
            quint64 usedMask = 0;                   // 每一位代表一个索引是否被选过
            for (int i = 0; i < want; ++i) {
                int idx;
                for (int tries = 0; tries < 128; ++tries) {
                    idx = QRandomGenerator::global()->bounded(len);   // 0 ~ len-1
                    if (!(usedMask & (1ULL << idx))) {
                        usedMask |= (1ULL << idx);   // 标记已使用
                        break;
                    }
                }
                QJsonObject button;
                button["id"] = QString("A%1").arg(QRandomGenerator::global()->bounded(40, 23124));
                QJsonObject renderData;
                renderData["label"] = bt.hxc[idx];
                renderData["style"] = 2;
                button["render_data"] = renderData;
                prompt_keyboard.append(button);
            }
            keyboard = bt.btnjson;
            isok=true;
            break;
        }
        if(isok) break;
    }

    if (keyboard.isUndefined() || keyboard.isNull() || (keyboard.isObject() && keyboard.toObject().isEmpty()))
    {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(keyboard_data.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
        {
            if (doc.isObject()) {
                keyboard = doc.object();
            } else if (doc.isArray()) {
                keyboard = doc.array();  // 这就兼容了你说的“实际是数组”的情况
            } else {
                keyboard = parseLabelsToKeyboard(keyboard_data);
            }
        }
        else
        {
            keyboard = parseLabelsToKeyboard(keyboard_data);
        }
    }
    //小尾巴

    const QList<zdywb> &wb= m_info->zdywblist;
    for (const zdywb &w : wb)
    {
        bool isok=false;
        for(int i=0;i< w.zl.size();++i)
        {
            switch (w.pplx) {
            case 0:
                if(QString::compare(log2.msg, w.zl[i], Qt::CaseInsensitive) != 0) continue; //判断等于
                break;
            case 1:
                if(!log2.msg.startsWith(w.zl[i],Qt::CaseInsensitive)) continue; //判断头部
                break;
            case 2:
                if(!log2.msg.contains(w.zl[i],Qt::CaseInsensitive)) continue; //判断包含
                break;
            case 3:
                if(!text.contains(w.zl[i],Qt::CaseInsensitive)) continue;  //判断text 包含
                break;
            default:
                continue;
            }
            bool ok=false;
            for(int i2=0;i2< w.jzc.size();++i2)
            {
                if(text.contains(w.jzc[i2]))
                {
                    ok = true;
                    break;
                }
            }
            if(ok) continue;
            for(int i2=0;i2<w.thck.size();++i2)
            {
                text.replace(w.thck[i2],w.thcv[i2]);

            }

            if(!w.data.isEmpty())  
            {
                QString data = w.data;
                data.replace("【*】",text);
                text = data;
            }
            isok=true;
            break;
        }


        if(isok) break;
    }


    if(text.contains("{{name}}"))
    {
        if(g_botdb.contains(m_info->appid_int))  {
            auto *db = g_botdb [m_info->appid_int];
            QString username;
            db->getOrUpdateUser(openid,username);
            text.replace("{{name}}", username);
        }

    }


    text.replace("{{appid}}", m_info->appid);
    text.replace("{{botname}}", m_info->nickname);
    text.replace("{{group}}", openid);
    text.replace("{{user}}", log2.user);
    text.replace("{{msg}}", log2.msg);
    text.replace("{{昵称}}", log2.name);
    text.replace("{{msgid}}", log2.ch);
    static QRegularExpression re("\\{\\{([^}]+)\\}\\}");
    QRegularExpressionMatchIterator it = re.globalMatch(text);

    QList<QPair<int, int>> ranges;
    QStringList replacements;

    while (it.hasNext()) {
        auto match = it.next();
        QString inner = match.captured(1).trimmed();

        // 只处理含有逗号的关键字（参数化）
        if (!inner.contains(','))
            continue;

        QStringList parts = inner.split(',');
        if (parts.isEmpty())
            continue;

        QString keyword = parts[0].trimmed();
        QString replacement;

        if (keyword == "随机数") {
            int minVal = 0, maxVal = 100;   // 默认范围
            if (parts.size() >= 3) {
                minVal = parts[1].trimmed().toInt();
                maxVal = parts[2].trimmed().toInt();
            } else if (parts.size() == 2) {
                maxVal = parts[1].trimmed().toInt();
            }
            if (minVal > maxVal) qSwap(minVal, maxVal);
            int random = QRandomGenerator::global()->bounded(minVal, maxVal + 1);
            replacement = QString::number(random);
        }
        else if (keyword == "选择") {
            // 从第2个参数开始均为选项
            if (parts.size() < 2) {
                replacement = match.captured(0);  // 参数不足则保留原样
            } else {
                QStringList options;
                for (int i = 1; i < parts.size(); ++i) {
                    options << parts[i].trimmed();
                }
                int idx = QRandomGenerator::global()->bounded(options.size());
                replacement = options[idx];
            }
        }
        else if (keyword == "日期") {
            QString format = "yyyy-MM-dd hh:mm:ss";   // 默认格式
            if (parts.size() >= 2) {
                format = parts[1].trimmed();
            }
            replacement = QDateTime::currentDateTime().toString(format);
        }
        else {
            // 未知关键字：原样保留
            replacement = match.captured(0);
        }

        ranges.append(qMakePair(match.capturedStart(0), match.capturedLength(0)));
        replacements.append(replacement);
    }

    // 从后往前替换
    for (int i = ranges.size() - 1; i >= 0; --i) {
        text.replace(ranges[i].first, ranges[i].second, replacements[i]);
    }
}


QString processText(const QString &text, int timeoutMs = 30000);
QPair<int, QString> splitWrappedMsgId(const QString &wrapped);
QString QQBotClient::send_msgAsync(int type, const QString &openid,const QString &pname, QString &text,
                              const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref,Callback cb)
{
    CosPutPendingGuard _cosPutPoolGuard;   // 发送完成后（含异常路径）把本次用过的 put 链接立即回池
    if(type==18) type =0;

    if(type<0 || type >3 ) return R"({"msg":"发送类型错误 不在0-3之间"})";
    QString newtext;
    if(m_info->xxwb.isEmpty())
        newtext = text;
    else
        newtext = text+m_info->xxwb;
    if(text.contains("#python"))
    {
        MessageEvent ev;
        ev.appid = m_info->appid_int;
        ev.groupId = openid;
        ev.msgId=msgid;
        ev.type = type;
        newtext =python_code(text,ev);
    }
    if(text.contains("[get url") || text.contains("[post url")){
        auto [index, realMsgId] = splitWrappedMsgId(msgid);
        QJsonValue keyboard;
        QJsonArray prompt_keyboard;
        QString mb;
        QString textB = normalizeNewlinesToCR(newtext); //处理换行
        bianl(type,index,textB,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴
        auto *processor = new AsyncApiProcessor(textB, [this,type,openid,pname,msgid,is_wakeup,mode,sendType,noref,mb,prompt_keyboard,keyboard,cb](const QString &result) {
            QString text = result;
            return send_messagesAsync2(type,openid,pname,text,msgid,is_wakeup,mode,sendType,noref,mb,prompt_keyboard,keyboard,cb);
        });
        processor->start();
        return "{}";
    }

    // cb 必须透传下去，否则调用方等不到回调（send_messagesAsync 里才会 ctx.cb = cb）
    return send_messagesAsync(type,openid,pname,newtext,msgid,is_wakeup,mode,sendType,noref,cb);
}

QString QQBotClient::send_messages(int type, const QString &openid,const QString &pname, QString &text,
                                    const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref)
{
    CosPutPendingGuard _cosPutPoolGuard;   // 发送完成后（含异常路径）把本次用过的 put 链接立即回池
    if(type!=18){
        if(type<0 || type >3 ) return R"({"msg":"发送类型错误 不在0-3之间"})";
    }

    QString newtext;
    if(m_info->xxwb.isEmpty())
        newtext = text;
    else
        newtext = text+m_info->xxwb;

    if(text.contains("#python"))
    {
        MessageEvent ev;
        ev.appid = m_info->appid_int;
        ev.groupId = openid;
        ev.msgId=msgid;
        ev.type = type;
        newtext =python_code(text,ev);
    }

    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    QJsonValue keyboard;
    QJsonArray prompt_keyboard;
    QString message_reference,mb;

    newtext=normalizeNewlinesToCR(newtext); //处理换行

    bianl(type,index,newtext,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴
    if(newtext.contains("[get url") || newtext.contains("[post url"))
        newtext = processText(newtext);
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    QString newtext2 = sendOneMedia(type,openid,pname,newtext,now_us,msgid,is_wakeup,mode,sendType,noref,MessageLogContext());//检查也没有要发送 的语言视频 文件 原位修改text
    if (newtext.isEmpty()) return newtext2;

    bool mbise= mb.isEmpty();
    if(newtext.isEmpty() && mbise)
    {
        QString response = R"({"message":"发送内容不能为空"})";
        addmsglog(response,index,pname,text,now_us,type,openid);
        return response;
    }
    int seq_index=0;
    if(index>=0){
        seq_index=g_logdb[type+1]->incrementBufferStatus(index);
    }
    if(noref) seq_index =1;
    QString response,fileinfo;
    if(type==1 || type ==3)
    {
        if(!mbise)
        {
            newtext = processImageTags(mb,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(newtext);
            response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
            addmsglog(response,index,pname,text,now_us,type,openid);
            return response;
        }
        if(!mode && m_info->markdown_pd_mb || mode && sendType==2) //模板
        {
            newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(newtext);
            //response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup);
            response = R"({"message":"暂时不支持模板方式"})";
        }else if(!mode && m_info->markdown_pd || mode && sendType==1) //原生
        {
            newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(newtext);//违禁词过滤
            response = send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
        }else {
            newtext = processImageTags(newtext,2,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(newtext);//违禁词过滤
            QString url = get_url(type, openid, "messages");
            response = send_messages_pd(url,realMsgId,textA,fileinfo,message_reference,seq_index,MessageLogContext(),noref);
        }
        addmsglog(response,index,pname,newtext,now_us,type,openid);
        return response;
    }
    if(!mbise)
    {
        newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(newtext);
        response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
    }
    if(!mode && m_info->markdown || mode && sendType==1)
    {
        newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(newtext);
        response = send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
    }else{

        newtext = processImageTags(newtext,0,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(newtext);
        response = send_messages(type, openid, textA,fileinfo,prompt_keyboard, message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
    }
    addmsglog(response,index,pname,newtext,now_us,type,openid);
    return response;


}

QString QQBotClient::send_messagesAsync(int type, const QString &openid,const QString &pname, QString &text,
                                   const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref,Callback cb)
{
    CosPutPendingGuard _cosPutPoolGuard;   // 发送完成后（含异常路径）把本次用过的 put 链接立即回池

    QString newtext = text;
    if(text.contains("#python"))
    {
        MessageEvent ev;
        ev.appid = m_info->appid_int;
        ev.groupId = openid;
        ev.msgId=msgid;
        ev.type = type;
        newtext =python_code(text,ev);
    }

    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    MessageLogContext ctx;
    ctx.index = 0;
    ctx.pname = pname;                     // 拷贝
    ctx.jsonString = text;
    ctx.now_us = now_us;
    ctx.type = type;
    ctx.openid = openid;
    ctx.cb =cb;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    ctx.index = index;
    QJsonValue keyboard;
    QJsonArray prompt_keyboard;
    QString message_reference,mb;
    QString textB = normalizeNewlinesToCR(newtext); //处理换行
    bianl(type,index,textB,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴



    QString newtext2 = sendOneMedia(type,openid,pname,textB,now_us,msgid,is_wakeup,mode,sendType,noref,ctx);//检查也没有要发送 的语言视频 文件 原位修改text

    if (textB.isEmpty()) return newtext2;

    bool mbise= mb.isEmpty();
    if(textB.isEmpty() && mbise) return  R"({"message":"发送内容不能为空"})";
    int seq_index=0;
    bool ok=false;
    if(index>=0){

          g_logdb[type+1]->setBuffer_250(index,ok);
    }
    if(ok)
        seq_index = 1;
    else if(noref) return "{}";
    else seq_index = 2;

    QString response="{}",fileinfo;
    const QString msgIdCopy = realMsgId;   // structured binding 不能被 lambda 捕获（C++17），先拷贝
    if(type==1 || type ==3)
    {
        if(!mbise)
        {
            processImageTagsAsync(mb,1,type,openid,
                [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
                (const QString &tb, const QString &, const QString &mr) {
                    QString textA = forbidden->filterText(tb);
                    send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx,noref);
                });
            return response;
        }
        if(!mode && m_info->markdown_pd_mb || mode && sendType==2) //模板
        {
            textB = processImageTags(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(textB);
            //response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup);
            response = R"({"message":"暂时不支持模板方式"})";
        }else if(!mode && m_info->markdown_pd || mode && sendType==1) //原生
        {
            processImageTagsAsync(textB,1,type,openid,
                [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
                (const QString &tb, const QString &, const QString &mr) {
                    QString textA = forbidden->filterText(tb);//违禁词过滤
                    send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
                });
        }else {
            processImageTagsAsync(textB,2,type,openid,
                [this, type, openid, msgIdCopy, seq_index, ctx, noref]
                (const QString &tb, const QString &fi, const QString &mr) {
                    QString textA = forbidden->filterText(tb);//违禁词过滤
                    QString url = get_url(type, openid, "messages");
                    send_messages_pd(url,msgIdCopy,textA,fi,mr,seq_index,ctx,noref);
                });
        }
        return response;
    }

    if(!mbise) //模板 一般用不到
    {
        processImageTagsAsync(textB,1,type,openid,
            [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
            (const QString &tb, const QString &, const QString &mr) {
                QString textA = forbidden->filterText(tb);
                send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
            });
    }
    if(!mode && m_info->markdown || mode && sendType==1)
    {
        processImageTagsAsync(textB,1,type,openid,
            [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
            (const QString &tb, const QString &, const QString &mr) {
                QString textA = forbidden->filterText(tb);//违禁词过滤
                send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
            });
    }else{
        processImageTagsAsync(textB,0,type,openid,
            [this, type, openid, prompt_keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
            (const QString &tb, const QString &fi, const QString &mr) {
                QString textA = forbidden->filterText(tb);//违禁词过滤
                send_messages(type, openid, textA,fi,prompt_keyboard, mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
            });
    }
    return response;
}
QString QQBotClient::send_messagesAsync2(int type, const QString &openid, const QString &pname, QString &text,
                                         const QString &msgid, bool is_wakeup, bool mode, int sendType, bool noref, const QString &mb2,
                                         const QJsonArray &prompt_keyboard, const QJsonValue  &keyboard, Callback cb)
{
    CosPutPendingGuard _cosPutPoolGuard;   // 发送完成后（含异常路径）把本次用过的 put 链接立即回池
    QString mb=mb2;
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    MessageLogContext ctx;
    ctx.index = 0;
    ctx.pname = pname;                     // 拷贝
    ctx.jsonString = text;
    ctx.now_us = now_us;
    ctx.type = type;
    ctx.openid = openid;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    ctx.index = index;
    ctx.cb =cb;
    QString message_reference;
    QString newtext2 = sendOneMedia(type,openid,pname,text,now_us,msgid,is_wakeup,mode,sendType,noref,ctx);//检查也没有要发送 的语言视频 文件 原位修改text

    if (text.isEmpty()) return newtext2;

    bool mbise= mb.isEmpty();
    if(text.isEmpty() && mbise) return  R"({"message":"发送内容不能为空"})";
    int seq_index=0;
    bool ok=false;
    if(index>=0){

        g_logdb[type+1]->setBuffer_250(index,ok);
    }
    if(ok)
        seq_index = 1;
    else if(noref) return "{}";
    else seq_index = 2;

    QString response="{}",fileinfo;
    const QString msgIdCopy = realMsgId;   // structured binding 不能被 lambda 捕获（C++17），先拷贝
    if(type==1 || type ==3)
    {
        if(!mbise)
        {
            processImageTagsAsync(mb,1,type,openid,
                [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
                (const QString &tb, const QString &, const QString &mr) {
                    QString textA = forbidden->filterText(tb);
                    send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx,noref);
                });
            return response;
        }
        if(!mode && m_info->markdown_pd_mb || mode && sendType==2) //模板
        {
            text = processImageTags(text,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(text);
            //response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup);
            response = R"({"message":"暂时不支持模板方式"})";
        }else if(!mode && m_info->markdown_pd || mode && sendType==1) //原生
        {
            processImageTagsAsync(text,1,type,openid,
                [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
                (const QString &tb, const QString &, const QString &mr) {
                    QString textA = forbidden->filterText(tb);//违禁词过滤
                    send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
                });
        }else {
            processImageTagsAsync(text,2,type,openid,
                [this, type, openid, msgIdCopy, seq_index, ctx, noref]
                (const QString &tb, const QString &fi, const QString &mr) {
                    QString textA = forbidden->filterText(tb);//违禁词过滤
                    QString url = get_url(type, openid, "messages");
                    send_messages_pd(url,msgIdCopy,textA,fi,mr,seq_index,ctx,noref);
                });
        }
        return response;
    }

    if(!mbise) //模板 一般用不到
    {
        processImageTagsAsync(text,1,type,openid,
            [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
            (const QString &tb, const QString &, const QString &mr) {
                QString textA = forbidden->filterText(tb);
                send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
            });
    }
    if(!mode && m_info->markdown || mode && sendType==1)
    {
        processImageTagsAsync(text,1,type,openid,
            [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
            (const QString &tb, const QString &, const QString &mr) {
                QString textA = forbidden->filterText(tb);
                send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
            });
    }else{
        processImageTagsAsync(text,0,type,openid,
            [this, type, openid, prompt_keyboard, msgIdCopy, is_wakeup, seq_index, ctx, noref]
            (const QString &tb, const QString &fi, const QString &mr) {
                QString textA = forbidden->filterText(tb);
                send_messages(type, openid, textA,fi,prompt_keyboard, mr, msgIdCopy, is_wakeup,seq_index,ctx, noref);
            });
    }
    return response;
}

