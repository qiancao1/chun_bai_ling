#include <future>
#include <QNetworkAccessManager>
#include <qmutex.h>

class NetManager : public QObject {
    Q_OBJECT
public:
    static NetManager* instance() {
        static NetManager manager;
        return &manager;
    }

    // 注意这里返回的是标准库的 std::future
    std::future<QString> post(const QString &url, const QByteArray &jsonData,
                              const QHash<QString, QString> &headers, int timeoutMs);
    std::future<QString> get(const QString &url,const QHash<QString, QString> &headers, int timeoutMs) ;
private:
    NetManager() { init(); }
    ~NetManager() { cleanup(); }
    void init();
    void cleanup();

    QThread *m_netThread = nullptr;
    QList<QNetworkAccessManager*> m_netManagers;
    int m_netManagerIndex = 0;
    QMutex m_managerMutex;
};