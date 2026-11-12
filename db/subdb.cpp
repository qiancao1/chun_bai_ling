/*
 * 纯白铃 - QQ 机器人管理平台 - 订阅数据库（SQLite 版）
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

// 设计说明见 subdb.h 顶部。这里只强调两点实现约束：
//   1. 单连接 + 一把递归锁：订阅的读写在定时任务/消息处理里都是零散调用，
//      并发量很低，不值得做连接池；FULLMUTEX + 自己的锁已经足够。
//   2. 所有「读-改-写」都包在 BEGIN IMMEDIATE 里：否则两个线程同时 addSubscription
//      会互相覆盖（后写入的把先写入的条目吃掉），这是 LMDB 版靠写事务自动解决的。

#include "subdb.h"

#include <QDir>
#include <QDebug>
#include <QSet>
#include <QDateTime>

#include <sqlite3.h>

std::unique_ptr<SubDB> g_subdb;

namespace {
const int SUBDB_CACHE_KB = 2048;   // 页缓存硬顶 2MB（订阅量很小，2MB 足够几百张页）
const int SUBDB_BUSY_MS  = 3000;

bool bindText(sqlite3_stmt *st, int idx, const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    // SQLITE_TRANSIENT：让 sqlite 自己拷一份，utf8 是临时对象，出了作用域就没了
    return sqlite3_bind_text(st, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}
} // namespace

SubDB::SubDB(const QString &dirPath)
    : m_dirPath(dirPath)
    , m_filePath(dirPath + QStringLiteral("/subscriptions.db"))
{
}

SubDB::~SubDB()
{
    close();
}

bool SubDB::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_db != nullptr;
}

bool SubDB::exec(const char *sql)
{
    // 调用方必须已持 m_mutex
    if (!m_db) return false;
    char *err = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        qWarning().noquote() << QStringLiteral("SubDB: SQL 失败 [%1] %2 —— %3")
                                    .arg(QString::fromUtf8(sql),
                                         QString::fromUtf8(err ? err : ""),
                                         QString::fromUtf8(sqlite3_errmsg(m_db)));
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

bool SubDB::openConnection()
{
    QMutexLocker locker(&m_mutex);

    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }

    if (!QDir().mkpath(m_dirPath)) {
        qCritical() << "SubDB: 无法创建目录" << m_dirPath;
        return false;
    }

    const QByteArray file = m_filePath.toUtf8();
    const int rc = sqlite3_open_v2(file.constData(), &m_db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "SubDB: 打开失败" << m_filePath
                    << (m_db ? sqlite3_errmsg(m_db) : "sqlite3_open_v2 返回 NULL");
        if (m_db) { sqlite3_close_v2(m_db); m_db = nullptr; }
        return false;
    }
    sqlite3_busy_timeout(m_db, SUBDB_BUSY_MS);

    const QByteArray cacheSql = "PRAGMA cache_size=-" + QByteArray::number(SUBDB_CACHE_KB);

    exec("PRAGMA journal_mode=WAL");            // 追加写，读不阻塞写
    exec("PRAGMA synchronous=NORMAL");          // WAL 下崩溃最多丢最后一次提交，不坏库
    exec(cacheSql.constData());
    exec("PRAGMA mmap_size=0");                 // ⚠ 就是这一条让我们不吃映射内存
    exec("PRAGMA temp_store=MEMORY");
    exec("PRAGMA auto_vacuum=INCREMENTAL");     // 取消订阅删行后能原地把页还回去

    // 主键就是「任务 mark + 会话类型 + 会话 id」，WITHOUT ROWID：
    // listSubscriptions(mark) 变成一次主键前缀范围扫描，不需要额外索引。
    if (!exec("CREATE TABLE IF NOT EXISTS subs ("
              "  mark  TEXT    NOT NULL,"
              "  ctype INTEGER NOT NULL,"
              "  conv  TEXT    NOT NULL,"
              "  data  TEXT    NOT NULL,"
              "  PRIMARY KEY (mark, ctype, conv)"
              ") WITHOUT ROWID")) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
        return false;
    }

    // 迁移标记表（键值对，只写几条）
    exec("CREATE TABLE IF NOT EXISTS meta ("
         "  k TEXT PRIMARY KEY,"
         "  v TEXT"
         ") WITHOUT ROWID");

    qDebug().noquote() << QStringLiteral("SubDB 打开完成(SQLite) %1 现有订阅 %2 条 页缓存上限 %3 KB")
                              .arg(m_filePath).arg(rowCount()).arg(SUBDB_CACHE_KB);
    return true;
}

bool SubDB::open()
{
    return openConnection();
}

void SubDB::close()
{
    QMutexLocker locker(&m_mutex);
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

// ==================== 内部：读 data 列（调用方持锁） ====================
// 没有记录 / 读失败 → 空列表；data 列是 "a|||b|||c" 形式
static QStringList readDataLocked(sqlite3 *db, const QString &mark, int ctype, const QString &conv)
{
    QStringList out;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT data FROM subs WHERE mark=?1 AND ctype=?2 AND conv=?3",
                           -1, &st, nullptr) != SQLITE_OK)
        return out;

    bindText(st, 1, mark);
    sqlite3_bind_int(st, 2, ctype);
    bindText(st, 3, conv);

    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(st, 0);
        const QString all = txt ? QString::fromUtf8(reinterpret_cast<const char *>(txt)) : QString();
        if (!all.isEmpty())
            out = all.split(QStringLiteral("|||"));
    }
    sqlite3_finalize(st);
    return out;
}

// ==================== 添加 ====================
bool SubDB::addSubscription(const QString &mark, int ctype, const QString &conv, const QStringList &dataList)
{
    if (mark.isEmpty() || conv.isEmpty() || dataList.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    // IMMEDIATE：一开始就拿写锁，避免「读到旧值 → 各自追加 → 后写的覆盖先写的」
    if (!exec("BEGIN IMMEDIATE"))
        return false;

    QStringList cur = readDataLocked(m_db, mark, ctype, conv);
    QSet<QString> seen(cur.constBegin(), cur.constEnd());
    bool anyAdded = false;
    for (const QString &item : dataList) {
        if (item.isEmpty() || seen.contains(item)) continue;
        seen.insert(item);
        cur.append(item);
        anyAdded = true;
    }

    if (!anyAdded) {
        exec("COMMIT");      // 没有新条目，什么都不用写（跟 LMDB 版行为一致：算成功）
        return true;
    }

    bool ok = false;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db,
                           "INSERT INTO subs(mark, ctype, conv, data) VALUES(?1, ?2, ?3, ?4) "
                           "ON CONFLICT(mark, ctype, conv) DO UPDATE SET data=excluded.data",
                           -1, &st, nullptr) == SQLITE_OK) {
        bindText(st, 1, mark);
        sqlite3_bind_int(st, 2, ctype);
        bindText(st, 3, conv);
        bindText(st, 4, cur.join(QStringLiteral("|||")));
        ok = (sqlite3_step(st) == SQLITE_DONE);
        if (!ok)
            qWarning() << "SubDB: 写入订阅失败" << mark << ctype << conv << sqlite3_errmsg(m_db);
    }
    sqlite3_finalize(st);

    exec(ok ? "COMMIT" : "ROLLBACK");
    return ok;
}

// ==================== 删除 ====================
bool SubDB::removeSubscription(const QString &mark, int ctype, const QString &conv, const QStringList &dataList)
{
    if (mark.isEmpty() || conv.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    // 空列表 = 删掉整个目标（取消订阅时最常见的路径）
    if (dataList.isEmpty()) {
        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(m_db, "DELETE FROM subs WHERE mark=?1 AND ctype=?2 AND conv=?3",
                               -1, &st, nullptr) != SQLITE_OK)
            return false;
        bindText(st, 1, mark);
        sqlite3_bind_int(st, 2, ctype);
        bindText(st, 3, conv);
        const bool ok = (sqlite3_step(st) == SQLITE_DONE);
        sqlite3_finalize(st);
        return ok;
    }

    if (!exec("BEGIN IMMEDIATE"))
        return false;

    QStringList cur = readDataLocked(m_db, mark, ctype, conv);
    if (cur.isEmpty()) {
        exec("COMMIT");      // 本来就没有，视为成功
        return true;
    }

    QSet<QString> targets(dataList.constBegin(), dataList.constEnd());
    QStringList remain;
    remain.reserve(cur.size());
    for (const QString &item : cur) {
        if (!targets.contains(item)) remain.append(item);
    }

    if (remain.size() == cur.size()) {
        exec("COMMIT");      // 一条都没命中，不用改
        return true;
    }

    bool ok = false;
    sqlite3_stmt *st = nullptr;
    if (remain.isEmpty()) {
        if (sqlite3_prepare_v2(m_db, "DELETE FROM subs WHERE mark=?1 AND ctype=?2 AND conv=?3",
                               -1, &st, nullptr) == SQLITE_OK) {
            bindText(st, 1, mark);
            sqlite3_bind_int(st, 2, ctype);
            bindText(st, 3, conv);
            ok = (sqlite3_step(st) == SQLITE_DONE);
        }
    } else {
        if (sqlite3_prepare_v2(m_db, "UPDATE subs SET data=?4 WHERE mark=?1 AND ctype=?2 AND conv=?3",
                               -1, &st, nullptr) == SQLITE_OK) {
            bindText(st, 1, mark);
            sqlite3_bind_int(st, 2, ctype);
            bindText(st, 3, conv);
            bindText(st, 4, remain.join(QStringLiteral("|||")));
            ok = (sqlite3_step(st) == SQLITE_DONE);
        }
    }
    sqlite3_finalize(st);

    exec(ok ? "COMMIT" : "ROLLBACK");
    return ok;
}

// ==================== 查询 ====================
QString SubDB::getSubscriptions(const QString &mark, int ctype, const QString &conv) const
{
    if (mark.isEmpty() || conv.isEmpty())
        return QString();

    QMutexLocker locker(&m_mutex);
    if (!m_db) return QString();

    return readDataLocked(m_db, mark, ctype, conv).join(QStringLiteral("|||"));
}

QStringList SubDB::listSubscriptions(const QString &mark) const
{
    QStringList result;
    if (mark.isEmpty()) return result;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return result;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT ctype, conv FROM subs WHERE mark=?1 ORDER BY ctype, conv",
                           -1, &st, nullptr) != SQLITE_OK)
        return result;

    bindText(st, 1, mark);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const int ctype = sqlite3_column_int(st, 0);
        const unsigned char *txt = sqlite3_column_text(st, 1);
        const QString conv = txt ? QString::fromUtf8(reinterpret_cast<const char *>(txt)) : QString();
        // 调用方（scheduleconfigwidget）就是按 '|' 切出 type 和 id 的，格式必须一致
        result.append(QStringLiteral("%1|%2").arg(ctype).arg(conv));
    }
    sqlite3_finalize(st);
    return result;
}

bool SubDB::clearSubscriptionsByMark(const QString &mark)
{
    if (mark.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM subs WHERE mark=?1", -1, &st, nullptr) != SQLITE_OK)
        return false;
    bindText(st, 1, mark);
    const bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

// ==================== 迁移辅助 ====================
bool SubDB::importRow(const QString &mark, int ctype, const QString &conv, const QStringList &dataList)
{
    if (mark.isEmpty() || conv.isEmpty() || dataList.isEmpty())
        return false;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    sqlite3_stmt *st = nullptr;
    // INSERT OR IGNORE：新库里已有的（迁移后又加过的）不覆盖
    if (sqlite3_prepare_v2(m_db,
                           "INSERT OR IGNORE INTO subs(mark, ctype, conv, data) VALUES(?1, ?2, ?3, ?4)",
                           -1, &st, nullptr) != SQLITE_OK)
        return false;
    bindText(st, 1, mark);
    sqlite3_bind_int(st, 2, ctype);
    bindText(st, 3, conv);
    bindText(st, 4, dataList.join(QStringLiteral("|||")));
    const bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

bool SubDB::isMigrated(const QString &tag) const
{
    if (tag.isEmpty()) return false;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT 1 FROM meta WHERE k=?1", -1, &st, nullptr) != SQLITE_OK)
        return false;
    bindText(st, 1, tag);
    const bool found = (sqlite3_step(st) == SQLITE_ROW);
    sqlite3_finalize(st);
    return found;
}

void SubDB::markMigrated(const QString &tag)
{
    if (tag.isEmpty()) return;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT OR REPLACE INTO meta(k, v) VALUES(?1, ?2)",
                           -1, &st, nullptr) != SQLITE_OK)
        return;
    bindText(st, 1, tag);
    bindText(st, 2, QDateTime::currentDateTime().toString(Qt::ISODate));
    sqlite3_step(st);
    sqlite3_finalize(st);
}

qint64 SubDB::rowCount() const
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return 0;

    qint64 n = 0;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM subs", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW)
            n = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
    }
    return n;
}
