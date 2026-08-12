#include <future>
#include <QNetworkAccessManager>
#include <qmutex.h>
#include <qnetworkreply.h>
#include <qthread.h>
using Callback = std::function<void(const QString&, QNetworkReply::NetworkError)>;
class NetManager : public QObject {
    Q_OBJECT
public:
    static NetManager* instance() {
        static QList<NetManager*> managers;
        static int index = 0;
        if (managers.isEmpty()) {
            // 创建多个实例，比如根据 CPU 核心数
            int count = qMax(4, QThread::idealThreadCount());
            for (int i = 0; i < count; ++i) {
                managers.append(new NetManager(i));
            }
        }
        // 轮询返回（或随机）
        int idx = index++ % managers.size();
        return managers[idx];
    }


    // 注意这里返回的是标准库的 std::future
    std::future<QString> post(const QString &url, const QByteArray &jsonData,
                              const QHash<QString, QString> &headers, int timeoutMs);
    std::future<QString> get(const QString &url,const QHash<QString, QString> &headers, int timeoutMs) ;
    std::future<QString> Patch (const QString &url, const QByteArray &jsonData,
                               const QHash<QString, QString> &headers, int timeoutMs);
    std::future<QString> put(const QString &url, const QByteArray &jsonData,
                                         const QHash<QString, QString> &headers, int timeoutMs);
    void putAsync(const QString& url, const QByteArray& data,
                              const QHash<QString, QString>& headers, int timeoutMs,
                              Callback callback);
    std::future<QString> Delete(const QString &url,
                                const QByteArray &data,
                                const QHash<QString, QString> &headers = {},
                                int timeoutMs = 30000);
    void Delete2(const QString &url, const QByteArray &data,
                 const QHash<QString, QString> &headers, int timeoutMs=30000, Callback callbacks=Callback());



    // 异步 POST，回调在 context 所在线程执行
    void postAsync(const QString& url, const QByteArray& data,
                   const QHash<QString, QString>& headers, int timeoutMs, Callback callback=Callback());
    void getAsync(const QString &url, const QHash<QString, QString> &headers, int timeoutMs, Callback callbacks=Callback());

private:
    NetManager(int index) : m_index(index) { init(); }
    ~NetManager() { cleanup(); }
    void init();
    void cleanup();
    int m_index;
    QThread *m_netThread = nullptr;
    QList<QNetworkAccessManager*> m_netManagers;
    int m_netManagerIndex = 0;
    QMutex m_managerMutex;
};