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
#include <QEventLoop>
#include <QTimer>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

const int OUTLOG = 1;
const int API_ID_SEND_MESSAGES    = 2;
const int API_ID_SEND_MESSAGES_ARK = 3;
const int API_ID_DELETE_MESSAGES  = 4;
const int API_ID_GENERATE_SHARE_LINK = 5;
const int API_ID_RESPOND_INTERACTION = 6;
const int API_ID_BOT_LIST = 7;
const int API_ID_GET_OPENID = 8;
const int API_ID_GET_USER_NAME=9;
const int API_ID_PYTHON_HTTP=10;
const int API_ID_GET_USER_ID=11;

const int API_ID_HTMLIMG1=12;
const int API_ID_HTMLIMG2=13;
const int API_ID_DS=14;
const int API_ID_AI=15;
const int API_ID_GET_MEMBER=16;
const int API_ID_GET_MEMBER_LIST=17;
const int API_ID_GET_groups_info=18;
const int API_ID_GET_groups_bot_state=19;

const int API_ID_SET_JOIN_REQUEST=20;
const int API_ID_GET_JOIN_REQUEST_LIST=21;
const int API_ID_SET_MUTE_G=22;
const int API_ID_GET_MUTE_LIST_G=23;


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
                               lowerUrl.startsWith("mqqapi://"));

        if (shouldConvert) {
            QString encodedUrl = QString::fromUtf8(QUrl::toPercentEncoding(url));

            encodedUrl.replace("\\","\\\\");
            encodedUrl.replace("\"","\\\"");
            if(encodedUrl.isEmpty())
                encodedUrl=showText;
            if(url.size()>95)
            {
                QString xmlTag = QString("[%1](qagent://aio/inlinecmd?command=%2)")
                .arg(showText,encodedUrl);
                qDebug() <<xmlTag;
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
        auto *db = g_botdb[info->appid_int];
        obj["dau"] = db->m_userDailyMsg.size();
        obj["group_dau"] = db->m_groupDailyMsg.size();

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

static std::string handleSandboxCallback(int apiId, const char* _1, const char* _2, const char* _3,
                                         const char* _4, const char* _5, const char* _6,
                                         const char* _7, const char* _8) {
    QString logMsg;
    std::string result;
    switch (apiId) {
    case OUTLOG: {
        QString text = toQString(_1);
        if (_2 != nullptr && strlen(_2) > 0) {
            AppendEventLog(text, toInt(_2));
        } else {
            AppendEventLog(text);
        }
        result = R"({"code":0,"msg":"log output ok"})";
        break;
    }
    case API_ID_SEND_MESSAGES: {
        QString openid = toQString(_2);
        QString text = toQString(_3);
        Sandbox->appendOutput(QString("[沙盒消息] 向 %1 发送: %2").arg(openid,text));

        QMetaObject::invokeMethod(Sandbox, [text]() {
            Sandbox->addChatMessage(text, false);
        }, Qt::QueuedConnection);

        result = R"({"code":0,"msg":"send success simulated","message_id":"sandbox_msg_123"})";
        break;
    }
    case API_ID_SEND_MESSAGES_ARK: {
        QString openid = toQString(_2);
        QJsonObject ark = toJsonObject(_3);
        QString arkStr = QString::fromUtf8(QJsonDocument(ark).toJson(QJsonDocument::Compact));
        Sandbox->appendOutput(QString("[沙盒ARK消息] 向 %1 发送: %2").arg(openid,arkStr));
        result = R"({"code":0,"msg":"ark send success simulated"})";
        break;
    }
    case API_ID_DELETE_MESSAGES: {
        QString openid = toQString(_2);
        QString msgid = toQString(_3);
        Sandbox->appendOutput(QString("[沙盒操作] 删除消息: openid=%1, msgid=%2").arg(openid,msgid));
        result = R"({"code":0,"msg":"delete success simulated"})";
        break;
    }
    case API_ID_BOT_LIST: {
        result = botlist().toStdString();
        break;
    }
    case API_ID_GET_OPENID: {
        result = "查询本项需要传递appid 然鹅沙盒模型并没有提供这个";
        break;
    }
    case API_ID_PYTHON_HTTP: {

        result ="弃用..";
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
        ScheduleTask newTask;
        newTask.StringToTime(text2);
        if(newTask.scheduleTime.isEmpty())
            result = "定时时间解析失败请确认 格式正确 年,月,日,时,分|||... 添加多个 分是必传 其他可省略 |||分割添加多个时间短触发 -1为每分钟触发一次";
        else
            result="定时参数检查无误 沙箱环境不会真的添加";

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
    default:
        QString params;
        Sandbox->appendOutput(QString("[沙盒模拟] 调用了未特别处理的 API: %1，返回成功").arg(apiId));
        result = R"({"code":0,"msg":"simulated success"})";
        break;
    }

    return result;
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
    if(!g_sandboxuuid.isEmpty() && uuid==g_sandboxuuid)
    {
        result= handleSandboxCallback(apiId, _1, _2, _3, _4, _5, _6, _7, _8);
        return result.c_str();//这里
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
        if(!ok) return result.c_str();
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
        BotDB *db = g_botdb[appid];
        UserRecord user{};
        db->getUserBySeqId(toInt(_1),user);
        result =user.nickname;
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
        int limit = toInt(_2);
        int index = toInt(_3);
        if(limit<=0) limit =100;
        result = client->get_members_list(text,limit,index).toStdString();
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
    default:
        result = R"({"error":"Unknown apiId"})";
        break;
    }

    return result.c_str();
}


void QQBotClient::addmsglog(const QString &response,int index,const QString &pname,const QString &text,qint64 now_us, int type,const QString &openid)
{

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();

    QString message = obj["message"].toString();

    QString deleteid = obj["id"].toString();
    QJsonObject obj2 =obj["ext_info"].toObject();
    QString ref = obj2["ref_idx"].toString();

    int tabIndex = mapTypeToTabIndex(type);
    m_info->message_sent++;
    m_info->sent++;
    m_info->sent_day++;
    double diff_ms=0;
    bool ok=false;
    if(index>0)
    {
        qint64 us = g_logdb[tabIndex]->setBuffer_255(index,ok);
        qint64 diff_us = now_us - us;
        diff_ms = diff_us / 1000.0;
    }
    if(openid == chatPage->currentContactId)
    {
        QMetaObject::invokeMethod(this, [=]() {
            Message m("","",true, QDateTime::currentDateTime().toString("hh:mm:ss"),"","[ref,msg_idx="+ref+"]","");
            if(pname.contains("%1"))
                m.direction = pname.arg(diff_ms) + text;
            else
                m.direction = pname + text;
            if (deleteid.isEmpty() && message != "消息提交安全审核成功")
            {
                m.direction+="\n\n--------------------------\n\n"+response;

            }
            m.plugin_ch =deleteid;
            chatPage->addMessage(m);
        });

    }
    Message msg;
    if(ok)
    {
        g_logdb[tabIndex]->readLog(m_info->appid,openid,index,msg);
        msg.plugin_ch = deleteid;

        if(pname.contains("%1"))
            msg.direction = pname.arg(diff_ms) + text;
        else
            msg.direction = pname + text;

        msg.Color_0 = Color_0;
        if (deleteid.isEmpty() && message != "消息提交安全审核成功")
        {
            msg.direction+="\n\n--------------------------\n\n"+response;
            msg.Color_0 = 0xff0000;
        }
        g_logdb[tabIndex]->updateLog(m_info->appid,openid,index,msg);
        logPage->findRowBySeq(tabIndex,m_info->appid_int,index,msg.direction);
        msg.isSelf=true;
        msg.seq = index;
        if(ws_server) ws_server->broadcastMessage(msg,m_info->appid_int,type,openid);
        return ;
    }

    msg.isSelf = true;
    msg.plugin_ch = deleteid;
    msg.Color_0 = Color_0;
    if(!ref.isEmpty())
        msg.hf="[ref,msg_idx="+ref+"]";
    else
        msg.hf.clear();

    if(pname.contains("%1"))
        msg.direction = pname.arg(diff_ms) + text;
    else
        msg.direction = pname + text;

    if (deleteid.isEmpty() && message != "消息提交安全审核成功")
    {
        msg.direction+="\n\n--------------------------\n\n"+response;
        msg.Color_0 = 0xff0000;
    }

    msg.seq = g_logdb[tabIndex]->appendLog(m_info->appid,openid,msg);
    logPage->onNewLogAdded(tabIndex,0,m_info->appid_int,openid,msg);
    if(ws_server) ws_server->broadcastMessage(msg,m_info->appid_int,type,openid);

    DelFileSync_Cnb();
    return ;
}

QPair<int, QString> splitWrappedMsgId(const QString &wrapped) {
    if (wrapped.isEmpty()) return qMakePair(-1, QString());
    int firstBar = wrapped.indexOf('|');
    if (firstBar == -1) return qMakePair(-1, wrapped);
    int secondBar = wrapped.indexOf('|', firstBar + 1);
    if (secondBar == -1) return qMakePair(-1, wrapped);
    bool ok;
    int addr = QStringView(wrapped).mid(firstBar + 1, secondBar - firstBar - 1).toInt(&ok);
    if (!ok) addr = -1;
    QString realMsgId = wrapped.mid(secondBar + 1);
    return qMakePair(addr, realMsgId);
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
                QString result = uploadRichMedia(targetType, openid, 1,replacements[replaceIdx].fileData,QString(), // filename 可以为空
                                                         expireTime, md5, ok, uploadedUrl);
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



                        QString fileInfo = uploadRichMediaA(targetType, openid, 1, newUrl, ok);

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


QString QQBotClient::uploadRichMediaA(int targetType, const QString& openid,int fileType, const QString& filePath, bool &ok)
{


    qint64 expireTime=0;
    QString md5,info,url;
    if(filePath.startsWith("http"))
    {
        info = uploadRichMedia_url(targetType,openid,fileType,filePath,expireTime,ok);
    }else{
        info = uploadRichMedia(targetType,openid,fileType,filePath,expireTime,md5,ok,url);
    }
    if(!ok) return info;
    QString typeStr;
    switch (fileType) {
    case 1: typeStr = "image"; break;
    case 2: typeStr = "video"; break;
    case 3: typeStr = "audio"; break;
    case 4: typeStr = "file"; break;
    default: typeStr = "unknown";
    }
    return QString("[%1,path=%2,md5=%3,Time=%4]").arg(typeStr,info,md5).arg(expireTime);
}
QString QQBotClient::uploadRichMediaB(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename, bool &ok)
{
    qint64 expireTime=0;
    QString md5,url;
    QString info = uploadRichMedia(targetType,openid,fileType,data,filename,expireTime,md5,ok,url);
    if(!ok) return info;
    QString typeStr;
    switch (fileType) {
    case 1: typeStr = "image"; break;
    case 2: typeStr = "video"; break;
    case 3: typeStr = "audio"; break;
    case 4: typeStr = "file"; break;
    default: typeStr = "unknown";
    }
    return QString("[%1,path=%2,md5=%3,Time=%4]").arg(typeStr,info,md5).arg(expireTime);
}

//上传富媒体
QString QQBotClient::uploadRichMedia_url(int targetType, const QString& openid,int fileType, const QString& fileurl,
                                     qint64& expireTime,bool &ok)
{
    ok=false;
    if(!fileurl.startsWith("http"))return QString();
    QJsonObject obj;
    obj["file_type"]=fileType;
    obj["url"]=fileurl;

    QString url = get_url(targetType, openid, "files”");
    QString file_info,response;
    for(int i=0;i<10;i++)
    {
        response =PostSync(url,obj,QString(),300000);
        if (response.isEmpty()) return QString();
        QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
        if (respDoc.isNull()) return QString();
        QJsonObject respObj = respDoc.object();
        file_info = respObj["file_info"].toString();
        if(!file_info.isEmpty())
        {
            ok=true;
            expireTime = QDateTime::currentSecsSinceEpoch() + respObj["ttl"].toInt();
            return file_info;
        }
        QString err = respObj["message"].toString();
        if(err!="富媒体文件上传超时") return response;
        QThread::sleep(128);
    }
    return response;
}

QString QQBotClient::uploadRichMedia(int targetType, const QString& openid,int fileType, const QString& filePath,
                                     qint64& expireTime,QString &md5,bool &ok,QString &outurl) {
    ok=false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        //qWarning() << "无法打开文件:" << filePath;
        return QString();
    }
    QByteArray fileData = file.readAll();
    file.close();
    QFileInfo info(filePath);
    QString filename = info.fileName();
    return uploadRichMedia(targetType,openid,fileType,fileData,filename,expireTime,md5,ok,outurl);
}

QString QQBotClient::uploadRichMedia(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename,
                                    qint64& expireTime,QString &md5,bool &ok,QString &outurl) {


    qint64 fileSize = data.size();
     ok=false;
    // 2. 计算哈希值
    QCryptographicHash md5Hash(QCryptographicHash::Md5);
    md5Hash.addData(data);
    md5 = md5Hash.result().toHex();
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(data);
    QString sha1 = sha1Hash.result().toHex();
    int tenM = 10 * 1024 * 1024;
    QByteArray first10M = data.left(tenM);
    QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
    md5_10mHash.addData(first10M);
    QString md5_10m = md5_10mHash.result().toHex();

    // 3. 准备上传准备请求
    QJsonObject prepJson;
    prepJson["file_type"] = fileType;
    prepJson["file_name"] = filename;
    prepJson["file_size"] = (qint64)fileSize;
    prepJson["md5"] = md5;
    prepJson["sha1"] = sha1;
    prepJson["md5_10m"] = md5_10m;
    //prepJson["block_size"] = fileSize;
    QString url = get_url(targetType, openid, "upload_prepare");
    QString response = PostSync(url, prepJson,QString(), 30000);
    if (response.isEmpty()) return QString();

    // 4. 解析响应获取 upload_id 和 parts
    QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
    if (respDoc.isNull()) return QString();
    QJsonObject respObj = respDoc.object();
    QString upload_id = respObj["upload_id"].toString();
    if (upload_id.isEmpty()) return response; // 错误信息

    QJsonArray parts = respObj["parts"].toArray();

    // 5. 准备分片完成确认用的 JSON 基座
    QJsonObject partFinishBase;
    partFinishBase["upload_id"] = upload_id;
    int start =0;
    const int MAX_RETRIES = 3;
    const int BASE_TIMEOUT_MS = 30000;
    QString finishUrl = get_url(targetType, openid, "upload_part_finish");
    if(g_neiw.isEmpty()){
        for (int i = 0; i < parts.size(); ++i) {
            QJsonObject part = parts[i].toObject();
            int index = part["index"].toInt();
            QString blockSize = part["block_size"].toString();
            int blockSizeA=blockSize.toInt();
            QString presignedUrl = part["presigned_url"].toString();


            QByteArray chunk = data.mid(start, blockSizeA);
            start += blockSizeA;
            bool success = false;
            int retry=0;
            int currentTimeout = BASE_TIMEOUT_MS;
            while (retry < MAX_RETRIES && !success) {
                try {

                    put(presignedUrl, chunk, "application/octet-stream", currentTimeout);
                    success = true;
                } catch (const std::exception &e) {
                    //qWarning() << "分片" << index << "上传失败 (尝试" << retry+1 << "):" << e.what();
                    retry++;
                    if (retry < MAX_RETRIES) {
                        int sleepMs = 1000 * (1 << (retry - 1));
                        QThread::msleep(sleepMs);
                        currentTimeout += 10000;
                    }
                }
            }
            if(success==false)
            {
                ok=false;
                return QString("在上传%1分片时重试多次失败").arg(index);
            }
            QJsonObject finishJson;
            finishJson["upload_id"] = upload_id;
            finishJson["part_index"] = index;
            finishJson["block_size"] = chunk.size();
            QCryptographicHash chunkMd5(QCryptographicHash::Md5);
            chunkMd5.addData(chunk);
            finishJson["md5"] = QString(chunkMd5.result().toHex());

            QString finishResp = PostSync(finishUrl, finishJson,QString(), 30000);
        }
    }else{

        QElapsedTimer times;
        times.start();
        int totalParts = parts.size();
        int startPos = 0;

        std::vector<std::future<QByteArray>> futures;
        QList<QByteArray> chunks;          // 保存分片数据
        QList<QJsonObject> finishJsons;

        for (int i = 0; i < totalParts; ++i) {
            QJsonObject part = parts[i].toObject();
            int index = part["index"].toInt();
            int blockSize = part["block_size"].toString().toInt();
            QString presignedUrl = part["presigned_url"].toString();
            QByteArray chunk = data.mid(startPos, blockSize);
            startPos += blockSize;
            chunks.append(chunk);

            std::future<QByteArray> fut = put2(presignedUrl, chunk, "application/octet-stream", BASE_TIMEOUT_MS);
            futures.push_back(std::move(fut));
            QJsonObject finishJson;
            finishJson["upload_id"] = upload_id;
            finishJson["part_index"] = index;
            finishJson["block_size"] = chunk.size();
            QCryptographicHash chunkMd5(QCryptographicHash::Md5);
            chunkMd5.addData(chunk);
            finishJson["md5"] = QString(chunkMd5.result().toHex());
            finishJsons.append(finishJson);
        }

        for (int j = 0; j < futures.size(); ++j) {
            bool success = false;
            int retry = 0;
            int currentTimeout = BASE_TIMEOUT_MS;
            const QByteArray &chunk = chunks[j]; // 保存的数据，用于重试
            while (retry < MAX_RETRIES && !success) {
                try {
                    QString resp;
                    if (retry == 0) {
                        // 第一次使用已存储的 future
                        resp = futures[j].get();

                    } else {
                        // 重试：重新发起上传（需要新的 future）

                        std::future<QByteArray> newFut = put2(
                            parts[j].toObject()["presigned_url"].toString(), // 直接用索引 j
                            chunk,
                            "application/octet-stream",
                            currentTimeout
                            );
                        resp = newFut.get();
                    }
                    success = true;
                } catch (const std::exception &e) {
                    //qWarning() << "分片" << finishJsons[j]["part_index"].toInt()
                    //    << "上传失败 (尝试" << retry+1 << "):" << e.what();
                    retry++;
                    if (retry < MAX_RETRIES) {
                        QThread::msleep(1000 * (1 << (retry - 1)));
                        currentTimeout += 10000;
                    }
                }
            }
            if (!success) {
                ok = false;
                return QString("分片%1重试多次失败").arg(finishJsons[j]["part_index"].toInt());
            }
            //QString finishResp = PostSync(finishUrl, finishJsons[j], QString(), 30000);
        }
        //AppendEventLog("分片上传完成 通知服务器："+QString::number(futures.size()));
        for (int j = 0; j < futures.size(); ++j) {
            QString finishResp = PostSync(finishUrl, finishJsons[j], QString(), 30000);
        }


        /*
        int totalFinish = finishJsons.size();
        int finishedCount = 0;
        bool hasError = false;
        QMutex mutex; // 保护计数器和错误标志（若回调在非主线程）
        QEventLoop loop;

        for (int j = 0; j < totalFinish; ++j) {
            QJsonObject finishJson = finishJsons[j]; // 拷贝一份，避免引用失效
            doWork(2000);

            PostAsync(finishUrl, finishJson, QString(), 30000,
                      [&, j](const QString& response, QNetworkReply::NetworkError error) {
                          // 回调可能在任意线程，必须加锁
                          QMutexLocker locker(&mutex);
                          finishedCount++;
                          if (error != QNetworkReply::NoError || response.isEmpty()) {
                              hasError = true;
                              AppendEventLog("分片" + QString::number(j) + "完成请求失败:" + response);

                          }
                          // 如果全部完成，退出事件循环
                          if (finishedCount == totalFinish) {
                              loop.quit();
                          }
                      });
        }

        // 等待所有完成请求结束
        loop.exec();

        if (hasError) {
            ok = false;
            return QString("部分分片完成请求失败");
        }
        */
        //AppendEventLog("所有分片上传并完成 耗时："+QString::number(times.elapsed()));



    }
    // 7. 完成上传，请求 /files
    QJsonObject filesJson;
    filesJson["upload_id"] = upload_id;

    QString filesUrl = get_url(targetType, openid, "files");
    QString filesResp = PostSync(filesUrl, filesJson,QString(), 30000);
    if (filesResp.isEmpty()) return QString();

    QJsonDocument filesRespDoc = QJsonDocument::fromJson(filesResp.toUtf8());
    if (filesRespDoc.isNull()) return QString();
    QJsonObject filesObj = filesRespDoc.object();
    QString file_info = filesObj["file_info"].toString();
    if (file_info.isEmpty()) return filesResp; // 错误信息
    outurl = filesObj["raw_url"].toString();

    // 获取过期时间（秒为单位）
    expireTime = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
    ok=true;
    return file_info;
}


QString convertAudioToSilk(const QString &srcFilePath)
{
    if (!QFile::exists(srcFilePath)) {
        //qWarning() << "源文件不存在:" << srcFilePath;
        return {};
    }

    // 去掉“小于1MB直接返回”的捷径（防止视频体积小但无音频的情况）
    // 无论大小，都走转换流程，确保输出格式统一

#ifdef Q_OS_WIN
    QString ffmpegPath = QDir(ffmpegdiv).filePath("ffmpeg.exe");
#else
    QString ffmpegPath = "ffmpeg";
#endif

    QString outputFilePath = srcFilePath + ".m4a";

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

    if (!ffmpeg.waitForFinished(30000)) {
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

QString QQBotClient::sendOneMedia(int type, const QString &openid,const QString &pname,QString &text,qint64 now_us,
                                  const QString &msgid,bool is_wakeup,bool mode,int 发送类型,bool noref,MessageLogContext ctx)
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
                    int timeIdx = cached.lastIndexOf(",time=");
                    if (timeIdx != -1) {
                        qint64 expire = cached.mid(timeIdx + 6).toLongLong();
                        if (QDateTime::currentSecsSinceEpoch() < expire) {
                            fileInfo = cached.left(timeIdx);
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
                QString newpath = filePath+".m4a";
                if (!QFile::exists(newpath)) //检查有没有有就不转换了
                    filePath = convertAudioToSilk(filePath);
                else
                    filePath=newpath;
            }

        }
        bool ok = true;
        if (needUpload) {
            qint64 expireTime = 0;
            QString md5;
            QString uploadedUrl;
            /*
            if (g_cnb.e) {
                uploadedUrl = uploadFileSync(filePath);
            }
            // 2. COS
            if (uploadedUrl.isEmpty() && g_cos.e) {
                uploadedUrl = uploadFileSync_cos(filePath);
            }
            */
            if(uploadedUrl.isEmpty())
            {
                uploadedUrl = filePath;
            }
            fileInfo = uploadRichMediaA(type, openid, fileType, uploadedUrl,ok);

            if(!ok)
            {
                if(ctx.openid.isEmpty())
                    send_messages(type,openid,pname,fileInfo,msgid,is_wakeup,mode,发送类型,noref);
                else
                    send_msgAsync(type,openid,pname,fileInfo,msgid,is_wakeup,mode,发送类型,noref);
            }else if (!fileInfo.isEmpty() && cache_db && !fileMd5.isEmpty()) { //发的链接是没有md5的
                cache_db->put(QString("%1_%2").arg(mediaType,fileMd5), fileInfo);
            }
        }

        if (ok && !fileInfo.isEmpty()) {

            response = send_Media(type, openid,pname, fileInfo,now_us, msgid,is_wakeup,noref,ctx); // 增加 fileType 参数
        }
        text.remove(info.start, info.length);
    }

    return response;
}

QString QQBotClient::send_Media(int type,const QString &openid,const QString &pname,const QString &info,qint64 now_us,
                                const QString &msgid,bool is_wakeup,bool noref, MessageLogContext ctx)
{
    QJsonObject json;
    json["msg_type"] = 7;
    if (info.isEmpty()) return R"({"msg":"要发送的富媒体标签码为空"})";
    QString info2=extractBetween(info,"path=",",");
    if (info2.isEmpty()) return R"({"msg":"无法从path获取info"})";
    QJsonObject refObj;
    refObj["file_info"] = info2;
    json["media"] = refObj;
    json["noref"] = noref;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    ctx.index=index;
    int seq_index=0;
    bool ok=false;
    if(index>=0){

        g_logdb[type+1]->setBuffer_255(index,ok);
    }
    if(ok)
        seq_index = 1;
    else if(noref) return "{}";
    else seq_index = 2;
    initjgt(json, QJsonArray(),"",realMsgId,is_wakeup,seq_index);
    QString url= get_url(type,openid,"messages");
    if(ctx.openid.isEmpty()){
        QString response= PostSync(url, json,QString(), 5000);

        addmsglog(response,index,pname,info,now_us,type,openid);

        return response;

    }
    PostAsync(url, json, "", 5000,
              [this, ctx](const QString &resp, QNetworkReply::NetworkError err) {
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
              });
    return "{}";
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

    // 按行分割
    const QStringList lines = labelsText.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // 匹配每行中的每个 [ ... ] 块
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
void QQBotClient::bianl(int type,int log, QString &text,QJsonObject &keyboard,QJsonArray &prompt_keyboard,const QString &openid,QString &mb)
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
    if (keyboard.isEmpty())        // 如果当前 keyboard 为空（没有任何键值对）
    {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(keyboard_data.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject())
        {
            keyboard = doc.object();   // 解析成功，赋值给 keyboard
        }
        else
        {
            keyboard =parseLabelsToKeyboard(keyboard_data);
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
        auto *db = g_botdb [m_info->appid_int];
        QString username;
        db->getOrUpdateUser(openid,username);
        text.replace("{{name}}", username);

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

    // 存储匹配项（从后往前替换保证偏移正确）
    QList<QPair<int, int>> ranges;      // <起始位置, 长度>
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

QString QQBotClient::send_messages_pd(const QString &url,const QString &msgId, const QString &content, const QString &imagePath,
                                      const QString &message_reference, int seq_index,const MessageLogContext ctx,bool noref)
{
    QByteArray postData;
    QString headers;
    bool useJson = imagePath.isEmpty() || imagePath.startsWith("http", Qt::CaseInsensitive);

    if (useJson) {
        QJsonObject obj;
        if (!imagePath.isEmpty() && imagePath.startsWith("http")) {
            obj["image"] = imagePath;
        }
        if (!content.isEmpty()) {
            obj["content"] = content;
        }
        obj["noref"] = noref;
        if (msgId.contains("INTERACTION") || msgId.contains("FRIEND_ADD") || msgId.contains("GROUP_MEMBER")) //GROUP_MEMBER_ADD
            obj["event_id"] = msgId;
        else
            obj["msg_id"] = msgId;

        if (!message_reference.isEmpty()) {
            QJsonObject refObj;
            refObj["message_id"] = message_reference;
            refObj["ignore_get_message_error"] = false;
            obj["message_reference"] = refObj;
        }
        headers = "application/json";
        postData = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    } else {
        QString boundary = QString("----WebKitFormBoundary%1")
        .arg(QString::number(QRandomGenerator::global()->generate(), 16));
        QByteArray body;
        if (!content.isEmpty()) {
            QByteArray contentData = content.toUtf8();
            body += "--" + boundary.toUtf8() + "\r\n";
            body += "Content-Disposition: form-data; name=\"content\"\r\n";
            body += "Content-Length: " + QByteArray::number(contentData.size()) + "\r\n";
            body += "\r\n";
            body += contentData + "\r\n";
        }
        if (!imagePath.isEmpty() && QFile::exists(imagePath)) {
            QFile file(imagePath);
            if (file.open(QIODevice::ReadOnly)) {
                QByteArray imageData = file.readAll();
                file.close();
                body += "--" + boundary.toUtf8() + "\r\n";
                body += "Content-Disposition: form-data; name=\"file_image\"; filename=\"image.jpeg\"\r\n";
                body += "Content-Type: image/jpeg\r\n";
                body += "\r\n";
                body += imageData + "\r\n";
            }
        }
        QString idFieldName;
        if (msgId.contains("INTERACTION") || msgId.contains("FRIEND_ADD") || msgId.contains("GROUP_MEMBER"))
            idFieldName = "event_id";
        else
            idFieldName = "msg_id";

        QByteArray idData = msgId.toUtf8();
        body += "--" + boundary.toUtf8() + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + idFieldName.toUtf8() + "\"\r\n";
        body += "Content-Length: " + QByteArray::number(idData.size()) + "\r\n";
        body += "\r\n";
        body += idData + "\r\n";
        body += "--" + boundary.toUtf8() + "--\r\n";
        headers = QString("multipart/form-data; boundary=%1").arg(boundary);
        postData = body;
    }

    if (ctx.openid.isEmpty()) {
        return PostSync(url, postData, headers, 10000);
    } else {
        QHash<QString, QString> headers2;
        headers2.insert("X-Union-Appid", m_info->appid);
        headers2.insert("Authorization", "QQBot " + m_accessToken);
        headers2.insert("Content-Type", headers);

        postRawAsync(url, postData, headers2, 20000,
                     [this, ctx](const QString &resp, QNetworkReply::NetworkError err) {
                         addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                                   ctx.now_us, ctx.type, ctx.openid);
                     });
        return QString();
    }
}
QString processText(const QString &text, int timeoutMs = 30000);

QString QQBotClient::send_msgAsync(int type, const QString &openid,const QString &pname, QString &text,
                              const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref)
{
    if(type==18) type =0;

    if(type<0 || type >3 ) return R"({"msg":"发送类型错误 不在0-3之间"})";

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
    if(text.contains("[get url") || text.contains("[post url")){
        auto [index, realMsgId] = splitWrappedMsgId(msgid);
        QJsonObject keyboard;
        QJsonArray prompt_keyboard;
        QString mb;
        QString textB = normalizeNewlinesToCR(newtext); //处理换行
        bianl(type,index,textB,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴
        auto *processor = new AsyncApiProcessor(textB, [this,type,openid,pname,msgid,is_wakeup,mode,sendType,noref,mb,prompt_keyboard,keyboard](const QString &result) {
            QString text = result;
            return send_messagesAsync2(type,openid,pname,text,msgid,is_wakeup,mode,sendType,noref,mb,prompt_keyboard,keyboard);
        });
        processor->start();
        return "{}";
    }

    return send_messagesAsync(type,openid,pname,newtext,msgid,is_wakeup,mode,sendType,noref);
}

QString QQBotClient::send_messages(int type, const QString &openid,const QString &pname, QString &text,
                                    const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref)
{
    if(type!=18){
        if(type<0 || type >3 ) return R"({"msg":"发送类型错误 不在0-3之间"})";
    }

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

    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    QJsonObject keyboard;
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
        addmsglog(response,index,pname,newtext,now_us,type,openid);
        return response;
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
                                   const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref)
{

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
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    ctx.index = index;
    QJsonObject keyboard;
    QJsonArray prompt_keyboard;
    QString message_reference,mb;
    QString textB = normalizeNewlinesToCR(newtext); //处理换行
    bianl(type,index,textB,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴



    QString newtext2 = sendOneMedia(type,openid,pname,newtext,now_us,msgid,is_wakeup,mode,sendType,noref,ctx);//检查也没有要发送 的语言视频 文件 原位修改text
    if (newtext.isEmpty()) return newtext2;

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
    if(type==1 || type ==3)
    {
        if(!mbise)
        {
            textB = processImageTags(mb,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(textB);
            send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx,noref);
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
            textB = processImageTags(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(textB);//违禁词过滤
             send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
        }else {
            textB = processImageTags(textB,2,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(textB);//违禁词过滤
            QString url = get_url(type, openid, "messages");
            send_messages_pd(url,realMsgId,textA,fileinfo,message_reference,seq_index,ctx,noref);
        }
        return response;
    }

    if(!mbise) //模板 一般用不到
    {
        textB = processImageTags(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(textB);
        send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
    }
    if(!mode && m_info->markdown || mode && sendType==1)
    {
        textB = processImageTags(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(textB);
        send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
    }else{
        textB = processImageTags(textB,0,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(textB);
        send_messages(type, openid, textA,fileinfo,prompt_keyboard, message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
    }
    return response;
}
QString QQBotClient::send_messagesAsync2(int type, const QString &openid,const QString &pname, QString &text,
                                        const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref,const QString &mb2,
                                         const QJsonArray &prompt_keyboard,const QJsonObject &keyboard)
{
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
    if(type==1 || type ==3)
    {
        if(!mbise)
        {
            text = processImageTags(mb,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(text);
            send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx,noref);
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
            text = processImageTags(text,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(text);//违禁词过滤
            send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
        }else {
            text = processImageTags(text,2,fileinfo,type,openid,message_reference);//处理图片 + 回复
            QString textA = forbidden->filterText(text);//违禁词过滤
            QString url = get_url(type, openid, "messages");
            send_messages_pd(url,realMsgId,textA,fileinfo,message_reference,seq_index,ctx,noref);
        }
        return response;
    }

    if(!mbise) //模板 一般用不到
    {
        text = processImageTags(text,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(text);
        send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
    }
    if(!mode && m_info->markdown || mode && sendType==1)
    {
        text = processImageTags(text,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(text);
        send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
    }else{
        text = processImageTags(text,0,fileinfo,type,openid,message_reference);//处理图片 + 回复
        QString textA = forbidden->filterText(text);
        send_messages(type, openid, textA,fileinfo,prompt_keyboard, message_reference, realMsgId, is_wakeup,seq_index,ctx, noref);
    }
    return response;
}


QString QQBotClient::send_messages(int type, const QString &openid, const QString &text, const QString &info,
                                   const QJsonArray &prompt_keyboard, const QString &message_reference, const QString &msgid,
                                   bool is_wakeup, int seq_index, const MessageLogContext ctx,bool noref)
{
    QJsonObject json;
    if(info.isEmpty())
    {
        json["msg_type"] = 0;
    }else{
        json["msg_type"] = 7;
        json["media"] =QJsonObject{{"file_info",info}};
    }
    json["noref"] = noref;
    json["content"] = text;
    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup,seq_index);
    QString url = get_url(type, openid, "messages");
    if(ctx.openid.isEmpty()) return PostSync(url, json,QString(), 5000);
    PostAsync(url, json, "", 5000,
              [this, ctx](const QString &resp, QNetworkReply::NetworkError err) {
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
              });
    return QString();
}


QString QQBotClient::send_messages_ark(int type, const QString &openid,const QString &pname,
                                       const QJsonObject &ark, const QString &msgid,
                                       bool is_wakeup, int seq_index,const MessageLogContext ctx)
{
    QJsonArray prompt_keyboard;
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    auto [index, realMsgId] = splitWrappedMsgId(msgid);

    QJsonObject json;
    json["msg_type"] = 3;
    json["ark"] = ark;

    initjgt(json, prompt_keyboard, "", realMsgId, is_wakeup, seq_index);
    QString url = get_url(type, openid, "messages");

    if (!ctx.openid.isEmpty())
    {
        QString pnameCopy = pname;                     // 引用转为拷贝
        QString jsonString = QJsonDocument(ark).toJson(QJsonDocument::Compact);
        int indexCopy = index;
        qint64 now_us_copy = now_us;
        int typeCopy = type;
        QString openidCopy = openid;
        PostAsync(url, json, "", 5000,
                  [this, pnameCopy, jsonString, indexCopy, now_us_copy,
                   typeCopy, openidCopy]
                  (const QString &resp, QNetworkReply::NetworkError err) {
                      // 如果担心 this 被销毁，可以用 QPointer 检查（可选）
                      addmsglog(resp, indexCopy, pnameCopy, jsonString,
                                now_us_copy, typeCopy, openidCopy);
                  });
        return QString();   // 立即返回，结果通过回调处理
    }
    else
    {
        QString response = PostSync(url, json, QString(), 5000);
        QJsonDocument doc(ark);
        QString jsonString = doc.toJson(QJsonDocument::Compact);
        addmsglog(response, index, pname, jsonString, now_us, type, openid);
        return response;
    }
}

QString QQBotClient::send_messages_markdown(int type, const QString &openid,const QString &markdown,const QJsonArray prompt_keyboard,
                                            const QJsonObject keyboard,const QString &message_reference,
                                            const QString &msgid,bool is_wakeup,int seq_index,const MessageLogContext ctx,bool noref)
{
    QJsonObject json;
    json["msg_type"] = 2;
    json["markdown"] = QJsonObject{{"content", markdown}};
    json["noref"] = noref;
    if (keyboard.contains("keyboard")){
        json["keyboard"] = keyboard["keyboard"];
    }else if(keyboard.contains("content")){
        json["keyboard"] = keyboard;
    }else if(keyboard.contains("rows")){
        json["keyboard"] = QJsonObject{{"content",keyboard}};
    }

    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup,seq_index);
    QString url= get_url(type,openid,"messages");

    if(ctx.openid.isEmpty()) return PostSync(url, json,QString(), 5000);
    PostAsync(url, json, "", 5000,
              [this, ctx](const QString &resp, QNetworkReply::NetworkError err) {
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
              });
    return QString();
}
QString QQBotClient::send_messages_mb(int type, const QString &openid,const QString &markdown,const QJsonArray prompt_keyboard,
                                            const QJsonObject keyboard,const QString &message_reference,
                                            const QString &msgid,bool is_wakeup, int seq_index,const MessageLogContext ctx,bool noref)
{
    QJsonObject json;
    json["msg_type"] = 2;
    QJsonParseError err;
    QJsonDocument dom =QJsonDocument::fromJson(markdown.toUtf8(),&err);
    if(err.error !=QJsonParseError::NoError)
    {
        return QString();
    }

    json["markdown"] = dom.object();
    json["noref"] = noref;
    if (keyboard.contains("keyboard")){
        json["keyboard"] = keyboard["keyboard"];
    }else if(keyboard.contains("content")){
        json["keyboard"] = keyboard;
    }else if(keyboard.contains("rows")){
        json["keyboard"] = QJsonObject{{"content",keyboard}};
    }

    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup,seq_index);
    QString url= get_url(type,openid,"messages");
    if(ctx.openid.isEmpty()) return PostSync(url, json,QString(), 5000);
    PostAsync(url, json, "", 5000,
              [this, ctx](const QString &resp, QNetworkReply::NetworkError err) {
                  addmsglog(resp, ctx.index, ctx.pname, ctx.jsonString,
                            ctx.now_us, ctx.type, ctx.openid);
              });
    return QString();
}


QString QQBotClient::delete_messages(int type, const QString &openid, const QString &msgid,Callback callbacks)
{
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    QString url = get_url(type, openid, "messages", realMsgId);
    return Delete(url,QJsonObject(),QString(),10000,callbacks);
}
// 生成邀请链接
QString QQBotClient::generate_share_link(const QString& callback_data,Callback callbacks)
{
    QJsonObject json;
    if (!callback_data.isEmpty()) {
        QByteArray utf8Data = callback_data.toUtf8();
        if (utf8Data.size() > 32) {
            utf8Data = utf8Data.left(32);   // 截断到32字节
        }
        json["callback_data"] = QString::fromUtf8(utf8Data);
    }else{
        json["callback_data"] = m_info->appid;
    }
    return Post("https://api.bot.qq.com/v2/generate_url_link", json,QString(), 5000,callbacks);
}

//获取 群成员列表 频道成员列表
QString QQBotClient::get_members_list(const QString& group,int limit,int index,Callback callbacks)
{
    QString url= get_url(0,group,"members");
    return Get(QString("%1?limit=%2&start_index=%3").arg(url).arg(limit).arg(index),"", 10000,callbacks);
}
QString QQBotClient::get_groups_list(int limit,int index,Callback callbacks)
{
    QString url="https://api.bot.qq.com/users/@me/groups";
    return Get(QString("%1?limit=%2&start_index=%3").arg(url).arg(limit).arg(index),"", 10000,callbacks);
}
QString QQBotClient::get_users_list(int limit,int index,Callback callbacks)
{
    QString url="https://api.bot.qq.com/users/@me/users";
    return Get(QString("%1?limit=%2&start_index=%3").arg(url).arg(limit).arg(index),"", 10000,callbacks);
}
QString QQBotClient::get_groups_members(const QString& group,const QString &user,Callback callbacks)
{
    return Get(get_url(0,group,"members",user),QString(), 10000,callbacks);
}

//回应回调
QString QQBotClient::respond_interaction(const QString &interaction_id, int code, const QString &data)
{
    QString url = "https://api.bot.qq.com/interactions/" + interaction_id;

    QJsonObject json;
    json["code"] = code;
    if (!data.isEmpty()) {
        json["data"] = data;
    }
    QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    try {
       return put(url,body,QString(),5000);
    } catch (const std::exception &e) {
        return e.what();  // 失败返回空字符串
    }

}
QString QQBotClient::get_groups_info(const QString& group,Callback callbacks)
{
    return Get(get_url(0,group,"info"),QString(), 10000,callbacks);
}
QString QQBotClient::get_groups_bot_state(const QString& group,Callback callbacks)
{
    return Get(get_url(0,group,"bot_state"),QString(), 10000,callbacks);
}
QString QQBotClient::del_members (int type,const QString& group,const QString &user,bool add_blacklist,int delete_history_msg_days,Callback callbacks)
{

    QString url = get_url(type,group,"members",user);
    if(add_blacklist || delete_history_msg_days!=0){
        QJsonObject obj;
        obj["add_blacklist"] = add_blacklist;
        obj["delete_history_msg_days"]=delete_history_msg_days;
        return Delete(url,obj,QString(),10000,callbacks);
    }
    return Delete(url,QJsonObject(),QString(),10000,callbacks);

}

QString QQBotClient::approveGroupJoinRequest(const QString& group,const QString& user, bool op,const QString& joinRequestId,
                                             const QString& rejectReason,bool addToBlacklist,Callback callbacks)
{
    // 1. 构造 URL（替换路径参数）
    if(joinRequestId.isEmpty())
    {
        QString result=R"({"message":"joinRequestId 为空"})";
        if(callbacks)
         callbacks(result,QNetworkReply::NetworkError());
        return result;
    }
    QString url =get_url(0,group,"approval_join_request",user);

    // 2. 构建请求体 JSON
    QJsonObject requestBody;
    requestBody["op"] = op? "approve" : "decline";

    // 可选字段：只在有值时添加
    if (!joinRequestId.isEmpty()) {
        requestBody["join_request_id"] = joinRequestId;
    }
    if(!op){
        if (!rejectReason.isEmpty()) {
            requestBody["reject_reason"] = rejectReason;
        }
        requestBody["add_to_member_blacklist"] = addToBlacklist;
    }

    return Post(url, requestBody, QString(), 10000,callbacks);
}
// 在您的 Client 类中新增重载

QString QQBotClient::setGroupRestrictChatSetting(const QString& groupOpenId,const QString& memberOpenId,
                                                 int muteSeconds,Callback callbacks)
{
    // 1. 构造 URL
    QString url =get_url(0,groupOpenId,"restrict_chat_setting");;
    if(muteSeconds<0)
        muteSeconds=30;
    if(muteSeconds>=30*1440*60)
    {
        muteSeconds=30*1440*60-1;
    }
    QJsonObject memberObj;

    memberObj["member_openid"] = memberOpenId;

    if(muteSeconds!=0)
    {
        memberObj["op"] = "add";
        QDateTime expireTime = QDateTime::currentDateTime().addSecs(muteSeconds);
        QString expireStr = expireTime.toString(Qt::ISODate);
        int offsetSecs = expireTime.offsetFromUtc();
        int offsetHours = offsetSecs / 3600;
        int offsetMinutes = qAbs(offsetSecs % 3600) / 60;
        QString timezoneStr = (offsetSecs >= 0) ?
                                  QString("+%1:%2").arg(offsetHours, 2, 10, QChar('0')).arg(offsetMinutes, 2, 10, QChar('0')) :
                                  QString("-%1:%2").arg(-offsetHours, 2, 10, QChar('0')).arg(offsetMinutes, 2, 10, QChar('0'));
        QString rfc3339 = expireStr + timezoneStr;
        memberObj["mute_expire_at"] = rfc3339;
    }else{
        memberObj["op"] = "del";
    }

    QJsonArray membersArray;
    membersArray.append(memberObj);
    QJsonObject requestBody;
    requestBody["members"] = membersArray;


    return Post(url, requestBody, QString(), 10000,callbacks);
}
//设置禁言
QString QQBotClient::setGroupRestrictChatSetting(const QString& group, const QJsonArray& membersJson,Callback callbacks)
{
    QString url = get_url(0,group,"restrict_chat_setting");
    QJsonObject requestBody;
    requestBody["members"] = membersJson;
    return Post (url, requestBody, QString(), 10000,callbacks);
}

//获取加群列表
QString QQBotClient::getjoin_request_list(const QString& group,int limit,const QString &cursor,Callback callbacks)
{
    return Get(get_url(0,group,"join_request_list"), QString(), 10000,callbacks);
}

//获取禁言列表
QString QQBotClient::getGroupRestrictChatSetting(const QString& group,Callback callbacks)
{
    return Get(get_url(0,group,"restrict_chat_setting"), QString(), 10000,callbacks);
}

//设置禁言——频道
QString QQBotClient::set_mute(const QString& group,const QString &user,qint64 mute_seconds)
{

    QString url = QString("https://api.bot.qq.com/guilds/%1/mute").arg(group);
    QJsonObject obj;
    if(mute_seconds>31104000)//判定为时间戳
        obj["mute_end_timestamp"]=mute_seconds;
    else
        obj["mute_seconds"] = mute_seconds;
    if(!user.isEmpty()){
        QStringList list = user.split(",");
        obj["user_ids"] = QJsonArray::fromStringList(list);
    }

    return PatchSync(url,obj,QString(),10000) ;
}


// ==================== 自定义菜单接口 ====================

// 1. 查询菜单 (GET)
QString QQBotClient::getMenu(Callback callbacks)
{
    return Get("https://api.bot.qq.com/v2/menu", QString(), 10000, callbacks);
}

// 2. 创建/更新菜单 (POST)
QString QQBotClient::updateMenu(const QJsonObject& menuData, Callback callbacks)
{

    return put2("https://api.bot.qq.com/v2/menu", QJsonDocument(menuData).toJson(QJsonDocument::Compact), QString(), 10000, callbacks);
}

// ==================== 指令面板接口 ====================

// 4. 创建面板 (POST)
QString QQBotClient::createPanel(const QJsonObject& panelData, Callback callbacks)
{
    //qDebug() << panelData;
    return Post("https://api.bot.qq.com/v2/panels", panelData, QString(), 10000, callbacks);
}

// 5. 查询面板列表 (GET)
QString QQBotClient::listPanels(const QString& scope, int limit, const QString& cursor, Callback callbacks)
{
    QString url = "https://api.bot.qq.com/v2/panels?scope=" + scope;
    if (limit > 0) url += "&limit=" + QString::number(limit);
    if (!cursor.isEmpty()) url += "&cursor=" + cursor;
    return Get(url, QString(), 10000, callbacks);
}

// 6. 查询面板详情 (GET)
QString QQBotClient::getPanel(const QString& panelId, Callback callbacks)
{
    return Get("https://api.bot.qq.com/v2/panels/" + panelId, QString(), 10000, callbacks);
}

// 7. 修改面板 (PATCH)
QString QQBotClient::updatePanel(const QString& panelId, const QJsonObject& panelData, Callback callbacks)
{
    //qDebug() << panelData;
    return put2("https://api.bot.qq.com/v2/panels/" + panelId, QJsonDocument(panelData).toJson(QJsonDocument::Compact), QString(), 10000, callbacks);
}

// 8. 删除面板 (DELETE)
QString QQBotClient::deletePanel(const QString& panelId, Callback callbacks)
{
    return Delete("https://api.bot.qq.com/v2/panels/" + panelId,QJsonObject() ,QString(), 10000, callbacks);
}

// 9. 修改面板关联对象 (PATCH)
QString QQBotClient::updatePanelTarget(const QString& panelId, const QJsonObject& targetData, Callback callbacks)
{
    return put2("https://api.bot.qq.com/v2/panels/" + panelId + "/target", QJsonDocument(targetData).toJson(QJsonDocument::Compact), QString(), 10000, callbacks);
}











