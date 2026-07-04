// websocketserver.cpp
#include "websocketserver.h"
#include "global.h"
#include <QUrlQuery>
#include <QJsonArray>
#include <QJsonValue>
#include <QDebug>
#include <qfileinfo.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qtimer.h>
QString getBotName(int appid);



WebSocketServer::WebSocketServer(QObject *parent)
    : QObject(parent)
{
    m_server = new QWebSocketServer("ChatBackend", QWebSocketServer::NonSecureMode, this);
}

bool WebSocketServer::open(quint16 port)
{
    if (!m_server->listen(QHostAddress::Any, port)) return false;
    connect(m_server, &QWebSocketServer::newConnection, this, &WebSocketServer::onNewConnection);
    return true;
}
void WebSocketServer::close()
{
    m_server->close();
    qDeleteAll(m_clients);
}
WebSocketServer::~WebSocketServer()
{
    close();
}

void WebSocketServer::onNewConnection()
{
    QWebSocket *socket = m_server->nextPendingConnection();
    if (!socket) return;

    // 1. 检查是否处于封禁状态（10 秒内）
    if (m_tokenErrorBlocked) {
        if (m_tokenErrorTimer.elapsed() < 10000) {
            qWarning() << "Server is temporarily blocking new connections (token error cooldown)";
            socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Server busy, please retry later");
            socket->deleteLater();
            return;
        } else {
            // 超过 10 秒，解除封禁
            m_tokenErrorBlocked = false;
        }
    }

    QUrl url = socket->requestUrl();
    QUrlQuery query(url);
    QString token = query.queryItemValue("token");

    if (token != ws_token) {
        qWarning() << "Invalid token from" << socket->peerAddress().toString();

        // 2. 触发封禁（记录当前时间）
        m_tokenErrorBlocked = true;
        m_tokenErrorTimer.start();

        socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Invalid token");
        socket->deleteLater();
        return;
    }


    ClientConnection *client = new ClientConnection(socket, token, this);
    m_clients.append(client);

    connect(client, &ClientConnection::disconnected, this, &WebSocketServer::onClientDisconnected);
    connect(client, &ClientConnection::messageReceived, this, &WebSocketServer::onClientMessageReceived);
    AppendEventLog("IP:"+client->getPeerAddress().toString()+"登录聊天室 当前聊天室成员："+QString::number(m_clients.size())+" 注意 当你高频链接时 可能人员会不是1 因为你之前的链接可能没释放",0xff);

    broadcastOnlineCount();
}

void WebSocketServer::onClientDisconnected()
{
    ClientConnection *client = qobject_cast<ClientConnection*>(sender());
    if (client) {
        m_clients.removeAll(client);
        qDebug() << "Client disconnected, remaining:" << m_clients.size();
        broadcastOnlineCount();
    }
}

void WebSocketServer::broadcastOnlineCount()
{
    QSet<QString> ipSet;
    for (const ClientConnection *c : std::as_const(m_clients)) {
        ipSet.insert(c->getPeerAddress().toString());
    }

    QJsonObject data;
    data["total"] = m_clients.size();
    data["uniqueIps"] = ipSet.size();

    QJsonObject message;
    message["type"] = "onlineCount";
    message["data"] = data;

    for (ClientConnection *c : std::as_const(m_clients)) {
        c->sendMessage(message);
    }
}
// 假设您已经有了一个处理 JSON 消息的函数



class ___wefs : public QRunnable {
public:
    // 通过构造函数把需要的数据传进来（如果有的话）
    ___wefs(QJsonObject &par,const QString &req,ClientConnection *c) : params(par),reqId(req),client(c) {}

    void run() override {
        QString groupId = params.value("groupId").toString();
        int appid = params.value("appid").toInt();
        int type = params.value("type").toInt();
        QString text = params.value("text").toString();
        QString msgid = params.value("msgid").toString();
        int mark = params.value("markdown").toInt();

        // 线程安全的发送函数
        auto sendResponse = [this](const QJsonObject &resp) {
            QPointer<ClientConnection> safeClient = client;
            QMetaObject::invokeMethod(qApp, [=]() {
                if (safeClient) {
                    safeClient->sendMessage(resp);
                }
            }, Qt::QueuedConnection);
        };

        int index = accinfo(appid);
        if (index == -1) {
            QJsonObject response;
            response["cmd"] = "send_msg";
            response["success"] = false;
            response["msg"] = "appid 错误 appid不在账号列表";
            if (!reqId.isEmpty()) response["reqId"] = reqId;
            sendResponse(response);
            return;
        }

        if (!m_botClients.contains(appid)) {
            QJsonObject response;
            response["cmd"] = "send_msg";
            response["success"] = false;
            response["msg"] = "指定appid 对应机器人未登录";
            if (!reqId.isEmpty()) response["reqId"] = reqId;
            sendResponse(response);
            return;
        }

        QJsonValue fileDataVal = params.value("file_data");
        QStringList fileDataList;
        if (fileDataVal.isArray()) {
            const QJsonArray arr = fileDataVal.toArray();
            for (auto v : arr) {
                if (v.isString()) fileDataList.append(v.toString());
            }
        } else if (fileDataVal.isString()) {
            fileDataList.append(fileDataVal.toString());
        }

        if (!fileDataList.isEmpty()) {
            // 辅助 Lambda：保存 Base64 到文件，返回完整路径
            auto saveBase64ToFile = [&](const QString &base64Data, const QString &dir, const QString &filename) -> QString {
                QDir dirPath(dir);
                if (!dirPath.exists()) {
                    if (!dirPath.mkpath(".")) {
                        qWarning() << "无法创建目录：" << dir;
                        return QString();
                    }
                }

                // 1. 去除 Data URL 前缀（如果存在）
                QString cleanBase64 = base64Data;
                if (cleanBase64.startsWith("data:")) {
                    int commaPos = cleanBase64.indexOf(',');
                    if (commaPos != -1) {
                        cleanBase64 = cleanBase64.mid(commaPos + 1);
                    }
                }

                // 2. 解码 Base64
                QByteArray raw = QByteArray::fromBase64(cleanBase64.toLatin1());
                if (raw.isEmpty()) {
                    qWarning() << "Base64 解码失败，数据开头：" << cleanBase64.left(30);
                    return QString();
                }

                // 3. 写入文件
                QString fullPath = dir + "/" + filename;
                // 若文件存在，添加随机后缀
                if (QFile::exists(fullPath)) {
                    QFileInfo fi(filename);
                    QString base = fi.baseName();
                    QString ext = fi.completeSuffix();
                    QString newName = base + "_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(6);
                    if (!ext.isEmpty()) newName += "." + ext;
                    fullPath = dir + "/" + newName;
                }

                QFile file(fullPath);
                if (!file.open(QIODevice::WriteOnly)) {
                    qWarning() << "无法写入文件：" << fullPath;
                    return QString();
                }
                file.write(raw);
                file.close();
                return fullPath;
            };

            // 1. 收集所有标记及其在文本中的位置（按出现顺序）
            struct TagInfo {
                int pos;
                QString fullMatch;
                QString type;   // image, file, audio, video
                QString name;   // 原始文件名
            };
            QList<TagInfo> tags;

            // 正则匹配： [image, name=xxx] 或 [image, path=xxx] 或 [image] 等
            QRegularExpression tagRegex(R"(\[(image|file|audio|video)(?:\s*,\s*(?:name|path)\s*=\s*([^\],]+))?\])",
                                        QRegularExpression::CaseInsensitiveOption);
            auto it = tagRegex.globalMatch(text);
            while (it.hasNext()) {
                auto match = it.next();
                TagInfo info;
                info.pos = match.capturedStart();
                info.fullMatch = match.captured(0);
                info.type = match.captured(1).toLower();
                info.name = match.captured(2).trimmed();
                if (info.name.isEmpty()) {
                    // 如果没有提供 name，生成默认名
                    info.name = QString("%1_%2.%3")
                                    .arg(info.type)
                                    .arg(QDateTime::currentDateTime().toTime_t())
                                    .arg(info.type == "audio" ? "mp3" : (info.type == "video" ? "mp4" : "bin"));
                }
                tags.append(info);
            }

            // 兼容图片的 ![]() 格式（如果前端还使用）
            QRegularExpression imgRegex(R"(!\[.*?\]\(([^)]+)\))");
            auto it2 = imgRegex.globalMatch(text);
            while (it2.hasNext()) {
                auto match = it2.next();
                // 将 ![]() 也视为 image 标记，但我们需要知道它的文件名
                TagInfo info;
                info.pos = match.capturedStart();
                info.fullMatch = match.captured(0);
                info.type = "image";
                info.name = match.captured(1); // 可能是纯文件名或路径
                // 提取实际文件名（去掉路径）
                QFileInfo fi(info.name);
                info.name = fi.fileName();
                if (info.name.isEmpty()) info.name = "image.jpg";
                tags.append(info);
            }

            // 按位置排序
            std::sort(tags.begin(), tags.end(), [](const TagInfo &a, const TagInfo &b) {
                return a.pos < b.pos;
            });

            // 从后往前替换，避免位置偏移
            for (int i = tags.size() - 1; i >= 0; --i) {
                if (fileDataList.isEmpty()) break;
                const TagInfo &info = tags[i];
                QString b64 = fileDataList.takeLast(); // 反向取最后一个

                // 确定目标目录
                QString targetDir;
                if (info.type == "image") targetDir = "tmp/image";
                else if (info.type == "file") targetDir = "tmp/file";
                else if (info.type == "audio") targetDir = "tmp/audio";
                else if (info.type == "video") targetDir = "tmp/video";
                else continue;

                QString savedPath = saveBase64ToFile(b64, targetDir, info.name);
                if (!savedPath.isEmpty()) {
                    QString newTag;
                    if (info.type == "image") {
                        // 图片使用 Markdown 语法或 [image, path=...] 均可，这里使用 [image, path=...]
                        newTag = QString("[image, path=%1]").arg(savedPath);
                    } else {
                        newTag = QString("[%1, path=%2]").arg(info.type,savedPath);
                    }
                    text.replace(info.pos, info.fullMatch.length(), newTag);
                }
            }

            // 如果有剩余的 file_data（没有标记对应），保存为普通文件并追加到末尾
            if (!fileDataList.isEmpty()) {
                QString extraText;
                for (const QString &b64 : fileDataList) {
                    QString filename = QString("file_%1_%2.bin")
                    .arg(QDateTime::currentDateTime().toTime_t())
                        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(6));
                    QString savedPath = saveBase64ToFile(b64, "tmp/file", filename);
                    if (!savedPath.isEmpty()) {
                        extraText += QString("[file, path=%1]").arg(savedPath);
                    }
                }
                if (!extraText.isEmpty()) {
                    text += " " + extraText;
                }
            }
        }

        // ========== 发送消息（使用更新后的 text） ==========
        QString pname = "[web聊天室]";
        QString res = m_botClients[appid]->send_messages(type, groupId, pname, text, msgid, false, true, mark);
        if (!res.contains("ROBOT")) {
            res = m_botClients[appid]->send_messages(type, groupId, pname, text, QString(), type == 2, true, mark);
        }

        QJsonObject response;
        response["cmd"] = "send_msg";
        response["params"] = params;
        response["success"] = true;
        if (!reqId.isEmpty()) response["reqId"] = reqId;
        sendResponse(response);
    }


private:
    QJsonObject params;
    QString reqId;
    ClientConnection *client;
};

QString botlist();
void WebSocketServer::onClientMessageReceived(const QJsonObject &request)
{
    ClientConnection *client = qobject_cast<ClientConnection*>(sender());
    if (!client) return;

    QString action = request.value("action").toString();
    QJsonObject params = request.value("params").toObject();
    QString reqId = request.value("reqId").toString();

    if (action == "getGroupList") {
        handleGetGroupList(params, client, reqId);
    } else if (action == "getRecentMessages") {
        handleGetRecentMessages(params, client, reqId);
    } else if (action == "send_msg") {
        auto *task = new ___wefs(params,reqId,client);
        QThreadPool::globalInstance()->start(task);
    } else if (action == "getLogs") {
        handleGetLogs(params, client, reqId);
    }else if(action == "getFile")
    {
        QString path = params.value("path").toString();
        QFileInfo fi(path);
        QString absolutePath = fi.absoluteFilePath();

        // 限制只能读取 tmp/ 目录下的文件
        QDir tmpDir("tmp/");
        QString tmpAbsolute = tmpDir.absolutePath();
        if (!absolutePath.startsWith(tmpAbsolute)) {
            QJsonObject response;
            response["action"] = "getFile";
            response["reqId"] = reqId;
            response["success"] = false;
            response["msg"] = "Access denied: file outside tmp directory";
            client->sendMessage(response);
            return;
        }

        QFile file(path);
        QJsonObject response;
        response["action"] = "getFile";
        response["reqId"] = reqId;

        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QString base64 = data.toBase64();
            response["success"] = true;
            response["data"] = base64;
            response["path"] = path;
        } else {
            response["success"] = false;
            response["msg"] = "文件不存在或无法读取: " + path;
        }
        client->sendMessage(response);
        return;
    }else if(action == "getBotList"){
        QJsonObject response;
        response["success"] = true;
        response["data"] = botlist();
        response["cmd"] = "getBotList";
        if (!reqId.isEmpty()) response["reqId"] = reqId;
        client->sendMessage(response);

        return;
    }else if(action == "deleteMsg"){ // 撤回

        QString groupId = params.value("groupId").toString();  //群id 私聊时传递 好友
        int appid = params.value("appid").toInt();
        int type = params.value("type").toInt(); //0 群 1频道 2私聊 3频道私聊
        if(type<0 || type>3)
        {
            QJsonObject response;
            response["success"] = false;
            response["data"] = "type 不在 0-3 之间"; //success 为 false时 错误信息
            response["cmd"] = "deleteMsg";
            if (!reqId.isEmpty()) response["reqId"] = reqId;
            client->sendMessage(response);
            return ;
        }
        int seq = params.value("seq").toInt(0); //getRecentMessages 返回的有
        bool isSelf = params.value("isSelf").toBool();
        QString msgid = params.value("msgid").toString();        //不出意外是 用户发的 ch 机器人发的 plugin_ch


        QString res;
        if(m_botClients.contains(appid))
        {
           res = m_botClients[appid]->delete_messages(type,groupId,msgid);
            if(!res.contains("message") && seq !=0) {
                Message msg;

                if(g_logdb[type+1]->readLog(QString::number(appid),groupId,seq,msg))
                {
                    msg.Color_0 = 0xAB56F6;
                    msg.hf=QString();
                    if(isSelf){
                        msg.plugin_ch= "[已撤回]";

                        msg.direction = "[已撤回]\n"+msg.direction;

                    }else{
                        msg.ch= "[已撤回]";
                        msg.msg = "[已撤回]\n"+msg.msg;
                    }
                    g_logdb[type+1]->updateLog(QString::number(appid),groupId,seq,msg);
                }
            }
        }
        QJsonObject response;
        response["success"] = (!res.contains("mess"));
        response["data"] = res; //success 为 false时 错误信息
        response["cmd"] = "deleteMsg";
        if (!reqId.isEmpty()) response["reqId"] = reqId;
        client->sendMessage(response);
        return;
    }
    else {
        sendError(client, "Unknown action: " + action, reqId);
    }
}

// 群列表
void WebSocketServer::handleGetGroupList(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    int type = params.value("type").toInt(0);
    if (type < 0 || type > 5) {
        QJsonObject errorResp;
        errorResp["success"] = false;
        errorResp["cmd"] = "getGroupList";
        errorResp["error"] = "Invalid type, must be 0-5";
        if (!reqId.isEmpty()) errorResp["reqId"] = reqId;
        client->sendMessage(errorResp);
        return;
    }

    QJsonArray groupList;

    switch (type) {
    case 0: {
        bool sw = g_logdb[1]->beginTransaction(true);
        for (auto it = chatPage->全量群.begin(); it != chatPage->全量群.end(); ++it) {
            QString groupId = it.key();
            int appid = it.value();
            QString name = chatPage->customGroupNames.value(groupId);
            if (name.isEmpty()) name = groupId;
            Message msg;
            if(sw)
            {
                g_logdb[1]->getLatestLogInTxn(g_logdb[1]->getCurrentTxn(),QString::number(appid), groupId, msg);
            }else{
                g_logdb[1]->getLatestLog(QString::number(appid),groupId,msg);


            }
            QJsonObject groupObj;
            groupObj["groupId"] = groupId;
            groupObj["groupName"] = name;
            groupObj["appid"] = appid;
            groupObj["type"] = 0;
            if(msg.name.isEmpty())
                 groupObj["msg"] ="无信息";
            else
                groupObj["msg"] = msg.name+":"+msg.msg;

            groupList.append(groupObj);
        }
        if(sw) g_logdb[1]->commitTransaction();
        break;
    }
    case 1:
    case 2:
    case 3:
    case 4: {
        int bufferIdx = type;
        const QStringList keys = g_logdb[bufferIdx]->getLatestKeys(1000);
        QSet<QPair<int, QString>> seen;
        bool sw = g_logdb[bufferIdx]->beginTransaction(true);
        for (const QString &keyStr : keys) {
            QStringList parts = keyStr.split(':');
            if (parts.size() != 3) continue;
            bool ok;
            int appid = parts[1].toInt(&ok);
            if (!ok) continue;
            QString groupId = parts[2];
            QPair<int, QString> key(appid, groupId);
            if (seen.contains(key)) continue;
            seen.insert(key);
            QString name = chatPage->customGroupNames.value(groupId);
            if (name.isEmpty()) name = groupId;
            uint64_t seq = parts[0].toULongLong(&ok);
            Message msg;
            if(sw)
            {
                g_logdb[bufferIdx]->readLogInTxn(g_logdb[bufferIdx]->getCurrentTxn(),QString::number(appid), groupId,seq, msg);
            }else{
                g_logdb[bufferIdx]->readLog(QString::number(appid), groupId, seq, msg);

            }

            QJsonObject groupObj;
            groupObj["groupId"] = groupId;
            groupObj["groupName"] = name;
            groupObj["appid"] = appid;
            groupObj["type"] = type;
            groupObj["msg"] = msg.name+":"+msg.msg;
            groupList.append(groupObj);
        }
        if(sw) g_logdb[bufferIdx]->commitTransaction();
        break;
    }
    case 5: {

        for (auto it = chatPage->最近对话.begin(); it != chatPage->最近对话.end(); ++it) {
            QString key = it.key();
            qint64 value = it.value();
            int appid = 0, typeFromValue = 0;
            parseFromId(value, appid, typeFromValue);
            QString name = chatPage->customGroupNames.value(key);
            if (name.isEmpty()) name = key;
            QJsonObject groupObj;
            groupObj["groupId"] = key;
            groupObj["groupName"] = name;
            groupObj["appid"] = appid;
            groupObj["type"] = 5;
            groupObj["subType"] = typeFromValue;
            groupList.append(groupObj);
        }
        break;
    }
    default:
        break;
    }

    QJsonObject response;
    response["success"] = true;
    response["data"] = groupList;
    response["cmd"] = "getGroupList";
    response["total"] = groupList.size();
    if (!reqId.isEmpty()) response["reqId"] = reqId;
    client->sendMessage(response);
}
//获取日志

void WebSocketServer::handleGetLogs(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    // 参数：type (0-4), count (默认100), offset (默认0)
    int type = params.value("type").toInt(0);
    if (type < 0 || type > 4) {
        sendError(client, "Invalid log type, must be 0-4", reqId);
        return;
    }

    int count = params.value("count").toInt(100);
    int offset = params.value("offset").toInt(0);
    if (count <= 0) count = 100;
    if (offset < 0) offset = 0;

    QStringList logLines;

    if (g_logdb.size() > type && g_logdb[type]) {
        // 获取消息列表，appid 传 0 表示不过滤
        auto list = g_logdb[type]->getLatestMessagesWithOffset(0, count, offset);
        for (const auto &pair : std::as_const(list)) {
            const Message &msg = pair.second;
            const QString &key  = pair.first;
            QStringList parts = key.split(':');
            if (parts.size() != 3) continue;
            int appid = parts[1].toInt();
            QString groupId;
            if(parts[2]!="0") groupId = parts[2];
            QString line = QString("%1 - %2 - %3 - %4 - %5 - %6")
                               .arg(msg.timestamp,getBotName(appid),groupId,msg.name.isEmpty() ? msg.user : msg.name,msg.msg,msg.direction);

            logLines.append(line);
        }
    } else {
        sendError(client, "Log database not available for type " + QString::number(type), reqId);
        return;
    }


    // 构造 JSON 数组
    QJsonArray arr;
    for (const QString &line : std::as_const(logLines)) {
        arr.append(line);
    }

    QJsonObject response;
    response["success"] = true;
    response["data"] = arr;
    if (!reqId.isEmpty()) response["reqId"] = reqId;
    client->sendMessage(response);
}
//有新信息
void WebSocketServer::broadcastMessage(const Message &msg, int appid, int type, const QString &groupId)
{
    if(m_clients.size()==0) return;
    QJsonObject data;
    data["appid"] = appid;
    data["groupId"] = groupId;
    data["type"] = type;   // 群聊类型，与前端对应

    QJsonObject msgObj;
    msgObj["seq"] = msg.seq;
    msgObj["user"] = msg.user;
    msgObj["msg"] = msg.msg;
    msgObj["timestamp"] = msg.timestamp;
    msgObj["name"] = msg.name;
    msgObj["hf"] = msg.hf;
    msgObj["ch"] = msg.ch;
    msgObj["plugin_ch"] = msg.plugin_ch;
    msgObj["direction"] = msg.direction;
    msgObj["color"] = msg.Color_0;
    msgObj["isSelf"] = msg.isSelf;
    msgObj["ref_name"] = msg.ref_name;
    msgObj["ref_msg"] = msg.ref_msg;


    data["msg"] = msgObj;

    QJsonObject message;
    message["type"] = "newMessage";
    message["data"] = data;

    // 广播给所有在线客户端
    QMetaObject::invokeMethod(qApp, [=]() {
        for (ClientConnection *c : std::as_const(m_clients)) {
            c->sendMessage(message);
        }
    }, Qt::QueuedConnection);

}
// 获取聊天记录
void WebSocketServer::handleGetRecentMessages(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    QString groupId = params.value("groupId").toString();
    int appid = params.value("appid").toInt();
    int type = params.value("type").toInt();
    int count = params.value("count").toInt(50);
    int offset = params.value("offset").toInt(0);
    if(type<=0) type=1;
    if (type < 1 || type > 4) {
        QJsonObject response;
        response["cmd"] = "getRecentMessages";
        response["success"] = false;
        response["msg"] = "type错误 不在0-4之间";
        if (!reqId.isEmpty()) response["reqId"] = reqId;
        client->sendMessage(response);
        return;
    }

    if(offset<=0) offset=2147483636;
    QList<Message> list = g_logdb[type]->getRecentLogs(QString::number(appid), groupId, offset,count);

    QJsonArray messagesArray;
    for (const auto &msg : std::as_const(list)) {

        QJsonObject msgObj;
        msgObj["seq"] = msg.seq;
        msgObj["user"] = msg.user;
        msgObj["msg"] = msg.msg;
        msgObj["timestamp"] = msg.timestamp;
        msgObj["name"] = msg.name;
        msgObj["hf"] = msg.hf;
        msgObj["ch"] = msg.ch;
        msgObj["plugin_ch"] = msg.plugin_ch;
        msgObj["direction"] = msg.direction;
        msgObj["color"] = msg.Color_0;
        msgObj["isSelf"] = msg.isSelf;
        msgObj["ref_name"] = msg.ref_name;
        msgObj["ref_msg"] = msg.ref_msg;
        messagesArray.append(msgObj);
    }

    QJsonObject response;
    response["success"] = true;
    response["data"] = messagesArray;
    response["total"] = list.size();
    response["offset"] = offset;
    response["count"] = count;
    response["cmd"] = "getRecentMessages";
    if (!reqId.isEmpty()) response["reqId"] = reqId;
    client->sendMessage(response);
}

void WebSocketServer::sendError(ClientConnection *client, const QString &errorMsg, const QString &reqId)
{
    QJsonObject response;
    response["success"] = false;
    response["error"] = errorMsg;
    if (!reqId.isEmpty()) response["reqId"] = reqId;
    client->sendMessage(response);
}