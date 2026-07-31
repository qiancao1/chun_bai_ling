#include <future>
#include <QNetworkAccessManager>
#include <qmutex.h>
#include <qnetworkreply.h>

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
    std::future<QString> Patch (const QString &url, const QByteArray &jsonData,
                               const QHash<QString, QString> &headers, int timeoutMs);
    std::future<QString> put(const QString &url, const QByteArray &jsonData,
                                         const QHash<QString, QString> &headers, int timeoutMs);

    std::future<QString> Delete(const QString &url,
                                const QByteArray &data,
                                const QHash<QString, QString> &headers = {},
                                int timeoutMs = 30000);


    using Callback = std::function<void(const QString&, QNetworkReply::NetworkError)>;

    // 异步 POST，回调在 context 所在线程执行
    void postAsync(const QString& url, const QByteArray& data,
                   const QHash<QString, QString>& headers, int timeoutMs,
                   QObject* context, Callback callback);

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