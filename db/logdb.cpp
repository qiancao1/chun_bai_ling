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
// logdb.cpp
#include "logdb.h"
#include "botdb.h"
#include <QDebug>
#include <QDir>
#include <QDataStream>
#include <QDateTime>
#include <QSet>
#include <QCoreApplication>
#include <QThreadPool>
#include <QRunnable>
#include <QFile>
#include <QFileInfo>
#include <cstring>

#include <algorithm>

// 常量定义
static const size_t DEFAULT_MAPSIZE = 64ULL * 1024 * 1024;   // 64 MB
static const size_t MAX_MAPSIZE = 16ULL * 1024 * 1024 * 1024; // 16 GB
static const int SEQ_DIGITS = 20; // 序号填充位数

// ---- 被动回复 msgid 的有效窗口（2026-10-11）----
// 被动回复用的 msg_id 有效期：群 5 分钟、好友 1 小时（用户口径）。超窗的 msg_id 发出去会失败，
// 所以按各自窗口取最近一条即可；拿不到就传空 msgid 走主动发送。
static const qint64 GROUP_MSGID_WINDOW_SECS  = 5 * 60;      // 群消息：5 分钟
static const qint64 FRIEND_MSGID_WINDOW_SECS = 60 * 60;     // 好友/单聊：1 小时

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

// ========== 构造函数、析构函数 ==========
LogDB::LogDB(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_bufferSize(10000)
    , m_buffer(new std::atomic<uint64_t>[m_bufferSize])
    , m_dbPath(dbPath)
    , m_env(nullptr)
    , m_dbi_main(0)
    , m_currentTxn(nullptr)          // 初始化事务指针
{
    for (size_t i = 0; i < m_bufferSize; ++i) {
        m_buffer[i].store(0, std::memory_order_relaxed);
    }
    m_flushTimer = new QTimer(this);
    m_flushTimer->setInterval(5000);   // 4000 毫秒
    connect(m_flushTimer, &QTimer::timeout, this, &LogDB::flushCacheToDB);
    m_flushTimer->start();
}

LogDB::~LogDB()
{
    close();
}

// ========== 关闭 ==========
void LogDB::close()
{
    // 0. 后台清理可能正持着 m_mutex 扫全库（十几万条要几秒）——必须等它跑完再关 env，
    //    否则它会拿着已经 close 掉的 m_env 继续 mdb_cursor_get（踩野指针）。
    if (m_cleanPool) m_cleanPool->waitForDone();

    // 1. 停止定时器，防止在刷库过程中再次触发
    if (m_flushTimer) {
        m_flushTimer->stop();
        delete m_flushTimer;
        m_flushTimer = nullptr;
    }

    // 2. 刷库（将缓存数据写入数据库）—— 内部会加锁
    flushCacheToDB();

    // 3. 关闭数据库环境
    QMutexLocker locker(&m_mutex);
    if (m_env) {
        if (m_currentTxn) {
            mdb_txn_abort(m_currentTxn);
            m_currentTxn = nullptr;
        }
        if (m_dbi_main) mdb_dbi_close(m_env, m_dbi_main);
        mdb_env_close(m_env);
        m_env = nullptr;
        m_dbi_main = 0;
    }
    if (m_buffer) {
        delete[] m_buffer;
        m_buffer = nullptr;
    }
}


// ========== 事务控制 ==========
bool LogDB::beginTransaction(bool readOnly)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;
    if (m_currentTxn) {
        qWarning() << "LogDB: 已有活动事务，不允许嵌套";
        return false;
    }
    int rc = mdb_txn_begin(m_env, nullptr, readOnly ? MDB_RDONLY : 0, &m_currentTxn);
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: 开启事务失败:" << mdb_strerror(rc);
        return false;
    }
    return true;
}

bool LogDB::commitTransaction()
{
    QMutexLocker locker(&m_mutex);
    if (!m_currentTxn) {
        qWarning() << "LogDB: 没有活动事务可提交";
        return false;
    }
    int rc = mdb_txn_commit(m_currentTxn);
    m_currentTxn = nullptr;
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: 提交事务失败:" << mdb_strerror(rc);
        return false;
    }
    return true;
}

bool LogDB::abortTransaction()
{
    QMutexLocker locker(&m_mutex);
    if (!m_currentTxn) {
        qWarning() << "LogDB: 没有活动事务可回滚";
        return false;
    }
    mdb_txn_abort(m_currentTxn);
    m_currentTxn = nullptr;
    return true;
}

// ========== 使用外部事务的读取（不自动开启事务） ==========
bool LogDB::readLogInTxn(MDB_txn* txn, const QString& appid, const QString& groupId,
                         uint64_t seq, Message& msg) const
{
    // ⚠ 整段读都在 m_mutex 里跑（不是整轮循环，只锁一次读，不会把刷库堵住）。
    //   原因：调用方（network/websocketserver.cpp / ui/chatpage.cpp）的用法是
    //   beginTransaction(true) → for(全量群){ readLogInTxn(getCurrentTxn(),…) } → commit，
    //   **读的时候不持锁**；而 5 秒刷缓存撞 MAP_FULL 会走 expandAndReopen() → mdb_env_close()，
    //   close() 也会 mdb_txn_abort(m_currentTxn)。环境/事务被拆掉之后再 mdb_get 就是踩野指针，
    //   表现是堆破坏 → fail-fast（进程秒退、连 dump 都留不下）。
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    // 传进来的 txn 必须还是「当前那一个」；否则（已 close / 已扩容 / 传了作废指针）
    // 就自己开一个临时读事务，读完立刻还掉 —— 绝不拿作废的指针去 mdb_get。
    MDB_txn *useTxn = nullptr;
    bool ownTxn = false;
    if (txn && txn == m_currentTxn) {
        useTxn = txn;
    } else {
        if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &useTxn) != MDB_SUCCESS)
            return false;
        ownTxn = true;
    }

    QString keyStr = makeKey(appid, groupId, seq);
    QByteArray keyBytes = keyStr.toUtf8();

    MDB_val key, value;
    key.mv_data = keyBytes.data();
    key.mv_size = keyBytes.size();

    int rc = mdb_get(useTxn, m_dbi_main, &key, &value);
    // 先把数据拷出来，再结束临时事务（读事务的 map 页在 abort 后不保证仍可引用）
    QByteArray blob;
    if (rc == MDB_SUCCESS)
        blob = QByteArray((const char*)value.mv_data, value.mv_size);
    if (ownTxn) mdb_txn_abort(useTxn);

    if (rc != MDB_SUCCESS) {
        if (rc != MDB_NOTFOUND)
            qWarning() << "LogDB: readLogInTxn 失败:" << mdb_strerror(rc);
        return false;
    }

    return deserializeMessage(blob, msg);
}

bool LogDB::open()
{
    if (m_env) close();

    QDir dir;
    if (!dir.mkpath(m_dbPath)) {
        qCritical() << "LogDB: 无法创建目录" << m_dbPath;
        return false;
    }

    // 上次压缩中途被打断（进程被杀/断电）的现场恢复：
    //   data.mdb 已被挪成 .bak、新的还没换上来 → 把备份改回去，别让库开不起来。
    const QString dataPath = m_dbPath + "/data.mdb";
    const QString bakPath  = m_dbPath + "/data.mdb.bak";
    const QString tmpDir   = m_dbPath + "/compact_tmp";
    if (!QFile::exists(dataPath) && QFile::exists(bakPath)) {
        if (QFile::rename(bakPath, dataPath))
            qWarning() << "LogDB: 上次压缩中断，已从备份恢复" << m_dbPath;
    } else if (QFile::exists(dataPath) && QFile::exists(bakPath)) {
        // 新库已经换上了，.bak 只是没来得及删 —— 直接清掉，别白占几百 MB
        QFile::remove(bakPath);
    }
    if (QDir(tmpDir).exists()) QDir(tmpDir).removeRecursively();

    // 直接调用 reopenEnv 完成初始打开
    m_mapsize = DEFAULT_MAPSIZE;
    if (!reopenEnv(m_mapsize)) return false;

    // 启动时顺手压一次（只在文件远大于实际数据时才真做，见 compactIfNeeded）
    compactIfNeeded();
    // 压缩里任何一步失败都会回滚重开；万一连回滚都没开起来，这里必须如实报失败
    return m_env != nullptr;
}

QString LogDB::makeKey(const QString &appid, const QString &groupId, uint64_t seq) const
{
    QString seqStr = QString("%1").arg(seq, SEQ_DIGITS, 10, QChar('0'));
    return seqStr + ":" +appid + ":" + groupId  ;
}


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

bool LogDB::getLatestLogInTxn(MDB_txn* txn, const QString& appid, const QString& groupId, Message& msg) const
{
    // 与 readLogInTxn 同理：整段读持 m_mutex，并对调用方传入的 txn 做作废校验。
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    MDB_txn *useTxn = nullptr;
    bool ownTxn = false;
    if (txn && txn == m_currentTxn) {
        useTxn = txn;
    } else {
        if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &useTxn) != MDB_SUCCESS)
            return false;
        ownTxn = true;
    }

    bool found = false;
    MDB_cursor *cursor = nullptr;
    int rc = mdb_cursor_open(useTxn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: getLatestLogInTxn 打开游标失败:" << mdb_strerror(rc);
    } else {
        MDB_val key, value;
        rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);   // 从最大 seq 开始
        int i=0;
        while (rc == MDB_SUCCESS) {
            QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
            QStringList parts = keyStr.split(':');
            if (parts.size() == 3) {
                QString appidFromKey = parts[1];
                QString groupIdFromKey = parts[2];
                if (appidFromKey == appid && groupIdFromKey == groupId) {
                    QByteArray blob((const char*)value.mv_data, value.mv_size);
                    deserializeMessage(blob, msg);
                    if(!msg.Gname.isEmpty()){
                        found = true;
                        break;
                    }

                }
                i++;
                if(i>5000) break;
            }
            rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
        }
        mdb_cursor_close(cursor);
    }

    if (ownTxn) mdb_txn_abort(useTxn);
    return found;
}

// ---------- 公共接口 ----------

bool LogDB::getLatestLog(const QString &appid, const QString &groupId, Message &msg) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return false;
    }

    MDB_val key, value;
    // 从数据库末尾（最大 seq）开始向前遍历
    rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);
    while (rc == MDB_SUCCESS) {
        QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
        QStringList parts = keyStr.split(':');
        if (parts.size() == 3) {
            QString appidFromKey = parts[1];
            QString groupIdFromKey = parts[2];
            if (appidFromKey == appid && groupIdFromKey == groupId) {
                QByteArray blob((const char*)value.mv_data, value.mv_size);
                deserializeMessage(blob, msg);
                if(!msg.Gname.isEmpty()){
                    mdb_cursor_close(cursor);
                    mdb_txn_abort(txn);
                    return true;
                }



            }
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return false;
}
QList<Message> LogDB::getRecentLogs(const QString &appid, const QString &groupId,
                                    int seq, int N,bool daoxu) const {
    QMutexLocker locker(&m_mutex);
    QList<Message> result;
    if (!m_env || N <= 0) return result;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return result;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return result;
    }

    // 构造目标 key 前缀，宽度请与存储格式一致（例如 6 位或 10 位）
    int width = 20;  // 根据你的实际宽度调整，可以动态从数据库读取或配置
    QString targetPrefix = QString("%1:").arg(seq, width, 10, QChar('0'));
    QByteArray keyBuf = targetPrefix.toUtf8(); // 确保生命周期
    MDB_val key, value;
    key.mv_data = keyBuf.data();
    key.mv_size = keyBuf.size();

    // 定位到第一个 >= targetPrefix 的记录
    rc = mdb_cursor_get(cursor, &key, &value, MDB_SET_RANGE);
    if (rc != MDB_SUCCESS) {
        // 没有比目标更大的 key（即所有 key 都小于目标），则从末尾开始
        rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);
    }

    int fetched = 0;
    while (rc == MDB_SUCCESS && fetched < N) {
        QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
        QStringList parts = keyStr.split(':');
        if (parts.size() == 3) {
            int seq2 = parts[0].toInt();
            // 只接受 seq 严格小于传入 seq 的记录（避免包含 >= 传入 seq 的）
            if (seq > seq2) {
                QString appidFromKey = parts[1];
                QString groupIdFromKey = parts[2];
                if (appidFromKey == appid && groupIdFromKey == groupId) {
                    // 反序列化消息（原有逻辑保持不变）
                    QByteArray blob((const char*)value.mv_data, value.mv_size);
                    Message msg;
                    msg.seq = seq2;
                    deserializeMessage(blob, msg) ;

                    if (!msg.direction.isEmpty() && !msg.msg.isEmpty()) {
                        msg.isSelf = true;
                        QString hf = msg.hf;
                        msg.hf = QString();
                        result.append(msg);
                        msg.isSelf = false;
                        msg.hf = hf;
                    }
                    result.append(msg);
                    fetched++;
                }
            }
        }
        // 向前移动（key 更小）
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    if(!daoxu)
    std::reverse(result.begin(), result.end());
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
    if (!m_env || appid.isEmpty() || maxPerConv <= 0) return;

    QSet<QString> seen;                 // 缓存/库里同一条消息去重（msgid 全局唯一）
    QHash<QString, int> convCount;      // 每个会话已经收了多少个

    // 1) 内存缓存优先：刚收到、还没落盘（最多 5 秒窗口）的消息也要算
    for (auto it = m_cache.cbegin(); it != m_cache.cend(); ++it) {
        const QStringList parts = it.key().split(':');
        if (parts.size() != 3 || parts[1] != appid) continue;
        const QString &conv = parts[2];
        if (!onlyConv.isEmpty() && conv != onlyConv) continue;
        if (convCount.value(conv) >= maxPerConv) continue;

        const Message &m = it.value().msg;
        // ch = 用户发来的消息 id（被动回复用它）；plugin_ch = 机器人发出去的（只能撤回，不能发送）
        if (m.isSelf || !m.plugin_ch.isEmpty() || m.ch.isEmpty() || seen.contains(m.ch)) continue;
        if (parseLogTimeString(m.timestamp) < cutoffSecs) continue;

        seen.insert(m.ch);
        out[conv].append(m.ch);
        convCount[conv] = convCount.value(conv) + 1;
    }

    // 2) 数据库：从最大 seq 往回扫；撞到比时间窗还早的记录就停（seq 递增 ≈ 时间递增）
    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS) return;

    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_main, &cursor) != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return;
    }

    MDB_val key, value;
    int rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);
    int scanned = 0;
    while (rc == MDB_SUCCESS && scanned < scanLimit) {
        ++scanned;
        const QByteArray blob((const char*)value.mv_data, (int)value.mv_size);

        const qint64 ts = logBlobTimeSecs(blob);
        if (ts > 0 && ts < cutoffSecs) break;    // 0=时间未知（旧格式）→ 不据此提前结束

        const QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
        const QStringList parts = keyStr.split(':');
        if (parts.size() == 3 && parts[1] == appid
            && (onlyConv.isEmpty() || parts[2] == onlyConv)) {
            const QString &conv = parts[2];
            if (convCount.value(conv) < maxPerConv) {
                Message m;
                if (deserializeMessage(blob, m) && !m.isSelf && m.plugin_ch.isEmpty()
                    && !m.ch.isEmpty() && !seen.contains(m.ch)) {
                    seen.insert(m.ch);
                    out[conv].append(m.ch);
                    convCount[conv] = convCount.value(conv) + 1;
                }
            }
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
}
QStringList LogDB::getLatestKeys(int N) const
{
    QMutexLocker locker(&m_mutex);
    QStringList keys;
    if (!m_env || N <= 0) return keys;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return keys;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return keys;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);   // 最大 seq
    int count = 0;
    while (rc == MDB_SUCCESS && count < N) {
        if(key.mv_size>32)
            keys.append(QString::fromUtf8((const char*)key.mv_data, key.mv_size));
        count++;
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return keys;
}

QList<QPair<QString, Message>> LogDB::getLatestMessagesWithOffset(int appid, int limit, int offset) const
{
    QMutexLocker locker(&m_mutex);
    QList<QPair<QString, Message>> result;
    if (!m_env || limit <= 0) return result;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return result;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return result;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);   // 从最后一条开始（最大 seq）
    int skipped = 0;
    int fetched = 0;
    BotDB *db=nullptr;

    while (rc == MDB_SUCCESS && fetched < limit) {
        QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
        QStringList parts = keyStr.split(':');
        if (parts.size() == 3) {
            // 新格式：parts[0]=seq, parts[1]=appid, parts[2]=groupId
            int appidFromKey = parts[1].toInt();
            if (appid == 0 || appidFromKey == appid) {

                if (skipped < offset) {
                    skipped++;
                } else {
                    QByteArray blob((const char*)value.mv_data, value.mv_size);
                    Message msg;
                    deserializeMessage(blob, msg);

                    result.append(qMakePair(keyStr, msg));
                    fetched++;

                }
            }
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    std::reverse(result.begin(), result.end());


    return result;
}

QStringList LogDB::getAllKeys() const
{
    QMutexLocker locker(&m_mutex);
    QStringList keys;
    if (!m_env) return keys;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return keys;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return keys;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        keys.append(QString::fromUtf8((const char*)key.mv_data, key.mv_size));
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return keys;
}
bool LogDB::updateLog(const QString &keyStr, const Message &msg)
{
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        it->msg = msg;
        it->dirty = true;   // 数据变更，需重新落盘
        return true;
    }
    // 缓存没有，尝试从数据库加载后再更新
    Message old;
    if (loadFromDB(keyStr, old)) {
        m_cache[keyStr] = {msg,true};
        return true;
    }
    return false;
}

bool LogDB::updateLog(const QString &appid, const QString &groupId, uint64_t seq, const Message &msg)
{
    if (!m_env) return false;
    QString keyStr = makeKey(appid, groupId, seq);

    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        it->msg = msg;
        it->dirty = true;   // 数据变更，需重新落盘
        return true;
    }
    // 缓存没有，尝试从数据库加载后再更新
    Message old;
    if (loadFromDB(keyStr, old)) {
        m_cache[keyStr] = {msg, true};
        return true;
    }
    return false;
}
// 原 flushCacheToDB 改为：
void LogDB::flushCacheToDB() {
    QMutexLocker locker(&m_mutex);
    flushCacheToDBUnlocked();
}

// 新增不加锁版本（复制原逻辑，去掉锁）
void LogDB::flushCacheToDBUnlocked() {
    // 假设已持有锁
    if (!m_env || m_cache.isEmpty()) return;

    // 收集脏 key
    QList<QString> dirtyKeys;
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        if (it->dirty) dirtyKeys.append(it.key());
    if (dirtyKeys.isEmpty()) {
        if (m_cache.size() > 1000) { // 比如，超过500条只读缓存就直接清理，防止内存疯长
            m_cache.clear();
        }
        return;
    }

    const int MAX_RETRY = 3;
    for (int retry = 0; retry < MAX_RETRY; ++retry) {
        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
        if (rc != MDB_SUCCESS) {
            qWarning() << "flushCacheToDBUnlocked: 开启事务失败" << mdb_strerror(rc);
            return;
        }

        bool ok = true;
        for (const QString &key : dirtyKeys) {
            auto it = m_cache.find(key);
            if (it == m_cache.end() || !it->dirty) continue;

            QByteArray keyBytes = key.toUtf8();
            QByteArray valueBlob;
            if (!serializeMessage(it->msg, valueBlob)) {
                ok = false;
                break;
            }
            MDB_val k, v;
            k.mv_data = keyBytes.data();
            k.mv_size = keyBytes.size();
            v.mv_data = valueBlob.data();
            v.mv_size = valueBlob.size();

            rc = mdb_put(txn, m_dbi_main, &k, &v, 0);
            if (rc == MDB_MAP_FULL) {
                mdb_txn_abort(txn);
                if (!expandAndReopen()) {
                    qWarning() << "flushCacheToDBUnlocked: 扩容失败";
                    return;
                }
                ok = false;
                break;
            } else if (rc != MDB_SUCCESS) {
                qWarning() << "flushCacheToDBUnlocked: mdb_put 失败" << mdb_strerror(rc) << key;
                ok = false;
                break;
            }
        }

        if (!ok) {
            if (rc == MDB_MAP_FULL) continue;
            if (txn) mdb_txn_abort(txn);
            return;
        }

        rc = mdb_txn_commit(txn);
        if (rc == MDB_MAP_FULL) {
            // ⚠ mdb_txn_commit 一旦返回，无论成功失败事务都已经被释放，
            //   这里再 mdb_txn_abort(txn) 是「对已结束事务 abort」= 未定义行为（野指针）。
            if (!expandAndReopen()) {
                qWarning() << "flushCacheToDBUnlocked: 提交时扩容失败";
                return;
            }
            continue;
        } else if (rc != MDB_SUCCESS) {
            qWarning() << "flushCacheToDBUnlocked: 提交事务失败" << mdb_strerror(rc);
            return;
        }

        // 成功提交，清空缓存
        m_cache.clear();
        return;
    }

    qWarning() << "flushCacheToDBUnlocked: 重试次数耗尽，清空缓存（数据可能丢失）";
    m_cache.clear();
}

// appendLog 中添加主动刷库
uint64_t LogDB::appendLog(const QString &appid, const QString &groupId, const Message &msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_env) return 0;
    uint64_t seq = m_nextId.fetch_add(1);
    QString key = makeKey(appid, groupId, seq);
    m_cache[key] = {msg, true};
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
bool LogDB::readLog(const QString &appid, const QString &groupId, uint64_t seq, Message &msg) const
{
    if (!m_env) return false;

    QString keyStr = makeKey(appid, groupId, seq);
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        msg = it->msg;
        return true;
    }
    // 缓存未命中，从数据库读取并放入缓存（dirty=false）
    if (loadFromDB(keyStr, msg)) {
        m_cache[keyStr] = {msg, false};
        return true;
    }
    return false;
}
bool LogDB::readLog(const QString &keyStr, Message &msg) const
{
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(keyStr);
    if (it != m_cache.end()) {
        msg = it->msg;
        return true;
    }
    // 缓存未命中，从数据库读取并放入缓存（dirty=false）
    if (loadFromDB(keyStr, msg)) {
        m_cache[keyStr] = {msg,false};
        return true;
    }
    return false;
}
bool LogDB::loadFromDB(const QString &keyStr, Message &msg) const {
    // 注意：此函数假设调用者已持有 m_mutex，因此不再加锁
    if (!m_env) return false;

    QByteArray keyBytes = keyStr.toUtf8();

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;

    MDB_val key, value;
    key.mv_data = keyBytes.data();
    key.mv_size = keyBytes.size();

    rc = mdb_get(txn, m_dbi_main, &key, &value);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return false;
    }

    QByteArray blob((const char*)value.mv_data, value.mv_size);
    bool ok = deserializeMessage(blob, msg);
    mdb_txn_abort(txn);
    return ok;
}
bool LogDB::cleanDatabase(int keepN)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env || keepN < 0) return false;

    // 特殊情况：清空所有
    if (keepN == 0) {
        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
        if (rc != MDB_SUCCESS) return false;
        rc = mdb_drop(txn, m_dbi_main, 1);  // 1 = 清空
        if (rc != MDB_SUCCESS) {
            mdb_txn_abort(txn);
            return false;
        }
        rc = mdb_txn_commit(txn);
        return rc == MDB_SUCCESS;
    }

    // 1. 只读事务：统计总记录数（只遍历键，不解析）
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return false;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    size_t totalCount = 0;
    while (rc == MDB_SUCCESS) {
        totalCount++;
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    // 如果总数 <= 保留数，不需要删
    if (totalCount <= (size_t)keepN) {
        return true;
    }

    // 2. 计算要删除的数量（最旧的那批）
    size_t toDelete = totalCount - keepN;

    // 3. 写事务：从头（最旧）开始，逐条删除前 toDelete 条
    MDB_txn *delTxn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &delTxn);
    if (rc != MDB_SUCCESS) return false;

    rc = mdb_cursor_open(delTxn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(delTxn);
        return false;
    }

    // 定位到最旧的第一条
    rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    size_t deleted = 0;
    while (rc == MDB_SUCCESS && deleted < toDelete) {
        // 删除当前游标指向的条目
        rc = mdb_cursor_del(cursor, 0);
        if (rc != MDB_SUCCESS) {
            mdb_cursor_close(cursor);
            mdb_txn_abort(delTxn);
            return false;
        }
        deleted++;
        // 删除后，游标自动指向下一条（MDB_NEXT）
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    rc = mdb_txn_commit(delTxn);
    return rc == MDB_SUCCESS;
}

// ========== 后台清理 / 启动压缩 ==========

// 后台清理：丢进一个单线程池跑，界面不卡。
// ⚠ 日志库单库十几万条时 cleanDatabase 要扫全库 + 逐条删，主线程跑会明显卡顿
//   （尤其是打开「日志」页那一下）。
bool LogDB::cleanDatabaseAsync(int keepN)
{
    if (!m_env || keepN < 0) return false;

    // 一轮没跑完就别排第二轮（定时器和「打开日志页」可能同时触发，扫两遍纯浪费）
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
    }));
    return true;
}

// 启动时压缩：LMDB 删条目只把页还给 freelist，data.mdb 一分不缩，文件会停在历史峰值。
// 实测 logdb_1 已经 512 MB —— 每天清一次库，文件却永远不降。
// ⚠ 只在启动时做（此时没有活动事务、没有并发读），运行期重开 env 风险太大。
// ⚠ 它会 rename data.mdb，所以同一目录绝不能双开两个实例（本来双开也会互相覆盖 seq）。
bool LogDB::compactIfNeeded()
{
    QMutexLocker locker(&m_mutex);        // Recursive：reopenEnv 内部还会再拿一次
    if (!m_env) return false;

    const QString dataPath = m_dbPath + "/data.mdb";
    QFileInfo fi(dataPath);
    if (!fi.exists()) return false;
    const qint64 fileSize = fi.size();

    // 手工修库入口：库目录里放一个 compact.force，下次启动强制重建一次。
    //   copy2 压坏的库只有 freelist 坏了，阈值判断看不出来（文件跟数据一样大），
    //   必须能强制重建才能修好 —— 重建走 mdb_put，新库的 freelist 是干净的。
    const QString forcePath = m_dbPath + "/compact.force";
    const bool forced = QFile::exists(forcePath);

    if (!forced && fileSize < 128LL * 1024 * 1024)
        return false;                                    // 小库不值得动（复制本身就要写一遍）

    // 一周最多压一次：删掉旧日志后空洞只会越积越多，每天启动都重写几百 MB 太浪费
    const QString stampPath = m_dbPath + "/compact.stamp";
    QFileInfo stamp(stampPath);
    if (!forced && stamp.exists()
        && stamp.lastModified().secsTo(QDateTime::currentDateTime()) < 7 * 24 * 3600) {
        return false;
    }

    // 实际占用 = 子库 B-tree 的页数 × 页大小（不含 freelist 里的空洞）。
    // ⚠ 不能用 mdb_env_info().me_last_pgno：它只是「最高页号」，
    //   删掉的是最旧的那批（页号在前半段），空洞留在中间，last_pgno 根本不降 ——
    //   用它判断会得出「文件没浪费」的错误结论，压缩永远不触发。
    MDB_stat st;
    MDB_txn *statTxn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &statTxn) != MDB_SUCCESS) return false;
    const int src = mdb_stat(statTxn, m_dbi_main, &st);
    mdb_txn_abort(statTxn);
    if (src != MDB_SUCCESS) return false;

    const double pageSize = st.ms_psize > 0 ? static_cast<double>(st.ms_psize) : 4096.0;
    const double used = (static_cast<double>(st.ms_branch_pages)
                         + static_cast<double>(st.ms_leaf_pages)
                         + static_cast<double>(st.ms_overflow_pages)) * pageSize;
    if (used <= 0.0) return false;

    // 省不出 25% 就算了：重建要把整个库重写一遍，白写几百 MB 只省几十 MB 不值
    // （forced = 修坏库，不按这个来）
    if (!forced
        && (static_cast<double>(fileSize) < used * 1.25
            || fileSize - qint64(used) < 64LL * 1024 * 1024)) {
        qDebug() << "LogDB: 文件无需压缩" << m_dbPath
                 << "文件" << fileSize / 1048576 << "MB / 逻辑占用" << qint64(used) / 1048576 << "MB";
        return false;
    }

    flushCacheToDBUnlocked();             // 先落盘，别把缓存里的脏数据丢了

    const QString tmpDir  = m_dbPath + "/compact_tmp";
    const QString bakPath = m_dbPath + "/data.mdb.bak";
    QDir(tmpDir).removeRecursively();
    if (!QDir().mkpath(tmpDir)) {
        qWarning() << "LogDB: 无法创建压缩临时目录" << tmpDir;
        return false;
    }

    // ==== 手工重建，不能用 mdb_env_copy2(MDB_CP_COMPACT) ====
    // ⚠ 2026-11-08 实测崩溃：copy2 压完库能打开、能读，3 分钟后第一次「删除 + 提交」就
    //   在 mdb_freelist_save() 里断言 'x <= dl[0].mid' 直接 abort（0.9.x 的 compact copy
    //   会把目标库的空闲页链表算坏）。自己按 key 一条条 mdb_put 进一个新库虽然慢，
    //   但目标库的 freelist 是 LMDB 正常写出来的，不会留下这个雷。
    MDB_env *dst = nullptr;
    if (mdb_env_create(&dst) != MDB_SUCCESS) {
        qWarning() << "LogDB: 压缩目标环境创建失败";
        QDir(tmpDir).removeRecursively();
        return false;
    }
    mdb_env_set_maxdbs(dst, 1);
    const qint64 wantMap = qMax<qint64>(fileSize, 64LL * 1024 * 1024) * 3 / 2;
    const size_t dstMap  = static_cast<size_t>(qMin<qint64>(wantMap, static_cast<qint64>(MAX_MAPSIZE)));
    mdb_env_set_mapsize(dst, dstMap);
    int rc = mdb_env_open(dst, tmpDir.toUtf8().constData(), MDB_NOSYNC, 0664);
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: 压缩目标库打开失败" << mdb_strerror(rc);
        mdb_env_close(dst);
        if (forced) QFile::remove(forcePath);
        QDir(tmpDir).removeRecursively();
        return false;
    }

    bool copyOk = true;
    size_t copied = 0;
    MDB_txn *srcTxn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &srcTxn) != MDB_SUCCESS) {
        copyOk = false;
    } else {
        MDB_cursor *cur = nullptr;
        if (mdb_cursor_open(srcTxn, m_dbi_main, &cur) != MDB_SUCCESS) {
            copyOk = false;
        } else {
            MDB_txn *dstTxn = nullptr;
            MDB_dbi dstDbi = 0;
            bool setupOk = (mdb_txn_begin(dst, nullptr, 0, &dstTxn) == MDB_SUCCESS);
            if (setupOk && mdb_dbi_open(dstTxn, "logs", MDB_CREATE, &dstDbi) != MDB_SUCCESS)
                setupOk = false;
            if (!setupOk) {
                // dbi_open 失败时事务还开着，必须还掉，否则 mdb_env_close 会断言
                if (dstTxn) { mdb_txn_abort(dstTxn); dstTxn = nullptr; }
                copyOk = false;
            } else {
                MDB_val k, v;
                int crc = mdb_cursor_get(cur, &k, &v, MDB_FIRST);
                while (crc == MDB_SUCCESS) {
                    if (mdb_put(dstTxn, dstDbi, &k, &v, 0) != MDB_SUCCESS) {
                        copyOk = false;
                        break;
                    }
                    // 分批提交：一次事务塞十万条会让脏页表爆掉
                    if (++copied % 20000 == 0) {
                        if (mdb_txn_commit(dstTxn) != MDB_SUCCESS) {
                            dstTxn = nullptr;
                            copyOk = false;
                            break;
                        }
                        dstTxn = nullptr;
                        // dbi 句柄在同一个 env 内一直是有效的，重开事务不用重开 dbi
                        if (mdb_txn_begin(dst, nullptr, 0, &dstTxn) != MDB_SUCCESS) {
                            copyOk = false;
                            break;
                        }
                    }
                    crc = mdb_cursor_get(cur, &k, &v, MDB_NEXT);
                }
                if (copyOk && dstTxn) {
                    if (mdb_txn_commit(dstTxn) != MDB_SUCCESS) copyOk = false;
                } else if (!copyOk && dstTxn) {
                    mdb_txn_abort(dstTxn);
                }
                dstTxn = nullptr;
            }
            mdb_cursor_close(cur);
        }
        mdb_txn_abort(srcTxn);
    }

    if (copyOk && copied > 0) {
        // MDB_NOSYNC：最后一次交给 fsync，失败就别替换（半截库比大库糟多了）
        if (mdb_env_sync(dst, 1) != MDB_SUCCESS) {
            qWarning() << "LogDB: 重建后落盘失败，放弃压缩" << m_dbPath;
            copyOk = false;
        }
    }
    mdb_env_close(dst);

    if (!copyOk || copied == 0) {
        qWarning() << "LogDB: 重建复制失败，放弃压缩" << m_dbPath << "已复制" << copied;
        if (forced) QFile::remove(forcePath);   // 失败一次就摘开关，别每次启动都卡几秒重试
        QDir(tmpDir).removeRecursively();
        return false;
    }
    QFileInfo copiedFile(tmpDir + "/data.mdb");
    if (!copiedFile.exists() || copiedFile.size() < 4096) {
        qWarning() << "LogDB: 压缩产物不可用，放弃";
        if (forced) QFile::remove(forcePath);
        QDir(tmpDir).removeRecursively();
        return false;
    }

    // 拆掉环境 —— 不能调 close()：那会把 m_buffer 删掉、把 flushTimer 停掉
    if (m_currentTxn) { mdb_txn_abort(m_currentTxn); m_currentTxn = nullptr; }
    if (m_dbi_main) { mdb_dbi_close(m_env, m_dbi_main); m_dbi_main = 0; }
    mdb_env_close(m_env);
    m_env = nullptr;
    QFile::remove(m_dbPath + "/lock.mdb");   // 旧锁跟着旧库走，重开时 LMDB 自己重建

    // 换文件：原库先留成 .bak，新库能正常打开才删 —— 任何一步失败都要把 .bak 换回来
    QFile::remove(bakPath);
    if (!QFile::rename(dataPath, bakPath)) {
        qWarning() << "LogDB: 备份原库失败，放弃压缩" << m_dbPath;
        QDir(tmpDir).removeRecursively();
        reopenEnv(DEFAULT_MAPSIZE);
        return false;
    }
    if (!QFile::rename(tmpDir + "/data.mdb", dataPath)) {
        qWarning() << "LogDB: 新库改名失败，回滚" << m_dbPath;
        QFile::rename(bakPath, dataPath);
        QDir(tmpDir).removeRecursively();
        reopenEnv(DEFAULT_MAPSIZE);
        return false;
    }

    const uint64_t keepSeq = m_nextId.load(std::memory_order_relaxed);
    bool ok = reopenEnv(DEFAULT_MAPSIZE);
    if (!ok) {
        // 新库打不开 → 把备份换回来再开一次。宁可回到大文件，也不能让库消失。
        qWarning() << "LogDB: 压缩后打开失败，回滚到备份" << m_dbPath;
        QFile::remove(dataPath);
        QFile::rename(bakPath, dataPath);
        ok = reopenEnv(DEFAULT_MAPSIZE);
    }
    // 序号绝不回退：reopenEnv 是按「库里最大 key」恢复的，清理/压缩后可能比原来小
    if (ok && m_nextId.load(std::memory_order_relaxed) < keepSeq)
        m_nextId.store(keepSeq, std::memory_order_relaxed);
    if (ok) {
        QFile::remove(bakPath);
        QFile::remove(forcePath);          // 强制重建已生效，摘掉开关
        QFile sf(stampPath);
        if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            sf.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
    }
    QDir(tmpDir).removeRecursively();

    if (ok) {
        qDebug() << "LogDB: 压缩完成" << m_dbPath
                 << fileSize / 1048576 << "MB →"
                 << QFileInfo(dataPath).size() / 1048576 << "MB";
    }
    return ok;
}

// 在 LogDB.cpp 中实现
bool LogDB::reopenEnv(size_t minSize) {
    // 换环境全程持锁（m_mutex 是 Recursive：expandAndReopen 进来时已经持有了）。
    QMutexLocker locker(&m_mutex);

    // ⚠ 先把活动长事务掐掉再关 env。否则 mdb_env_close() 之后 m_currentTxn 的 mt_env
    //   就是野指针，调用方再 commit/abort/读就是踩刚释放的堆（fail-fast 最常见的来源）。
    //   有了这把锁 + 读接口也持锁，这里不会和「正在 mdb_get 的线程」撞车。
    if (m_currentTxn) {
        mdb_txn_abort(m_currentTxn);
        m_currentTxn = nullptr;
    }

    // 1. 获取当前数据文件大小（data.mdb）
    QFileInfo fileInfo(m_dbPath + "/data.mdb");
    size_t fileSize = fileInfo.exists() ? (size_t)fileInfo.size() : 0;

    // 2. 计算目标 mapsize：至少为 minSize，且大于文件大小，留有余量
    size_t targetSize = std::max(minSize, fileSize * 2);          // 翻倍
    targetSize = std::max(targetSize, static_cast<size_t>(fileSize + 10ULL * 1024 * 1024));
    targetSize = std::min(targetSize, MAX_MAPSIZE);

    // 4. 关闭旧环境
    if (m_env) {
        if (m_dbi_main) mdb_dbi_close(m_env, m_dbi_main);
        mdb_env_close(m_env);
        m_env = nullptr;
        m_dbi_main = 0;
    }

    // 5. 创建新环境
    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) {
        qCritical() << "reopenEnv: mdb_env_create 失败" << mdb_strerror(rc);
        return false;
    }

    mdb_env_set_maxdbs(m_env, 1);
    mdb_env_set_mapsize(m_env, targetSize);

    QByteArray pathBytes = m_dbPath.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), MDB_WRITEMAP | MDB_NOMETASYNC, 0664);
    if (rc != MDB_SUCCESS) {
        qCritical() << "reopenEnv: mdb_env_open 失败" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    // 6. 打开子库、恢复序号（与原来相同）
    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "reopenEnv: 开始事务失败" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    rc = mdb_dbi_open(txn, "logs", MDB_CREATE, &m_dbi_main);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qCritical() << "reopenEnv: 打开 logs 子库失败" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    // 恢复最大序号
    uint64_t maxSeq = 0;
    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc == MDB_SUCCESS) {
        MDB_val key, value;
        rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);
        if (rc == MDB_SUCCESS) {
            QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
            QStringList parts = keyStr.split(':');
            if (!parts.isEmpty()) {
                bool ok;
                uint64_t seq = parts[0].toULongLong(&ok);
                if (ok && seq > maxSeq) maxSeq = seq;
            }
        }
        mdb_cursor_close(cursor);
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "reopenEnv: 提交事务失败" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    m_nextId.store(maxSeq + 1, std::memory_order_relaxed);
    m_mapsize = targetSize;
    qDebug() << "LogDB 打开/扩容完成，实际 mapsize =" << targetSize
             << "，文件大小 =" << fileSize << "，下一个序号 =" << m_nextId.load();
    return true;
}

bool LogDB::expandAndReopen()
{
    // 必须在持有 m_mutex 时调用（Recursive，重入也安全）
    QMutexLocker locker(&m_mutex);

    // 本函数自己会先关 env，所以长事务要在这里也掐一次（reopenEnv 里那次赶不上）
    if (m_currentTxn) {
        mdb_txn_abort(m_currentTxn);
        m_currentTxn = nullptr;
    }

    size_t newSize = m_mapsize * 2;
    if (newSize > MAX_MAPSIZE) newSize = MAX_MAPSIZE;
    if (newSize == m_mapsize) {
        qWarning() << "LogDB: 已达到最大 mapsize，无法继续扩容";
        return false;
    }

    // 关闭当前环境（注意：此时不应有任何活动事务）
    if (m_env) {
        if (m_dbi_main) mdb_dbi_close(m_env, m_dbi_main);
        mdb_env_close(m_env);
        m_env = nullptr;
        m_dbi_main = 0;
    }

    return reopenEnv(newSize);
}