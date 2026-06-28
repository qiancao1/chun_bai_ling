// websocketserver.h
#ifndef WEBSOCKETSERVER_H
#define WEBSOCKETSERVER_H

#include <QObject>
#include <QWebSocketServer>
#include <QList>
#include "clientconnection.h"

class WebSocketServer : public QObject
{
    Q_OBJECT
public:
    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer();
    void close();
    bool open(quint16 port);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onClientMessageReceived(const QJsonObject &request);

private:
    void broadcastOnlineCount();
    void handleGetGroupList(const QJsonObject &params, ClientConnection *client, const QString &reqId);
    void handleGetRecentMessages(const QJsonObject &params, ClientConnection *client, const QString &reqId);

    // websocketserver.h 中增加
    void handleGetLogs(const QJsonObject &params, ClientConnection *client, const QString &reqId);
    // 修改 sendError 增加 reqId 参数（可选）
    void sendError(ClientConnection *client, const QString &errorMsg, const QString &reqId = QString());

    QWebSocketServer *m_server;
    QList<ClientConnection*> m_clients;
};

#endif