// clientconnection.h
#ifndef CLIENTCONNECTION_H
#define CLIENTCONNECTION_H

#include <QObject>
#include <QWebSocket>
#include <QHostAddress>

class ClientConnection : public QObject
{
    Q_OBJECT
public:
    explicit ClientConnection(QWebSocket *socket, const QString &token, QObject *parent = nullptr);
    ~ClientConnection();

    QString getToken() const { return m_token; }
    QHostAddress getPeerAddress() const { return m_socket->peerAddress(); }
    void sendMessage(const QJsonObject &message);

signals:
    void disconnected();
    void messageReceived(const QJsonObject &request);

private slots:
    void onTextMessageReceived(const QString &message);
    void onSocketDisconnected();

private:
    QWebSocket *m_socket;
    QString m_token;
};

#endif