// node_process.h
#ifndef NODE_PROCESS_H
#define NODE_PROCESS_H

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <QHash>
#include <QTimer>

class NodeProcess : public QObject {
    Q_OBJECT
public:
    explicit NodeProcess(const QString& uuid, const QString& pluginPath, QObject* parent = nullptr);
    ~NodeProcess();

    bool start(bool isManual = true);
    void stop();

    void writeMessage(const QByteArray& jsonMsg);
    int sendRequest(const QString& method, const QJsonArray& params,
                    std::function<void(const QJsonObject&)> callback);
    void sendResponse(int id, const QString& result, const QString& error = QString());
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    bool isRunning() const { return m_process->state() == QProcess::Running; }
    const QString& uuid() const { return m_uuid; }

signals:
    void requestReceived(int id, const QString& method, const QJsonArray& params);
    void exited();

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onRestartTimer();

private:
    QProcess* m_process;
    QString m_uuid;
    QString m_pluginPath;
    QByteArray m_readBuffer;
    bool m_enabled;
    int m_nextId;
    QHash<int, std::function<void(const QJsonObject&)>> m_pendingCallbacks;

    // 崩溃重启相关
    QTimer* m_restartTimer;
    int m_restartCount;
    bool m_autoRestart;
    static const int MAX_RESTART_COUNT = 5;
};

#endif // NODE_PROCESS_H