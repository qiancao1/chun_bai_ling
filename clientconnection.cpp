// clientconnection.cpp
#include "clientconnection.h"
#include <QJsonDocument>
#include <QJsonObject>

ClientConnection::ClientConnection(QWebSocket *socket, const QString &token, QObject *parent)
    : QObject(parent), m_socket(socket), m_token(token)
{
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ClientConnection::onTextMessageReceived);
    connect(m_socket, &QWebSocket::disconnected, this, &ClientConnection::onSocketDisconnected);
}

ClientConnection::~ClientConnection()
{
    if (m_socket) m_socket->deleteLater();
}

void ClientConnection::sendMessage(const QJsonObject &message)
{
    QJsonDocument doc(message);
    m_socket->sendTextMessage(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void ClientConnection::onTextMessageReceived(const QString &message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isObject()) {
        emit messageReceived(doc.object());
    } else {
        // 发送错误响应
        QJsonObject error;
        error["success"] = false;
        error["error"] = "Invalid JSON";
        sendMessage(error);
    }
}

void ClientConnection::onSocketDisconnected()
{
    emit disconnected();
    deleteLater();
}