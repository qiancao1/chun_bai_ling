#pragma once
#include <future>
#include <QNetworkAccessManager>
#include <qhash.h>
#include <qlist.h>
#include <qmutex.h>
#include <qnetworkreply.h>
#include <qobject.h>
#include <qthread.h>
using Callback = std::function<void(const QString&, QNetworkReply::NetworkError)>;

class NetManager : public QObject {
    Q_OBJECT
public:
    // 单例：整个进程只有一条网络线程、一套 QNAM 连接池。
    //
    // 原来这里按 CPU 核数创建 16 个实例（16 条线程 × 50 个 QNAM = 800 个 QNAM），
    // 问题有三：① 连接池彼此独立，同一 host 的请求被撒到 16 个池里，keep-alive 复用被稀释；
    // ② 800 个 QNAM 大部分空转，内存/句柄纯浪费；③ 首次调用没有锁保护，多线程
    //    同时进来会重复创建整套实例。
    //
    // 异步 QNAM 本身是事件循环驱动的，1 条线程就能撑起上千并发 ——
    // 并发能力来自 QNAM 数量（每个 QNAM 对同一 host 有 6 条连接），跟线程数无关。
    static NetManager* instance() {
        static NetManager *s_inst = nullptr;
        static QMutex    s_initMutex;
        if (!s_inst) {
            QMutexLocker locker(&s_initMutex);
            if (!s_inst)
                s_inst = new NetManager(0);
        }
        return s_inst;
    }


    // ── 回调执行线程 ──
    // 这些 *Async / Delete2 的回调**不在网络线程执行**，而是丢进「线程池」。
    //
    // 为什么不用"投回调用方线程"：调用方通常是 QThreadPool 的 QRunnable worker，
    // 它们跑完 run() 就被回收、**没有事件循环**，投过去要么没人执行、要么上下文已销毁。
    //
    // 也不投主线程：GUI 线程被回调的重活拖住会卡界面。
    //
    // 所以用 Qt 的全局线程池（QThreadPool::globalInstance()）：
    //   - worker 本身没有事件循环，所以**不能 invokeMethod 投递**，
    //     只能把回调体包成 QRunnable 丢进去执行（见 .cpp 的 dispatchCallback）。
    //   - 复用现成 worker，不额外起线程、不额外上下文切换。
    //   - 前提：回调是轻量的（收完响应就转手），不能在里面阻塞等网络，
    //     否则会占死 worker。
    enum CallbackThread {
        CallbackOnPoolThread  = 0,   // 丢线程池（默认）
        CallbackOnNetThread   = 1    // 就在网络线程跑（回调极轻量时才用）
    };

    // 全局默认（仅作默认值读写口，各方法都带 where 参数）
    static CallbackThread s_cbThread;
    static void setDefaultCallbackThread(CallbackThread t) { s_cbThread = t; }
    static CallbackThread defaultCallbackThread() { return s_cbThread; }

    // 注意这里返回的是标准库的 std::future
    std::future<QByteArray> post(const QString &url, const QByteArray &jsonData,
                              const QHash<QString, QString> &headers, int timeoutMs);
    std::future<QByteArray> get(const QString &url,const QHash<QString, QString> &headers=QHash<QString, QString>(), int timeoutMs=30000) ;
    std::future<QByteArray> Patch (const QString &url, const QByteArray &jsonData,
                               const QHash<QString, QString> &headers, int timeoutMs);
    std::future<QByteArray> put(const QString &url, const QByteArray &jsonData,
                                         const QHash<QString, QString> &headers, int timeoutMs);
    void putAsync(const QString& url, const QByteArray& data,
                              const QHash<QString, QString>& headers, int timeoutMs,
                              Callback callback,
                              CallbackThread where = CallbackOnPoolThread);
    std::future<QByteArray> Delete(const QString &url,
                                const QByteArray &data,
                                const QHash<QString, QString> &headers = {},
                                int timeoutMs = 30000);
    void Delete2(const QString &url, const QByteArray &data,
                 const QHash<QString, QString> &headers, int timeoutMs=30000,
                 Callback callbacks=Callback(),
                 CallbackThread where = CallbackOnPoolThread);



    // 异步 POST，回调默认丢线程池（见 CallbackThread 说明）
    void postAsync(const QString& url, const QByteArray& data,
                   const QHash<QString, QString>& headers, int timeoutMs,
                   Callback callback=Callback(),
                   CallbackThread where = CallbackOnPoolThread);
    void getAsync(const QString &url, const QHash<QString, QString> &headers, int timeoutMs,
                  Callback callbacks=Callback(),
                  CallbackThread where = CallbackOnPoolThread);

private:
    NetManager(int index) : m_index(index) { init(); }
    ~NetManager() { cleanup(); }
    void init();
    void cleanup();

    // 按 host 取一个 QNetworkAccessManager，并在其池内轮询。
    // 池内 NAM 数量随该 host 的实时在途数自适应：流量小就只用一个（复用最好），
    // 流量大再逐步加（一个 NAM 对同一 host 只有 6 条连接，是 Qt 的硬限制）。
    QNetworkAccessManager *pickManager(const QString &url, int needConcurrency = 1);

    // 请求完成时调用，让池知道该 host 的在途数降了，必要时缩容
    void notifyFinished(const QString &url);

    // 把回调交出去执行。必须在「网络线程」中调用。
    //
    //   CallbackOnPoolThread → 包成 QRunnable 丢进全局线程池（默认）
    //   CallbackOnNetThread  → 直接在当前线程原地执行
    static void dispatchCallback(CallbackThread where,
                                 const QString &response,
                                 QNetworkReply::NetworkError err,
                                 Callback cb);

    int m_index;
    QThread *m_netThread = nullptr;

    // ---- 惰性连接池（按 host 建组）----
    // 不预建 NAM，用到哪个 host 才建哪个 host 的池。
    struct HostPool {
        QList<QNetworkAccessManager*> nams;
        int rr = 0;            // 池内轮询下标
        int inflight = 0;      // 该 host 当前在途请求数
        int idleTicks = 0;     // 连续空转轮次数，用于缩容
    };

    // 池内 NAM 数量上下限。kConnPerNam=6 是 Qt「单 NAM 对单 host」的连接上限。
    enum { kNamMin = 1, kNamMax = 256, kConnPerNam = 6 };

    HostPool *poolFor(const QString &host);   // 取池，无则建（调用方须持有 m_managerMutex）

    QHash<QString, HostPool*> m_pools;        // host -> 池
    QMutex m_managerMutex;                    // 保护 m_pools
};