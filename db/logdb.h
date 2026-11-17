#ifndef LOGDB_H
#define LOGDB_H

#include "chatpage.h"
#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QMutex>
#include <atomic>

// 2026-11-11：日志库从 LMDB 换成 SQLite。
//
// 为什么换（LMDB 不适合「写多读极少 + 定期删旧」的日志负载）：
//   1. 删条目不缩文件 —— 只能靠「重写整个库」的 compact，贵到要 3 天冷却；
//   2. 清理必须从头删到边界，10 万条 = 把 199MB 的页全摸进进程工作集，
//      而 Windows 在内存不紧张时永不回收 → 「每天 05:00 过后内存涨一大截」；
//   3. key 是 seq:appid:group（按全局序号排序），「取某会话最近 N 条」只能倒扫过滤；
//   4. 整文件 mmap，碰过的页一直算在进程工作集（任务管理器上就是几百 MB）。
//
// SQLite 这边：cache_size 是**硬上限**（8MB），不开 mmap，删旧走主键 O(log n)，
// auto_vacuum=INCREMENTAL 能原地释放空闲页，会话查询走 (appid,conv,seq) 索引。
//
// ⚠ 对外接口签名保持不变（只把 MDB_txn* 换成不透明的 LogTxn），
//   所有调用方（logpage / chatpage / websocketserver / plts / api2 / global）一行都不用改。

struct sqlite3;   // 只前向声明：不在头文件里拉 sqlite3.h，避免整个工程重编

// 事务句柄。原来这里是 MDB_txn*；换成 SQLite 后事务状态就在连接上，不需要真句柄，
// 但外部有 4 处是「begin → getCurrentTxn() 透传给 readLogInTxn → commit」的写法，
// 保留一个不透明指针，让他们继续原样编译。
using LogTxn = void*;

class LogDB : public QObject
{
    Q_OBJECT
public:
    explicit LogDB(const QString &dbPath, QObject *parent = nullptr);
    ~LogDB();

    // 打开/关闭数据库
    bool open();
    void close();
    bool beginTransaction(bool readOnly = false);
    bool commitTransaction();
    bool abortTransaction();
    bool getLatestLogInTxn(LogTxn txn, const QString& appid, const QString& groupId, Message& msg) const;
    bool getLatestLog(const QString &appid, const QString &groupId, Message &msg) const;
    // 加锁读：外面拿到的只是一个不透明标记，事务真正挂在连接上；
    // readLogInTxn / getLatestLogInTxn 会自己校验它是否仍然有效。
    LogTxn getCurrentTxn() const { QMutexLocker locker(&m_mutex); return m_currentTxn; }
    // 使用外部已有事务进行读取（不自动开启事务）
    bool readLogInTxn(LogTxn txn, const QString& appid, const QString& groupId,
                      uint64_t seq, Message& msg) const;
    // 添加日志，返回分配的序号（全局唯一递增），失败返回 0
    uint64_t appendLog(const QString &appid, const QString &groupId, const Message &msg);
    QString makeKey(const QString &appid, const QString &groupId, uint64_t seq) const;
    // 获取指定 appid:groupId 最近 N 条（按序号从大到小，即最新在前）
    QList<Message> getRecentLogs(const QString &appid, const QString &groupId, int seq, int N,bool daoxu=false) const;

    // ===== 被动回复用的 msgid（2026-11-13：改为纯内存）=====
    // 收到消息时登记一条，批量/定时任务直接内存取 —— 不再反扫日志库（旧实现一次扫 2 万条
    // 并逐条 deserializeMessage，几十毫秒且全程持 m_mutex）。
    //   isGroup = true  → 群：只认最近 5 分钟内登记的
    //   isGroup = false → 好友/单聊：最近 1 小时
    // ⚠ 只登记「用户发来的消息 id」；机器人自己发出去的 id（plugin_ch）绝不能用于发送。
    //   拿不到就返回空表，调用方传空 msgid 走主动发送（群 60 条/分钟、好友 30 条/分钟）。
    QStringList getRecentMsgIds(const QString &appid, const QString &groupId, bool isGroup,
                                int maxCount = 5) const;
    // 一次取该 appid 下所有会话的 msgid（批量推送用）
    //   返回 <会话id, msgid 列表>；isGroup 决定时间窗（群 5 分钟 / 好友 1 小时）
    QHash<QString, QStringList> getRecentMsgIdMap(const QString &appid, bool isGroup,
                                                  int maxCountPerConv = 5) const;
    // 登记一条可用于被动回复的 msgid（收消息时调，见 qqbotclient.cpp）
    // ⚠ 只有「全量群」的群消息才需要登记：非全量群没有主动推送能力，也就用不上被动额度。
    //   同一会话只保留最新一条（调用方只取 value(0)）→ 10 万群约占 10MB。
    //   tsSecs <= 0 = 用当前时间。
    void recordMsgId(const QString &appid, const QString &conv, const QString &msgId,
                     qint64 tsSecs = 0);

    // ===== 会话列表批量读（2026-11-13）=====
    // 聊天页刷新列表原来是「每个会话一条 SQL」（1000 个会话 = 1000 次 prepare/step/finalize），
    // 而且套在只读事务里 —— 事务开着期间 flushCacheToDBUnlocked 直接 return，日志停止落盘。
    // 现在拆成两步：先取每会话最新 seq（只读索引），再一条 IN 查询把 blob 全取回来。
    struct LogRef {
        QString appid;
        QString conv;
        quint64 seq = 0;
    };
    // 某 appid 下每个会话的最新 seq（合并 m_cache 里未落盘的），走 idx_logs_conv 覆盖索引
    QList<QPair<QString, quint64>> getLatestSeqPerConv(const QString &appid) const;
    // 按 (appid, conv, seq) 批量取消息：先查 m_cache，miss 的合并成一条 IN 查询
    //   返回 seq -> Message（库里没有的 seq 不出现）
    QHash<quint64, Message> getMessagesBySeqs(const QList<LogRef> &refs) const;

    // 获取所有 key（格式 "appid:groupId:序号"）的列表
    QStringList getAllKeys() const;
    QStringList getLatestKeys(int N) const;

    // 修改指定 key 对应的 Message
    bool updateLog(const QString &appid, const QString &groupId, uint64_t seq, const Message &msg);
    bool updateLog(const QString &keyStr, const Message &msg);
    // 读取指定 key 对应的 Message
    bool readLog(const QString &appid, const QString &groupId, uint64_t seq, Message &msg) const;
    bool readLog(const QString &keyStr, Message &msg) const;
    // 清理数据库，保留全局最新 N 条，删除其余
    bool cleanDatabase(int keepN);
    // 后台清理（不阻塞 UI，不排队）：单库十几万条时同步删会把界面卡住。
    //   已有一次在跑就直接返回 false；实际删除逻辑与 cleanDatabase 完全相同。
    bool cleanDatabaseAsync(int keepN);
    // 回收空闲页（SQLite 的 auto_vacuum=INCREMENTAL 是原地释放，不需要 LMDB 那种
    //   「重写整个库」的 compact）。启动时顺手跑一次，把上次删旧留下的空洞还掉。
    bool compactIfNeeded();

    // 「卸载再挂上」：关掉 SQLite 连接再重开。
    //   目的不是省 mmap（SQLite 根本不开 mmap），而是把 page cache 里那 8MB 连同
    //   所有临时缓冲一起扔掉；配合 trimWorkingSet 可以把进程工作集压到真实活跃集。
    // ⚠ 只动连接，不动 m_buffer / m_flushTimer；openConnection 全程持 m_mutex，
    //   读/写线程会被挡住，看不到中间的 m_db == nullptr。
    // ⚠ 必须在主线程调用（失败兜底会走 open()，它要重建 QTimer）。
    // trimWorkingSet：成功后是否顺带 SetProcessWorkingSetSize(-1,-1)。
    //   日常清理/内存看门要 true；设置页那个「重载数据库」诊断按钮必须传 false ——
    //   否则每一步读数都已经被 trim 过，前后对比全部失真。
    bool reloadAfterClean(bool trimWorkingSet = true);

    // 后台清理是否正在跑。cleanDatabase 全程持 m_mutex，这时候调 reloadAfterClean()
    // 会把主线程一起卡住 —— 内存看门要先问这个。
    bool isCleaning() const { return m_cleaning.load(std::memory_order_relaxed); }


    void setBufferRaw(uint64_t seq, uint64_t value) {
        m_buffer[seq % m_bufferSize].store(value, std::memory_order_release);
    }
    // 读取完整的 64 位值
    uint64_t getBufferRaw(uint64_t seq) const {
        return m_buffer[seq % m_bufferSize].load(std::memory_order_acquire);
    }

    // 便捷接口：设置时间戳（微秒）和状态（1字节）
    void setBufferDurationAndStatus(uint64_t seq, uint64_t timeUs, uint8_t status) {
        uint64_t value = (timeUs << 8) | (status & 0xFF);
        setBufferRaw(seq, value);
    }

    uint64_t setBuffer_255(uint64_t seq, bool &ok)
    {
        size_t idx = seq % m_bufferSize;
        uint64_t old = m_buffer[idx].load(std::memory_order_acquire);
        while (true) {
            uint8_t status = static_cast<uint8_t>(old & 0xFF);
            if (status == 255) {
                ok = false;
                return old >> 8;   // 返回时间
            }
            uint64_t newVal = (old & ~0xFFULL) | 255ULL;  // 保留时间，状态设为255
            // 尝试原子交换
            if (m_buffer[idx].compare_exchange_weak(old, newVal,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                ok = true;
                return newVal >> 8;   // 返回时间（与 old >> 8 相同）
            }
            // 如果 CAS 失败，old 已被更新为当前值，继续循环
        }
    }
    uint64_t setBuffer_250(uint64_t seq, bool &ok)
    {

        size_t idx = seq % m_bufferSize;
        uint64_t old = m_buffer[idx].load(std::memory_order_acquire);

        while (true) {
            uint8_t status = static_cast<uint8_t>(old & 0xFF);

            if (status >= 250) {
                ok = false;
                return old >> 8;   // 返回时间
            }
            uint64_t newVal = (old & ~0xFFULL) | 250ULL;  // 保留时间，状态设为255
            // 尝试原子交换
            if (m_buffer[idx].compare_exchange_weak(old, newVal,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                ok = true;
                return newVal >> 8;   // 返回时间
            }
        }
    }
    uint64_t setBuffer_200(uint64_t seq, bool &ok)
    {

        size_t idx = seq % m_bufferSize;
        uint64_t old = m_buffer[idx].load(std::memory_order_acquire);

        while (true) {
            uint8_t status = static_cast<uint8_t>(old & 0xFF);

            if (status >= 200) {
                ok = false;
                return old >> 8;   // 返回时间
            }
            uint64_t newVal = (old & ~0xFFULL) | 200ULL;  // 保留时间，状态设为255
            // 尝试原子交换
            if (m_buffer[idx].compare_exchange_weak(old, newVal,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
                ok = true;
                return newVal >> 8;   // 返回时间
            }
        }
    }
    // 获取时间戳（微秒）
    uint64_t getBufferTime(uint64_t seq) const {
        return getBufferRaw(seq) >> 8;
    }

    // 获取状态
    uint8_t getBufferStatus(uint64_t seq) const {
        return static_cast<uint8_t>(getBufferRaw(seq) & 0xFF);
    }

    // 更新状态（不影响时间）
    void updateBufferStatus(uint64_t seq, uint8_t newStatus) {
        size_t index = seq % m_bufferSize;
        uint64_t oldValue = m_buffer[index].load(std::memory_order_acquire);
        uint64_t newValue = (oldValue & ~0xFFULL) | (newStatus & 0xFFULL);
        m_buffer[index].store(newValue, std::memory_order_release);
    }

    // 原子加1（对状态位加1，溢出时饱和为255）
    uint8_t incrementBufferStatus(uint64_t seq) {
        if (m_bufferSize == 0) return 0;  // 防止除零
        size_t index = seq % m_bufferSize;

        uint64_t oldValue = m_buffer[index].load(std::memory_order_acquire);
        uint64_t newValue;
        uint8_t oldStatus, newStatus;

        do {
            oldStatus = static_cast<uint8_t>(oldValue & 0xFF);
            if (oldStatus >= 255) {
                return 255;  // 已饱和
            }
            newStatus = oldStatus + 1;
            newValue = (oldValue & ~0xFFULL) | (newStatus & 0xFFULL);
        } while (!m_buffer[index].compare_exchange_weak(
            oldValue, newValue,
            std::memory_order_acq_rel,
            std::memory_order_acquire));

        return newStatus;
    }
    uint8_t incrementBufferStatus2(uint64_t seq, int value) {
        if (m_bufferSize == 0) return 0;  // 防止除零
        size_t index = seq % m_bufferSize;

        uint64_t oldValue = m_buffer[index].load(std::memory_order_acquire);
        uint64_t newValue;
        uint8_t oldStatus, newStatus;

        do {
            oldStatus = static_cast<uint8_t>(oldValue & 0xFF);
            if (oldStatus >= 255) {
                return 255;  // 已经饱和
            }

            int sum = static_cast<int>(oldStatus) + value;
            if (sum < 0) sum = 0;        // 根据业务调整
            if (sum > 255) sum = 255;    // 饱和到 255
            newStatus = static_cast<uint8_t>(sum);

            newValue = (oldValue & ~0xFFULL) | (newStatus & 0xFFULL);
        } while (!m_buffer[index].compare_exchange_weak(
            oldValue, newValue,
            std::memory_order_acq_rel,
            std::memory_order_acquire));

        return newStatus;
    }
    size_t bufferSize() const { return m_bufferSize; }
    QList<QPair<QString, Message>> getLatestMessagesWithOffset(int appid, int limit, int offset) const;

private:
    // ---- 环形缓冲区成员 ----
    size_t m_bufferSize;
    std::atomic<uint64_t>* m_buffer = nullptr;   // 8 字节原子类型（close() 里释放，open() 重新建）

    // ---- 数据库成员 ----
    QString m_dbPath;
    sqlite3 *m_db = nullptr;         // SQLite 连接（单连接 + m_mutex 串行化）

    // Recursive：openConnection() / flushCacheToDBUnlocked() 内部会重入加锁，
    // 同线程必须能再拿一次，否则自锁。
    mutable QMutex m_mutex{QMutex::Recursive};

    std::atomic<uint64_t> m_nextId{1};

    bool serializeMessage(const Message &msg, QByteArray &data) const;
    bool deserializeMessage(const QByteArray &data, Message &msg) const;

    // 建立/重建连接（建表 + 设 PRAGMA + 恢复最大序号）。已存在会先关掉。
    bool openConnection();
    // 按 seq 直接取（SQLite 里 seq 就是主键，不用拼 key 字符串）
    bool loadBySeq(uint64_t seq, Message &msg) const;
    // 从 makeKey 拼出来的字符串里取回 seq（前 SEQ_DIGITS 位）
    static uint64_t seqFromKey(const QString &keyStr);

    LogTxn m_currentTxn = nullptr;   // 非空 = 有活动事务（只是一个标记，事务在连接上）

    struct CacheEntry {
        Message msg;
        QString appid;      // 落盘时直接用，不必再拆 key
        QString conv;
        uint64_t seq = 0;
        bool dirty = true;  // true = 尚未写入数据库
    };
    mutable QMap<QString, CacheEntry> m_cache;
    QTimer* m_flushTimer = nullptr;   // close() 会删掉，open() 通过 ensureRuntime() 重建

    // ---- 后台清理（单线程池，懒创建）----
    // 用 QThreadPool 而不是自己 new QThread：池是 this 的子对象，
    // 析构会自动 waitForDone，不会出现「线程还在跑、对象已经没了」。
    class QThreadPool *m_cleanPool = nullptr;
    std::atomic<bool> m_cleaning{false};   // 防止定时器 + 打开日志页同时触发两轮清理

    void flushCacheToDB() ;
    void flushCacheToDBUnlocked();   // 不加锁版本
    // 把 m_buffer / m_flushTimer 补齐（close() 会把它们拆掉，open() 要能重建）。可重复调用。
    void ensureRuntime();
    // 缓存积压兜底：超过上限就丢最旧的那批（调用方需已持 m_mutex）
    void trimCacheIfTooLarge();
    // ---- 被动回复 msgid 内存表（2026-11-13）----
    // 原来是反扫日志库 + 每行 deserializeMessage，现在改成收消息时直接登记。
    //   appid -> conv -> {msgId, ts}；每会话只留最新一条（调用方只取 value(0)）
    struct MsgIdEntry {
        QString msgId;
        qint64  ts = 0;      // 登记时间（Unix 秒）
    };
    mutable QHash<QString, QHash<QString, MsgIdEntry>> m_msgIds;
    qint64 m_msgIdSweptAt = 0;   // 上次做过期清理的时间（避免每条消息都全表扫）
    // 禁用拷贝
    LogDB(const LogDB&) = delete;
    LogDB& operator=(const LogDB&) = delete;
};

#endif // LOGDB_H
