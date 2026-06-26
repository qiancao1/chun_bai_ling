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


#include "qqbotclient.h"
#include <QRandomGenerator>
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



/**
 * @brief 将 Markdown 链接 [text](url) 按规则转换：
 *        如果 url 以 http:// 或 https:// 开头，保持原样；
 *        否则只保留方括号内的 text。
 * @param input 原始字符串（可含多个 [](url)）
 * @return 转换后的字符串
 */
QString convertMdLinksKeepHttp(const QString &input)
{
    // 正则匹配 [任意字符(非']')](任意字符(非')'))
    QRegularExpression re(R"((?<!!)\[([^\]]*?)\]\(([^\)]*?)\))");
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
            QString encodedShow = QString::fromUtf8(QUrl::toPercentEncoding(showText));
            if(encodedUrl.isEmpty())
                encodedUrl=encodedShow;
            QString xmlTag = QString("<qqbot-cmd-input text=\"%1\" show=\"%2\" reference=\"false\" />")
                                 .arg(encodedUrl, encodedShow);
            output.append(xmlTag);
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
QByteArray convertMp3ToSilk(const QByteArray &mp3Data);
QString convertAudioToSilk(const QString &srcFilePath);
QString calculateFileMD5(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开文件:" << filePath;
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    const qint64 bufferSize = 8192; // 8 KB 缓冲区
    QByteArray buffer;
    buffer.resize(bufferSize);

    while (!file.atEnd()) {
        qint64 bytesRead = file.read(buffer.data(), bufferSize);
        if (bytesRead <= 0) {

            return QString();
        }
        hash.addData(buffer.constData(), bytesRead);

    }

    file.close();
    QByteArray result = hash.result();
    return QString::fromLatin1(result.toHex());
}


QString python_http(const QString qurl, QString method, QString headersJsonStr, QString bodyBase64, int timeoutSec)
{
    // 1. 解码请求体（Base64 -> 原始字节）
    QByteArray bodyData = QByteArray::fromBase64(bodyBase64.toUtf8());

    // 2. 创建请求对象并设置 URL
    QNetworkRequest request;
    request.setUrl(QUrl(qurl));

    // 3. 解析并设置自定义请求头
    if (!headersJsonStr.isEmpty()) {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(headersJsonStr.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                if (it.value().isString()) {
                    request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
                }
            }
        }
    }

    // 4. 创建网络管理器（局部变量，在同步阻塞模式下安全）
    QNetworkAccessManager manager;
    QNetworkReply *reply = nullptr;

    // 5. 根据 HTTP 方法执行不同请求
    QString upperMethod = method.toUpper();
    if (upperMethod == "GET") {
        reply = manager.get(request);
    } else if (upperMethod == "POST") {
        reply = manager.post(request, bodyData);
    } else if (upperMethod == "PUT") {
        reply = manager.put(request, bodyData);
    } else if (upperMethod == "DELETE") {
        reply = manager.deleteResource(request);
    } else if (upperMethod == "HEAD") {
        reply = manager.head(request);
    } else {
        // 未知方法，返回错误
        QJsonObject jsonResult;
        jsonResult["success"] = false;
        jsonResult["status_code"] = 0;
        jsonResult["content"] = "";
        jsonResult["error"] = "Unsupported HTTP method: " + method;
        QJsonDocument doc(jsonResult);
        return doc.toJson(QJsonDocument::Compact);
    }

    // 6. 同步等待：事件循环 + 超时定时器
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutSec * 1000);  // 毫秒
    loop.exec();  // 阻塞直到请求完成或超时

    // 7. 收集结果
    bool success = false;
    int statusCode = 0;
    QByteArray responseBody;
    QString errorMsg;

    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        success = true;
        // 获取 HTTP 状态码
        statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        responseBody = reply->readAll();
    } else {
        // 请求失败（网络错误或超时）
        success = false;
        if (!timer.isActive()) {
            errorMsg = "Request timeout";
        } else {
            errorMsg = reply->errorString();
        }
        // 尝试获取可能的部分响应头状态码
        statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    }

    reply->deleteLater();

    // 8. 构建返回 JSON
    QJsonObject jsonResult;
    jsonResult["success"] = success;
    jsonResult["status_code"] = statusCode;
    jsonResult["content"] = QString::fromUtf8(responseBody.toBase64());
    jsonResult["error"] = errorMsg;
    QJsonDocument doc(jsonResult);
    return doc.toJson(QJsonDocument::Compact);
}
// 模拟处理沙盒内的插件调用（返回 JSON 字符串）
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
        QString qurl = toQString(_1);
        if(qurl.isEmpty()) break;

        QString method = toQString(_2).toUpper();
        QString headersJsonStr = toQString(_3);
        QString bodyBase64 = toQString(_4);
        int timeoutSec = 30;
        if (_5 != nullptr && strlen(_5) > 0) {
            timeoutSec = toInt(_5);
        }
        result = python_http(qurl,method,headersJsonStr,bodyBase64,timeoutSec).toStdString();
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
    static std::string result="{}";

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
            pname = "["+m_pluginList[i].name+"|%1ms]";
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
        QString ret = client->send_messages(type, openid,pname, text,msgid, is_wakeup);
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
        QString qurl = toQString(_1);
        if(qurl.isEmpty()) break;

        QString method = toQString(_2).toUpper();
        QString headersJsonStr = toQString(_3);
        QString bodyBase64 = toQString(_4);
        int timeoutSec = 30;
        if (_5 != nullptr && strlen(_5) > 0) {
            timeoutSec = toInt(_5);
        }
        result = python_http(qurl,method,headersJsonStr,bodyBase64,timeoutSec).toStdString();
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
        uint32_t id=db->getOrUpdateUser(toQString(_1),name);
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

    default:
        result = R"({"error":"Unknown apiId"})";
        break;
    }

    return result.c_str();
}


void QQBotClient::addmsglog(QString &response,int index,QString &pname,const QString &text,qint64 now_us,int type,QString &msgid,const QString &openid)
{

    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    QJsonObject obj = doc.object();
    QString deleteid = obj["id"].toString();
    QJsonObject obj2 =obj["ext_info"].toObject();
    QString ref = obj2["ref_idx"].toString();
    QString message = obj["message"].toString();
    int tabIndex = mapTypeToTabIndex(type);
    m_info->message_sent++;
    m_info->sent++;
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
        return ;
    }

    msg.isSelf=true;
    msg.ch = deleteid;
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

    g_logdb[tabIndex]->appendLog(m_info->appid,openid,msg);
    logPage->onNewLogAdded(tabIndex,0,m_info->appid_int,openid,msg);

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


QString sendPutRequest(const QString& url, const QByteArray& data, int timeoutMs)
{
    // 1. 构造请求
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    // 2. 创建管理器（局部变量，在同步等待下安全）
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.put(request, data);

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    QByteArray responseBody;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        responseBody = reply->readAll();
    } else {

        responseBody.clear();
    }

    reply->deleteLater();
    return QString::fromUtf8(responseBody);
}

QString get_url(int type,const QString &openid,const QString &text = QString(),const QString &text2 = QString())
{
    QString url;
    if(type==0) url = "https://api.sgroup.qq.com/v2/groups/" + openid;
    else if(type==1) url = "https://api.sgroup.qq.com/channels/" + openid;
    else if(type==2) url = "https://api.sgroup.qq.com/v2/users/" + openid;
    else url = "https://api.sgroup.qq.com/dms/" + openid;
    if(!text.isEmpty()) url +="/" + text;
    if(!text2.isEmpty()) url +="/" + text2;
    return url;
}


QString uploadImageToCdn(const QString &path);

    /**
 * @brief 从参数文本（例如 "url=xxx, x=100, y=200"）中提取指定键的值。
 * @param params  参数文本视图（不含外层括号）
 * @param key     要查找的键，如 "url"
 * @param value   输出参数，存储找到的值（若找到）
 * @return true 如果找到该键
 *
 * 解析规则：key=value，key 前后允许空格，value 直到下一个逗号或结尾，
 * value 前后的空格会被去除。
 */
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

    /**
 * @brief 解析一个 [image ...] 标签，提取其中的 url/path 以及 x, y。
 * @param tagText 标签内容视图（例如 "image, url=..., x=100"）
 * @return 包含 urlOrPath, x, y 的结构体
 */
struct ImageInfo {
    QString urlOrPath;
    float x = 0.0f;
    float y = 0.0f;
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
    if (extractParamValue(params, QStringLiteral("x"), xStr)) info.x = xStr.toFloat();
    if (extractParamValue(params, QStringLiteral("y"), yStr)) info.y = yStr.toFloat();

    return info;
}

    /**
 * @brief 主处理函数：移除或替换所有 [image ...] 标签（以及处理 [ref,...] 标签）
 * @param text      输入输出文本（会被修改）
 * @param type      0：仅上传第一个图片的信息，并删除所有图片标签；1：替换为 Markdown 图片
 * @param info      输出参数，用于返回第一个图片的上传结果（type==0 时）
 * @param targetType, openid, message_reference 等外部参数（保持与原接口一致）
 * @return 处理后的文本（同 text 引用）
 *
 * 注意：此函数不使用任何正则表达式，完全基于 QString::indexOf 和手动解析，
 * 并采用构建新字符串的方式减少内存分配。
 */
QString QQBotClient::processImageTags(QString &text, int type, QString &info,
                                          int targetType, const QString &openid,
                                          QString &message_reference) {
    struct TagPos {
        int start;      // 标签起始索引
        int length;     // 标签总长度
        ImageInfo img;  // 解析出的图片信息
    };
    QList<TagPos> tags;   // 存储所有标签位置（数量通常很少，QList 足够）
    int searchFrom = 0;

    while (true) {
        int imgStart = text.indexOf(QLatin1String("[image"), searchFrom, Qt::CaseInsensitive);
        if (imgStart == -1) break;

        // 找到匹配的右方括号（简单假设标签内无嵌套括号，仅跳过转义？一般无嵌套）
        int imgEnd = imgStart + 1;
        int bracketDepth = 1;
        while (imgEnd < text.size() && bracketDepth > 0) {
            if (text[imgEnd] == '[') bracketDepth++;
            else if (text[imgEnd] == ']') bracketDepth--;
            ++imgEnd;
        }
        if (bracketDepth != 0) break; // 没有闭合，停止搜索（异常情况）
        int tagLen = imgEnd - imgStart;
        int contentStart = imgStart + 6; // "[image" 长度
        while (contentStart < imgEnd - 1 && (text[contentStart].isSpace() || text[contentStart] == ','))
            ++contentStart;
        int contentLen = tagLen - (contentStart - imgStart) - 1; // 减1去掉最后的']'
        if (contentLen < 0) contentLen = 0;
        QStringView tagContentView = QStringView(text).mid(contentStart, contentLen);

        TagPos tag;
        tag.start = imgStart;
        tag.length = tagLen;
        tag.img = parseImageTagContent(tagContentView);
        tags.append(tag);

        searchFrom = imgEnd; // 继续往后找
    }

    if (tags.isEmpty()) {
        if(type==0)
            text = convertMdLinksKeepHttp(text);
        else
            text = convertMarkdownLinksToXml(text);

        return text;   // 无图片标签，直接返回
    }

    if (type == 0) {
        const ImageInfo &firstImg = tags.first().img;

        QString fileMd5;
        if (!firstImg.urlOrPath.isEmpty()) {

            if(!firstImg.urlOrPath.startsWith("http"))
            {
                fileMd5=calculateFileMD5(firstImg.urlOrPath);
            }else{
                QCryptographicHash hash(QCryptographicHash::Md5);
                hash.addData(firstImg.urlOrPath.toUtf8());
                QByteArray md5Binary = hash.result();
                fileMd5 = QString::fromLatin1(md5Binary.toHex());
            }
            QString fileInfo;
            if (cache_db && !fileMd5.isEmpty()) {
                QString cacheKey = QString("imageA_%1").arg(fileMd5);
                QString cached = cache_db->get(cacheKey);
                if (!cached.isEmpty()) {
                    int timeIdx = cached.lastIndexOf(",time=");
                    if (timeIdx != -1) {
                        qint64 expire = cached.mid(timeIdx + 6).toLongLong();
                        if (QDateTime::currentSecsSinceEpoch() < expire) {
                            fileInfo = cached.left(timeIdx);
                        }
                    } else {
                        fileInfo = cached;
                    }
                }
            }
            if(fileInfo.isEmpty())
            {
                bool ok = false;
                qint64 expireTime = 0;
                QString md5;
                fileInfo = uploadRichMediaA(targetType, openid, 1, firstImg.urlOrPath,ok);
                if (ok)
                     cache_db->put(QString("imageA_%1").arg(fileMd5), fileInfo);
                else
                    fileInfo.clear();

            }
            if(!fileInfo.isEmpty())
            {
                info = extractBetween(fileInfo,"path=",",");
            }


        }
        for (int i = tags.size() - 1; i >= 0; --i) {
            text.remove(tags[i].start, tags[i].length);
        }
        text = convertMdLinksKeepHttp(text);
    }
    else if (type == 1) { //md语法处理
        // 构建新字符串，一次性分配足够内存
        QString result;
        result.reserve(text.size());
        int lastPos = 0;
        for (const TagPos &tag : std::as_const(tags)) {
            result.append(QStringView(text).mid(lastPos, tag.start - lastPos));
            QString url = tag.img.urlOrPath;
            if (url.isEmpty()) {

            } else {
                if (!url.startsWith(QLatin1String("http"))) { //只检查本地图片的 缓存
                    QString fileMd5=calculateFileMD5(url);
                    QString fileInfo;
                    if (cache_db && !fileMd5.isEmpty()) {
                        QString cacheKey = QString("imageB_%1").arg(fileMd5);
                        QString cached = cache_db->get(cacheKey);
                        if (!cached.isEmpty()) {
                            int timeIdx = cached.lastIndexOf(",Time=");
                            if (timeIdx != -1) {
                                qint64 expire = cached.mid(timeIdx + 6).toLongLong();
                                if (QDateTime::currentSecsSinceEpoch() < expire) {
                                    fileInfo = cached.left(timeIdx);
                                }
                            } else {
                                fileInfo = cached;
                            }
                        }
                    }
                    if(fileInfo.isEmpty())
                    {
                        QString uploadedUrl = uploadImageToCdn(url);
                        if (!uploadedUrl.isEmpty()) url = uploadedUrl;
                        if(!url.isEmpty())
                        {
                            fileInfo =  QString("%1||||%2").arg(QDateTime::currentSecsSinceEpoch()+1440*60).arg(url);
                            cache_db->put(QString("imageB_%1").arg(fileMd5), fileInfo);
                        }

                    }else if(!fileInfo.isEmpty())
                    {
                        url = extractBetween(fileInfo,"path=",",");
                    }
                }
                QString markdownImg;
                if (tag.img.x > 0 && tag.img.y > 0) {
                    markdownImg = QStringLiteral("![#%1px #%2px](%3)")
                    .arg(tag.img.x).arg(tag.img.y).arg(url);
                } else {
                    markdownImg = QStringLiteral("![#500px #0px](%1)").arg(url);
                }
                result.append(markdownImg);
            }
            lastPos = tag.start + tag.length;
        }
        result.append(QStringView(text).mid(lastPos));
        text = std::move(result);
        text = convertMarkdownLinksToXml(text);
    }
    return text;
}


QString QQBotClient::processImageTags2(QString &text, int type, QString &info,
                                      int targetType, const QString &openid,
                                      QString &message_reference)
{
    // ---------- 1. 处理 [ref,...] 标签（只处理第一个） ----------
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

    // ---------- 2. 定义统一的图片标签结构 ----------
    struct ImgTag {
        int start;      // 起始位置
        int length;     // 原始长度
        QString url;    // 图片路径或 URL
        int width;      // 宽度（像素）
        int height;     // 高度（像素）
    };
    QList<ImgTag> allTags;

    // ---------- 3. 解析 [image,...] 标签 ----------
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
            tag.width = imgInfo.x;   // 若有 x,y 则使用，否则为 0
            tag.height = imgInfo.y;
            allTags.append(tag);
        }
        searchFrom = imgEnd;
    }

    // ---------- 4. 解析 Markdown 图片标签 ![]() ----------
    QRegularExpression mdImgRe(R"(!\[([^\]]*)\]\(([^)]*)\))");
    QRegularExpressionMatchIterator it = mdImgRe.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString alt = match.captured(1).trimmed();
        QString url = match.captured(2).trimmed();
        if (url.isEmpty()) continue;

        // 从 alt 中提取尺寸 #数字px
        int width = 0, height = 0;
        QRegularExpression sizeRe(R"(#(\d+)px)");
        QRegularExpressionMatchIterator sizeIt = sizeRe.globalMatch(alt);
        if (sizeIt.hasNext()) {
            width = sizeIt.next().captured(1).toInt();
            if (sizeIt.hasNext()) {
                height = sizeIt.next().captured(1).toInt();
            }
        }

        ImgTag tag;
        tag.start = match.capturedStart();
        tag.length = match.capturedLength();
        tag.url = url;
        tag.width = width;
        tag.height = height;
        allTags.append(tag);
    }

    // ---------- 5. 若没有任何图片标签，处理其他 Markdown 链接后返回 ----------
    if (allTags.isEmpty()) {
        if (type == 0)
            text = convertMdLinksKeepHttp(text);
        else
            text = convertMarkdownLinksToXml(text);
        return text;
    }

    // ---------- 按起始位置降序排序（从后往前替换） ----------
    std::sort(allTags.begin(), allTags.end(),
              [](const ImgTag &a, const ImgTag &b) { return a.start > b.start; });

    bool firstProcessed = false;  // 用于 type==0 只处理第一个标签

    for (int idx = 0; idx < allTags.size(); ++idx) {
        const ImgTag &tag = allTags[idx];
        QString newUrl = tag.url;
        bool isHttp = newUrl.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
                      newUrl.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);

        // 仅对本地非 HTTP 路径执行上传（HTTP 直接使用原链接）
        if (!isHttp && !newUrl.isEmpty()) {
            QString fileMd5 = calculateFileMD5(newUrl);   // 计算文件 MD5

            if (type == 0) {
                // ========== 类型 0：富媒体上传，只处理第一个标签 ==========
                if (!firstProcessed) {
                    firstProcessed = true;

                    QString cacheKey = "imageA_" + fileMd5;
                    bool cacheValid = false;
                    QString cachedUrl;

                    // 检查缓存（新格式：timestamp||||url）
                    if (cache_db && !fileMd5.isEmpty()) {
                        QString cached = cache_db->get(cacheKey);
                        if (!cached.isEmpty()) {
                            int sepIdx = cached.lastIndexOf("||||");
                            if (sepIdx != -1) {
                                qint64 expireTime = cached.left(sepIdx).toLongLong();
                                cachedUrl = cached.mid(sepIdx + 4);
                                if (QDateTime::currentSecsSinceEpoch() < expireTime) {
                                    cacheValid = true;
                                }
                            }
                        }
                    }

                    if (cacheValid) {
                        newUrl = cachedUrl;
                    } else {
                        // 上传富媒体
                        bool ok = false;
                        QString fileInfo = uploadRichMediaA(targetType, openid, 1, newUrl, ok);
                        if (ok) {
                            QString path = extractBetween(fileInfo, "path=", ",");
                            if (!path.isEmpty()) {
                                newUrl = path;
                                // 存入缓存
                                qint64 expire = QDateTime::currentSecsSinceEpoch() + 1440 * 60; // 24小时
                                cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(newUrl));
                            } else {
                                newUrl = tag.url; // 提取失败，回退
                            }
                        } else {
                            newUrl = tag.url; // 上传失败，保留原路径
                        }
                    }

                    // 将 info 赋值为最终路径（仅第一个）
                    info = newUrl;
                }

                // 类型 0：所有图片标签均被删除（替换为空字符串）
                text.replace(tag.start, tag.length, QString());
            }
            else if (type == 1) {
                // ========== 类型 1：图床上传，每个标签单独处理 ==========
                QString cacheKey = "imageB_" + fileMd5;
                bool cacheValid = false;
                QString cachedUrl;

                // 检查缓存
                if (cache_db && !fileMd5.isEmpty()) {
                    QString cached = cache_db->get(cacheKey);
                    if (!cached.isEmpty()) {
                        int sepIdx = cached.lastIndexOf("||||");
                        if (sepIdx != -1) {
                            qint64 expireTime = cached.left(sepIdx).toLongLong();
                            cachedUrl = cached.mid(sepIdx + 4);
                            if (QDateTime::currentSecsSinceEpoch() < expireTime) {
                                cacheValid = true;
                            }
                        }
                    }
                }

                if (cacheValid) {
                    newUrl = cachedUrl;
                } else {
                    // 上传图床
                    QString uploadedUrl = uploadImageToCdn(newUrl);
                    if (!uploadedUrl.isEmpty()) {
                        newUrl = uploadedUrl;
                        // 存入缓存
                        qint64 expire = QDateTime::currentSecsSinceEpoch() + 1440 * 60;
                        cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(newUrl));
                    } else {
                        newUrl = tag.url; // 上传失败，保留原路径
                    }
                }

                // 重组为 Markdown 图片格式
                int w = (tag.width > 0) ? tag.width : 1000;
                int h = tag.height;
                QString markdownImg;
                if (h > 0) {
                    markdownImg = QStringLiteral("![#%1px #%2px](%3)").arg(w).arg(h).arg(newUrl);
                } else {
                    markdownImg = QStringLiteral("![#%1px #0px](%2)").arg(w).arg(newUrl);
                }
                text.replace(tag.start, tag.length, markdownImg);
            }
        }
        else {
            if (type == 0) {
                text.replace(tag.start, tag.length, QString());
            } else if (type == 1) {
                int w = (tag.width > 0) ? tag.width : 1000;
                int h = tag.height;
                QString markdownImg;
                if (h > 0) {
                    markdownImg = QStringLiteral("![#%1px #%2px](%3)").arg(w).arg(h).arg(newUrl);
                } else {
                    markdownImg = QStringLiteral("![#%1px #0px](%2)").arg(w).arg(newUrl);
                }
                text.replace(tag.start, tag.length, markdownImg);
            }
        }
    }
    if (type == 0)
        text = convertMdLinksKeepHttp(text);
    else
        text = convertMarkdownLinksToXml(text);

    return text;
}
QString QQBotClient::uploadRichMediaA(int targetType, const QString& openid,int fileType, const QString& filePath, bool &ok)
{
    qint64 expireTime=0;
    QString md5,info;
    if(filePath.startsWith("http"))
    {
        info = uploadRichMedia_url(targetType,openid,fileType,filePath,expireTime,ok);
    }else{
        info = uploadRichMedia(targetType,openid,fileType,filePath,expireTime,md5,ok);
    }
    if(!ok) return info;
    QString typeStr;
    switch (fileType) {
    case 1: typeStr = "pic"; break;
    case 2: typeStr = "audio"; break;
    case 3: typeStr = "video"; break;
    case 4: typeStr = "file"; break;
    default: typeStr = "unknown";
    }
    return QString("[%1,path=%2,md5=%3,Time=%4]").arg(typeStr,info,md5).arg(expireTime);
}
QString QQBotClient::uploadRichMediaB(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename, bool &ok)
{
    qint64 expireTime=0;
    QString md5;
    QString info = uploadRichMedia(targetType,openid,fileType,data,filename,expireTime,md5,ok);
    if(!ok) return info;
    QString typeStr;
    switch (fileType) {
    case 1: typeStr = "pic"; break;
    case 2: typeStr = "audio"; break;
    case 3: typeStr = "video"; break;
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
    for(int i=0;i<6;i++)
    {
        response =_Post(url,obj,300000);
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
                                     qint64& expireTime,QString &md5,bool &ok) {
    // 1. 读取文件
    ok=false;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "无法打开文件:" << filePath;
        return QString();
    }
    QByteArray fileData = file.readAll();
    file.close();
    qint64 fileSize = fileData.size();
    QCryptographicHash md5Hash(QCryptographicHash::Md5);
    md5Hash.addData(fileData);
    md5 = md5Hash.result().toHex();
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(fileData);
    QString sha1 = sha1Hash.result().toHex();
    int tenM = 10 * 1024 * 1024;
    QByteArray first10M = fileData.left(tenM);
    QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
    md5_10mHash.addData(first10M);
    QString md5_10m = md5_10mHash.result().toHex();
    QString fileName = QFileInfo(filePath).fileName();
    QJsonObject prepJson;
    prepJson["file_type"] = fileType;
    prepJson["file_name"] = fileName;
    prepJson["file_size"] = (qint64)fileSize;
    prepJson["md5"] = md5;
    prepJson["sha1"] = sha1;
    prepJson["md5_10m"] = md5_10m;
    QString url = get_url(targetType, openid, "upload_prepare");
    QString response = _Post(url, prepJson, 30000);
    if (response.isEmpty()) return QString();
    QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
    if (respDoc.isNull()) return QString();
    QJsonObject respObj = respDoc.object();
    QString upload_id = respObj["upload_id"].toString();
    if (upload_id.isEmpty()) return response; // 错误信息
    QJsonArray parts = respObj["parts"].toArray();
    QJsonObject partFinishBase;
    partFinishBase["upload_id"] = upload_id;
    for (int i = 0; i < parts.size(); ++i) {
        QJsonObject part = parts[i].toObject();
        int index = part["index"].toInt();
        QString blockSize = part["block_size"].toString();
        int blockSizeA=blockSize.toInt();
        QString presignedUrl = part["presigned_url"].toString();
        int start = (index - 1) * blockSizeA;
        QByteArray chunk = fileData.mid(start, blockSizeA);
        QString putResp = sendPutRequest(presignedUrl, chunk, 30000);
        QJsonObject finishJson;
        finishJson["upload_id"] = upload_id;
        finishJson["part_index"] = index;
        finishJson["block_size"] = chunk.size();
        QCryptographicHash chunkMd5(QCryptographicHash::Md5);
        chunkMd5.addData(chunk);
        finishJson["md5"] = QString(chunkMd5.result().toHex());
        QString finishUrl = get_url(targetType, openid, "upload_part_finish");
        QString finishResp = _Post(finishUrl, finishJson, 30000);
    }
    QString filesUrl = get_url(targetType, openid, "files");
    QString filesResp = _Post(filesUrl, respObj, 30000);

    if (filesResp.isEmpty()) return QString();
    QJsonDocument filesRespDoc = QJsonDocument::fromJson(filesResp.toUtf8());
    if (filesRespDoc.isNull()) return filesResp;
    QJsonObject filesObj = filesRespDoc.object();
    QString file_info = filesObj["file_info"].toString();
    if (file_info.isEmpty()) return filesResp; // 错误信息
    expireTime = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
    ok=true;
    return file_info;
}

QString QQBotClient::uploadRichMedia(int targetType, const QString& openid,int fileType, const QByteArray& data,const QString &filename,
                                    qint64& expireTime,QString &md5,bool &ok) {


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

    QString url = get_url(targetType, openid, "upload_prepare");
    QString response = _Post(url, prepJson, 30000);
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

    // 6. 循环上传每个分片
    for (int i = 0; i < parts.size(); ++i) {
        QJsonObject part = parts[i].toObject();
        int index = part["index"].toInt();
        int blockSize = part["block_size"].toInt();
        QString presignedUrl = part["presigned_url"].toString();

        // 取出分片数据
        int start = (index - 1) * blockSize;
        QByteArray chunk = data.mid(start, blockSize);

        // PUT 上传分片
        QString putResp = sendPutRequest(presignedUrl, chunk, 30000);
        // 忽略响应，一般只要成功即可

        // 报告分片完成
        QJsonObject finishJson;
        finishJson["upload_id"] = upload_id;
        finishJson["part_index"] = index;
        finishJson["block_size"] = chunk.size();

        QCryptographicHash chunkMd5(QCryptographicHash::Md5);
        chunkMd5.addData(chunk);
        finishJson["md5"] = QString(chunkMd5.result().toHex());
        QString finishUrl = get_url(targetType, openid, "upload_part_finish");
        QString finishResp = _Post(finishUrl, finishJson, 30000);
    }

    // 7. 完成上传，请求 /files
    QJsonObject filesJson;
    filesJson["upload_id"] = upload_id;

    QString filesUrl = get_url(targetType, openid, "files");
    QString filesResp = _Post(filesUrl, filesJson, 30000);
    if (filesResp.isEmpty()) return QString();

    QJsonDocument filesRespDoc = QJsonDocument::fromJson(filesResp.toUtf8());
    if (filesRespDoc.isNull()) return QString();
    QJsonObject filesObj = filesRespDoc.object();
    QString file_info = filesObj["file_info"].toString();
    if (file_info.isEmpty()) return filesResp; // 错误信息

    // 获取过期时间（秒为单位）
    expireTime = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
    ok=true;
    return file_info;
}



QString QQBotClient::sendOneMedia(int type, const QString &openid,QString &pname,QString &text,qint64 now_us, const QString &msgid,bool is_wakeup)
{
    // 匹配短标签或全名标签：f/file, a/audio, v/video, flie(笔误)
    QRegularExpression re(R"(\[(f(?:ile)?|a(?:udio)?|v(?:ideo)?|flie)\s*,\s*([^\]]+)\])",
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
        QRegularExpression pathRe(R"(path\s*=\s*([^,\]]+))");
        QRegularExpression urlRe(R"(url\s*=\s*([^,\]]+))");

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
            fileMd5=calculateFileMD5(filePath);
            //媒体类型：1 图片，2 视频，3 语音，4 文件
            if (cache_db && !fileMd5.isEmpty()) {
                QString cacheKey = QString("media_%1").arg(fileMd5);
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

            fileInfo = uploadRichMediaA(type, openid, fileType, filePath,ok);

            if(!ok)
            {
                send_messages(type,openid,pname,fileInfo,msgid,is_wakeup);
            }else if (!fileInfo.isEmpty() && cache_db && !fileMd5.isEmpty()) { //发的链接是没有md5的
                cache_db->put(QString("media_%1").arg(fileMd5), fileInfo);
            }
        }

        if (ok && !fileInfo.isEmpty()) {
            response = send_Media(type, openid,pname, fileInfo,now_us, msgid,is_wakeup); // 增加 fileType 参数
        }
        text.remove(info.start, info.length);
    }

    return response;
}

QString QQBotClient::send_Media(int type,const QString &openid,QString &pname,const QString &info,qint64 now_us,const QString &msgid,bool is_wakeup)
{
    QJsonObject json;
    json["msg_type"] = 7;
    if (info.isEmpty()) return R"({"msg":"要发送的富媒体标签码为空"})";
    QString info2=extractBetween(info,"path=",",");
    if (info2.isEmpty()) return R"({"msg":"无法从path获取info"})";
    QJsonObject refObj;
    refObj["file_info"] = info2;
    json["media"] = refObj;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    initjgt(json, QJsonArray(),"",realMsgId,is_wakeup);
    QString url= get_url(type,openid,"messages");
    QString response= _Post(url, json, 5000);

    addmsglog(response,index,pname,info,now_us,type,realMsgId,openid);
    return response;
}

//统一字符串方便其他语言
void QQBotClient::initjgt(QJsonObject &json,const QJsonArray &prompt_keyboard,const QString &message_reference, const QString &msgid, bool is_wakeup)
{
    if (!message_reference.isEmpty()) {
        QJsonObject refObj;
        refObj["message_id"] = message_reference;
        refObj["ignore_get_message_error"] = false;
        json["message_reference"] = refObj;
    }
    json["msg_seq"] = m_info->message_sent;
    if (!is_wakeup) {
        if (msgid.contains("INTERACTION") || msgid.contains("FRIEND_ADD") || msgid.contains("GROUP_MEMBER")) //GROUP_MEMBER_ADD
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

            // 字段索引定义
            // 0: 标题, 1: 数据, 2: 类型(0链接/1回调/2文本，默认2), 3: 立即发送(0/1，对应enter),
            // 4: 引用(0/1，对应reply), 5: 风格(0-n，默认1, 9999为small),
            // 6: 弹出内容, 7: 确认按钮文字, 8: 取消按钮文字
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
void QQBotClient::bianl(int type,int log, QString &text,QJsonObject &keyboard,QJsonArray &prompt_keyboard,const QString &openid)
{
    QString keyboard_data = extractBetween(text,"#b:#","#b:#");
    if(!keyboard_data.isEmpty())
        text=replaceBetweenAll(text,"#b:#","#b:#","");
    int index = mapTypeToTabIndex(type);

    Message log2;
    g_logdb[index]->readLog(m_info->appid,openid,log,log2);
    const QList<mdbtn> &bts = m_info->mdbtn;
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

    const QList<zdywb> &wb= m_info->zdywb;
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
                text = subTextReplace(text,w.thck[i2],w.thcv[i2]); //原位修改
            }
            if(!w.data.isEmpty())
            text = subTextReplace(w.data,"【*】",text);
            isok=true;
            break;
        }


        if(isok) break;
    }

    if(text.contains("{{appid}}"))
        text=subTextReplace(text,"{{appid}}",m_info->appid);
    if(text.contains("{{botname}}"))
        text=subTextReplace(text,"{{botname}}",m_info->nickname);
    if(text.contains("{{name}}"))
    {
        auto *db = g_botdb [m_info->appid_int];
        QString username;
        db->getOrUpdateUser(openid,username);
        text=subTextReplace(text,"{{name}}",username);
    }
    if(text.contains("{{group}}"))
        text=subTextReplace(text,"{{group}}",openid);
    if(text.contains("{{user}}"))
        text=subTextReplace(text,"{{user}}",log2.user);
    if(text.contains("{{msg}}"))
        text=subTextReplace(text,"{{msg}}",log2.msg);
    if(text.contains("{{msgid}}"))
        text=subTextReplace(text,"{{msgid}}",log2.ch);

}


QString QQBotClient::send_messages(int type, const QString &openid,QString &pname, QString &text,
                                    const QString &msgid,bool is_wakeup,bool mode)
{
    if(type<0 || type >3) return R"({"msg":"发送类型错误 不在0-3之间"})";

    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    QString newtext=sendOneMedia(type,openid,pname,text,now_us,msgid,is_wakeup);//检查也没有要发送 的语言视频 文件 原位修改text

    if (!text.isEmpty())
    {
        auto [index, realMsgId] = splitWrappedMsgId(msgid);
        QJsonObject keyboard;
        QJsonArray prompt_keyboard;
        QString message_reference;

        QString textB=normalizeNewlinesToCR(text); //处理换行

        bianl(type,index,textB,keyboard,prompt_keyboard,openid);//挂载按钮解析 小尾巴
        if(textB.isEmpty())
        {
            QString response = R"({"message":"发送内容不能为空"})";
            addmsglog(response,index,pname,text,now_us,type,realMsgId,openid);
            return response;
        }
        QString response,fileinfo;
        if(!mode && m_info->markdown || mode && 聊天发送模式==1)
        {
            textB = processImageTags2(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            if(textB.contains("[image,path="))
                textB = processImageTags(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复

            QString textA = forbidden->filterText(textB);
            response = send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup);
            if(response.contains("token not exist or expire")) //token过期
            {
                m_accessToken.clear();
                refreshAccessToken();
                response = send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup);
            }


        }else{
            textB = processImageTags2(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            if(textB.contains("[image,path="))
                textB = processImageTags(textB,1,fileinfo,type,openid,message_reference);//处理图片 + 回复


            QString textA = forbidden->filterText(textB);
            response = send_messages(type, openid, textA,fileinfo,prompt_keyboard, message_reference, realMsgId, is_wakeup);
            if(response.contains("token not exist or expire"))
            {
                m_accessToken.clear();
                refreshAccessToken();
                response = send_messages(type, openid, textA,fileinfo, prompt_keyboard, message_reference, realMsgId, is_wakeup);
            }
        }
        addmsglog(response,index,pname,text,now_us,type,realMsgId,openid);
        return response;
    }
    return newtext;
}

QString QQBotClient::send_messages(int type, const QString &openid, const QString &text,const QString &info,
                                   const QJsonArray &prompt_keyboard,const QString &message_reference, const QString &msgid,
                                   bool is_wakeup)
{
    QJsonObject json;
    if(info.isEmpty())
    {
        json["msg_type"] = 0;
    }else{
        json["msg_type"] = 7;
        json["media"] =QJsonObject{{"file_info",info}};
    }


    json["content"] = text;
    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup);
    QString url = get_url(type, openid, "messages");
    return _Post(url, json, 5000);
}

QString QQBotClient::send_messages_ark(int type,const QString &openid,QString &pname,const QJsonObject &ark,const QString &msgid,bool is_wakeup)
{
    QJsonArray prompt_keyboard;
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    QString response =  send_messages_ark(type, openid, ark,prompt_keyboard,realMsgId,is_wakeup);
    QJsonDocument doc(ark);
    QString jsonString = doc.toJson(QJsonDocument::Compact); // 紧凑格式
    addmsglog(response,index,pname,jsonString,now_us,type,realMsgId,openid);
    return response;
}
QString QQBotClient::send_messages_ark(int type,const QString &openid,const QJsonObject &ark,const QJsonArray prompt_keyboard,
                                       const QString &msgid,bool is_wakeup)
{
    QJsonObject json;
    json["msg_type"] = 3;
    json["ark"] = ark;

    initjgt(json,prompt_keyboard,"",msgid,is_wakeup);
    QString url= get_url(type,openid,"messages");
    return _Post(url, json, 5000);
}
QString QQBotClient::send_messages_markdown(int type, const QString &openid,const QString &markdown,const QJsonArray prompt_keyboard,
                                            const QJsonObject keyboard,const QString &message_reference,
                                            const QString &msgid,bool is_wakeup)
{
    QJsonObject json;
    json["msg_type"] = 2;
    json["markdown"] = QJsonObject{{"content", markdown}};

    if (keyboard.contains("keyboard")){
        json["keyboard"] = keyboard["keyboard"];
    }else if(keyboard.contains("content")){
        json["keyboard"] = keyboard;
    }else if(keyboard.contains("rows")){
        json["keyboard"] = QJsonObject{{"content",keyboard}};
    }

    initjgt(json,prompt_keyboard,message_reference,msgid,is_wakeup);
    QString url= get_url(type,openid,"messages");
    return _Post(url, json, 5000);
}
//撤回信息
QString QQBotClient::delete_messages(int type, const QString &openid, const QString &msgid)
{
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    QString url = get_url(type, openid, "messages", realMsgId);

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("Authorization", QString("QQBot %1").arg(m_accessToken).toUtf8());
    request.setRawHeader("X-Union-Appid", m_info->appid.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.deleteResource(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(30000);  // 30秒超时，可根据需要调整
    loop.exec();

    QByteArray result = reply->readAll();
    reply->deleteLater();
    return QString::fromUtf8(result);
}


// 生成邀请链接
QString QQBotClient::generate_share_link(const QString& callback_data)
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
    return _Post("https://api.sgroup.qq.com/v2/generate_url_link", json, 5000);
}

//回应回调
QString QQBotClient::respond_interaction(const QString &interaction_id, int code, const QString &data)
{
    QString url = "https://api.sgroup.qq.com/interactions/" + interaction_id;

    QJsonObject json;
    json["code"] = code;
    if (!data.isEmpty()) {
        json["data"] = data;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("QQBot %1").arg(m_accessToken).toUtf8());
    request.setRawHeader("X-Union-Appid", m_info->appid.toUtf8());
    QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = manager.put(request, body);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(10000); // 5秒超时
    loop.exec();
    QString result;
    if (timer.isActive()) {
        if (reply->error() == QNetworkReply::NoError) {
            result = QString::fromUtf8(reply->readAll());
        } else {
            result = QString("Error: %1").arg(reply->errorString());
        }
    } else {
        reply->abort();
        result = "Error: timeout";
    }

    reply->deleteLater();
    return result;
}
