#ifndef MEMKV_H
#define MEMKV_H

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>
#include <atomic>

// ============================================================================
// MemKV —— 纯内存 KV 缓存（2026-11-13）
//
// 为什么不用数据库：cache_db 存的**只有上传缓存**（COS 链接 / file_info / 文件指纹），
// 值里自带过期时间（1430 分钟 ≈ 23.8 小时），过了就没人要了。
//   · 条目可枚举上限：key 是文件绝对路径，用户预计不超过 10 万条
//   · 重启丢失可接受：代价只是「同一个文件重新上传一次」
//   · 磁盘持久化带来的是纯负担：WAL 追加、commit、页缓存 miss、全局 m_mutex 串行
//
// 之前的实现（SQLite 单连接 + KvDB::m_mutex）：命中一条也要
//   拿全局锁 → reset → bind → step(B-tree) → 拷贝 → 解锁
// 实测 0.7ms/次，其中真正干活约 2μs，其余全是排队 —— 多线程全卡在这把锁上。
//
// 本类：64 个分片，每片一把 QMutex（Windows 上是 CRITICAL_SECTION，无竞争约 20ns）。
//   读：hash 定片 → 加锁 → QHash 查 → 判过期 → 返回（约 100ns，冲突概率 1/64）
//   内存：10 万条 × 约 500B ≈ 50MB（32 位进程也扛得住）
//
// 值格式沿用原来的 "<过期秒>||||<payload>"：put 时解析出过期秒，get 时自动判过期，
// 调用方（api.cpp 那一堆 cachedValueIfFresh / plugin: 键）一行都不用改。
// ============================================================================
class MemKV
{
public:
    // maxEntries：条目总数上限。**不再整表清空**（2026-11-13）：
    //   按片均摊成「每片上限」，某片满了就在**该片内**按过期时间淘汰最老的一批。
    //   原来超限就整表清空，等于缓存每分钟被抹掉一次 —— 图片上传会变成每条消息都重传
    //   （实测：跑几十分钟后图片耗时从 1ms 涨到 700+ms）。
    explicit MemKV(int maxEntries = 100000);
    ~MemKV() = default;

    // 与 KvDB 同名同签名，api.cpp 的调用点全部零改动
    bool put(const QString &key, const QString &value);
    QString get(const QString &key) const;
    bool remove(const QString &key);
    bool contains(const QString &key) const;

    void clear();
    // main.cpp 退出流程会调 close()（原来是清 KV 库的句柄），这里语义 = 释放内存
    void close() { clear(); }
    bool isOpen() const { return true; }

    // 条目总数（含已过期但还没被 sweepExpired 清掉的）
    int size() const { return m_count.load(std::memory_order_relaxed); }
    int maxEntries() const { return m_maxEntries; }
    int shardCap() const { return m_shardCap; }

    // 定时清理：删掉所有已过期的条目（**不再整表清空**，超限淘汰在 put 里按片做）
    // nowSec <= 0 表示取当前时间
    void sweepExpired(qint64 nowSec = 0);

private:
    struct Entry {
        QString value;
        qint64  expireAt = 0;   // 0 = 永不过期（value 里没有 "||||" 分隔符时）
    };

    static const int kShards = 64;          // 必须是 2 的幂（shardOf 用位与）
    struct Shard {
        mutable QMutex lock;
        QHash<QString, Entry> map;
    };

    static int shardOf(const QString &key);
    // 从 "<过期秒>||||<payload>" 里取出过期秒；取不到返回 0（视为不过期）
    static qint64 parseExpire(const QString &value);

    // 调用方必须已持 s.lock。把该片降到 3/4 容量：优先删**过期时间最早**的
    // （同一 TTL 下 expireAt 最小 = 写入最早），expireAt==0（永不过期）排最后才删。
    void evictLocked(Shard &s);

    Shard m_shards[kShards];
    int   m_maxEntries;
    int   m_shardCap;                       // = maxEntries / kShards（至少 64）
    std::atomic<int> m_count{0};
};

#endif // MEMKV_H
