/*
 * 纯白铃 - QQ 机器人管理平台 - 日志数据库（SQLite 版）
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

// ════════════════════════════════════════════════════════════════════════════
// 2026-11-11：从 LMDB 换成 SQLite。
//
// 为什么换 —— LMDB 和「日志」这种负载是反着来的：
//   ① 删条目不缩文件。data.mdb 会永远停在历史峰值（实测 logdb_1 = 198.9MB /
//      10 万条），想缩只能「重写整个库」的 compact，贵到要 3 天冷却。
//   ② 清理必须全表遍历。10 万条就是 199MB 的页全被摸进进程工作集，而 Windows
//      在内存不紧张时永不回收 → 每天 05:00 过后任务管理器涨一大截。
//   ③ 整文件 mmap。碰过的页一直算在进程头上（这就是「600MB」里那 200MB）。
//   ④ key 是 seq:appid:group（按全局序号排序），「取某会话最近 N 条」只能倒扫
//      全库再按 appid 过滤 —— 聊天页开一个会话就要走几千条。
//
// SQLite 这边：
//   · cache_size 是**硬上限**（8MB），无论库多大，常驻就这么点；
//   · mmap_size=0，数据页走 read()，根本不会把整个文件映射进进程；
//   · 删旧走主键（seq INTEGER PRIMARY KEY）O(log n)，只碰要删那一段；
//   · auto_vacuum=INCREMENTAL 原地释放空闲页，不需要重写整库；
//   · (appid,conv,seq) 索引让「取某会话最近 N 条」变成一次索引范围扫描。
//
// 对外接口一行没改（只把 MDB_txn* 换成了不透明的 LogTxn），所以
// logpage / chatpage / websocketserver / plts / api2 / global 全都不用动。
// ════════════════════════════════════════════════════════════════════════════

#include "logdb.h"
#include <QDebug>
#include <QDir>
#include <QDataStream>
#include <QDateTime>
#include <QSet>
#include <QThreadPool>
#include <QRunnable>
#include <QFile>
#include <QFileInfo>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>

#ifdef Q_OS_WIN
// qt_windows.h 内部已定义 NOMINMAX / WIN32_LEAN_AND_MEAN，不会把 std::min/std::max 打成宏
#include <qt_windows.h>
#endif

// 序号填充位数（makeKey 用；seqFromKey 也按这个宽度取）
static const int SEQ_DIGITS = 20;

// 缓存积压的硬上限：正常 5 秒刷一次，这里只兜「写不进去」的异常路径
// （坏记录 / 磁盘满 / I-O 错误），否则 appendLog 会一直往里堆 = 无界涨内存。
static const int MAX_PENDING_CACHE = 20000;

// ---- 被动回复 msgid 的有效窗口（2026-10-11）----
// 被动回复用的 msg_id 有效期：群 5 分钟、好友 1 小时（用户口径）。超窗的 msg_id 发出去会失败，
// 所以按各自窗口取最近一条即可；拿不到就传空 msgid 走主动发送。
static const qint64 GROUP_MSGID_WINDOW_SECS  = 5 * 60;      // 群消息：5 分钟
static const qint64 FRIEND_MSGID_WINDOW_SECS = 60 * 60;     // 好友/单聊：1 小时

// ---- SQLite 参数 ----
// cache_size 是「负值 = KB」，这里是**硬上限**：不管 logs.db 是 200MB 还是 2GB，
// 进程里就只有这 8MB 页缓存。这才是「设 100 万条也不吃内存」的关键。
static const int LOGDB_CACHE_KB   = 8 * 1024;
static const int LOGDB_BUSY_MS    = 5000;      // 撞锁最多等 5 秒（单连接 + 我们的锁，正常撞不到）
// 每次回收空闲页的上限（页数）：一次还太多会把清理拖到几秒，分批更稳
static const int LOGDB_VACUUM_PAGES = 2000;

// "yyyy-MM-dd hh:mm:ss"（入站消息）与 "yyyy-MM-dd HH:mm:ss"（出站/日志）两种写法都吃
static qint64 parseLogTimeString(const QString &ts)
{
    if (ts.isEmpty()) return 0;
    QDateTime dt = QDateTime::fromString(ts, "yyyy-MM-dd HH:mm:ss");
    if (!dt.isValid()) dt = QDateTime::fromString(ts, "yyyy-MM-dd hh:mm:ss");
    if (!dt.isValid()) dt = QDateTime::fromString(ts, Qt::ISODate);
    return dt.isValid() ? dt.toSecsSinceEpoch() : 0;
}

// 只读出记录的前 4 个字段（user / msg / isSelf / timestamp）拿时间戳。
// 扫描时每条都要判一次时间，全量 deserializeMessage 太贵，这里只取到 timestamp 就止步。
static qint64 logBlobTimeSecs(const QByteArray &blob)
{
    QDataStream stream(blob);
    stream.setVersion(QDataStream::Qt_5_15);
    QString user, msg, ts;
    bool isSelf = false;
    stream >> user >> msg >> isSelf >> ts;
    if (stream.status() != QDataStream::Ok) return 0;   // 旧格式/坏记录 → 0=未知
    return parseLogTimeString(ts);
}

// 静默执行一条 SQL（失败只返回 false，不打印）
static bool execQuiet(sqlite3 *db, const char *sql)
{
    if (!db || !sql) return false;
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

// 执行并打印错误（带 tag 方便定位）
static bool execLog(sqlite3 *db, const char *sql, const char *tag)
{
    if (!db || !sql) return false;
    char *msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        qWarning() << "LogDB:" << tag << "失败" << (msg ? msg : sqlite3_errmsg(db));
        if (msg) sqlite3_free(msg);
        return false;
    }
    if (msg) sqlite3_free(msg);
    return true;
}

// ========== 构造函数、析构函数 ==========
LogDB::LogDB(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_bufferSize(10000)
    , m_buffer(nullptr)
    , m_dbPath(dbPath)
    , m_currentTxn(nullptr)
{
    // 环形缓冲 + 刷库定时器由 ensureRuntime 统一创建：
    // close() 会把这两个拆掉，而 open() 允许在 close() 之后再调（= 重开），
    // 所以「建」必须能重复执行，不能只写在构造函数里。
    ensureRuntime();
}

// 补齐运行期资源（环形缓冲 + 刷库定时器）。已存在就什么都不做，可反复调用。
// ⚠ 必须在主线程调用（QTimer 的 start 要求事件循环所在线程）。
void LogDB::ensureRuntime()
{
    if (!m_buffer) {
        m_buffer = new std::atomic<uint64_t>[m_bufferSize];
        for (size_t i = 0; i < m_bufferSize; ++i)
            m_buffer[i].store(0, std::memory_order_relaxed);
    }
    if (!m_flushTimer) {
        m_flushTimer = new QTimer(this);
        m_flushTimer->setInterval(5000);
        connect(m_flushTimer, &QTimer::timeout, this, &LogDB::flushCacheToDB);
        m_flushTimer->start();
    }
}

LogDB::~LogDB()
{
    close();
}

// ========== 关闭 ==========
// ⚠ close() 会把「运行期资源」（环形缓冲、刷库定时器）一起拆掉 —— 之后这个对象
//   只能靠 open() 重开：open() 第一件事就是 ensureRuntime() 把它们重新建出来。
//   所以「关了再开」是安全的，「关了继续 appendLog」不是（会把 m_buffer 当空指针写）。
void LogDB::close()
{
    // 0. 后台清理可能正持着 m_mutex（几万条 DELETE 要几秒）——必须等它跑完再关连接
    if (m_cleanPool) m_cleanPool->waitForDone();

    // 1. 停止定时器，防止在刷库过程中再次触发
    if (m_flushTimer) {
        m_flushTimer->stop();
        delete m_flushTimer;
        m_flushTimer = nullptr;
    }

    {
        QMutexLocker locker(&m_mutex);
        // 2. 外部开着的只读/写事务：直接回滚。没提交的数据本来就不该留，
        //    而且 SQLite 单连接上挂着事务去 close 会返回 SQLITE_BUSY。
        if (m_currentTxn) {
            if (m_db) execQuiet(m_db, "ROLLBACK");
            m_currentTxn = nullptr;
        }
        // 3. 刷库（将缓存数据写入数据库）—— 已在锁内，用不加锁版本
        flushCacheToDBUnlocked();

        // 4. 关连接
        if (m_db) {
            sqlite3_close_v2(m_db);   // v2 = 允许延迟关闭（万一还有 stmt 没 finalize 也不报 BUSY）
            m_db = nullptr;
        }
        // 缓存里剩下的（刷库失败/被跳过的那批）在关库后已经不可能再落盘，直接丢掉省内存。
        m_cache.clear();
        if (m_buffer) {
            delete[] m_buffer;
            m_buffer = nullptr;
        }
    }
}

// ========== 连接 ==========
// 建连接 + 设 PRAGMA + 建表 + 恢复最大序号。已存在会先关掉（供 reloadAfterClean 复用）。
// ⚠ 全程持 m_mutex：调用方看不到中间的 m_db == nullptr。
bool LogDB::openConnection()
{
    QMutexLocker locker(&m_mutex);

    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
    m_currentTxn = nullptr;

    if (!QDir().mkpath(m_dbPath)) {
        qCritical() << "LogDB: 无法创建目录" << m_dbPath;
        return false;
    }

    const QByteArray file = (m_dbPath + "/logs.db").toUtf8();
    const int rc = sqlite3_open_v2(file.constData(), &m_db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "LogDB: 打开失败" << m_dbPath
                    << (m_db ? sqlite3_errmsg(m_db) : "sqlite3_open_v2 返回 NULL");
        if (m_db) { sqlite3_close_v2(m_db); m_db = nullptr; }
        return false;
    }
    sqlite3_busy_timeout(m_db, LOGDB_BUSY_MS);

    // ⚠ auto_vacuum 必须在**建表之前**设置，且只对「新建的空库」生效。
    //   INCREMENTAL = 删记录只把页标成空闲，之后 PRAGMA incremental_vacuum 能
    //   **原地**把文件缩回去 —— 这就是 LMDB 那个「必须重写整个库」的 compact 的替代。
    const QByteArray cacheSql = "PRAGMA cache_size=-" + QByteArray::number(LOGDB_CACHE_KB);

    execLog(m_db, "PRAGMA journal_mode=WAL",   "journal_mode=WAL");     // 追加写，不阻塞读
    execLog(m_db, "PRAGMA synchronous=NORMAL", "synchronous=NORMAL");   // WAL 下够了：崩溃不坏库，最多丢最后一次提交
    execLog(m_db, cacheSql.constData(),        "cache_size");
    execLog(m_db, "PRAGMA mmap_size=0",        "mmap_size=0");          // ⚠ 明确不用 mmap
    execLog(m_db, "PRAGMA temp_store=MEMORY",  "temp_store=MEMORY");
    execLog(m_db, "PRAGMA auto_vacuum=INCREMENTAL", "auto_vacuum=INCREMENTAL");

    // 表结构：
    //   seq    = 全局递增序号，直接当主键（= rowid 别名，查得最快）
    //   appid  = 账号 appid（字符串原样存，查询按它过滤）
    //   conv   = 会话 id（群号 / openid）
    //   appid_i= appid 的整数值，专门给 getLatestMessagesWithOffset 那种按数字 appid 过滤的查询
    //   blob   = serializeMessage 出来的 Message
    if (!execLog(m_db,
                 "CREATE TABLE IF NOT EXISTS logs ("
                 "  seq     INTEGER PRIMARY KEY,"
                 "  appid   TEXT    NOT NULL,"
                 "  conv    TEXT    NOT NULL,"
                 "  appid_i INTEGER NOT NULL DEFAULT 0,"
                 "  blob    BLOB    NOT NULL)",
                 "CREATE TABLE logs")) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
        return false;
    }
    // 这个索引是「取某会话最近 N 条」的命门：SQLite 直接按 (appid,conv,seq) 定位，
    // 不用像 LMDB 那样从库尾一条条倒扫再过滤 appid/group。
    execLog(m_db, "CREATE INDEX IF NOT EXISTS idx_logs_conv  ON logs(appid, conv, seq)",
            "CREATE INDEX idx_logs_conv");
    execLog(m_db, "CREATE INDEX IF NOT EXISTS idx_logs_appid ON logs(appid_i, seq)",
            "CREATE INDEX idx_logs_appid");

    // 恢复最大序号（保证重启后 seq 接着往上走，不会覆盖旧消息）
    uint64_t maxSeq = 0;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT IFNULL(MAX(seq),0) FROM logs", -1, &st, nullptr) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW)
            maxSeq = static_cast<uint64_t>(sqlite3_column_int64(st, 0));
        sqlite3_finalize(st);
    }
    m_nextId.store(maxSeq + 1, std::memory_order_relaxed);

    qDebug() << "LogDB 打开完成(SQLite)" << m_dbPath
             << "最大序号 =" << maxSeq << "页缓存上限 =" << LOGDB_CACHE_KB << "KB";
    return true;
}

bool LogDB::open()
{
    if (m_db) close();
    // close() 会把环形缓冲和刷库定时器一起拆掉（见 close 上方说明），这里必须补回来
    ensureRuntime();

    if (!openConnection()) return false;

    // 启动时顺手回收一批空闲页（上次删旧留下的空洞）。
    // SQLite 是原地释放，不像 LMDB 要重写整个库，所以可以放心每次启动都做。
    compactIfNeeded();
    return m_db != nullptr;
}

QString LogDB::makeKey(const QString &appid, const QString &groupId, uint64_t seq) const
{
    QString seqStr = QString("%1").arg(seq, SEQ_DIGITS, 10, QChar('0'));
    return seqStr + ":" + appid + ":" + groupId;
}

uint64_t LogDB::seqFromKey(const QString &keyStr)
{
    if (keyStr.size() < SEQ_DIGITS) return 0;
    bool ok = false;
    const uint64_t s = keyStr.left(SEQ_DIGITS).toULongLong(&ok);
    return ok ? s : 0;
}

// ========== 事务控制 ==========
// SQLite 的事务挂在连接上，没有 LMDB 那种 txn 句柄；这里 m_currentTxn 只是一个
// 「有活动事务」的非空标记，专门给 getCurrentTxn() 透传给外部那 4 处调用点用。
bool LogDB::beginTransaction(bool readOnly)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;
    if (m_currentTxn) {
        qWarning() << "LogDB: 已有活动事务，不允许嵌套";
        return false;
    }
    // 只读用 BEGIN（DEFERRED，不拿写锁）；读写用 BEGIN IMMEDIATE 立刻拿写锁，
    // 免得到提交那一刻才升级锁、撞上 SQLITE_BUSY。
    const char *sql = readOnly ? "BEGIN" : "BEGIN IMMEDIATE";
    if (!execLog(m_db, sql, sql)) return false;
    m_currentTxn = reinterpret_cast<LogTxn>(static_cast<quintptr>(1));
    return true;
}

bool LogDB::commitTransaction()
{
    QMutexLocker locker(&m_mutex);
    if (!m_db || !m_currentTxn) {
        qWarning() << "LogDB: 没有活动事务可提交";
        return false;
    }
    const bool ok = execLog(m_db, "COMMIT", "COMMIT");
    m_currentTxn = nullptr;      // ⚠ 无论成败都要清：COMMIT 失败时事务已被 SQLite 结束
    return ok;
}

bool LogDB::abortTransaction()
{
    QMutexLocker locker(&m_mutex);
    if (!m_db || !m_currentTxn) {
        qWarning() << "LogDB: 没有活动事务可回滚";
        return false;
    }
    execLog(m_db, "ROLLBACK", "ROLLBACK");
    m_currentTxn = nullptr;
    return true;
}

// ========== 序列化 ==========
bool LogDB::serializeMessage(const Message &msg, QByteArray &data) const
{
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << msg.user << msg.msg << msg.isSelf << msg.timestamp
           << msg.name << msg.hf << msg.ch<< msg.plugin_ch << msg.direction << msg.Color_0 <<msg.ref_name <<msg.ref_msg << msg.Gname;
    return stream.status() == QDataStream::Ok;
}

bool LogDB::deserializeMessage(const QByteArray &data, Message &msg) const
{
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_15);
    stream >> msg.user >> msg.msg >> msg.isSelf >> msg.timestamp
        >> msg.name >> msg.hf >> msg.ch >> msg.plugin_ch >> msg.direction >> msg.Color_0 >> msg.ref_name >> msg.ref_msg >> msg.Gname;

    return stream.status() == QDataStream::Ok;
}

// ========== 点查 ==========
// 调用方需已持 m_mutex。
bool LogDB::loadBySeq(uint64_t seq, Message &msg) const
{
    if (!m_db) return false;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT blob FROM logs WHERE seq=?", -1, &st, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(seq));
    bool ok = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const void *p = sqlite3_column_blob(st, 0);
        const int len = sqlite3_column_bytes(st, 0);
        if (p && len > 0)
            ok = deserializeMessage(QByteArray(static_cast<const char *>(p), len), msg);
    }
    sqlite3_finalize(st);
    return ok;
}

bool LogDB::readLogInTxn(LogTxn txn, const QString& appid, const QString& groupId,
                         uint64_t seq, Message& msg) const
{
    // 整段读持 m_mutex：调用方的用法是 beginTransaction(true) → 循环读 → commit，
    // 读的时候不持锁。SQLite 单连接，如果外面同时在刷库就会撞 SQLITE_BUSY，
    // 所以这里统一加锁（和 LMDB 版的锁口径完全一致，不会把刷库堵死：单次读很快）。
    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;
    if (txn && txn != m_currentTxn) {
        // 传进来的句柄已经作废（事务被 commit/rollback/重载掉了）→ 退化成裸查，绝不去踩旧状态
        qDebug() << "LogDB: readLogInTxn 收到已失效的事务句柄，退化为普通读取";
    }

    const QString keyStr = makeKey(appid, groupId, seq);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        msg = it->msg;
        return true;
    }
    return loadBySeq(seq, msg);
}

bool LogDB::getLatestLogInTxn(LogTxn txn, const QString& appid, const QString& groupId, Message& msg) const
{
    Q_UNUSED(txn);   // SQLite 版不需要真实事务句柄
    return getLatestLog(appid, groupId, msg);
}

bool LogDB::getLatestLog(const QString &appid, const QString &groupId, Message &msg) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    // 原逻辑：从最大 seq 往回找「同 appid:group 且 Gname 非空」的第一条，最多看 5000 条。
    // SQLite 用索引直接限定到该会话，再按 seq 倒序取 5000 条 —— 比全库倒扫快得多。
    static const char *kSql =
        "SELECT blob FROM logs WHERE appid=? AND conv=? ORDER BY seq DESC LIMIT 5000";
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "LogDB: getLatestLog prepare 失败" << sqlite3_errmsg(m_db);
        return false;
    }
    const QByteArray a = appid.toUtf8();
    const QByteArray c = groupId.toUtf8();
    sqlite3_bind_text(st, 1, a.constData(), a.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c.constData(), c.size(), SQLITE_TRANSIENT);

    bool found = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const void *p = sqlite3_column_blob(st, 0);
        const int len = sqlite3_column_bytes(st, 0);
        if (!p || len <= 0) continue;
        if (deserializeMessage(QByteArray(static_cast<const char *>(p), len), msg) && !msg.Gname.isEmpty()) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(st);
    return found;
}

// ========== 按会话取最近 N 条 ==========
QList<Message> LogDB::getRecentLogs(const QString &appid, const QString &groupId,
                                    int seq, int N, bool daoxu) const
{
    QMutexLocker locker(&m_mutex);
    QList<Message> result;
    if (!m_db || N <= 0) return result;

    // seq < ? + 索引 (appid,conv,seq) 倒序 → 等价于原来「SET_RANGE 定位后向前扫」，
    // 但只碰要取的那 N 条，不再一条条读出来比对 appid/group。
    static const char *kSql =
        "SELECT seq, blob FROM logs WHERE appid=? AND conv=? AND seq<? "
        "ORDER BY seq DESC LIMIT ?";
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "LogDB: getRecentLogs prepare 失败" << sqlite3_errmsg(m_db);
        return result;
    }
    const QByteArray a = appid.toUtf8();
    const QByteArray c = groupId.toUtf8();
    sqlite3_bind_text(st, 1, a.constData(), a.size(), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, c.constData(), c.size(), SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, static_cast<sqlite3_int64>(seq));
    sqlite3_bind_int(st, 4, N);

    while (sqlite3_step(st) == SQLITE_ROW) {
        const qint64 s = sqlite3_column_int64(st, 0);
        const void *p = sqlite3_column_blob(st, 1);
        const int len = sqlite3_column_bytes(st, 1);
        if (!p || len <= 0) continue;

        Message msg;
        msg.seq = static_cast<int>(s);
        deserializeMessage(QByteArray(static_cast<const char *>(p), len), msg);

        // ⚠ 这段是聊天页的渲染约定，别删：机器人自己发出去的消息（direction 非空且有正文）
        //   先补一条 isSelf=true、hf 清空的副本，界面才能把「发送方气泡」和「正文」分开画。
        if (!msg.direction.isEmpty() && !msg.msg.isEmpty()) {
            msg.isSelf = true;
            QString hf = msg.hf;
            msg.hf = QString();
            result.append(msg);
            msg.isSelf = false;
            msg.hf = hf;
        }
        result.append(msg);
    }
    sqlite3_finalize(st);

    if (!daoxu) std::reverse(result.begin(), result.end());
    return result;
}

// ================= 被动回复 msgid（2026-10-11）=================
//
// 背景：发送分两种，频率限制互不占用 ——
//   ① 被动回复（携带 msg_id，即 Message.ch 保存的那条消息 id）：群 100 条/秒、好友 60 条/秒，
//      **没有分钟限制**，强度轻；
//   ② 主动发送（不携带 msg_id）：群 60 条/分钟、好友 30 条/分钟。
// 所以 plts（批量推送）/ scheduleconfigwidget（定时）先来这里要 msgid，做成「混合发送」：
// 拿到 msgid 就用 msgid 发（走被动额度，不吃主动的分钟频率），拿不到才退回主动发送。
// 被动那条路统一走 paceMsgIdReply(60) 的全局 60 条/秒（比群 100 更保守，好友也刚好卡住）。
//
// ⚠ 数据来源是「收到的消息」的 Message.ch（core/qqbotclient.cpp 里
//   Message mes{ev.user, ev.msg, false, tiems, "", ev.replyTo, ev.msgId} 的第 7 个参数）
//   —— ch 才是「用户发来的消息 id」，只有它才能当被动回复用；它可能是空的，跳过即可。
// ⚠⚠ 机器人自己发出去的消息 id 存在 Message.plugin_ch（core/api2.cpp: msg.plugin_ch = deleteid），
//   那个只能拿去做撤回（delete_messages），绝不能用它来发送！所以 isSelf==true 或
//   plugin_ch 非空的一律过滤掉。
QStringList LogDB::getRecentMsgIds(const QString &appid, const QString &groupId, bool isGroup,
                                   int maxCount) const
{
    QStringList result;
    if (groupId.isEmpty() || maxCount <= 0) return result;

    const qint64 windowSecs = isGroup ? GROUP_MSGID_WINDOW_SECS : FRIEND_MSGID_WINDOW_SECS;
    QHash<QString, QStringList> map;
    QMutexLocker locker(&m_mutex);
    collectRecentMsgIds(appid, groupId, QDateTime::currentSecsSinceEpoch() - windowSecs,
                        maxCount, map);
    auto it = map.constFind(groupId);
    if (it != map.constEnd()) result = it.value();
    return result;
}

QHash<QString, QStringList> LogDB::getRecentMsgIdMap(const QString &appid, bool isGroup,
                                                     int maxCountPerConv) const
{
    QHash<QString, QStringList> map;
    if (appid.isEmpty() || maxCountPerConv <= 0) return map;

    const qint64 windowSecs = isGroup ? GROUP_MSGID_WINDOW_SECS : FRIEND_MSGID_WINDOW_SECS;
    QMutexLocker locker(&m_mutex);
    collectRecentMsgIds(appid, QString(), QDateTime::currentSecsSinceEpoch() - windowSecs,
                        maxCountPerConv, map);
    return map;
}

void LogDB::collectRecentMsgIds(const QString &appid, const QString &onlyConv, qint64 cutoffSecs,
                                int maxPerConv, QHash<QString, QStringList> &out, int scanLimit) const
{
    if (!m_db || appid.isEmpty() || maxPerConv <= 0) return;

    QSet<QString> seen;                 // 缓存/库里同一条消息去重（msgid 全局唯一）
    QHash<QString, int> convCount;      // 每个会话已经收了多少个

    // 1) 内存缓存优先：刚收到、还没落盘（最多 5 秒窗口）的消息也要算
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const CacheEntry &e = it.value();
        if (e.appid != appid) continue;
        const QString &conv = e.conv;
        if (!onlyConv.isEmpty() && conv != onlyConv) continue;
        if (convCount.value(conv) >= maxPerConv) continue;

        const Message &m = e.msg;
        // ch = 用户发来的消息 id（被动回复用它）；plugin_ch = 机器人发出去的（只能撤回，不能发送）
        if (m.isSelf || !m.plugin_ch.isEmpty() || m.ch.isEmpty() || seen.contains(m.ch)) continue;
        if (parseLogTimeString(m.timestamp) < cutoffSecs) continue;

        seen.insert(m.ch);
        out[conv].append(m.ch);
        convCount[conv] = convCount.value(conv) + 1;
    }

    // 2) 数据库：按会话（有索引）从新到旧扫；撞到比时间窗还早的记录就停（seq 递增 ≈ 时间递增）
    QByteArray sql = "SELECT conv, blob FROM logs WHERE appid=?";
    if (!onlyConv.isEmpty()) sql += " AND conv=?";
    sql += " ORDER BY seq DESC LIMIT ?";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.constData(), -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "LogDB: collectRecentMsgIds prepare 失败" << sqlite3_errmsg(m_db);
        return;
    }
    const QByteArray a = appid.toUtf8();
    sqlite3_bind_text(st, 1, a.constData(), a.size(), SQLITE_TRANSIENT);
    int idx = 2;
    QByteArray c;
    if (!onlyConv.isEmpty()) {
        c = onlyConv.toUtf8();
        sqlite3_bind_text(st, idx++, c.constData(), c.size(), SQLITE_TRANSIENT);
    }
    sqlite3_bind_int(st, idx, scanLimit);

    int scanned = 0;
    while (sqlite3_step(st) == SQLITE_ROW && scanned < scanLimit) {
        ++scanned;

        const void *p = sqlite3_column_blob(st, 1);
        const int len = sqlite3_column_bytes(st, 1);
        if (!p || len <= 0) continue;
        const QByteArray blob(static_cast<const char *>(p), len);

        const qint64 ts = logBlobTimeSecs(blob);
        if (ts > 0 && ts < cutoffSecs) break;    // 0=时间未知（旧格式）→ 不据此提前结束

        const char *convText = reinterpret_cast<const char *>(sqlite3_column_text(st, 0));
        const QString conv = QString::fromUtf8(convText);
        if (convCount.value(conv) >= maxPerConv) continue;

        Message m;
        if (deserializeMessage(blob, m) && !m.isSelf && m.plugin_ch.isEmpty()
            && !m.ch.isEmpty() && !seen.contains(m.ch)) {
            seen.insert(m.ch);
            out[conv].append(m.ch);
            convCount[conv] = convCount.value(conv) + 1;
        }
    }
    sqlite3_finalize(st);
}

// ========== key 列表 ==========
QStringList LogDB::getLatestKeys(int N) const
{
    QMutexLocker locker(&m_mutex);
    QStringList keys;
    if (!m_db || N <= 0) return keys;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT seq, appid, conv FROM logs ORDER BY seq DESC LIMIT ?",
                           -1, &st, nullptr) != SQLITE_OK)
        return keys;
    sqlite3_bind_int(st, 1, N);

    while (sqlite3_step(st) == SQLITE_ROW) {
        const uint64_t seq = static_cast<uint64_t>(sqlite3_column_int64(st, 0));
        const QString appid = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 1)));
        const QString conv  = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 2)));
        // 原逻辑：key 总长 > 32 才收（过滤掉 appid/conv 都很短的脏记录）
        const QString key = makeKey(appid, conv, seq);
        if (key.size() > 32) keys.append(key);
    }
    sqlite3_finalize(st);
    return keys;
}

QStringList LogDB::getAllKeys() const
{
    QMutexLocker locker(&m_mutex);
    QStringList keys;
    if (!m_db) return keys;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT seq, appid, conv FROM logs ORDER BY seq ASC",
                           -1, &st, nullptr) != SQLITE_OK)
        return keys;

    while (sqlite3_step(st) == SQLITE_ROW) {
        const uint64_t seq = static_cast<uint64_t>(sqlite3_column_int64(st, 0));
        const QString appid = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 1)));
        const QString conv  = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 2)));
        keys.append(makeKey(appid, conv, seq));
    }
    sqlite3_finalize(st);
    return keys;
}

// 日志页 / WebUI 的分页查询：从最新往回按 appid 过滤，跳过 offset 条，取 limit 条。
// SQLite 直接用 LIMIT/OFFSET 走索引，比在代码里一条条跳过快得多。
QList<QPair<QString, Message>> LogDB::getLatestMessagesWithOffset(int appid, int limit, int offset) const
{
    QMutexLocker locker(&m_mutex);
    QList<QPair<QString, Message>> result;
    if (!m_db || limit <= 0) return result;
    if (offset < 0) offset = 0;

    const char *kSqlAll  = "SELECT seq, appid, conv, blob FROM logs "
                           "ORDER BY seq DESC LIMIT ? OFFSET ?";
    const char *kSqlApp  = "SELECT seq, appid, conv, blob FROM logs WHERE appid_i=? "
                           "ORDER BY seq DESC LIMIT ? OFFSET ?";
    const bool all = (appid == 0);

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, all ? kSqlAll : kSqlApp, -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "LogDB: getLatestMessagesWithOffset prepare 失败" << sqlite3_errmsg(m_db);
        return result;
    }
    int idx = 1;
    if (!all) sqlite3_bind_int64(st, idx++, appid);
    sqlite3_bind_int(st, idx++, limit);
    sqlite3_bind_int(st, idx, offset);

    while (sqlite3_step(st) == SQLITE_ROW) {
        const uint64_t seq  = static_cast<uint64_t>(sqlite3_column_int64(st, 0));
        const QString appid_ = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 1)));
        const QString conv   = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(st, 2)));
        const void *p = sqlite3_column_blob(st, 3);
        const int len = sqlite3_column_bytes(st, 3);
        if (!p || len <= 0) continue;

        Message msg;
        deserializeMessage(QByteArray(static_cast<const char *>(p), len), msg);
        result.append(qMakePair(makeKey(appid_, conv, seq), msg));
    }
    sqlite3_finalize(st);

    std::reverse(result.begin(), result.end());   // 调用方要「旧 → 新」
    return result;
}

// ========== 更新 ==========
bool LogDB::updateLog(const QString &keyStr, const Message &msg)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        it->msg = msg;
        it->dirty = true;   // 数据变更，需重新落盘
        return true;
    }

    // 缓存没有 → 先确认库里确实有这条，有才放进缓存等下一轮刷
    Message old;
    if (!loadBySeq(seqFromKey(keyStr), old)) return false;

    CacheEntry e;
    e.msg = msg;
    e.dirty = true;
    e.seq = seqFromKey(keyStr);
    // key = "000…seq:appid:conv"，冒号后面的两段拆回去（appid/conv 本身不含冒号）
    const QStringList parts = keyStr.split(':');
    if (parts.size() == 3) { e.appid = parts[1]; e.conv = parts[2]; }
    m_cache[keyStr] = e;
    return true;
}

bool LogDB::updateLog(const QString &appid, const QString &groupId, uint64_t seq, const Message &msg)
{
    if (!m_db) return false;
    const QString keyStr = makeKey(appid, groupId, seq);

    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        it->msg = msg;
        it->dirty = true;   // 数据变更，需重新落盘
        return true;
    }
    Message old;
    if (!loadBySeq(seq, old)) return false;

    CacheEntry e;
    e.msg = msg;
    e.appid = appid;
    e.conv = groupId;
    e.seq = seq;
    e.dirty = true;
    m_cache[keyStr] = e;
    return true;
}

// ========== 读取 ==========
bool LogDB::readLog(const QString &appid, const QString &groupId, uint64_t seq, Message &msg) const
{
    if (!m_db) return false;

    const QString keyStr = makeKey(appid, groupId, seq);
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        msg = it->msg;
        return true;
    }
    // 缓存未命中 → 直接读库，**不往缓存里塞**。
    // 日志库是「写多读极少」的负载：只读缓存命中率极低，却会常驻一堆 Message 副本
    // （每条 13 个 QString）。刚写入的消息本来就以 dirty 形式在缓存里，读得到；
    // 历史消息只被读一两次，读完就扔才是省内存的做法。
    return loadBySeq(seq, msg);
}

bool LogDB::readLog(const QString &keyStr, Message &msg) const
{
    if (!m_db) return false;
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        msg = it->msg;
        return true;
    }
    // 同上：只读不缓存
    return loadBySeq(seqFromKey(keyStr), msg);
}

// ========== 写缓存 ==========
void LogDB::flushCacheToDB()
{
    QMutexLocker locker(&m_mutex);
    flushCacheToDBUnlocked();
}

// 缓存积压兜底：只在「一直写不进去」的异常路径上被调用，保证内存有界。
// ⚠ 调用方需已持 m_mutex（本函数不再加锁）。
void LogDB::trimCacheIfTooLarge()
{
    if (m_cache.size() <= MAX_PENDING_CACHE) return;
    int drop = m_cache.size() - MAX_PENDING_CACHE;
    const int before = m_cache.size();
    auto it = m_cache.begin();
    while (drop-- > 0 && it != m_cache.end())
        it = m_cache.erase(it);          // erase 返回下一条，QMap 迭代器失效规则安全
    qWarning() << "LogDB: 缓存积压" << before << "条，丢弃最旧" << (before - m_cache.size())
               << "条以保证内存有界" << m_dbPath;
}

// 批量落盘：一个事务里把所有脏条目 INSERT OR REPLACE 进去。
// ⚠ 调用方需已持 m_mutex。
void LogDB::flushCacheToDBUnlocked()
{
    if (!m_db || m_cache.isEmpty()) return;

    // ⚠ 外部事务（beginTransaction）进行中：SQLite 是**单连接**，不能嵌套 BEGIN。
    //   这一轮先不刷，等 commit/rollback 之后 5 秒定时器再来（脏数据还在缓存里，不丢）。
    if (m_currentTxn) return;

    // 缓存里现在只会有脏条目（读缓存已不再写入，见 readLog）
    QList<QString> dirtyKeys;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        if (it->dirty) dirtyKeys.append(it.key());
    if (dirtyKeys.isEmpty()) {
        // 只剩非脏条目（理论上不该有）：攒到一定量就清掉，别让它白占内存
        if (m_cache.size() > 2000) m_cache.clear();
        return;
    }

    static const char *kSql =
        "INSERT OR REPLACE INTO logs(seq,appid,conv,appid_i,blob) VALUES(?,?,?,?,?)";
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "LogDB: 刷库 prepare 失败" << sqlite3_errmsg(m_db);
        trimCacheIfTooLarge();
        return;
    }

    if (!execQuiet(m_db, "BEGIN IMMEDIATE")) {
        qWarning() << "LogDB: 刷库开启事务失败" << sqlite3_errmsg(m_db);
        sqlite3_finalize(st);
        trimCacheIfTooLarge();
        return;
    }

    bool ok = true;
    int written = 0;
    for (const QString &key : dirtyKeys) {
        auto it = m_cache.find(key);
        if (it == m_cache.end() || !it->dirty) continue;

        QByteArray blob;
        if (!serializeMessage(it->msg, blob)) {
            qWarning() << "LogDB: 序列化失败，跳过" << key;
            ok = false;
            break;
        }

        const QByteArray a = it->appid.toUtf8();
        const QByteArray c = it->conv.toUtf8();

        sqlite3_bind_int64(st, 1, static_cast<sqlite3_int64>(it->seq));
        sqlite3_bind_text(st, 2, a.constData(), a.size(), SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, c.constData(), c.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 4, it->appid.toLongLong());
        sqlite3_bind_blob(st, 5, blob.constData(), blob.size(), SQLITE_TRANSIENT);

        const int rc = sqlite3_step(st);
        sqlite3_reset(st);
        sqlite3_clear_bindings(st);

        if (rc != SQLITE_DONE) {
            qWarning() << "LogDB: 写入失败" << sqlite3_errmsg(m_db) << key;
            ok = false;
            break;
        }
        ++written;
    }
    sqlite3_finalize(st);

    if (!ok) {
        execQuiet(m_db, "ROLLBACK");
        // 写失败（坏记录 / 磁盘满 / I-O 错误）：脏数据留着下次再试，
        // 但要有上限 —— 否则 appendLog 一直堆、永远刷不动 = 无界涨内存。
        trimCacheIfTooLarge();
        return;
    }

    if (!execQuiet(m_db, "COMMIT")) {
        qWarning() << "LogDB: 提交刷库事务失败" << sqlite3_errmsg(m_db);
        execQuiet(m_db, "ROLLBACK");
        trimCacheIfTooLarge();
        return;
    }

    m_cache.clear();
    if (written > 0)
        qDebug() << "LogDB: 刷库" << written << "条" << m_dbPath;
}

// appendLog 中添加主动刷库
uint64_t LogDB::appendLog(const QString &appid, const QString &groupId, const Message &msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_db) return 0;

    const uint64_t seq = m_nextId.fetch_add(1);
    const QString key = makeKey(appid, groupId, seq);

    CacheEntry e;
    e.msg = msg;
    e.appid = appid;
    e.conv = groupId;
    e.seq = seq;
    e.dirty = true;
    m_cache[key] = e;

    // 更新环形缓冲区
    auto now = std::chrono::steady_clock::now();
    uint64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    setBufferDurationAndStatus(seq, now_us, 0);

    // 如果缓存达到上限，立即刷库
    if (m_cache.size() >= 10000) {
        flushCacheToDBUnlocked();  // 已在锁内，直接调用不加锁版本
    }
    return seq;
}

// ========== 清理 ==========
bool LogDB::cleanDatabase(int keepN)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db || keepN < 0) return false;

    // 外部事务开着时不能删（会跟它的快照打架）——让定时器下一轮再来
    if (m_currentTxn) {
        qDebug() << "LogDB: 有活动事务，跳过本轮清理" << m_dbPath;
        return false;
    }

    const int64_t before = [this]() -> int64_t {
        sqlite3_stmt *st = nullptr;
        int64_t n = -1;
        if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM logs", -1, &st, nullptr) == SQLITE_OK) {
            if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
            sqlite3_finalize(st);
        }
        return n;
    }();

    // 特殊情况：清空所有
    if (keepN == 0) {
        if (!execLog(m_db, "DELETE FROM logs", "DELETE all")) return false;
        execQuiet(m_db, "PRAGMA incremental_vacuum");
        qDebug() << "LogDB: 已清空" << m_dbPath << "原" << before << "条";
        return true;
    }

    // 保留最新 keepN 条：删除边界 = 第 keepN+1 大的 seq。
    //   · 走主键倒序 + OFFSET，O(log n) 定位到边界，**保留区一页都不碰**；
    //   · OFFSET 超出范围时子查询返回 NULL，`seq <= NULL` 恒为 NULL → 一行都不删（安全）。
    static const char *kSql =
        "DELETE FROM logs WHERE seq <= ("
        "  SELECT seq FROM logs ORDER BY seq DESC LIMIT 1 OFFSET ?)";
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, kSql, -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "LogDB: cleanDatabase prepare 失败" << sqlite3_errmsg(m_db);
        return false;
    }
    sqlite3_bind_int(st, 1, keepN);
    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) {
        qWarning() << "LogDB: cleanDatabase 删除失败" << sqlite3_errmsg(m_db);
        return false;
    }

    const int deleted = sqlite3_changes(m_db);

    // 回收空洞：auto_vacuum=INCREMENTAL 下文件不会自己缩，必须显式 incremental_vacuum。
    // 这里是**原地**释放（把尾部空闲页搬到前面再截断），不像 LMDB 要重写整个库。
    // 分批还，避免一次几万页把清理拖到几秒（上限见 LOGDB_VACUUM_PAGES，这里保持一致）
    execQuiet(m_db, "PRAGMA incremental_vacuum(2000)");

    const int64_t after = before > 0 ? before - deleted : -1;
    qDebug() << "LogDB: 清理完成" << m_dbPath << "删除" << deleted << "条，保留" << keepN
             << "（" << before << "→" << after << "）";
    return true;
}

// ========== 后台清理 / 空闲页回收 ==========

// 后台清理：丢进一个单线程池跑，界面不卡。
// ⚠ 单库十几万条时删旧仍要几毫秒到几百毫秒，主线程跑会明显卡顿
//   （尤其是打开「日志」页那一下）。
bool LogDB::cleanDatabaseAsync(int keepN)
{
    if (!m_db || keepN < 0) return false;

    // 一轮没跑完就别排第二轮（定时器和「打开日志页」可能同时触发）
    bool expected = false;
    if (!m_cleaning.compare_exchange_strong(expected, true))
        return false;

    if (!m_cleanPool) {
        m_cleanPool = new QThreadPool(this);
        m_cleanPool->setMaxThreadCount(1);     // 5 个库都清的时候别一起抢磁盘
        m_cleanPool->setObjectName(QStringLiteral("LogDB-clean"));
    }

    const QString path = m_dbPath;
    m_cleanPool->start(QRunnable::create([this, keepN, path]() {
        const bool ok = cleanDatabase(keepN);  // 内部自己拿 m_mutex
        m_cleaning.store(false, std::memory_order_release);
        qDebug() << "LogDB: 后台清理结束" << path << "keepN =" << keepN << "ok =" << ok;
        if (ok) {
            // 删旧之后的页缓存里全是冷数据，回主线程重开一次连接把它扔掉
            // （SQLite 本来只占 8MB，这里主要是配合内存看门把工作集压下去）
            QMetaObject::invokeMethod(this, [this]() { reloadAfterClean(); },
                                      Qt::QueuedConnection);
        }
    }));
    return true;
}

// 「卸载再挂上」：关连接 → 重开。
// 和 LMDB 版的区别：那边是 munmap 200MB 的映射文件；这边是扔掉 8MB page cache
// + 所有排序/临时缓冲，让 sqlite3 把内部的堆还给系统。
// ⚠ 只碰连接，不碰 m_buffer / m_flushTimer —— setBuffer_255 之类不加锁的写缓冲接口照常可用。
// ⚠ openConnection 全程持 m_mutex（Recursive），readLog / appendLog 会被挡在外面，
//   看不到中间的 m_db == nullptr。
bool LogDB::reloadAfterClean(bool trimWorkingSet)
{
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db) return false;
        // 外部事务开着就别重载（会把它悄悄回滚掉）
        if (m_currentTxn) {
            qDebug() << "LogDB: 有活动事务，跳过重载" << m_dbPath;
            return false;
        }
        flushCacheToDBUnlocked();       // 清理期间可能又进来新数据，先落盘再卸载
    }

    if (!openConnection()) {
        qWarning() << "LogDB: 重载失败，回退到完整 open()" << m_dbPath;
        return open() && m_db != nullptr;
    }

#ifdef Q_OS_WIN
    // 顺带把进程里其它冷页也收一收（进程级；清理一天一次，代价只是几毫秒软缺页）
    // ⚠ 诊断按钮传 false：这个调用会把**私有页也一起**挪进 standby，之后每一步读数都比实际低，
    //   前后对比就没法归因了。
    if (trimWorkingSet)
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#else
    Q_UNUSED(trimWorkingSet);
#endif
    return true;
}

// 回收空闲页。SQLite 有 auto_vacuum=INCREMENTAL，删记录只把页标空闲，
// 要显式 incremental_vacuum 才把文件缩回去 —— 但它是**原地**搬页 + 截断，
// 不需要 LMDB 那种「重写整个库」的 compact，所以每次启动顺手做都行。
bool LogDB::compactIfNeeded()
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    char *msg = nullptr;
    const QByteArray sql = "PRAGMA incremental_vacuum(" + QByteArray::number(LOGDB_VACUUM_PAGES) + ")";
    if (sqlite3_exec(m_db, sql.constData(), nullptr, nullptr, &msg) != SQLITE_OK) {
        qWarning() << "LogDB: 回收空闲页失败" << (msg ? msg : sqlite3_errmsg(m_db));
        if (msg) sqlite3_free(msg);
        return false;
    }
    if (msg) sqlite3_free(msg);
    return true;
}
