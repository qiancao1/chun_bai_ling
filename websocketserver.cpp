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
// 硬编码的合法 token（生产环境应改为数据库验证）
static const QString VALID_TOKEN = "my_secret_token_123";

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

    QUrl url = socket->requestUrl();
    QUrlQuery query(url);
    QString token = query.queryItemValue("token");
    if (token != VALID_TOKEN) {
        qWarning() << "Invalid token from" << socket->peerAddress().toString();
        socket->close(QWebSocketProtocol::CloseCodePolicyViolated, "Invalid token");
        socket->deleteLater();
        return;
    }

    ClientConnection *client = new ClientConnection(socket, token, this);
    m_clients.append(client);

    connect(client, &ClientConnection::disconnected, this, &WebSocketServer::onClientDisconnected);
    connect(client, &ClientConnection::messageReceived, this, &WebSocketServer::onClientMessageReceived);

    qDebug() << "New client connected, IP:" << client->getPeerAddress().toString()
             << "Total clients:" << m_clients.size();

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

class ___wefs : public QRunnable {
public:
    // 通过构造函数把需要的数据传进来（如果有的话）
    ___wefs(QJsonObject &par,const QString &req,ClientConnection *c) : params(par),reqId(req),client(c) {}

    void run() override {
        QString groupId = params.value("groupId").toString();
        int appid = params.value("appid").toInt();
        int type = params.value("type").toInt() - 1;
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

        QString pname = "web聊天室";
        QString res = m_botClients[appid]->send_messages(type, groupId, pname, text, msgid, false, true, mark);
        if (!res.contains("ROBOT")) {
            res = m_botClients[appid]->send_messages(type, groupId, pname, text, QString(), type == 2, true, mark);
        }

        QJsonObject response;
        response["cmd"] = "send_msg";
        response["params"] = params;
        if (!reqId.isEmpty()) response["reqId"] = reqId;

        // 解析返回结果
        QJsonDocument doc = QJsonDocument::fromJson(res.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            bool success = obj.contains("id") && !obj.contains("error");
            response["success"] = success;
            if (success) {
                QString id = obj["id"].toString();
                QJsonObject ext = obj["ext_info"].toObject();
                QString ref = ext["ref_idx"].toString();
                response["id"] = id;
                response["ref"] = "[ref,msg_idx=" + ref + "]";
            }
            response["msg"] = res;
        } else {
            bool success = res.contains("ROBOT");
            response["success"] = success;
            response["msg"] = res;
        }

        sendResponse(response);
    }
private:
    QJsonObject params;
    QString reqId;
    ClientConnection *client;
};
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
    } else {
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
// 获取聊天记录
void WebSocketServer::handleGetRecentMessages(const QJsonObject &params, ClientConnection *client, const QString &reqId)
{
    QString groupId = params.value("groupId").toString();
    int appid = params.value("appid").toInt();
    int type = params.value("type").toInt();
    int count = params.value("count").toInt(20);
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

    // 注意：这里你的 original 代码用了 g_logdb[appid]，但 appid 可能是账号id，而 type 是日志类型索引。根据上下文，你可能需要根据 type 选择日志库。此处保留原逻辑，但建议检查。
    QList<Message> list = g_logdb[type]->getRecentLogs(QString::number(appid), groupId, count);

    QJsonArray messagesArray;
    for (const auto &msg : std::as_const(list)) {

        QJsonObject msgObj;
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