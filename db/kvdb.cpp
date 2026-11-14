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

#include "kvdb.h"

#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

#include <sqlite3.h>

// lmdb 只用于「把旧 data.mdb 一次性迁过来」这一件事，迁完就可以整体删掉。
#include <lmdb.h>

// ---- SQLite 参数 ----
// cache_size 是「负值 = KB」，这里是**硬上限**：库涨到几个 G，进程里也只有这 2MB 页缓存。
static const int KV_CACHE_KB = 2 * 1024;
static const int KV_BUSY_MS  = 3000;    // 撞锁最多等 3 秒（单连接 + 我们的锁，正常撞不到）
static const int KV_MIGRATE_BATCH = 500; // 迁移时每多少条提交一次，免得 WAL 涨成一个巨块

// 静默执行一条 SQL
static bool kvExecQuiet(sqlite3 *db, const char *sql)
{
    if (!db || !sql) return false;
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// 执行并打印错误（带 tag 方便定位）
static bool kvExecLog(sqlite3 *db, const char *sql, const char *tag)
{
    if (!db || !sql) return false;
    char *msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        qWarning() << "KvDB:" << tag << "失败" << (msg ? msg : sqlite3_errmsg(db));
        if (msg) sqlite3_free(msg);
        return false;
    }
    if (msg) sqlite3_free(msg);
    return true;
}

// 从缓存值里取过期秒。本库惯例：值 = "<过期秒>||||<内容>"；
// 早期媒体缓存是 "[<类型>,path=…,md5=…,Time=<过期秒>]"。返回 -1 = 认不出（不动它）。
static qint64 kvExpireSec(const QByteArray &v)
{
    const int sep = v.indexOf("||||");
    if (sep > 0) {
        bool ok = false;
        const qint64 e = v.left(sep).trimmed().toLongLong(&ok);
        if (ok) return e;
    }
    if (v.startsWith('[')) {
        const int ti = v.lastIndexOf(",Time=");
        if (ti >= 0) {
            QByteArray s = v.mid(ti + 6);
            const int rb = s.indexOf(']');
            if (rb >= 0) s.truncate(rb);
            bool ok = false;
            const qint64 e = s.trimmed().toLongLong(&ok);
            if (ok) return e;
        }
    }
    return -1;
}

// ========== 构造 / 析构 ==========
// parent 只为兼容旧的 `new LmdbKV(path, parent)` 写法，本类不需要 QObject 生命周期。
KvDB::KvDB(const QString &dbPath, void *parent)
    : m_db(nullptr)
    , m_dbPath(dbPath)
    , m_filePath(dbPath + QStringLiteral("/kv.db"))
    , m_stmtPut(nullptr)
    , m_stmtGet(nullptr)
    , m_stmtDel(nullptr)
{
    Q_UNUSED(parent);
    openInternal();
}

KvDB::~KvDB()
{
    close();
}

bool KvDB::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_db != nullptr;
}

// 幂等关闭：关完 m_db 置空，后续 get/put/remove 一看为空就直接返回空/false。
// 这样「先关库、再删目录」才删得掉（Windows 下还有句柄就删不掉 kv.db / kv.db-wal）。
void KvDB::close()
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return;
    finalizeStatements();
    sqlite3_close_v2(m_db);   // v2 = 允许延迟关闭（万一还有 stmt 没 finalize 也不报 BUSY）
    m_db = nullptr;
}

// 三个热点 SQL 只让 SQLite 解析一次。之后每次 put/get/remove 只做
// reset + re-bind + step —— 省掉 sqlite3_prepare_v2 的词法/语法分析。
bool KvDB::prepareStatements()
{
    if (!m_db) return false;
    if (m_stmtPut) return true;

    static const char *kPut =
        "INSERT INTO kv(k,v) VALUES(?1,?2) ON CONFLICT(k) DO UPDATE SET v=excluded.v";
    static const char *kGet = "SELECT v FROM kv WHERE k=?1";
    static const char *kDel = "DELETE FROM kv WHERE k=?1";

    if (sqlite3_prepare_v2(m_db, kPut, -1, &m_stmtPut, nullptr) != SQLITE_OK
        || sqlite3_prepare_v2(m_db, kGet, -1, &m_stmtGet, nullptr) != SQLITE_OK
        || sqlite3_prepare_v2(m_db, kDel, -1, &m_stmtDel, nullptr) != SQLITE_OK) {
        qWarning() << "KvDB:" << m_dbPath << "prepare 失败" << sqlite3_errmsg(m_db);
        finalizeStatements();
        return false;
    }
    return true;
}

void KvDB::finalizeStatements()
{
    if (m_stmtPut) { sqlite3_finalize(m_stmtPut); m_stmtPut = nullptr; }
    if (m_stmtGet) { sqlite3_finalize(m_stmtGet); m_stmtGet = nullptr; }
    if (m_stmtDel) { sqlite3_finalize(m_stmtDel); m_stmtDel = nullptr; }
}

// ========== 打开 ==========
bool KvDB::openInternal()
{
    // ⚠ 打开/建表这一段单独括起来：下面的 migrateFromLmdb() 会调 putUnlocked()，
    //   而 putUnlocked 自己也要拿 m_mutex —— QMutex 不可重入，持锁去调必定死锁。
    {
    QMutexLocker locker(&m_mutex);
    if (m_db) {
        finalizeStatements();
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }

    if (!QDir().mkpath(m_dbPath)) {
        qCritical() << "KvDB: 无法创建数据库目录" << m_dbPath;
        return false;
    }

    // 最多试两次：第一次若是「文件不是数据库 / 已损坏」，把库改名留证后重建一次。
    // （SQLite 坏库只会返回错误码，不会像 LMDB 那样直接 AV 闪退 —— 这里顺手自愈。）
    for (int attempt = 0; attempt < 2; ++attempt) {
        const QByteArray file = QFile::encodeName(m_filePath);
        int rc = sqlite3_open_v2(file.constData(), &m_db,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK) {
            qCritical() << "KvDB: 打开失败" << m_filePath
                        << (m_db ? sqlite3_errmsg(m_db) : "sqlite3_open_v2 返回 NULL");
            if (m_db) { sqlite3_close_v2(m_db); m_db = nullptr; }
            return false;
        }
        sqlite3_busy_timeout(m_db, KV_BUSY_MS);

        // 表结构先建；坏库里这一步就会报 NOTADB / CORRUPT
        const bool schemaOk = ensureSchema();

        const QByteArray cacheSql = "PRAGMA cache_size=-" + QByteArray::number(KV_CACHE_KB);
        bool pragmaOk = true;
        pragmaOk = kvExecLog(m_db, "PRAGMA journal_mode=WAL",     "journal_mode=WAL") && pragmaOk;
        pragmaOk = kvExecLog(m_db, "PRAGMA synchronous=NORMAL",   "synchronous=NORMAL") && pragmaOk;
        pragmaOk = kvExecLog(m_db, cacheSql.constData(),          "cache_size") && pragmaOk;
        pragmaOk = kvExecLog(m_db, "PRAGMA mmap_size=0",          "mmap_size=0") && pragmaOk;   // ⚠ 明确不用 mmap
        pragmaOk = kvExecLog(m_db, "PRAGMA temp_store=MEMORY",    "temp_store=MEMORY") && pragmaOk;

        if (schemaOk && pragmaOk) break;   // 正常

        const int ec = m_db ? sqlite3_errcode(m_db) : SQLITE_ERROR;
        const bool broken = (ec == SQLITE_NOTADB || ec == SQLITE_CORRUPT);
        if (!broken || attempt == 1) {
            qCritical() << "KvDB: 初始化失败，本库当作不可用" << m_filePath
                        << (m_db ? sqlite3_errmsg(m_db) : "");
            if (m_db) { sqlite3_close_v2(m_db); m_db = nullptr; }
            return false;
        }

        // 损坏 → 改名留证再重建（缓存库丢一份不影响业务，闪退才是大事）
        if (m_db) { sqlite3_close_v2(m_db); m_db = nullptr; }
        const QString bad = m_filePath + QStringLiteral(".corrupt_")
                            + QString::number(QDateTime::currentSecsSinceEpoch());
        QFile::remove(m_filePath + QStringLiteral("-wal"));
        QFile::remove(m_filePath + QStringLiteral("-shm"));
        if (QFile::exists(m_filePath) && QFile::rename(m_filePath, bad))
            qCritical() << "KvDB: 库文件损坏，已改名为" << bad << "并重建空库";
        else
            QFile::remove(m_filePath);
    }

    if (!m_db) return false;
    prepareStatements();
    }   // ← 这里放锁：迁移会自己加锁

    // 旧 LMDB 一次性迁移（只在还没迁过的时候跑；没旧库就只打标记）
    if (!hasMigrated())
        migrateFromLmdb();

    return m_db != nullptr;
}

bool KvDB::ensureSchema()
{
    if (!m_db) return false;
    // WITHOUT ROWID + BLOB 主键 = 一棵 B-tree，跟原来的 LMDB 子库同构，
    // 等值查询 O(log n)，不需要额外的 rowid 表。
    if (!kvExecLog(m_db,
                   "CREATE TABLE IF NOT EXISTS kv(k BLOB PRIMARY KEY, v BLOB NOT NULL) WITHOUT ROWID",
                   "create kv"))
        return false;
    if (!kvExecLog(m_db,
                   "CREATE TABLE IF NOT EXISTS meta(k TEXT PRIMARY KEY, v TEXT)",
                   "create meta"))
        return false;
    return true;
}

// ========== 迁移标记 ==========
bool KvDB::hasMigrated()
{
    if (!m_db) return true;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT 1 FROM meta WHERE k='migrated'", -1, &st, nullptr) != SQLITE_OK)
        return true;    // 查不了就当已迁（别反复折腾）
    const bool found = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return found;
}

void KvDB::markMigrated()
{
    if (!m_db) return;
    kvExecQuiet(m_db, "INSERT INTO meta(k,v) VALUES('migrated','1') "
                      "ON CONFLICT(k) DO UPDATE SET v=excluded.v");
}

// ========== 旧 LMDB → SQLite 一次性迁移 ==========
// 只读打开旧的 data.mdb，把全部键值导进来；**旧文件一条不动**，留在磁盘上当离线备份。
// 打标记之后不再尝试（旧库若已经坏了也打标记，免得每次启动都白扫一遍）。
void KvDB::migrateFromLmdb()
{
    const QString legacyData = m_dbPath + QStringLiteral("/data.mdb");
    if (!QFileInfo::exists(legacyData)) {
        markMigrated();      // 没有旧库，直接打标记，以后不再检查
        return;
    }

    // 旧库可能比 16MB 大得多：mapsize 按文件体积给（LMDB 的 mapsize 只是地址空间预留）
    const qint64 sz = QFileInfo(legacyData).size();
    const size_t mapsize = static_cast<size_t>(
        qMax<qint64>(64LL * 1024 * 1024, sz * 2 + 16LL * 1024 * 1024));

    MDB_env *env = nullptr;
    if (mdb_env_create(&env) != MDB_SUCCESS) {
        qWarning() << "KvDB: 迁移失败，mdb_env_create" << m_dbPath;
        markMigrated();
        return;
    }
    mdb_env_set_mapsize(env, mapsize);

    const QByteArray dir = QFile::encodeName(m_dbPath);
    int rc = mdb_env_open(env, dir.constData(), MDB_RDONLY, 0664);
    if (rc != MDB_SUCCESS) {
        qWarning() << "KvDB: 迁移失败，旧库打不开" << m_dbPath << mdb_strerror(rc);
        mdb_env_close(env);
        markMigrated();
        return;
    }

    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) {
        qWarning() << "KvDB: 迁移失败，mdb_txn_begin" << m_dbPath << mdb_strerror(rc);
        mdb_env_close(env);
        markMigrated();
        return;
    }

    MDB_dbi dbi;
    rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc != MDB_SUCCESS) {
        qWarning() << "KvDB: 迁移失败，mdb_dbi_open" << m_dbPath << mdb_strerror(rc);
        mdb_txn_abort(txn);
        mdb_env_close(env);
        markMigrated();
        return;
    }

    MDB_cursor *cur = nullptr;
    rc = mdb_cursor_open(txn, dbi, &cur);
    if (rc != MDB_SUCCESS) {
        qWarning() << "KvDB: 迁移失败，mdb_cursor_open" << m_dbPath << mdb_strerror(rc);
        mdb_txn_abort(txn);
        mdb_env_close(env);
        markMigrated();
        return;
    }

    int imported = 0;
    int inBatch   = 0;
    kvExecQuiet(m_db, "BEGIN");
    MDB_val k, v;
    while (mdb_cursor_get(cur, &k, &v, MDB_NEXT) == MDB_SUCCESS) {
        const QByteArray key(static_cast<const char*>(k.mv_data), static_cast<int>(k.mv_size));
        const QByteArray val(static_cast<const char*>(v.mv_data), static_cast<int>(v.mv_size));
        if (key.isEmpty()) continue;
        if (putUnlocked(key, val)) ++imported;
        if (++inBatch >= KV_MIGRATE_BATCH) {
            kvExecQuiet(m_db, "COMMIT");
            kvExecQuiet(m_db, "BEGIN");
            inBatch = 0;
        }
    }
    kvExecQuiet(m_db, "COMMIT");   // 最后一批

    mdb_cursor_close(cur);
    mdb_txn_abort(txn);            // 只读事务 abort 即可
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);            // 这一步把旧文件的映射彻底还回去

    markMigrated();
    qDebug() << "KvDB: 已从旧 LMDB 迁移" << imported << "条 →" << m_filePath;
}

// ========== 写 ==========
bool KvDB::put(const QString &key, const QString &value)
{
    return putUnlocked(key.toUtf8(), value.toUtf8());
}

bool KvDB::put(const QByteArray &key, const QByteArray &value)
{
    return putUnlocked(key, value);
}

bool KvDB::putUnlocked(const QByteArray &key, const QByteArray &value)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db || key.isEmpty()) return false;
    if (!m_stmtPut && !prepareStatements()) return false;

    // 常驻 stmt：reset + 重新绑定即可，**不用再 prepare**（省掉每次的 SQL 解析）。
    sqlite3_reset(m_stmtPut);
    sqlite3_clear_bindings(m_stmtPut);
    sqlite3_bind_blob(m_stmtPut, 1, key.constData(), key.size(), SQLITE_TRANSIENT);
    // ⚠ 空值不能用 bind_blob(nullptr,0)：那会绑成 NULL，撞 v NOT NULL。
    if (value.isEmpty())
        sqlite3_bind_zeroblob(m_stmtPut, 2, 0);
    else
        sqlite3_bind_blob(m_stmtPut, 2, value.constData(), value.size(), SQLITE_TRANSIENT);

    const int rc = sqlite3_step(m_stmtPut);
    if (rc != SQLITE_DONE) {
        qWarning() << "KvDB:" << m_dbPath << "put 失败" << sqlite3_errmsg(m_db);
        return false;
    }
    return true;
}

// ========== 读 ==========
QString KvDB::get(const QString &key) const
{
    return QString::fromUtf8(getUnlocked(key.toUtf8()));
}

QByteArray KvDB::get(const QByteArray &key) const
{
    return getUnlocked(key);
}

QByteArray KvDB::getUnlocked(const QByteArray &key) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_db || key.isEmpty()) return QByteArray();
    if (!m_stmtGet && !const_cast<KvDB*>(this)->prepareStatements()) return QByteArray();

    sqlite3_reset(m_stmtGet);
    sqlite3_clear_bindings(m_stmtGet);
    sqlite3_bind_blob(m_stmtGet, 1, key.constData(), key.size(), SQLITE_TRANSIENT);

    QByteArray out;
    if (sqlite3_step(m_stmtGet) == SQLITE_ROW) {
        const void *p = sqlite3_column_blob(m_stmtGet, 0);
        const int   n = sqlite3_column_bytes(m_stmtGet, 0);
        if (p && n > 0) out = QByteArray(static_cast<const char*>(p), n);
    }
    return out;
}

// ========== 删 ==========
bool KvDB::remove(const QString &key)
{
    return removeInternal(key.toUtf8());
}

bool KvDB::remove(const QByteArray &key)
{
    return removeInternal(key);
}

bool KvDB::removeInternal(const QByteArray &key)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db || key.isEmpty()) return false;
    if (!m_stmtDel && !prepareStatements()) return false;

    sqlite3_reset(m_stmtDel);
    sqlite3_clear_bindings(m_stmtDel);
    sqlite3_bind_blob(m_stmtDel, 1, key.constData(), key.size(), SQLITE_TRANSIENT);
    const int rc = sqlite3_step(m_stmtDel);
    if (rc != SQLITE_DONE) {
        qWarning() << "KvDB:" << m_dbPath << "remove 失败" << sqlite3_errmsg(m_db);
        return false;
    }
    return true;
}

bool KvDB::removeBatch(const QList<QByteArray> &keys)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db || keys.isEmpty()) return true;

    if (!kvExecQuiet(m_db, "BEGIN")) {
        qWarning() << "KvDB:" << m_dbPath << "removeBatch BEGIN 失败" << sqlite3_errmsg(m_db);
        return false;
    }

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM kv WHERE k=?1", -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "KvDB:" << m_dbPath << "removeBatch 准备失败" << sqlite3_errmsg(m_db);
        kvExecQuiet(m_db, "ROLLBACK");
        return false;
    }

    bool ok = true;
    for (const QByteArray &key : keys) {
        if (key.isEmpty()) continue;
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);
        sqlite3_bind_blob(st, 1, key.constData(), key.size(), SQLITE_TRANSIENT);
        if (sqlite3_step(st) != SQLITE_DONE) {
            qWarning() << "KvDB:" << m_dbPath << "removeBatch 失败" << sqlite3_errmsg(m_db);
            ok = false;
            break;
        }
    }
    sqlite3_finalize(st);

    if (!ok) {
        kvExecQuiet(m_db, "ROLLBACK");
        return false;
    }
    if (!kvExecQuiet(m_db, "COMMIT")) {
        qWarning() << "KvDB:" << m_dbPath << "removeBatch COMMIT 失败" << sqlite3_errmsg(m_db);
        kvExecQuiet(m_db, "ROLLBACK");
        return false;
    }
    return true;
}

// ========== 全量键 / 过期清理 ==========
QList<QByteArray> KvDB::getAllKeysByteArray() const
{
    QMutexLocker locker(&m_mutex);
    QList<QByteArray> keys;
    if (!m_db) return keys;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT k FROM kv", -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "KvDB:" << m_dbPath << "getAllKeys 准备失败" << sqlite3_errmsg(m_db);
        return keys;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        const void *p = sqlite3_column_blob(st, 0);
        const int   n = sqlite3_column_bytes(st, 0);
        if (p && n > 0) keys.append(QByteArray(static_cast<const char*>(p), n));
    }
    sqlite3_finalize(st);
    return keys;
}

QStringList KvDB::getAllKeys() const
{
    const QList<QByteArray> byteKeys = getAllKeysByteArray();
    QStringList strKeys;
    strKeys.reserve(byteKeys.size());
    for (const QByteArray &ba : byteKeys)
        strKeys.append(QString::fromUtf8(ba));
    return strKeys;
}

int KvDB::removeExpired(qint64 nowSec)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return -1;

    // 过期秒藏在值的前缀里，SQL 解析不了 → 照样全表扫一遍再批量删。
    QList<QByteArray> dead;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT k, v FROM kv", -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "KvDB:" << m_dbPath << "removeExpired 准备失败" << sqlite3_errmsg(m_db);
        return -1;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        const void *kp = sqlite3_column_blob(st, 0);
        const int   kn = sqlite3_column_bytes(st, 0);
        const void *vp = sqlite3_column_blob(st, 1);
        const int   vn = sqlite3_column_bytes(st, 1);
        if (!kp || kn <= 0 || !vp || vn <= 0) continue;
        const QByteArray v(static_cast<const char*>(vp), vn);
        const qint64 expire = kvExpireSec(v);
        if (expire >= 0 && nowSec >= expire)
            dead.append(QByteArray(static_cast<const char*>(kp), kn));
    }
    sqlite3_finalize(st);

    if (dead.isEmpty()) return 0;

    int removed = 0;
    if (!kvExecQuiet(m_db, "BEGIN")) return -1;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM kv WHERE k=?1", -1, &st, nullptr) == SQLITE_OK) {
        for (const QByteArray &k : dead) {
            sqlite3_reset(st);
            sqlite3_clear_bindings(st);
            sqlite3_bind_blob(st, 1, k.constData(), k.size(), SQLITE_TRANSIENT);
            if (sqlite3_step(st) == SQLITE_DONE) ++removed;
        }
        sqlite3_finalize(st);
    }
    if (!kvExecQuiet(m_db, "COMMIT")) {
        kvExecQuiet(m_db, "ROLLBACK");
        return -1;
    }
    return removed;
}
