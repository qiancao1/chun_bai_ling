#include "memkv.h"

#include <QDateTime>
#include <QDebug>
#include <QMutexLocker>
#include <QVector>
#include <algorithm>
#include <limits>

MemKV::MemKV(int maxEntries)
    : m_maxEntries(maxEntries > 0 ? maxEntries : 100000)
{
    // 每片上限：总上限均摊。片满时只在本片内淘汰，**不会整表清空**
    //（整表清空 = 缓存每分钟被抹掉一次，命中率直接归零，见 memkv.h 顶部说明）。
    m_shardCap = qMax(64, m_maxEntries / kShards);
}

int MemKV::shardOf(const QString &key)
{
    return static_cast<int>(qHash(key) & static_cast<uint>(kShards - 1));
}

qint64 MemKV::parseExpire(const QString &value)
{
    // 值形如 "<过期秒>||||<payload>"（秒级时间戳，10 位）
    const int p = value.indexOf(QLatin1String("||||"));
    if (p <= 0 || p > 20) return 0;          // 没分隔符 / 前缀不像时间戳 → 视为不过期
    bool ok = false;
    const qint64 e = value.left(p).toLongLong(&ok);
    return (ok && e > 0) ? e : 0;
}

bool MemKV::put(const QString &key, const QString &value)
{
    if (key.isEmpty()) return false;

    Shard &s = m_shards[shardOf(key)];
    const qint64 exp = parseExpire(value);

    QMutexLocker locker(&s.lock);
    auto it = s.map.find(key);
    if (it != s.map.end()) {
        it.value().value    = value;
        it.value().expireAt = exp;
        return true;
    }

    // 本片满了 → 先腾地方（只动这一片，别的 63 片完全不受影响）
    if (s.map.size() >= m_shardCap)
        evictLocked(s);

    Entry e;
    e.value    = value;
    e.expireAt = exp;
    s.map.insert(key, e);
    m_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// 调用方必须已持 s.lock。
// 按 expireAt 升序淘汰到 3/4 容量：同一 TTL 下 expireAt 最小 = 写入最早，也就是最久没被刷新的。
// expireAt==0（永不过期）当作最大，排最后才删 —— 这类通常是不带过期时间的 plugin: 键，更该留。
// 一次砍到 3/4 是为了摊薄成本：之后要再攒 1/4 片容量的新条目才会再触发一次排序。
void MemKV::evictLocked(Shard &s)
{
    const int target = m_shardCap * 3 / 4;
    if (s.map.size() <= target) return;

    static const qint64 kNeverExpire = std::numeric_limits<qint64>::max();

    QVector<QPair<qint64, QString>> all;
    all.reserve(s.map.size());
    for (auto it = s.map.cbegin(); it != s.map.cend(); ++it) {
        const qint64 e = it.value().expireAt;
        all.append(qMakePair(e == 0 ? kNeverExpire : e, it.key()));
    }
    std::sort(all.begin(), all.end(),
              [](const QPair<qint64, QString> &a, const QPair<qint64, QString> &b) {
                  return a.first < b.first;
              });

    const int drop = all.size() - target;
    for (int i = 0; i < drop; ++i)
        s.map.remove(all.at(i).second);

    m_count.fetch_sub(drop, std::memory_order_relaxed);
}

QString MemKV::get(const QString &key) const
{
    if (key.isEmpty()) return QString();

    const Shard &s = m_shards[shardOf(key)];
    // lock 是 mutable，但 s 是 const 引用 —— QMutexLocker 要非 const 指针，这里转一下
    QMutexLocker locker(const_cast<QMutex *>(&s.lock));

    auto it = s.map.constFind(key);
    if (it == s.map.constEnd()) return QString();

    const Entry &e = it.value();
    if (e.expireAt != 0 && e.expireAt <= QDateTime::currentSecsSinceEpoch())
        return QString();       // 过期 = 未命中；真正删除交给 sweepExpired（读不升级成写锁）

    return e.value;
}

bool MemKV::contains(const QString &key) const
{
    if (key.isEmpty()) return false;

    const Shard &s = m_shards[shardOf(key)];
    QMutexLocker locker(const_cast<QMutex *>(&s.lock));

    auto it = s.map.constFind(key);
    if (it == s.map.constEnd()) return false;

    const Entry &e = it.value();
    return (e.expireAt == 0) || (e.expireAt > QDateTime::currentSecsSinceEpoch());
}

bool MemKV::remove(const QString &key)
{
    if (key.isEmpty()) return false;

    Shard &s = m_shards[shardOf(key)];
    QMutexLocker locker(&s.lock);

    if (s.map.remove(key) > 0) {
        m_count.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void MemKV::clear()
{
    for (int i = 0; i < kShards; ++i) {
        QMutexLocker locker(&m_shards[i].lock);
        m_shards[i].map.clear();
    }
    m_count.store(0, std::memory_order_relaxed);
}

void MemKV::sweepExpired(qint64 nowSec){
    if (nowSec <= 0) nowSec = QDateTime::currentSecsSinceEpoch();

    // 逐片清理：每片只持自己的锁，扫一遍 10 万条约几毫秒，分摊到 64 片每片约几十微秒
    int alive = 0;
    for (int i = 0; i < kShards; ++i) {
        Shard &s = m_shards[i];
        QMutexLocker locker(&s.lock);
        if (s.map.isEmpty()) continue;

        for (auto it = s.map.begin(); it != s.map.end(); ) {
            if (it.value().expireAt != 0 && it.value().expireAt <= nowSec) {
                it = s.map.erase(it);       // QHash::erase 返回下一条
            } else {
                ++alive;
                ++it;
            }
        }
    }
    m_count.store(alive, std::memory_order_relaxed);

    // ⚠ 这里**不再**做「超上限就整表清空」（2026-11-13 删）。
    //   旧逻辑：alive > maxEntries → clear()，等于缓存每分钟被抹一次，
    //   图片上传缓存永远热不起来（每条消息都重传，图片耗时从 1ms 涨到 700+ms）。
    //   现在条目上限由 put() 里的分片淘汰保证（evictLocked），这里只负责清已过期的。
}
