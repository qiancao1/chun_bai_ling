/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "botdb.h"
#include "global.h"
#include "subdb.h"      // 订阅数据已搬到 SQLite（见 subdb.h 顶部说明）
#include <QDir>
#include <QDebug>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <QTimer>


BotDB::BotDB(const QString& path, size_t initialMapSizeMB)
    : m_path(path), m_initialMapSize(initialMapSizeMB * 1024ULL * 1024ULL),
    m_currentMapSize(0), m_env(nullptr), m_dbi_users(0), m_dbi_seq_idx(0), m_dbi_groups(0), m_dbi_friends(0)
{
    if (m_initialMapSize < 1ULL * 1024 * 1024)   // 最小 1MB
        m_initialMapSize = 1ULL * 1024 * 1024;
    // 在 open() 成功后
    m_cacheTimer = new QTimer ;
    connect(m_cacheTimer, &QTimer::timeout, this, &BotDB::checkCleanup);
    m_cacheTimer->start(5 * 60 * 1000);  // 5分钟检查一次

    // 计数保存定时器：每10分钟保存一次
    m_saveTimer = new QTimer ;
    connect(m_saveTimer, &QTimer::timeout, this, &BotDB::saveDailyStats);
    m_saveTimer->start(10 * 60 * 1000); // 10分钟
    loadDailyStats();            // 加载当日统计
}

BotDB::~BotDB()
{
    close();
}
void BotDB::checkCleanup()
{
    static qint64 lastUserClean = 0;
    static qint64 lastGroupClean = 0;
    qint64 now = QDateTime::currentSecsSinceEpoch();

    // 每4小时清理用户缓存
    if (now - lastUserClean >= 4 * 3600) {
        cleanUserCache();
        lastUserClean = now;
    }
    // 每24小时清理群缓存
    if (now - lastGroupClean >= 24 * 3600) {
        cleanGroupCache();
        lastGroupClean = now;
    }
}

void BotDB::cleanUserCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_userCache.clear();     // 直接清空，下次访问会从LMDB重新加载

}

void BotDB::cleanGroupCache()
{
    QMutexLocker locker(&m_cacheMutex);
    for (auto it = m_groupCache.begin(); it != m_groupCache.end(); ) {
        const GroupRecord2& rec = it.value();
        if (rec.jojnyz.isEmpty() && rec.jojntime.isEmpty()) {
            it = m_groupCache.erase(it);
        } else {
            ++it;
        }
    }
}

// 设置页「重载数据库」按钮用：把用户/群两份内存缓存整个丢掉，下次访问按需从 LMDB 重读。
// ⚠ 群缓存**不能无脑全清**：jojnyz / jojntime（待入群验证名单）是纯内存数据、不入 LMDB
//   （见 getOrUpdateUser 里「直接覆盖 m_groupCache 会丢掉整份名单」那段注释），
//   清掉就等于把正在验证的人全丢了。所以这里和 cleanGroupCache 一样，只删没有验证名单的群
//   —— 实际上几乎所有群都属于这一类，省下来的内存没区别。
void BotDB::clearAllCaches()
{
    QMutexLocker locker(&m_cacheMutex);
    m_userCache.clear();
    for (auto it = m_groupCache.begin(); it != m_groupCache.end(); ) {
        const GroupRecord2 &rec = it.value();
        if (rec.jojnyz.isEmpty() && rec.jojntime.isEmpty()) {
            it = m_groupCache.erase(it);
        } else {
            ++it;
        }
    }
}
// 在 Windows 上尝试将 data.mdb 设为稀疏文件
bool BotDB::ensureSparseFile()
{
#ifdef Q_OS_WIN
    if (!m_env) return false;
    QString dataFile = m_path + "/data.mdb";
    HANDLE hFile = CreateFileW((LPCWSTR)dataFile.utf16(),
                               GENERIC_WRITE,
                               FILE_SHARE_WRITE | FILE_SHARE_READ,
                               NULL,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;
    DWORD dummy;
    BOOL result = DeviceIoControl(hFile, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &dummy, NULL);
    CloseHandle(hFile);
    return result != 0;
#else
    return true;
#endif
}

namespace {
// 进入/退出「换环境」临界区（RAII，异常/提前 return 都安全）
struct EnvChangeGuard {
    explicit EnvChangeGuard(BotDB *db) : m_db(db) { if (m_db) m_db->beginEnvChange(); }
    ~EnvChangeGuard() { if (m_db) m_db->endEnvChange(); }
    EnvChangeGuard(const EnvChangeGuard &) = delete;
    EnvChangeGuard &operator=(const EnvChangeGuard &) = delete;
private:
    BotDB *m_db;
};
} // namespace

bool BotDB::open(size_t mapSizeOverride)
{
    QMutexLocker locker(&m_mutex);
    // 整段「建环境 + 打开」都在换环境临界区里：mdb_env_create() 会先把 m_env 填成非空，
    // 但 mdb_env_open() 还没跑（me_metas 为空），读线程这时插进来 mdb_txn_begin 必崩。
    EnvChangeGuard envChange(this);

    if (m_env) close();

    QDir dir;
    if (!dir.mkpath(m_path)) {
        qCritical() << "无法创建数据库目录:" << m_path;
        return false;
    }

    // 0 = 首次打开（用构造时给的大小）；非 0 = 每日重载，沿用当前已经涨到的 mapsize，
    // 否则会退回 8MB 再一路 MAP_FULL 翻倍，白白多几次 close/open。
    m_currentMapSize = mapSizeOverride ? mapSizeOverride : m_initialMapSize;

    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_create 失败:" << mdb_strerror(rc);
        return false;
    }
    mdb_env_set_maxdbs(m_env, 10);
    mdb_env_set_mapsize(m_env, m_currentMapSize);
    QByteArray pathBytes = m_path.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), MDB_WRITEMAP | MDB_NOMETASYNC, 0664);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_open 失败:" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    // 设置为稀疏文件（Windows）
    ensureSparseFile();

    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_txn_begin 失败:" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    // 打开子数据库（注意：此处如果之前已经存在，会复用）
    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &m_dbi_users);
    if (rc != MDB_SUCCESS) goto fail;
    rc = mdb_dbi_open(txn, "seq_to_openid", MDB_CREATE, &m_dbi_seq_idx);
    if (rc != MDB_SUCCESS) goto fail;
    rc = mdb_dbi_open(txn, "groups", MDB_CREATE, &m_dbi_groups);
    if (rc != MDB_SUCCESS) goto fail;
    rc = mdb_dbi_open(txn, "friends", MDB_CREATE, &m_dbi_friends);
    if (rc != MDB_SUCCESS) goto fail;
    rc = mdb_dbi_open(txn, "subscriptions", MDB_CREATE, &m_dbi_subscriptions);
    if (rc != MDB_SUCCESS) goto fail;


    rc = mdb_dbi_open(txn, "account_stats", MDB_CREATE, &m_dbi_account_stats);
    if (rc != MDB_SUCCESS) goto fail;

    // 积分（商店货币）子库：与 users 同时创建，老库升级上来也会自动建好（空库 = 全员 0 分）
    rc = mdb_dbi_open(txn, "points", MDB_CREATE, &m_dbi_points);
    if (rc != MDB_SUCCESS) goto fail;

    // 签到记录子库：老库升级上来自动建好（空库 = 谁都没签过）
    rc = mdb_dbi_open(txn, "signs", MDB_CREATE, &m_dbi_signs);
    if (rc != MDB_SUCCESS) goto fail;


    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "提交事务失败:" << mdb_strerror(rc);
        // commit 失败事务已终止，不能 abort；但必须回收 env，否则句柄泄漏
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }
    //AppendEventLog( "数据库已打开，目录:" + m_path + "初始mapsize:" + QString::number((m_currentMapSize >> 20)) + "MB");

    // 订阅数据搬到 SQLite（每个账号库只搬一次，标记在 subdb 的 meta 表里）。
    // 必须在 open() 里做：订阅的读写在账号库打开后随时可能发生，迁移要是晚一步，
    // 就会「库里明明有订阅，listSubscriptions 却返回空」（读的是空的 SQLite）。
    migrateSubscriptions();

    return true;

fail:
    mdb_txn_abort(txn);
    qCritical() << "打开子数据库失败:" << mdb_strerror(rc);
    mdb_env_close(m_env);
    m_env = nullptr;
    return false;
}




// ==================== m_env 生命周期租约 ====================
// 背景（崩溃现场）：mdb_txn_renew+0x80 → mov rdx,[rax+80h]，rax=0。
// 那是 mdb_env_pick_meta 里 `metas[0]->mm_txnid < metas[1]->mm_txnid`：环境已经被
// mdb_env_close()（me_metas 置空）或者 mdb_env_create() 之后还没 mdb_env_open()（me_metas
// 尚未填），却被别的线程拿去做 mdb_txn_begin。BotDB 的读函数（getUserBySeqId /
// getGroupInfo / isFriend / getPoints …）原来只判 `if (!m_env)` 就直接 mdb_txn_begin，
// 与 close() / 扩容 reopenEnvironment() 之间没有任何同步 → 这就是那个空指针。
//
// 解法：读操作进来先领一张「租约」把 m_env 钉住；关/换环境时先拒绝新租约，再等
// 已有租约全部归还，之后才动 mdb_env_close()。写路径仍然只靠 m_mutex 串行（不加租约，
// 否则扩容时自己持着租约会把自己等死）。

bool BotDB::leaseEnv() const
{
    QMutexLocker locker(&m_envLeaseMutex);
    if (m_envChangeDepth > 0 || !m_env) return false;   // 正在换环境 / 已经关了
    ++m_envUsers;
    return true;
}

void BotDB::releaseEnv() const
{
    QMutexLocker locker(&m_envLeaseMutex);
    if (m_envUsers > 0) --m_envUsers;
    if (m_envUsers == 0) m_envIdleCond.wakeAll();
}

void BotDB::beginEnvChange()
{
    QMutexLocker locker(&m_envLeaseMutex);
    ++m_envChangeDepth;                                  // 可重入（close 里再 close）
    while (m_envUsers > 0)
        m_envIdleCond.wait(&m_envLeaseMutex);            // 等正在读的线程收工
}

void BotDB::endEnvChange()
{
    QMutexLocker locker(&m_envLeaseMutex);
    if (m_envChangeDepth > 0) --m_envChangeDepth;
    if (m_envChangeDepth == 0) m_envIdleCond.wakeAll();
}

// 关闭环境并释放所有句柄
void BotDB::close()
{
  {
    // 和写事务（retryWrite 全程持 m_mutex）互斥；扩容路径是在持锁状态下重入本函数的，
    // 所以 m_mutex 必须是 Recursive（见 botdb.h）。
    QMutexLocker locker(&m_mutex);

    // 先「关门」再「拆房子」：拒绝新租约 + 等所有正在读的线程退出。
    // 否则 mdb_env_close() 会把别人正踩着的 MDB_env 一起释放，下一句 mdb_txn_begin
    // 就会崩在 mdb_env_pick_meta（metas[0] 已被置空 → 读 [+0x80] 空指针）。
    EnvChangeGuard envChange(this);

    if (m_env) {
        // 关闭所有已打开的 DBI（必须在 mdb_env_close 之前，且此时 m_env 仍有效）
        if (m_dbi_users) mdb_dbi_close(m_env, m_dbi_users);
        if (m_dbi_seq_idx) mdb_dbi_close(m_env, m_dbi_seq_idx);
        if (m_dbi_groups) mdb_dbi_close(m_env, m_dbi_groups);
        if (m_dbi_friends) mdb_dbi_close(m_env, m_dbi_friends);
        if (m_dbi_subscriptions) mdb_dbi_close(m_env, m_dbi_subscriptions);
        if (m_dbi_account_stats) mdb_dbi_close(m_env, m_dbi_account_stats);
        if (m_dbi_points) mdb_dbi_close(m_env, m_dbi_points);
        if (m_dbi_signs) mdb_dbi_close(m_env, m_dbi_signs);
        mdb_env_close(m_env);
        m_env = nullptr;
    }
    // 所有 dbi 句柄统一作废，避免扩容重开后沿用陈旧句柄
    m_dbi_users = 0;
    m_dbi_seq_idx = 0;
    m_dbi_groups = 0;
    m_dbi_friends = 0;
    m_dbi_subscriptions = 0;
    m_dbi_account_stats = 0;
    m_dbi_points = 0;
    m_dbi_signs = 0;
  }   // ← m_mutex 只罩到环境拆完为止，后面拿 m_cacheMutex/m_msgMutex 时不再持它（避免锁序交叉）

    if (m_saveTimer) {
        m_saveTimer->stop();
        saveDailyStats();
    }

    // 环境已关闭，缓存全部作废，避免下次读到「库里其实没有」的陈旧记录
    {
        QMutexLocker locker(&m_cacheMutex);
        m_userCache.clear();
        m_groupCache.clear();
    }
}

// 每日 05:00 重载（声明处 botdb.h 有大段说明）：close() + open() 一次，把整份 LMDB 映射
// 还给系统，再按**当前 mapsize** 重开。代价是两份内存缓存被清空（下次访问按需从库里重读）。
bool BotDB::reload()
{
    // 1) 先快照「待入群验证名单」：jojnyz / jojntime 是纯内存数据、根本不进 LMDB
    //    （见 getOrUpdateUser 里那段注释），而 close() 会把 m_groupCache 整个清掉。
    //    不快照就等于每天 05:00 把正在验证的人全丢掉。
    QHash<BinKey, GroupRecord2> pendingGroups;
    {
        QMutexLocker locker(&m_cacheMutex);
        for (auto it = m_groupCache.constBegin(); it != m_groupCache.constEnd(); ++it) {
            if (!it.value().jojnyz.isEmpty() || !it.value().jojntime.isEmpty())
                pendingGroups.insert(it.key(), it.value());
        }
    }

    const size_t keepMapSize = m_currentMapSize ? m_currentMapSize : m_initialMapSize;

    // 2) 关环境：内部会 stop 并 saveDailyStats()（当日消息计数落盘）、等所有读租约退出、
    //    然后 mdb_env_close() —— 这一步才是真正把工作集里的映射页交还给系统。
    close();

    // 3) 按原来的 mapsize 重开（不传就会退回 m_initialMapSize）
    const bool ok = open(keepMapSize);

    // 4) 回填验证名单（跟库有没有开成功无关，内存状态照旧保留）
    if (!pendingGroups.isEmpty()) {
        QMutexLocker locker(&m_cacheMutex);
        for (auto it = pendingGroups.constBegin(); it != pendingGroups.constEnd(); ++it)
            m_groupCache.insert(it.key(), it.value());
    }

    // 5) close() 把保存定时器停了，open() 不会重启 —— 不拉起来「当天计数」就再也不落盘
    if (m_saveTimer && !m_saveTimer->isActive())
        m_saveTimer->start(10 * 60 * 1000);

    if (ok)
        qDebug().noquote() << QStringLiteral("BotDB 每日重载成功 %1（mapsize %2MB，保留验证名单 %3 个群）")
                                  .arg(m_path).arg(m_currentMapSize >> 20).arg(pendingGroups.size());
    else
        qCritical().noquote() << QStringLiteral("BotDB 每日重载失败 %1 —— 数据库现在是关闭状态，写入会全部失败")
                                     .arg(m_path);
    return ok;
}

// 重新打开环境（扩容后调用），保持原有的 m_currentMapSize
bool BotDB::reopenEnvironment()
{
    QMutexLocker locker(&m_mutex);
    // 换环境全流程（close → create → open → 重开子库）都在临界区里，
    // 这期间任何读线程都领不到租约，只能空手返回。
    EnvChangeGuard envChange(this);

    if (m_env) close();

    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) return false;

    mdb_env_set_maxdbs(m_env, 10);
    mdb_env_set_mapsize(m_env, m_currentMapSize);
    QByteArray pathBytes = m_path.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), MDB_WRITEMAP | MDB_NOMETASYNC, 0664);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    ensureSparseFile();

    // 重新打开所有子数据库（必须与 open() 保持一致，漏开会导致句柄为 0/陈旧）
    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &m_dbi_users);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "seq_to_openid", MDB_CREATE, &m_dbi_seq_idx);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "groups", MDB_CREATE, &m_dbi_groups);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "friends", MDB_CREATE, &m_dbi_friends);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "subscriptions", MDB_CREATE, &m_dbi_subscriptions);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "account_stats", MDB_CREATE, &m_dbi_account_stats);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "points", MDB_CREATE, &m_dbi_points);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "signs", MDB_CREATE, &m_dbi_signs);
    if (rc != MDB_SUCCESS) goto reopen_fail;

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        // commit 失败时事务已终止，不能再 abort，否则是未定义行为
        txn = nullptr;
        goto reopen_fail;
    }
    txn = nullptr;

    qDebug() << "扩容后重新打开环境成功，新 mapsize:" << (m_currentMapSize >> 20) << "MB";
    return true;

reopen_fail:
    if (txn) mdb_txn_abort(txn);
    close();
    return false;
}

// 翻倍扩容
bool BotDB::increaseMapSize()
{
    if (!m_env) return false;

    size_t newSize = m_currentMapSize * 2;
    // 防止溢出：最大限制为 16TB（可根据需求调整）
    const size_t MAX_MAP_SIZE = 16ULL * 1024 * 1024 * 1024 * 1024; // 16TB
    if (newSize > MAX_MAP_SIZE) {
        qCritical() << "mapsize 超过最大限制，无法继续扩容";
        return false;
    }

    qDebug() << "LMDB 空间不足，正在扩容:" << (m_currentMapSize >> 20) << "MB ->" << (newSize >> 20) << "MB";

    // 关闭当前环境（会提交未完成事务？注意：调用此函数时外部已经回滚了失败的事务）
    // 我们直接调用 reopenEnvironment 会调用 close()，然后使用新的 mapsize 打开
    m_currentMapSize = newSize;
    return reopenEnvironment();
}

// 泛型写入重试器

template<typename Func>
bool BotDB::retryWrite(Func writeFunc, int maxRetries)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    int retry = 0;
    while (retry <= maxRetries) {
        MDB_txn *txn = nullptr;

        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);

        if (rc != MDB_SUCCESS) return false;  // begin失败，无事务需释放

        bool needAbort = true;   // 标记是否需要手动abort
        try {
            int opResult = writeFunc(txn);

            if (opResult == MDB_SUCCESS) {
                rc = mdb_txn_commit(txn);
                if (rc == MDB_SUCCESS) {
                    needAbort = false;   // 提交成功，不再abort
                    return true;
                }
                // commit失败，根据LMDB文档事务已自动回滚，不应再调用abort
                // 但为了释放事务资源，我们仍需要将txn置为无效（无需调用abort）
                // 所以这里保持 needAbort = true，但不调用abort？不行，因为事务已回滚，调用abort是未定义行为。
                // 正确做法：对于commit失败，我们既不能abort，但资源已由LMDB释放，所以直接将needAbort设为false。
                needAbort = false;   // 关键：commit失败后事务已终止，不应再abort
                if (rc == MDB_MAP_FULL) {
                    if (!increaseMapSize()) return false;
                    retry++;
                    continue;
                }
                return false;
            } else if (opResult == MDB_MAP_FULL) {
                // writeFunc返回MAP_FULL，事务仍活跃，需要abort
                mdb_txn_abort(txn);
                needAbort = false;   // 已abort，不再重复
                if (!increaseMapSize()) return false;
                retry++;
                continue;
            } else {
                // 其他错误，手动abort
                mdb_txn_abort(txn);
                needAbort = false;
                return false;
            }
        } catch (...) {
            // 异常发生，事务仍活跃，必须abort
            mdb_txn_abort(txn);
            throw;   // 或 return false，取决于您的错误处理策略
        }

        // 如果needAbort为true（理论上不会到达这里，但以防万一）
        if (needAbort) {
            mdb_txn_abort(txn);
        }
    }
    return false;
}

// ---------- 内部辅助函数（未修改，但注意 putRecord 等返回 int 以便重试器使用）----------
uint32_t BotDB::nowMinutes()
{
    return static_cast<uint32_t>(std::time(nullptr) / 60);
}

// 修改 putRecord，返回 int (LMDB 错误码)
int BotDB::putRecord(MDB_txn *txn, MDB_dbi dbi, const QByteArray &keyData, const void *data, size_t size)
{
    MDB_val key, value;
    key.mv_data = (void*)keyData.constData();
    key.mv_size = keyData.size();
    value.mv_data = (void*)data;
    value.mv_size = size;
    return mdb_put(txn, dbi, &key, &value, 0);
}

bool BotDB::getRecord(MDB_txn *txn, MDB_dbi dbi, const QByteArray &keyData, void *outData, size_t size, int *outRc)
{
    MDB_val key, value;
    key.mv_data = (void*)keyData.constData();
    key.mv_size = keyData.size();
    int rc = mdb_get(txn, dbi, &key, &value);
    if (outRc) *outRc = rc;
    if (rc == MDB_SUCCESS) {
        size_t copySize = std::min<size_t>(value.mv_size, size);
        memcpy(outData, value.mv_data, copySize);
        if (copySize < size) {
            memset(static_cast<char*>(outData) + copySize, 0, size - copySize);
        }
        return true;
    }
    return false;
}

int BotDB::delRecord(MDB_txn *txn, MDB_dbi dbi, const QByteArray &keyData)
{
    MDB_val key;
    key.mv_data = (void*)keyData.constData();
    key.mv_size = keyData.size();
    return mdb_del(txn, dbi, &key, nullptr);
}

uint32_t BotDB::getNextSeqId(MDB_txn *txn)
{
    const char *seqKey = "_next_seq_id";
    MDB_val key, value;
    key.mv_data = (void*)seqKey;
    key.mv_size = strlen(seqKey) + 1;

    uint32_t nextId = 1;
    int rc = mdb_get(txn, m_dbi_seq_idx, &key, &value);
    if (rc == MDB_SUCCESS) {
        nextId = *(uint32_t*)value.mv_data + 1;
        if (nextId == 0) return 0;
    } else if (rc != MDB_NOTFOUND) {
        return 0;
    }

    value.mv_data = &nextId;
    value.mv_size = sizeof(nextId);
    rc = mdb_put(txn, m_dbi_seq_idx, &key, &value, 0);
    return (rc == MDB_SUCCESS) ? nextId : 0;
}

bool BotDB::saveSeqToOpenId(MDB_txn *txn, uint32_t seqId, const QByteArray &openidBin)
{
    QByteArray keyData((char*)&seqId, sizeof(seqId));
    return putRecord(txn, m_dbi_seq_idx, keyData, openidBin.constData(), openidBin.size()) == MDB_SUCCESS;
}

bool BotDB::getOpenIdBySeq(MDB_txn *txn, uint32_t seqId, QByteArray &outOpenidBin)
{
    QByteArray keyData((char*)&seqId, sizeof(seqId));
    MDB_val key, value;
    key.mv_data = keyData.data();
    key.mv_size = keyData.size();
    int rc = mdb_get(txn, m_dbi_seq_idx, &key, &value);
    if (rc == MDB_SUCCESS) {
        outOpenidBin = QByteArray((const char*)value.mv_data, value.mv_size);
        return true;
    }
    return false;
}

// ---------- 公开 API 实现（全部使用 retryWrite）----------

static QString fetchGroupNameFromApi(QQBotClient *qqbot, const QString &groupIdHex)
{
    QString json = qqbot->get_groups_info(groupIdHex);
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError) {
        QJsonObject obj = doc.object();
        return obj["group_name"].toString();
    }
    return QString();
}
uint32_t BotDB::getOrUpdateUser(const QString &openid, QString &name)
{
    QString review_name;
    return getOrUpdateUser(openid, name, review_name);
}

uint32_t BotDB::getOrUpdateUser(const QString &openid, QString &name, QString &review_name)
{
    uint32_t resultSeq = 0;
    UserRecord finalUser{};

    review_name.clear();   // 纯输出参数，没查到时保持空串

    QByteArray userKeyBytes = QByteArray::fromHex(openid.toUtf8());
    BinKey userKey{};
    memcpy(userKey.data, userKeyBytes.constData(),
           std::min<size_t>(userKeyBytes.size(), sizeof(userKey.data)));

    // ---------- 缓存命中：必须把昵称回填给调用方 ----------
    // ⚠ 旧实现在这里直接 return，name 从来没被写过：只要该用户进过一次缓存
    // （发过一次消息就会进），调用方拿到的昵称就恒为空串 ——
    // API_ID_GET_USER_NAME 与 {{name}} 替换全部失效。
    {
        QMutexLocker locker(&m_cacheMutex);
        auto itUser = m_userCache.find(userKey);
        if (itUser != m_userCache.end()) {
            const UserRecord &cachedUser = itUser.value();
            review_name = QString::fromUtf8(cachedUser.name);
            if (name.isEmpty()) {
                name = QString::fromUtf8(cachedUser.nickname);  // 库里的 nickname 保证是干净的 UTF-8
                return cachedUser.seq_id;
            }
            // 传入了新昵称：与缓存一致就不必再写库，否则落到下面的写事务去更新
            if (strcmp(cachedUser.nickname, name.toUtf8().constData()) == 0) {
                return cachedUser.seq_id;
            }
        }
    }


    bool success = retryWrite([&](MDB_txn *txn) -> int {
        QByteArray keyData = QByteArray::fromHex(openid.toUtf8());
        if (keyData.isEmpty()) return -1;

        MDB_val key, value;
        key.mv_data = keyData.data();
        key.mv_size = keyData.size();

        int rc = mdb_get(txn, m_dbi_users, &key, &value);
        if (rc == MDB_NOTFOUND) {

            return -1;
        } else if (rc == MDB_SUCCESS) {
            UserRecord record;
            // 按实际长度读取：旧格式记录只有 80 字节，直接 memcpy(sizeof(UserRecord))
            // 会越界读 48 字节垃圾，并把这些垃圾当作正式昵称 name 写回去。
            memset(&record, 0, sizeof(record));
            if (value.mv_size == sizeof(UserRecord)) {
                memcpy(&record, value.mv_data, sizeof(UserRecord));
            } else if (value.mv_size == 80) {
                const char *data = static_cast<const char*>(value.mv_data);
                memcpy(&record.seq_id, data, 4);
                memcpy(&record.bitmap, data + 4, 4);
                memcpy(&record.record_time, data + 8, 4);
                memcpy(&record.invited_group_count, data + 12, 4);
                memcpy(record.nickname, data + 16, 64);
                record.nickname[63] = '\0';
                record.name[0] = '\0';
            } else {
                return -1;
            }

            if (name.isEmpty()) {
                name = QString::fromUtf8(record.nickname);   // 数据库中的 nickname 保证是干净的 UTF-8
                resultSeq = record.seq_id;
                finalUser = record;

                return MDB_SUCCESS;
            }

            QByteArray newNameBytes = name.toUtf8();
            if (strcmp(record.nickname, newNameBytes.constData()) != 0) {
                size_t copyLen = std::min<size_t>(63, (size_t)newNameBytes.size());
                memcpy(record.nickname, newNameBytes.constData(), copyLen);
                record.nickname[copyLen] = '\0';
                record.record_time = nowMinutes();
                rc = putRecord(txn, m_dbi_users, keyData, &record, sizeof(record));
                if (rc != MDB_SUCCESS) return rc;
            }
            resultSeq = record.seq_id;
            finalUser = record;
            return MDB_SUCCESS;
        }
        return rc;
    });

    if (success && resultSeq != 0) {
        review_name = QString::fromUtf8(finalUser.name);   // 审核后的正式昵称，可能为空
        // 回填缓存：否则上面那条「缓存命中」分支会一直拿着旧昵称，
        // 每次调用都判定「昵称不同」→ 反复写库。
        // 这里直接用零初始化的 BinKey 写 m_userCache，不走 updateUserCache()
        // （后者内部是 memcpy(key.data, openidBin, 16)，openid 不足 16 字节时会越界读）。
        QMutexLocker locker(&m_cacheMutex);
        m_userCache[userKey] = finalUser;
    }

    return success ? resultSeq : 0;
}
uint32_t BotDB::getOrUpdateUser(QQBotClient *qqbot, MessageEvent &ev, bool hc)
{

    bool isGroup = (ev.type == 0);
    if (!isGroup) isGroup = (ev.type == 18);

    QByteArray userKeyBytes = QByteArray::fromHex(ev.user.toUtf8());
    BinKey userKey{};   // 零初始化：openid 不足 16 字节时不会留下未初始化字节
    memcpy(userKey.data, userKeyBytes.constData(),
           std::min<size_t>(userKeyBytes.size(), sizeof(userKey.data)));

    BinKey groupKey{};
    if (isGroup) {
        QByteArray groupKeyBytes = QByteArray::fromHex(ev.groupId.toUtf8());
        memcpy(groupKey.data, groupKeyBytes.constData(),
               std::min<size_t>(groupKeyBytes.size(), sizeof(groupKey.data)));
    }

    // 每日消息计数（不变）
    {
        QMutexLocker locker(&m_msgMutex);
        checkDayChange();
        m_userDailyMsg[userKey] = m_userDailyMsg.value(userKey, 0) + 1;
        if (isGroup) {
            m_groupDailyMsg[groupKey] = m_groupDailyMsg.value(groupKey, 0) + 1;
        }
        auto *info = qqbot-> m_info;
        info->message_received++;
        info->received++;
        info->received_day++;
    }

    // 缓存相关
    bool userCacheHit = false;
    bool groupCacheHit = false;
    UserRecord cachedUser{};
    GroupRecord2 cachedGroup{}; // [修改] 原为 GroupRecord cachedGroup{};

    // LMDB 读取结果
    bool userExists = false;
    bool groupExists = false;
    UserRecord oldUser{};
    GroupRecord oldGroup{};
    uint32_t oldSeqId = 0;

    // 决策变量
    bool needUpdateGroup = false;
    bool needUpdateUser = false;
    bool needUpdateBitmap = false;
    QString newGroupName;

    // 写事务结果
    uint32_t resultSeq = 0;
    QString finalGroupName;
    int finalBitmap = 0;
    QString finalUserNickname,finalUserNickname2;
    UserRecord finalUser{};
    GroupRecord finalGroup{};

    // ======================== 2. 尝试从缓存读取 ========================
    {
        QMutexLocker locker(&m_cacheMutex);
        auto itUser = m_userCache.find(userKey);
        if (itUser != m_userCache.end()) {
            cachedUser = itUser.value();
            userCacheHit = true;
        }
        if (isGroup) {
            auto itGroup = m_groupCache.find(groupKey);
            if (itGroup != m_groupCache.end()) {
                cachedGroup = itGroup.value();   // GroupRecord2
                groupCacheHit = true;
            }
        }
    }


    // ======================== 3. 如果缓存命中，基于缓存数据做决策 ========================
    if (userCacheHit) {
        // ---------- 用户缓存命中 ----------
        oldUser = cachedUser;          // UserRecord 是 POD，拷贝一次
        userExists = true;
        oldSeqId = cachedUser.seq_id;

        // ---------- 群缓存处理 ----------
        if (isGroup) {
            if (groupCacheHit) {
                groupExists = true;
                // 注意：cachedGroup 是 GroupRecord2，直接使用其字段
            } else {
                groupExists = false;
            }
        }

        // ---------- 决策：群名是否需要更新 ----------
        if (isGroup && hc == false) {
            if (!groupExists) {
                needUpdateGroup = true;
                newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
            } else {
                const qint64 ONE_MONTH = 30 * 24 * 3600;
                qint64 now = time(nullptr);
                // 直接使用 cachedGroup.ncgx（无需转换）
                if (cachedGroup.ncgx == 0 || (now - cachedGroup.ncgx > ONE_MONTH)) {
                    needUpdateGroup = true;
                    newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
                }
            }
        }

        // ---------- 决策：管理员位是否需要更新 ----------
        if (isGroup && groupExists && ev.at_you) {
            bool currentAdmin = (cachedGroup.bitmap & BIT_ADMIN) != 0;
            if (currentAdmin != ev.bot_admin) {
                needUpdateBitmap = true;
            }
        }

        // ---------- 决策：用户昵称是否需要更新 ----------
        if (!ev.nickname.isEmpty()) {
            QByteArray newName = ev.nickname.toUtf8();
            if (strcmp(oldUser.nickname, newName.constData()) != 0)
                needUpdateUser = true;
        }

        // ---------- 如果无需任何更新，直接返回缓存数据（零转换） ----------
        if (!needUpdateGroup && !needUpdateUser && !needUpdateBitmap) {
            if (isGroup && groupExists) {
                ev.groupname = cachedGroup.name;          // QString 共享，不复制字符
                ev.bitmap = cachedGroup.bitmap;
                memcpy(ev.qid, cachedGroup.qid, sizeof(ev.qid));
            }
            // 用户昵称
            ev.nickname = QString::fromUtf8(oldUser.nickname);
            ev.nickname2 = QString::fromUtf8(oldUser.name);
            if (!isGroup) {
                ev.bitmap = 0;
                memset(ev.qid, 0, sizeof(ev.qid));
            }
            if(ev.type == 0 && ev.subType==1)
            {
                for(int i=0;i<cachedGroup.jojnyz.size();++i)
                {
                    if(oldSeqId == cachedGroup.jojnyz[i])
                    {
                        cachedGroup.jojnyz.removeAt(i);
                        cachedGroup.jojntime.removeAt(i);

                        auto *c = m_botClients[ev.appid];
                        c->setGroupRestrictChatSetting(ev.groupId,ev.user,0,[](auto,auto){});
                        QString text = QString("<@%1> 入群验证已通过").arg(ev.user);
                        c->send_msgAsync(ev.type,ev.groupId,"[入群验证]",text,ev.msgId);

                    }
                }
            }
            return oldSeqId;
        }

        // ---------- 需要更新：将缓存的 GroupRecord2 转换为 GroupRecord（只做一次） ----------
        if (isGroup && groupExists) {
            oldGroup = toGroupRecord(cachedGroup);  // 转换，包含字符串复制到 char[]
        }

    }

    else {
        // ======================== 4. 缓存未命中：执行 LMDB 只读事务 ========================
        QByteArray userKey = QByteArray::fromHex(ev.user.toUtf8());
        int userRc = MDB_NOTFOUND;      // 区分「未找到」与「读取失败」
        int groupRc = MDB_NOTFOUND;

        MDB_txn *txn = nullptr;
        {
            // 租约作用域只包住这段只读事务：下面还要走 retryWrite（会拿 m_mutex），
            // 持着租约去等 m_mutex 会和 close() 里的「等租约归还」互相卡死。
            BotDBEnvLease lease(this);
            if (!lease)
                return 0;
            try {
                if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
                    return 0;
                if (isGroup && !ev.groupId.isEmpty()) {
                    QByteArray groupKey = QByteArray::fromHex(ev.groupId.toUtf8());
                    if (!groupKey.isEmpty())
                        groupExists = getRecord(txn, m_dbi_groups, groupKey, &oldGroup, sizeof(oldGroup), &groupRc);
                }

                if (!userKey.isEmpty()) {
                    userExists = getRecord(txn, m_dbi_users, userKey, &oldUser, sizeof(oldUser), &userRc);
                    if (userExists) oldSeqId = oldUser.seq_id;
                }
                mdb_txn_abort(txn);
                txn = nullptr;
            } catch (const std::exception &e) {
                qWarning() << "BotDB: 只读事务异常" << e.what();
                if (txn) mdb_txn_abort(txn);
                return 0;
            } catch (...) {
                if (txn) mdb_txn_abort(txn);
                return 0;
            }
        }

        // 读取失败（不是「记录不存在」）时绝不能继续：
        // 否则会把已入库的老用户误判成新用户，从而重新分配一个 seq_id。
        if (!userKey.isEmpty()
            && userRc != MDB_SUCCESS && userRc != MDB_NOTFOUND) {
            qCritical() << "BotDB: 读取用户记录失败，跳过本次 ID 分配"
                        << mdb_strerror(userRc) << "openid=" << ev.user;
            return 0;
        }
        userExists = (userRc == MDB_SUCCESS);
        if (!userExists) oldSeqId = 0;

        if (isGroup && groupRc != MDB_SUCCESS && groupRc != MDB_NOTFOUND) {
            // 群记录读失败按「不存在」处理即可，下一轮会重建，不影响 ID 分配
            qWarning() << "BotDB: 读取群记录失败" << mdb_strerror(groupRc) << "groupId=" << ev.groupId;
            groupExists = false;
        }

        // ----- 决策（群） -----
        if (isGroup && hc == false) {
            if (!groupExists) {
                needUpdateGroup = true;
                newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
            } else {
                const qint64 ONE_MONTH = 30 * 24 * 3600;
                qint64 now = time(nullptr);
                if (oldGroup.ncgx == 0 || (now - oldGroup.ncgx > ONE_MONTH)) {
                    needUpdateGroup = true;
                    newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
                }
            }
        }

        // ----- 决策（bitmap 管理员位）-----
        if (isGroup && groupExists && ev.at_you) {
            bool currentAdmin = (oldGroup.bitmap & BIT_ADMIN) != 0;
            if (currentAdmin != ev.bot_admin) {
                needUpdateBitmap = true;
            }
        }

        // ----- 决策（用户） -----
        if (userExists && !ev.nickname.isEmpty()) {
            QByteArray newName = ev.nickname.toUtf8();
            if (strcmp(oldUser.nickname, newName.constData()) != 0)
                needUpdateUser = true;
        }
    }

    // ======================== 5. 写事务 ========================
    bool success = retryWrite([&](MDB_txn *txn) -> int {
        int finalRc = MDB_SUCCESS;

        // 5a0. 并发二次确认（关键）：
        //      上面的「读缓存 + 只读事务」与这里的写事务之间没有加锁，
        //      同一用户的多个事件并发进入时，可能都判定为「新用户」，
        //      于是各自 getNextSeqId() 分配一个 ID → 用户的 ID 被反复重分配。
        //      写事务本身是排他的，在这里重新读一次即可保证「同一 openid 只分配一次」。
        if (!userExists) {
            QByteArray recheckKey = QByteArray::fromHex(ev.user.toUtf8());
            if (!recheckKey.isEmpty()) {
                UserRecord recheck{};
                int recheckRc = MDB_NOTFOUND;
                if (getRecord(txn, m_dbi_users, recheckKey, &recheck, sizeof(recheck), &recheckRc)) {
                    // 已被其他线程/事件创建 → 直接复用已有 ID，不再分配新号
                    userExists = true;
                    oldUser = recheck;
                    oldSeqId = recheck.seq_id;
                    needUpdateUser = false;
                    if (!ev.nickname.isEmpty()) {
                        QByteArray n = ev.nickname.toUtf8();
                        if (strcmp(oldUser.nickname, n.constData()) != 0)
                            needUpdateUser = true;
                    }
                    qDebug() << "BotDB: 写事务内发现用户已存在，复用 seq_id =" << oldSeqId
                             << "openid=" << ev.user;
                } else if (recheckRc != MDB_NOTFOUND) {
                    // 读取失败不能当作「不存在」，否则又会重复分配
                    qCritical() << "BotDB: 写事务内读取用户记录失败，放弃本次分配"
                                << mdb_strerror(recheckRc) << "openid=" << ev.user;
                    return -1;
                }
            }
        }

        // 5a. 处理群记录
        if (isGroup) {
            QByteArray groupKey = QByteArray::fromHex(ev.groupId.toUtf8());
            if (groupKey.isEmpty()) return -1;

            // 5a-1. 群记录二次确认（★ 群 bitmap 被清 0 的根因就在这里）：
            //   上面的 groupExists 只表示「群缓存是否命中」，而缓存未命中 ≠ 库里没有这条记录：
            //     · cleanGroupCache() 每 24h 会删掉所有 jojnyz/jojntime 为空的群缓存（几乎所有群）；
            //     · close() / 扩容 reopenEnvironment() 会整体清空 m_groupCache；
            //     · 用户缓存命中（该用户刚发过言）而该群从未进过缓存 → groupCacheHit=false。
            //   此时 needUpdateGroup 被判为 true，写事务走进下面的 if(!groupExists) 分支，
            //   用一条 memset 0 的新记录**覆盖**库里已存在的群记录 →
            //   bitmap / qid(代管列表) / autoref / inviter_seq_id / xychy_time / tq_CD 全丢。
            //   写事务本身排他，这里是唯一能可靠区分「真不存在」与「只是没缓存」的位置。
            if (!groupExists) {
                GroupRecord recheck{};
                int groupRecheckRc = MDB_NOTFOUND;
                if (getRecord(txn, m_dbi_groups, groupKey, &recheck, sizeof(recheck), &groupRecheckRc)) {
                    groupExists = true;
                    oldGroup = recheck;
                    // 记录确实存在 → 按已存在重做决策，避免用「新建」逻辑覆盖
                    if (hc == false) {
                        const qint64 ONE_MONTH = 30 * 24 * 3600;
                        qint64 now = time(nullptr);
                        needUpdateGroup = (recheck.ncgx == 0 || (now - recheck.ncgx > ONE_MONTH));
                    } else {
                        needUpdateGroup = false;
                    }
                    if (ev.at_you) {
                        bool currentAdmin = (recheck.bitmap & BIT_ADMIN) != 0;
                        needUpdateBitmap = (currentAdmin != ev.bot_admin);
                    }
                } else if (groupRecheckRc != MDB_NOTFOUND) {
                    // 读取失败不能当成「不存在」，否则又会覆盖已有群记录
                    qCritical() << "BotDB: 写事务内读取群记录失败，放弃本次写入"
                                << mdb_strerror(groupRecheckRc) << "groupId=" << ev.groupId;
                    return -1;
                }
            }

            GroupRecord currentGroup = oldGroup;
            if (!groupExists) {
                // 新建群
                memset(&currentGroup, 0, sizeof(currentGroup));
                currentGroup.create_time = static_cast<uint32_t>(time(nullptr));
                if (!newGroupName.isEmpty()) {
                    size_t copyLen = std::min<size_t>(newGroupName.toUtf8().size(), sizeof(currentGroup.name)-1);
                    memcpy(currentGroup.name, newGroupName.toUtf8().constData(), copyLen);
                    currentGroup.name[copyLen] = '\0';
                    currentGroup.ncgx = time(nullptr);
                } else {
                    currentGroup.name[0] = '\0';
                    currentGroup.ncgx = 0;
                }
                if (ev.at_you) {
                    if (ev.bot_admin)
                        currentGroup.bitmap |= BIT_ADMIN;
                    else
                        currentGroup.bitmap &= ~BIT_ADMIN;
                }
                int rc = putRecord(txn, m_dbi_groups, groupKey, &currentGroup, sizeof(currentGroup));
                if (rc != MDB_SUCCESS) finalRc = rc;
            } else {
                // 群已存在，处理各类更新
                if (needUpdateGroup && !newGroupName.isEmpty()) {
                    size_t copyLen = std::min<size_t>(newGroupName.toUtf8().size(), sizeof(currentGroup.name)-1);
                    memcpy(currentGroup.name, newGroupName.toUtf8().constData(), copyLen);
                    currentGroup.name[copyLen] = '\0';
                    currentGroup.ncgx = time(nullptr);
                }
                if (needUpdateBitmap) {
                    if (ev.bot_admin)
                        currentGroup.bitmap |= BIT_ADMIN;
                    else
                        currentGroup.bitmap &= ~BIT_ADMIN;
                }
                if (needUpdateGroup || needUpdateBitmap) {
                    int rc = putRecord(txn, m_dbi_groups, groupKey, &currentGroup, sizeof(currentGroup));
                    if (rc != MDB_SUCCESS) finalRc = rc;
                }
            }
            // 保存最终群记录
            finalGroup = currentGroup;
            finalGroupName = QString::fromUtf8(currentGroup.name, strnlen(currentGroup.name, sizeof(currentGroup.name)));
            finalBitmap = currentGroup.bitmap;
        }

        // 5b. 处理用户记录（保持不变）
        QByteArray userKey = QByteArray::fromHex(ev.user.toUtf8());
        if (userKey.isEmpty()) return -1;

        if (!userExists) {
            UserRecord newRec = {};
            uint32_t newSeq = getNextSeqId(txn);
            if (newSeq == 0) return -1;
            newRec.seq_id = newSeq;
            newRec.record_time = nowMinutes();
            newRec.invited_group_count = 0;

            // 诊断用：每一次真正的新分配都记一条，若同一 openid 反复出现同一行，
            // 说明仍有其它路径在丢记录（可据此定位）。
            qDebug() << "BotDB: 分配新 seq_id =" << newSeq << "openid=" << ev.user
                     << "type=" << ev.type << "subType=" << ev.subType;

            QByteArray nameBytes = ev.nickname.toUtf8();
            size_t copyLen = std::min<size_t>(nameBytes.size(), sizeof(newRec.nickname)-1);
            memcpy(newRec.nickname, nameBytes.constData(), copyLen);
            newRec.nickname[copyLen] = '\0';
            newRec.name[0] = '\0';

            int rc = putRecord(txn, m_dbi_users, userKey, &newRec, sizeof(newRec));
            if (rc != MDB_SUCCESS) {
                finalRc = rc;
            } else if (!saveSeqToOpenId(txn, newSeq, userKey)) {
                finalRc = -1;
            } else {
                resultSeq = newSeq;
                finalUser = newRec;
                finalUserNickname = ev.nickname;

            }
        } else {
            UserRecord updatedUser = oldUser;
            if (needUpdateUser) {
                QByteArray newNameBytes = ev.nickname.toUtf8();
                size_t copyLen = std::min<size_t>(newNameBytes.size(), sizeof(updatedUser.nickname)-1);
                memcpy(updatedUser.nickname, newNameBytes.constData(), copyLen);
                updatedUser.nickname[copyLen] = '\0';
                // 注意：绝不能在这里动 updatedUser.name
                // name 是「审核后的正式昵称」，与原始昵称 nickname 是两套数据。
                // 原代码写 updatedUser.name[copyLen]='\0'，会把审核好的正式昵称按原始昵称
                // 的长度截断/清空，导致审核结果丢失。
                updatedUser.record_time = nowMinutes();
                int rc = putRecord(txn, m_dbi_users, userKey, &updatedUser, sizeof(updatedUser));
                if (rc != MDB_SUCCESS) finalRc = rc;
            }
            if (finalRc == MDB_SUCCESS) {
                resultSeq = oldSeqId;
                finalUser = updatedUser;
                finalUserNickname = QString::fromUtf8(updatedUser.nickname);
                finalUserNickname2 = QString::fromUtf8(updatedUser.name);
            }

        }

        return finalRc;
    });

    // ======================== 6. 事务成功后，更新缓存 ========================
    if (success && resultSeq != 0) {
        {
            QMutexLocker locker(&m_cacheMutex);
            m_userCache[userKey] = finalUser;
        }
        if (isGroup) {
            QMutexLocker locker(&m_cacheMutex);

            // 将 finalGroup 转换为 GroupRecord2
            GroupRecord2 cacheRecord = toGroupRecord2(finalGroup);
            // jojnyz / jojntime 是纯内存数据（不入 LMDB），toGroupRecord2 不会带过来：
            // 直接覆盖 m_groupCache 会把整份「待入群验证」名单丢掉，下面的「验证通过」分支
            // 也会因为 jojnyz 恒为空而永不命中。这里先把旧缓存的名单继承过来。
            {
                auto itOldGroup = m_groupCache.find(groupKey);
                if (itOldGroup != m_groupCache.end()) {
                    cacheRecord.jojnyz = itOldGroup->jojnyz;
                    cacheRecord.jojntime = itOldGroup->jojntime;
                }
            }
            // 加载 jojnyz 数据（如果存在）
            QList<int> ids, times;

            if(ev.type == 0 && ev.subType==1)
            {
                for(int i=0;i<cacheRecord.jojnyz.size();++i)
                {
                    if(resultSeq == cacheRecord.jojnyz[i])
                    {
                        cacheRecord.jojnyz.removeAt(i);
                        cacheRecord.jojntime.removeAt(i);

                        auto *c = m_botClients[ev.appid];
                        c->setGroupRestrictChatSetting(ev.groupId,ev.user,0,[](auto,auto){});
                         QString text = QString("<@%1> 入群验证已通过").arg(ev.user);
                        c->send_msgAsync(ev.type,ev.groupId,"[入群验证]",text,ev.msgId);

                    }
                }
            }
            // 存入缓存
            m_groupCache[groupKey] = cacheRecord;

            ev.groupname = finalGroupName;
            ev.bitmap = finalBitmap;
            memcpy(ev.qid, finalGroup.qid, sizeof(ev.qid));
        }
        if (ev.nickname.isEmpty()) {
            ev.nickname = finalUserNickname;

        }
        if(!finalUserNickname2.isEmpty())
        {
            ev.nickname2 = finalUserNickname2;
        }

        return resultSeq;
    }

    return 0;
}


bool BotDB::getUserBySeqId(uint32_t seq_id, UserRecord &outRecord)
{
    // 只读操作不加 m_mutex（会被写事务堵住），但必须领租约：
    // 否则 close()/扩容换 env 时这里会拿到已经释放的 MDB_env
    BotDBEnvLease lease(this);
    if (!lease) return false;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    QByteArray openidBin;
    if (!getOpenIdBySeq(txn, seq_id, openidBin)) {
        mdb_txn_abort(txn);
        return false;
    }
    outRecord.name[0] = '\0';

    MDB_val key, value;
    key.mv_data = openidBin.data();
    key.mv_size = openidBin.size();
    rc = mdb_get(txn, m_dbi_users, &key, &value);
    if (rc == MDB_SUCCESS && value.mv_size == sizeof(UserRecord)) {
        memcpy(&outRecord, value.mv_data, sizeof(UserRecord));
        mdb_txn_abort(txn);
        return true;
    }
    mdb_txn_abort(txn);
    return false;
}
bool BotDB::getUsersByPage(bool onlyNameEmpty, int offset, int limit,
                           QList<UserRecord>& outUsers, int& totalCount)
{
    outUsers.clear();
    totalCount = 0;
    BotDBEnvLease lease(this);
    if (!lease) return false;

    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return false;

    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_users, &cursor) != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return false;
    }

    // 定义新旧记录大小
    const size_t OLD_RECORD_SIZE = 80;   // 无 name 字段
    const size_t NEW_RECORD_SIZE = sizeof(UserRecord);

    MDB_val key, value;
    int rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    int skipped = 0;
    int collected = 0;

    while (rc == MDB_SUCCESS) {
        UserRecord rec;
        bool valid = false;

        // 尝试新格式
        if (value.mv_size == NEW_RECORD_SIZE) {
            memcpy(&rec, value.mv_data, NEW_RECORD_SIZE);
            valid = true;
        }
        // 尝试旧格式（无 name 字段）
        else if (value.mv_size == OLD_RECORD_SIZE) {
            // 旧格式布局：前 80 字节与 UserRecord 前 80 字节一致（seq_id, bitmap, record_time, invited_group_count, nickname）
            const char* data = (const char*)value.mv_data;
            memcpy(&rec.seq_id, data, 4);
            memcpy(&rec.bitmap, data + 4, 4);
            memcpy(&rec.record_time, data + 8, 4);
            memcpy(&rec.invited_group_count, data + 12, 4);
            memcpy(rec.nickname, data + 16, 64);
            rec.nickname[63] = '\0';
            // 新字段 name 置空
            rec.name[0] = '\0';
            valid = true;
        }

        if (valid) {
            bool nameEmpty = (rec.name[0] == '\0');
            if(rec.name[0] != '\0' || rec.nickname[0] != '\0')  {
                if ((onlyNameEmpty && nameEmpty) || (!onlyNameEmpty && !nameEmpty)) {
                    totalCount++;
                    if (skipped < offset) {
                        skipped++;
                    } else if (collected < limit) {
                        outUsers.append(rec);
                        collected++;
                    }
                }
            }
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return true;
}

// 更新用户缓存（传入 openid 二进制和 UserRecord）
void BotDB::updateUserCache(const QByteArray &openidBin, const UserRecord &record) {
    BinKey key;
    memcpy(key.data, openidBin.constData(), 16);
    QMutexLocker locker(&m_cacheMutex);
    m_userCache[key] = record;

}


void BotDB::updateGroupCache(const QByteArray &groupIdBin, const GroupRecord2 &record) {
    BinKey key;
    memcpy(key.data, groupIdBin.constData(), 16);

    QMutexLocker locker(&m_cacheMutex);

    m_groupCache[key] = record;
}


bool BotDB::updateUserBySeqId(uint32_t seq_id, const UserRecord &newRecord)
{
    // 拷贝一份，确保 record_time 被刷新
    UserRecord toWrite = newRecord;
    toWrite.record_time = nowMinutes();   // 强制更新时间
    return updateUserBySeqId(seq_id, [&](UserRecord &r) {
        r = toWrite;

    });
}
bool BotDB::updateUserBySeqId(uint32_t seq_id, std::function<void(UserRecord&)> updater)
{
    if (!m_env) return false;

    UserRecord finalRecord;
    QByteArray finalOpenidBin;
    bool recordChanged = false;

    bool success = retryWrite([&](MDB_txn *txn) -> int {
        QByteArray openidBin;
        if (!getOpenIdBySeq(txn, seq_id, openidBin))
            return MDB_NOTFOUND;

        MDB_val key, value;
        key.mv_data = openidBin.data();
        key.mv_size = openidBin.size();
        int rc = mdb_get(txn, m_dbi_users, &key, &value);
        if (rc != MDB_SUCCESS) return rc;

        UserRecord record;
        bool valid = false;

        // 尝试新格式（完整记录）
        if (value.mv_size == sizeof(UserRecord)) {
            memcpy(&record, value.mv_data, sizeof(UserRecord));
            valid = true;
        }
        // 尝试旧格式（80字节，无 name 字段）
        else if (value.mv_size == 80) {
            const char* data = (const char*)value.mv_data;
            memcpy(&record.seq_id, data, 4);
            memcpy(&record.bitmap, data + 4, 4);
            memcpy(&record.record_time, data + 8, 4);
            memcpy(&record.invited_group_count, data + 12, 4);
            memcpy(record.nickname, data + 16, 64);
            record.nickname[63] = '\0';
            record.name[0] = '\0';   // 旧记录 name 为空
            valid = true;
        }

        if (!valid)
            return -1;

        // 调用 updater 修改记录（可修改 name 等字段）
        updater(record);
        record.record_time = nowMinutes();

        // 以新格式写回（升级为完整记录）
        rc = putRecord(txn, m_dbi_users, openidBin, &record, sizeof(record));
        if (rc == MDB_SUCCESS) {
            finalRecord = record;
            finalOpenidBin = openidBin;
            recordChanged = true;
        }
        return rc;
    });

    if (success && recordChanged) {
        updateUserCache(finalOpenidBin, finalRecord);
    }
    return success;
}



bool BotDB::getOpenIdBySeqId(uint32_t seqId, QString &outOpenidHex) {
    BotDBEnvLease lease(this);
    if (!lease) return false;
    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return false;
    QByteArray bin;
    bool ok = getOpenIdBySeq(txn, seqId, bin);  // 复用私有方法
    mdb_txn_abort(txn);
    if (ok) {
        outOpenidHex = bin.toHex(); // 转为32位Hex字符串
        return true;
    }
    return false;
}
void BotDB::loadDailyStats()
{
    QString dateStr = QDate::currentDate().toString("yyyy-MM-dd");
    if (m_todayDate == dateStr) return;  // 日期未变，保留当前数据
    m_todayDate = dateStr;

    // 清空旧数据
    QMutexLocker locker(&m_msgMutex);
    m_userDailyMsg.clear();
    m_groupDailyMsg.clear();

    // 加载用户统计
    QFile userFile(  m_path+ "/msgstat_user_" + dateStr + ".dat");
    if (userFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&userFile);
        in >> m_userDailyMsg;
    }

    // 加载群统计
    QFile groupFile(m_path+"/msgstat_group_" + dateStr + ".dat");
    if (groupFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&groupFile);
        in >> m_groupDailyMsg;
    }
}
bool BotDB::saveAccountStats(uint32_t appid, uint32_t minuteIndex, const AccountStats &stats)
{
    // 构造 key: appid + minute_index
    QByteArray keyData;
    keyData.append((char*)&appid, sizeof(appid));
    keyData.append((char*)&minuteIndex, sizeof(minuteIndex));

    return retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_account_stats, keyData, &stats, sizeof(stats));
    });
}
bool BotDB::getAccountStats(uint32_t appid, uint32_t minuteIndex, AccountStats &outStats)
{
    // 原来这里连 `if (!m_env)` 都没有，环境一关直接 mdb_txn_begin(nullptr) 崩
    BotDBEnvLease lease(this);
    if (!lease) return false;

    QByteArray keyData;
    keyData.append((char*)&appid, sizeof(appid));
    keyData.append((char*)&minuteIndex, sizeof(minuteIndex));

    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return false;

    bool ok = getRecord(txn, m_dbi_account_stats, keyData, &outStats, sizeof(outStats));
    mdb_txn_abort(txn);
    return ok;
}

// ==================== 积分（商店货币）系统 ====================
// 子库 "points"：key = openid 的 16 字节二进制，value = qint64 余额（原始 8 字节）。
// ⚠ 不足 16 字节的 key 用 0 补到 16 —— 与 users 子库的 BinKey 口径一致，
//   否则同一用户在不同接口下会算出两个不同 key（一会儿有分一会儿 0 分）。
static QByteArray pointsKey(const QByteArray &openidBin)
{
    QByteArray k = openidBin.left(16);
    if (k.size() < 16) k.append(QByteArray(16 - k.size(), '\0'));
    return k;
}

qint64 BotDB::getPoints(const QByteArray &openidBin)
{
    BotDBEnvLease lease(this);
    if (!lease || !m_dbi_points) return 0;
    const QByteArray keyData = pointsKey(openidBin);

    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return 0;

    qint64 balance = 0;   // 没记录 = 0 分（getRecord 找不到时保持原值不动）
    getRecord(txn, m_dbi_points, keyData, &balance, sizeof(balance));
    mdb_txn_abort(txn);
    return balance;
}

bool BotDB::setPoints(const QByteArray &openidBin, qint64 points)
{
    if (!m_env || !m_dbi_points) return false;
    const QByteArray keyData = pointsKey(openidBin);
    if (points < 0) points = 0;

    return retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_points, keyData, &points, sizeof(points));
    });
}

qint64 BotDB::addPoints(const QByteArray &openidBin, qint64 delta, bool *ok)
{
    if (ok) *ok = false;
    if (!m_env || !m_dbi_points) return 0;

    const QByteArray keyData = pointsKey(openidBin);
    qint64 result = 0;
    // 读-改-写放在同一个写事务里（retryWrite 全程持 m_mutex），并发加/扣不会互相覆盖
    const bool success = retryWrite([&](MDB_txn *txn) -> int {
        qint64 balance = 0;
        getRecord(txn, m_dbi_points, keyData, &balance, sizeof(balance));
        result = balance + delta;
        if (result < 0) result = 0;      // 不允许负分（减多了就停在 0）
        return putRecord(txn, m_dbi_points, keyData, &result, sizeof(result));
    });
    if (ok) *ok = success;
    return success ? result : 0;
}

bool BotDB::spendPoints(const QByteArray &openidBin, qint64 cost, qint64 *balanceAfter)
{
    if (cost <= 0) {                       // 0 元商品：算成功，余额原样返回
        const qint64 cur = getPoints(openidBin);
        if (balanceAfter) *balanceAfter = cur;
        return true;
    }
    if (!m_env || !m_dbi_points) return false;

    const QByteArray keyData = pointsKey(openidBin);

    // ⚠ 必须自己在 m_mutex 里做「读 → 判断 → 写」：retryWrite 只保证单个事务，
    //   先 getPoints 再 setPoints 的话，两条并发兑换都能读到旧余额 → 双花。
    QMutexLocker locker(&m_mutex);
    for (int retry = 0; retry <= 3; ++retry) {
        MDB_txn *txn = nullptr;
        if (mdb_txn_begin(m_env, nullptr, 0, &txn) != MDB_SUCCESS) return false;

        qint64 balance = 0;
        getRecord(txn, m_dbi_points, keyData, &balance, sizeof(balance));

        if (balance < cost) {              // 买不起：不写库，事务直接丢
            mdb_txn_abort(txn);
            if (balanceAfter) *balanceAfter = balance;
            return false;
        }

        const qint64 left = balance - cost;
        int rc = putRecord(txn, m_dbi_points, keyData, &left, sizeof(left));
        if (rc != MDB_SUCCESS) {           // 写不进去：MAP_FULL 就扩容重试，其它错误直接失败
            mdb_txn_abort(txn);
            if (rc == MDB_MAP_FULL && increaseMapSize()) continue;
            return false;
        }

        rc = mdb_txn_commit(txn);
        if (rc == MDB_SUCCESS) {
            if (balanceAfter) *balanceAfter = left;
            return true;
        }
        // commit 失败：事务已终止，不能再 abort
        if (rc == MDB_MAP_FULL && increaseMapSize()) continue;
        return false;
    }
    return false;
}

// ==================== 签到系统 ====================
// 子库 "signs"：key = openid 16 字节（**和 points 完全同一套 key**，直接复用 pointsKey()），
// value = SignRecord（3 个 quint32 = 12 字节 POD，本机字节序，与工程里其它 record 一致）。
// 没记录 = 从没签过（getSignRecord 返回 false，out 保持全 0，调用方不用额外判空）。
bool BotDB::getSignRecord(const QByteArray &openidBin, SignRecord &out)
{
    out = SignRecord();
    BotDBEnvLease lease(this);
    if (!lease || !m_dbi_signs) return false;

    const QByteArray keyData = pointsKey(openidBin);

    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return false;

    SignRecord rec;
    const bool found = getRecord(txn, m_dbi_signs, keyData, &rec, sizeof(rec));
    mdb_txn_abort(txn);
    if (found) out = rec;
    return found;
}

bool BotDB::putSignRecord(const QByteArray &openidBin, const SignRecord &rec)
{
    if (!m_env || !m_dbi_signs) return false;
    const QByteArray keyData = pointsKey(openidBin);
    SignRecord v = rec;                       // 拷贝一份进 lambda（避免引用悬空）
    return retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_signs, keyData, &v, sizeof(v));
    });
}

bool BotDB::getTodayDiff(uint32_t appid, const AccountStats &currentStats, AccountStats &diff)
{
    uint32_t nowMinute = QDateTime::currentSecsSinceEpoch() / 60;
    uint32_t yesterdayMinute = nowMinute - 1440;

    AccountStats yesterdayStats;
    if (!getAccountStats(appid, yesterdayMinute, yesterdayStats)) {
        // 如果昨天同一分钟没有数据，说明昨天这个时间还没有记录，无法对比
        return false;
    }

    // 今日数据使用传入的 currentStats（来自内存）
    diff.minute_index = nowMinute;
    diff.appid = appid;

    diff.message_received = currentStats.message_received - yesterdayStats.message_received;
    diff.message_sent = currentStats.message_sent - yesterdayStats.message_sent;
    diff.今日加群数量 = currentStats.今日加群数量 - yesterdayStats.今日加群数量;
    diff.今日退群数量 = currentStats.今日退群数量 - yesterdayStats.今日退群数量;
    diff.今日好友数量 = currentStats.今日好友数量 - yesterdayStats.今日好友数量;
    diff.今日删除好友数量 = currentStats.今日删除好友数量 - yesterdayStats.今日删除好友数量;
    diff.今日频道数量 = currentStats.今日频道数量 - yesterdayStats.今日频道数量;
    diff.今日退出频道数量 = currentStats.今日退出频道数量 - yesterdayStats.今日退出频道数量;
    diff.active_users = currentStats.active_users - yesterdayStats.active_users;
    diff.active_groups = currentStats.active_groups - yesterdayStats.active_groups;

    return true;
}

void BotDB::saveDailyStats()
{
    QMutexLocker locker(&m_msgMutex);
    QString dateStr = QDate::currentDate().toString("yyyy-MM-dd");

    QFile userFile(m_path+"/msgstat_user_" + dateStr + ".dat");
    if (userFile.open(QIODevice::WriteOnly)) {
        QDataStream out(&userFile);
        out << m_userDailyMsg;
    }
    QFile groupFile(m_path+ "/msgstat_group_" + dateStr + ".dat");
    if (groupFile.open(QIODevice::WriteOnly)) {
        QDataStream out(&groupFile);
        out << m_groupDailyMsg;
    }
}

void BotDB::checkDayChange()
{
    QString now = QDate::currentDate().toString("yyyy-MM-dd");
    if (m_todayDate != now) {
        m_userDailyMsg.clear();
        m_groupDailyMsg.clear();
        m_todayDate = now;



    }
}

quint64 BotDB::getUserTodayMsgCount(const QByteArray &openidBin) {
    BinKey key;
    memcpy(key.data, openidBin.constData(), 16);
    QMutexLocker locker(&m_msgMutex);
    return m_userDailyMsg.value(key, 0);
}
GroupRecord BotDB::toGroupRecord(const GroupRecord2& src) {
    GroupRecord dst{};
    dst.create_time = src.create_time;
    dst.inviter_seq_id = src.inviter_seq_id;
    dst.bitmap = src.bitmap;
    dst.xychy_time = src.xychy_time;
    dst.tq_CD = src.tq_CD;
    dst.ncgx = src.ncgx;

    // 复制 name
    QByteArray nameBytes = src.name.toUtf8();
    size_t copyLen = std::min<size_t>(nameBytes.size(), sizeof(dst.name) - 1);
    memcpy(dst.name, nameBytes.constData(), copyLen);
    dst.name[copyLen] = '\0';

    // 复制 autoref
    QByteArray autorefBytes = src.autoref.toUtf8();
    copyLen = std::min<size_t>(autorefBytes.size(), sizeof(dst.autoref) - 1);
    memcpy(dst.autoref, autorefBytes.constData(), copyLen);
    dst.autoref[copyLen] = '\0';

    // 复制 qid 数组
    memcpy(dst.qid, src.qid, sizeof(dst.qid));

    // jojnyz 和 jojntime 被忽略
    return dst;
}
bool BotDB::addJojiyzRecord(const QString &groupIdHex, int userId) {
    QByteArray groupKeyBin = QByteArray::fromHex(groupIdHex.toUtf8());
    if (groupKeyBin.size() != 16) return false;
    BinKey key;
    memcpy(key.data, groupKeyBin.constData(), 16);
    QMutexLocker locker(&m_cacheMutex);  // 确保线程安全
    auto it = m_groupCache.find(key);
    if (it != m_groupCache.end()) {
        it->jojnyz.append(userId);
        uint32_t nowMin = static_cast<uint32_t>(std::time(nullptr) / 60);
        it->jojntime.append(nowMin);
        return true;
    }


    return false;
}

void BotDB::cleanExpiredJojiyzCache(int expireMinutes,int appid)
{
    if(!m_botClients.contains(appid)) return;
    QMutexLocker locker(&m_cacheMutex);
    qint64 nowMin = QDateTime::currentSecsSinceEpoch() / 60;

    auto *c = m_botClients[appid];
    for (auto it = m_groupCache.begin(); it != m_groupCache.end(); ++it) {
        GroupRecord2& rec = it.value();
        QList<int>& ids = rec.jojnyz;
        QList<int>& times = rec.jojntime;
        if(ids.size()==0) continue;
        QByteArray byteArray(reinterpret_cast<const char*>(it.key().data), 16);

        // 转为 32 位长度的十六进制字符串（小写）
        QString hexStr = byteArray.toHex();
        QString user_list;

        for (int i = times.size() - 1; i >= 0; --i) {
            if (nowMin - static_cast<qint64>(times[i]) > expireMinutes) {

                QString user;
                getOpenIdBySeqId(ids[i],user);

                user_list.append(user).append(",");
                ids.removeAt(i);
                times.removeAt(i);

            }
        }
        if(!user_list.isEmpty()){
            c->del_members(hexStr,user_list,false,[c,hexStr,user_list](const QString &resp,auto){
                QString text = QString("入群验证移除 部分未完成验证成员..%1\n结果：%2").arg(user_list,resp);
                c->send_msgAsync(0,hexStr,"[入群验证]",text,QString());
            });
        }
    }
}
GroupRecord2 BotDB::toGroupRecord2(const GroupRecord& src) {
    GroupRecord2 dst{};
    dst.create_time = src.create_time;
    dst.inviter_seq_id = src.inviter_seq_id;
    dst.bitmap = src.bitmap;
    dst.xychy_time = src.xychy_time;
    dst.tq_CD = src.tq_CD;
    dst.ncgx = src.ncgx;

    dst.name = QString::fromUtf8(src.name, strnlen(src.name, sizeof(src.name)));
    dst.autoref = QString::fromUtf8(src.autoref, strnlen(src.autoref, sizeof(src.autoref)));
    memcpy(dst.qid, src.qid, sizeof(dst.qid));


    return dst;
}
quint64 BotDB::getGroupTodayMsgCount(const QByteArray &groupIdBin) {
    BinKey key;
    memcpy(key.data, groupIdBin.constData(), 16);
    QMutexLocker locker(&m_msgMutex);
    return m_groupDailyMsg.value(key, 0);
}



// 重载1：传入各字段，构造 GroupRecord 并调用核心函数
bool BotDB::addGroup(const QString &groupIdHex, uint32_t createTimeMinutes,
                     uint32_t inviterSeqId, uint32_t bitmap, const QString &name)
{
    QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
    if (keyData.isEmpty()) return false;
    GroupRecord record;
    record.create_time = static_cast<uint32_t>(time(nullptr)); // 注意：原代码如此，未使用 createTimeMinutes
    record.inviter_seq_id = inviterSeqId;
    record.bitmap = bitmap;
    record.xychy_time = 0;
    record.tq_CD = 0;
    record.ncgx = time(nullptr);
    QByteArray nameData = name.toUtf8();
    size_t copyLen = std::min<size_t>(nameData.size(), sizeof(record.name) - 1);
    memcpy(record.name, nameData.constData(), copyLen);
    record.name[copyLen] = '\0';
    memset(record.autoref, 0, sizeof(record.autoref));
    memset(record.qid, 0, sizeof(record.qid));

    bool success = retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_groups, keyData, &record, sizeof(record));
    });
    if(success)
    {
        GroupRecord2 record2 = toGroupRecord2(record);
        updateGroupCache(keyData, record2);
        return true;
    }
    return false;
}

// 重载2：接受 GroupRecord2，转换为 GroupRecord 后调用核心函数
bool BotDB::addGroup(const QString &groupIdHex, const GroupRecord2 &record2)
{
    QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
    if (keyData.isEmpty()) return false;
    GroupRecord record = toGroupRecord(record2);
    bool success = retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_groups, keyData, &record, sizeof(record));
    });
    if(success)
    {
        updateGroupCache(keyData, record2);
        return true;
    }
    return false;

}


QList<QString> BotDB::getAllGroupIds()
{
    QList<QString> result;
    BotDBEnvLease lease(this);
    if (!lease) return result;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return result;

    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_groups, &cursor) != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return result;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        // key 存储的是群 ID 的二进制数据（hex 解码后的字节）
        QByteArray keyData(static_cast<const char*>(key.mv_data), key.mv_size);
        // 转为十六进制字符串（与数据库中存储格式一致）
        QString hexId = keyData.toHex();
        result.append(hexId);
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return result;
}
template<typename Func>
void BotDB::withGroupInfo(const BinKey& key, Func&& func) {
    QMutexLocker locker(&m_cacheMutex);
    auto it = m_groupCache.find(key);
    if (it != m_groupCache.end())
        func(it.value()); // 传递 const GroupRecord2&
}
bool BotDB::getGroupInfo(const QString &groupIdHex, GroupRecord2 &outRecord)
{
    BotDBEnvLease lease(this);
    if (!lease) return false;
    QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
    if (keyData.isEmpty()) return false;
    BinKey groupKey;
    memcpy(groupKey.data, keyData.constData(), 16);
    {
        QMutexLocker locker(&m_cacheMutex);
        auto itGroup = m_groupCache.find(groupKey);
        if (itGroup != m_groupCache.end()) {
            outRecord = itGroup.value();
            return true;
        }
    }
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    GroupRecord Record;
    bool ok = getRecord(txn, m_dbi_groups, keyData, &Record, sizeof(Record));
    mdb_txn_abort(txn);
    outRecord = toGroupRecord2(Record);
    updateGroupCache(keyData, outRecord);
    return ok;
}


bool BotDB::deleteGroup(const QString &groupIdHex)
{
    return retryWrite([&](MDB_txn *txn) -> int {
        QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
        if (keyData.isEmpty()) return -1;
        return delRecord(txn, m_dbi_groups, keyData);
    });
}

uint64_t BotDB::makeFriendKey(uint32_t a, uint32_t b)
{
    if (a < b) return ((uint64_t)a << 32) | b;
    else       return ((uint64_t)b << 32) | a;
}

// 添加好友：key = userSeqId，value = addTimeMinutes
bool BotDB::addFriend(uint32_t userSeqId, uint32_t addTimeMinutes)
{
    if(!m_env) return false;
    QMutexLocker locker(&m_mutex);
    QByteArray keyData((char*)&userSeqId, sizeof(userSeqId));
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return false;

    int putRc = putRecord(txn, m_dbi_friends, keyData, &addTimeMinutes, sizeof(addTimeMinutes));
    if (putRc == MDB_SUCCESS) {
        rc = mdb_txn_commit(txn);
        return (rc == MDB_SUCCESS);
    } else {
        mdb_txn_abort(txn);
        return false;
    }
}

bool BotDB::removeFriend(uint32_t userSeqId)
{
    if(!m_env) return false;
    QMutexLocker locker(&m_mutex);
    QByteArray keyData((char*)&userSeqId, sizeof(userSeqId));
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return false;
    int putRc = delRecord(txn, m_dbi_friends, keyData);
    if (putRc == MDB_SUCCESS) {
        rc = mdb_txn_commit(txn);
        return (rc == MDB_SUCCESS);
    } else {
        mdb_txn_abort(txn);
        return false;
    }
}

bool BotDB::isFriend(uint32_t userSeqId)
{
    BotDBEnvLease lease(this);
    if (!lease) return false;
    QByteArray keyData((char*)&userSeqId, sizeof(userSeqId));
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    uint32_t dummy;
    bool ok = getRecord(txn, m_dbi_friends, keyData, &dummy, sizeof(dummy));
    mdb_txn_abort(txn);
    return ok;
}

QList<int> BotDB::getFriendList()
{
    QList<int> result;
    BotDBEnvLease lease(this);
    if (!lease) return result;
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return result;

    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_friends, &cursor) != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return result;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        if (key.mv_size == sizeof(uint32_t)) {
            uint32_t userSeqId;
            memcpy(&userSeqId, key.mv_data, sizeof(userSeqId));
            result.append(userSeqId);
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return result;
}


// 构造 Key
static QByteArray makeSubKey(const QString &mark, uint8_t param, const QString &groupId)
{
    return QString("%1:%2|%3").arg(mark).arg(param).arg(groupId).toUtf8();
}

// 构造前缀（用于遍历某个标记下的所有记录）
static QByteArray makeSubPrefix(uint32_t mark)
{
    return QByteArray::number(mark) + ":";
}

// 构造按参数筛选的前缀（用于快速获取某个标记下特定参数的群）
static QByteArray makeParamPrefix(uint32_t mark, uint8_t param)
{
    return QString("%1:%2|").arg(mark).arg(param).toUtf8();
}


// ==================== 订阅：统一走 SQLite（db/subdb.*）====================
// 2026-11-12 起订阅数据不再写 LMDB。下面这几个是**对外的**入口，只做转发；
// 真正的实现在原来的 *Legacy*（仅当订阅库打不开时兜底，见 subdb.h 顶部说明）。
bool BotDB::addSubscription(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
{
    if (param > 3 || groupId.isEmpty() || dataList.isEmpty())
        return false;
    if (!g_subdb)
        return addSubscriptionLegacy(mark, param, groupId, dataList);
    return g_subdb->addSubscription(mark, static_cast<int>(param), groupId, QStringList(dataList));
}

bool BotDB::addSubscriptionLegacy(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
{
    if (param > 3 || groupId.isEmpty() || dataList.isEmpty())
        return false;

    return retryWrite([&](MDB_txn *txn) -> int {
        QByteArray keyData = makeSubKey(mark, param, groupId);
        MDB_val key;
        key.mv_data = keyData.data();
        key.mv_size = keyData.size();

        // 1. 读取现有数据，解析所有已存在的条目（放入 QSet 用于快速去重）
        MDB_val existing;
        existing.mv_data = nullptr;
        existing.mv_size = 0;
        int rc = mdb_get(txn, m_dbi_subscriptions, &key, &existing);

        QSet<QByteArray> existingSet;
        QByteArray oldData;   // 保存原始二进制，用于后续重建
        if (rc == MDB_SUCCESS) {
            oldData.append(static_cast<const char*>(existing.mv_data), static_cast<int>(existing.mv_size));
            const char* raw = oldData.constData();
            size_t size = oldData.size();
            size_t offset = 0;
            while (offset + sizeof(uint32_t) <= size) {
                uint32_t len;
                memcpy(&len, raw + offset, sizeof(uint32_t));
                offset += sizeof(uint32_t);
                if (len > size - offset)
                    return MDB_BAD_DBI;
                existingSet.insert(QByteArray(raw + offset, static_cast<int>(len)));
                offset += len;
            }
        } else if (rc != MDB_NOTFOUND) {
            return rc;
        }

        // 2. 构建新数据（先复制旧数据，再追加新条目）
        QByteArray newData = oldData;   // 包含原有全部条目
        bool anyAdded = false;
        for (const QString &str : dataList) {
            QByteArray bytes = str.toUtf8();
            if (!existingSet.contains(bytes)) {
                uint32_t len = static_cast<uint32_t>(bytes.size());
                newData.append(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
                newData.append(bytes);
                existingSet.insert(bytes);   // 防止同一批次中的重复
                anyAdded = true;
            }
        }

        if (!anyAdded)
            return MDB_SUCCESS;   // 无新条目，无需写入

        // 3. 写回
        MDB_val value;
        value.mv_data = newData.data();
        value.mv_size = newData.size();
        return mdb_put(txn, m_dbi_subscriptions, &key, &value, 0);
    });
}
bool BotDB::removeSubscription(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
{
    if (groupId.isEmpty() || param > 3)
        return false;
    if (!g_subdb)
        return removeSubscriptionLegacy(mark, param, groupId, dataList);
    return g_subdb->removeSubscription(mark, static_cast<int>(param), groupId, QStringList(dataList));
}

bool BotDB::removeSubscriptionLegacy(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
{
    if (groupId.isEmpty() || param > 3)
        return false;

    // dataList 为空：删除整个键
    if (dataList.isEmpty()) {
        return retryWrite([&](MDB_txn *txn) -> int {
            QByteArray keyData = makeSubKey(mark, param, groupId);
            return delRecord(txn, m_dbi_subscriptions, keyData);
        });
    }

    // 将待删除列表转为 QSet 以便快速查找
    QSet<QByteArray> targets;
    for (const QString &str : dataList)
        targets.insert(str.toUtf8());

    return retryWrite([&](MDB_txn *txn) -> int {
        QByteArray keyData = makeSubKey(mark, param, groupId);
        MDB_val key;
        key.mv_data = keyData.data();
        key.mv_size = keyData.size();

        MDB_val existing;
        existing.mv_data = nullptr;
        existing.mv_size = 0;
        int rc = mdb_get(txn, m_dbi_subscriptions, &key, &existing);
        if (rc == MDB_NOTFOUND)
            return MDB_SUCCESS;
        if (rc != MDB_SUCCESS)
            return rc;

        const char* raw = static_cast<const char*>(existing.mv_data);
        size_t size = existing.mv_size;

        // 遍历所有条目，保留不在 targets 中的条目
        QByteArray newData;
        newData.reserve(static_cast<int>(size)); // 通常只减不增
        size_t offset = 0;
        bool anyRemoved = false;

        while (offset + sizeof(uint32_t) <= size) {
            uint32_t len;
            memcpy(&len, raw + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);
            if (len > size - offset)
                return MDB_BAD_DBI;

            QByteArray entry(raw + offset, static_cast<int>(len));
            if (targets.contains(entry)) {
                anyRemoved = true;
                // 跳过此条目，不追加
            } else {
                newData.append(reinterpret_cast<const char*>(&len), sizeof(uint32_t));
                newData.append(entry);
            }
            offset += len;
        }

        if (!anyRemoved)
            return MDB_SUCCESS;   // 没有匹配项，无需修改

        if (newData.isEmpty()) {
            return delRecord(txn, m_dbi_subscriptions, keyData);
        } else {
            MDB_val value;
            value.mv_data = newData.data();
            value.mv_size = newData.size();
            return mdb_put(txn, m_dbi_subscriptions, &key, &value, 0);
        }
    });
}
QString BotDB::getSubscriptions(const QString &mark, uint8_t param, const QString &groupId) const
{
    if (groupId.isEmpty() || param > 3)
        return QString();
    if (!g_subdb)
        return getSubscriptionsLegacy(mark, param, groupId);
    return g_subdb->getSubscriptions(mark, static_cast<int>(param), groupId);
}

QString BotDB::getSubscriptionsLegacy(const QString &mark, uint8_t param, const QString &groupId) const
{
    QString result;
    if (groupId.isEmpty() || param > 3)
        return result;

    // 原来这里没有 `if (!m_env)`，环境关掉后 mdb_txn_begin(nullptr) 直接崩
    BotDBEnvLease lease(this);
    if (!lease)
        return result;

    // 使用只读事务
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS)
        return result;

    QByteArray keyData = makeSubKey(mark, param, groupId);
    MDB_val key;
    key.mv_data = keyData.data();
    key.mv_size = keyData.size();

    MDB_val value;
    value.mv_data = nullptr;
    value.mv_size = 0;
    rc = mdb_get(txn, m_dbi_subscriptions, &key, &value);

    if (rc == MDB_SUCCESS && value.mv_data != nullptr && value.mv_size > 0) {
        const char* raw = static_cast<const char*>(value.mv_data);
        size_t size = value.mv_size;
        size_t offset = 0;

        while (offset + sizeof(uint32_t) <= size) {
            uint32_t len;
            memcpy(&len, raw + offset, sizeof(uint32_t));
            offset += sizeof(uint32_t);

            // 数据完整性检查
            if (len > size - offset)
                break;   // 数据损坏，停止解析

            QByteArray bytes(raw + offset, static_cast<int>(len));
            if(result.isEmpty())
                result.append(QString::fromUtf8(bytes));
            else {
                result.append("|||");
                result.append(QString::fromUtf8(bytes));
            }

            offset += len;
        }
    }

    mdb_txn_abort(txn);   // 只读事务直接 abort 即可
    return result;
}
QStringList BotDB::listSubscriptions(const QString &mark)
{
    if (!g_subdb)
        return listSubscriptionsLegacy(mark);
    return g_subdb->listSubscriptions(mark);
}

QStringList BotDB::listSubscriptionsLegacy(const QString &mark)
{
    QStringList result;
    BotDBEnvLease lease(this);
    if (!lease) return result;
    QString ke=mark+":";
    QByteArray prefix = ke.toUtf8();
    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return result;

    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_subscriptions, &cursor) != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return result;
    }

    MDB_val key, value;
    key.mv_data = (void*)prefix.constData();
    key.mv_size = prefix.size();
    int rc = mdb_cursor_get(cursor, &key, &value, MDB_SET_RANGE);

    while (rc == MDB_SUCCESS) {
        QByteArray currentKey((const char*)key.mv_data, key.mv_size);
        if (!currentKey.startsWith(prefix))
            break;

        // 截取 "参数|群ID" 部分（去掉前缀）
        QByteArray suffix = currentKey.mid(prefix.size());
        result.append(QString::fromUtf8(suffix));

        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return result;
}
//批量删除
bool BotDB::clearSubscriptionsByMark(const QString &mark)
{
    if (!g_subdb)
        return clearSubscriptionsByMarkLegacy(mark);
    return g_subdb->clearSubscriptionsByMark(mark);
}

bool BotDB::clearSubscriptionsByMarkLegacy(const QString &mark)
{
    return retryWrite([&](MDB_txn *txn) -> int {
        QString k = mark+":";
        QByteArray prefix =  k.toUtf8();  // "标记:"
        MDB_cursor *cursor = nullptr;
        int rc = mdb_cursor_open(txn, m_dbi_subscriptions, &cursor);
        if (rc != MDB_SUCCESS) return rc;

        MDB_val key, value;
        key.mv_data = (void*)prefix.constData();
        key.mv_size = prefix.size();

        // 定位到该标记的第一个键
        rc = mdb_cursor_get(cursor, &key, &value, MDB_SET_RANGE);

        while (rc == MDB_SUCCESS) {
            QByteArray currentKey((const char*)key.mv_data, key.mv_size);
            // 如果不再以该前缀开头，说明该标记下的数据已遍历完
            if (!currentKey.startsWith(prefix))
                break;

            // 删除当前记录（游标会自动移到下一条）
            rc = mdb_cursor_del(cursor, 0);
            if (rc != MDB_SUCCESS) {
                mdb_cursor_close(cursor);
                return rc;
            }
            // 获取下一条记录
            rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
        }

        mdb_cursor_close(cursor);
        // 如果是因为遍历到末尾而退出（MDB_NOTFOUND），视为正常结束
        if (rc == MDB_NOTFOUND)
            return MDB_SUCCESS;
        return rc;
    });
}
// ==================== 一次性迁移：LMDB subscriptions → SQLite ====================
// 只在「订阅库能打开」且「这个账号库还没搬过」时执行；搬完在 subdb 的 meta 表打标记。
// ⚠ 全程只读旧库（MDB_RDONLY），**旧数据一条都不删** —— 留着当离线备份，
//   要回退只需删掉 subscriptions.db（之后会自动退回 *Legacy 那套 LMDB 实现）。
void BotDB::migrateSubscriptions()
{
    if (!g_subdb || !g_subdb->isOpen()) return;

    const QString tag = QStringLiteral("botdb:") + m_path;
    if (g_subdb->isMigrated(tag)) return;     // 每个账号库只搬一次
    if (!m_env) return;

    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS) return;

    int total = 0, imported = 0, skipped = 0;
    bool iterated = false;          // 只有真的走完一遍才写标记，否则下次启动重试
    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_subscriptions, &cursor) == MDB_SUCCESS) {
        iterated = true;
        MDB_val key, value;
        int rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
        while (rc == MDB_SUCCESS) {
            ++total;
            // 旧 key = "<mark>:<param>|<conv>"。mark 是用户自定义的，可能含 ':' 甚至 '|'，
            // 但 param 是数字、conv（群号/openid）在最后且不含 '|' → 从右往左找最稳。
            const QString k = QString::fromUtf8(static_cast<const char *>(key.mv_data), int(key.mv_size));
            const int pipe  = k.lastIndexOf(QLatin1Char('|'));
            const int colon = k.lastIndexOf(QLatin1Char(':'), pipe);
            if (value.mv_data && pipe > 0 && colon > 0 && colon < pipe) {
                const QString mark = k.left(colon);
                bool okType = false;
                const int ctype = k.mid(colon + 1, pipe - colon - 1).toInt(&okType);
                const QString conv = k.mid(pipe + 1);

                // value = 一串 [uint32 len][utf8 bytes]
                QStringList data;
                const char *raw = static_cast<const char *>(value.mv_data);
                size_t size = value.mv_size, off = 0;
                while (off + sizeof(uint32_t) <= size) {
                    uint32_t len = 0;
                    memcpy(&len, raw + off, sizeof(uint32_t));
                    off += sizeof(uint32_t);
                    if (len > size - off) break;      // 数据损坏，停止解析（跟读路径一致）
                    data.append(QString::fromUtf8(raw + off, int(len)));
                    off += len;
                }

                if (okType && !conv.isEmpty() && !data.isEmpty()
                    && g_subdb->importRow(mark, ctype, conv, data))
                    ++imported;
                else
                    ++skipped;
            } else {
                ++skipped;
            }
            rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
        }
        mdb_cursor_close(cursor);
    }
    mdb_txn_abort(txn);

    if (!iterated) {
        qWarning() << "订阅迁移跳过（打不开 subscriptions 子库），下次启动再试" << m_path;
        return;
    }

    g_subdb->markMigrated(tag);
    qDebug().noquote()
        << QStringLiteral("订阅迁移完成 %1：旧记录 %2 条，导入 %3 条，跳过 %4 条（旧数据保留在 LMDB 里）")
               .arg(m_path).arg(total).arg(imported).arg(skipped);
}

bool BotDB::batchAddGroups(const QList<QString>& groupIdHexList, uint32_t createTimeMinutes)
{
    if (groupIdHexList.isEmpty())
        return true;

    return retryWrite([&](MDB_txn *txn) -> int {
        for (const QString& hex : groupIdHexList) {
            QByteArray keyData = QByteArray::fromHex(hex.toUtf8());
            if (keyData.isEmpty())
                return -1;   // 无效 key

            MDB_val key, value;
            key.mv_data = keyData.data();
            key.mv_size = keyData.size();

            int rc = mdb_get(txn, m_dbi_groups, &key, &value);
            if (rc == MDB_NOTFOUND) {
                // 构造记录：邀请者和 bitmap 均为 0
                GroupRecord record{ createTimeMinutes, 0, 0 };
                rc = putRecord(txn, m_dbi_groups, keyData, &record, sizeof(record));
                if (rc != MDB_SUCCESS)
                    return rc;
            } else if (rc != MDB_SUCCESS) {
                return rc;   // 其他错误（如磁盘问题）
            }
            // 若已存在，跳过
        }
        return MDB_SUCCESS;
    });
}
bool BotDB::batchAddFriends(const QList<uint32_t>& userSeqIds, uint32_t addTimeMinutes)
{
    if (userSeqIds.isEmpty())
        return true;

    return retryWrite([&](MDB_txn *txn) -> int {
        for (uint32_t seq : userSeqIds) {
            QByteArray keyData(reinterpret_cast<const char*>(&seq), sizeof(seq));
            MDB_val key, value;
            key.mv_data = keyData.data();
            key.mv_size = keyData.size();

            int rc = mdb_get(txn, m_dbi_friends, &key, &value);
            if (rc == MDB_NOTFOUND) {
                rc = putRecord(txn, m_dbi_friends, keyData, &addTimeMinutes, sizeof(addTimeMinutes));
                if (rc != MDB_SUCCESS)
                    return rc;
            } else if (rc != MDB_SUCCESS) {
                return rc;
            }
        }
        return MDB_SUCCESS;
    });
}