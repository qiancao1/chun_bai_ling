#ifndef LOGDB_H
#define LOGDB_H

#include "chatpage.h"
#include <QObject>
#include <QString>
#include <QList>
#include <QStringList>
#include <QMutex>
#include <atomic>
#include <lmdb.h>

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
        bool getLatestLogInTxn(MDB_txn* txn, const QString& appid, const QString& groupId, Message& msg) const;
    bool getLatestLog(const QString &appid, const QString &groupId, Message &msg) const;
    MDB_txn* getCurrentTxn() const { return m_currentTxn; }
    // 使用外部已有事务进行读取（不自动开启事务）
    bool readLogInTxn(MDB_txn* txn, const QString& appid, const QString& groupId,
                      uint64_t seq, Message& msg) const;
    // 添加日志，返回分配的序号（全局唯一递增），失败返回 0
    uint64_t appendLog(const QString &appid, const QString &groupId, const Message &msg);

    // 获取指定 appid:groupId 最近 N 条（按序号从大到小，即最新在前）
    QList<Message> getRecentLogs(const QString &appid, const QString &groupId, int N) const;

    // 获取所有 key（格式 "appid:groupId:序号"）的列表
    QStringList getAllKeys() const;
    QStringList getLatestKeys(int N) const;

    // 修改指定 key 对应的 Message
    bool updateLog(const QString &appid, const QString &groupId, uint64_t seq, const Message &msg);

    // 读取指定 key 对应的 Message
    bool readLog(const QString &appid, const QString &groupId, uint64_t seq, Message &msg) const;

    // 清理数据库，保留全局最新 N 条，删除其余
    bool cleanDatabase(int keepN);


    void setBufferRaw(uint64_t seq, uint64_t value) {
        m_buffer[seq % m_bufferSize].store(value, std::memory_order_release);
    }
    // 读取完整的 64 位值
    uint64_t getBufferRaw(uint64_t seq) const {
        return m_buffer[seq % m_bufferSize].load(std::memory_order_acquire);
    }

    // 便捷接口：设置时间戳（微秒）和状态（1字节）
    void setBufferTimeAndStatus(uint64_t seq, uint64_t timeUs, uint8_t status) {
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
        size_t index = seq % m_bufferSize;
        uint64_t oldValue = m_buffer[index].load(std::memory_order_acquire);
        uint8_t oldStatus = static_cast<uint8_t>(oldValue & 0xFF);
        if (oldStatus >= 255) return 255;
        uint8_t newStatus = oldStatus + 1;
        uint64_t newValue = (oldValue & ~0xFFULL) | (newStatus & 0xFFULL);
        m_buffer[index].store(newValue, std::memory_order_release);
        return newStatus;
    }

    size_t bufferSize() const { return m_bufferSize; }
    QList<QPair<QString, Message>> getLatestMessagesWithOffset(int appid, int limit, int offset) const;

private:
    // ---- 环形缓冲区成员 ----
    size_t m_bufferSize;
    std::atomic<uint64_t>* m_buffer;   // 改为 8 字节原子类型

    // ---- 数据库成员 ----
    QString m_dbPath;
    MDB_env *m_env;
    MDB_dbi m_dbi_main;
    MDB_dbi m_dbi_meta;
    mutable QMutex m_mutex;

    // 辅助函数
    QString makeKey(const QString &appid, const QString &groupId, uint64_t seq) const;
    bool getNextId(uint64_t &nextId) const;
    bool updateNextId(uint64_t nextId);
    bool serializeMessage(const Message &msg, QByteArray &data) const;
    bool deserializeMessage(const QByteArray &data, Message &msg) const;

    MDB_txn* m_currentTxn;   // 当前活动事务（由 beginTransaction 创建）

    // 禁用拷贝
    LogDB(const LogDB&) = delete;
    LogDB& operator=(const LogDB&) = delete;
};

#endif // LOGDB_H