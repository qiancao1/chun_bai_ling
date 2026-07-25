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
#include <QDebug>
#include <QDir>
#include <QDataStream>
#include <QCoreApplication>
#include <cstring>

#include <algorithm>

// 常量定义
static const size_t DEFAULT_MAPSIZE = 64ULL * 1024 * 1024;   // 64 MB
static const size_t MAX_MAPSIZE = 16ULL * 1024 * 1024 * 1024; // 16 GB
static const int SEQ_DIGITS = 20; // 序号填充位数

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
}

LogDB::~LogDB()
{
    close();
}

// ========== 关闭 ==========
void LogDB::close()
{
     QMutexLocker locker(&m_mutex);  // 防止并发关闭
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
        m_buffer = nullptr;   // 置空防止重复释放
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
    // 不加锁，因为不修改对象状态，但需要确保 m_env 有效（外部调用者应保证）
    if (!txn || !m_env) return false;

    QString keyStr = makeKey(appid, groupId, seq);
    QByteArray keyBytes = keyStr.toUtf8();

    MDB_val key, value;
    key.mv_data = keyBytes.data();
    key.mv_size = keyBytes.size();

    int rc = mdb_get(txn, m_dbi_main, &key, &value);
    if (rc != MDB_SUCCESS) {
        if (rc != MDB_NOTFOUND)
            qWarning() << "LogDB: readLogInTxn 失败:" << mdb_strerror(rc);
        return false;
    }

    QByteArray blob((const char*)value.mv_data, value.mv_size);
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

    // 直接调用 reopenEnv 完成初始打开
    m_mapsize = DEFAULT_MAPSIZE;
    return reopenEnv(m_mapsize);
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
           << msg.name << msg.hf << msg.ch<< msg.plugin_ch << msg.direction << msg.Color_0 <<msg.ref_name <<msg.ref_msg;
    return stream.status() == QDataStream::Ok;
}

bool LogDB::deserializeMessage(const QByteArray &data, Message &msg) const
{
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_15);
    stream >> msg.user >> msg.msg >> msg.isSelf >> msg.timestamp
        >> msg.name >> msg.hf >> msg.ch >> msg.plugin_ch >> msg.direction >> msg.Color_0 >> msg.ref_name >> msg.ref_msg;

    return stream.status() == QDataStream::Ok;
}

bool LogDB::getLatestLogInTxn(MDB_txn* txn, const QString& appid, const QString& groupId, Message& msg) const
{
    // 不加锁，因为外部事务已由调用者管理，且 m_env 只读
    if (!txn || !m_env) return false;

    MDB_cursor *cursor = nullptr;
    int rc = mdb_cursor_open(txn, m_dbi_main, &cursor);
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: getLatestLogInTxn 打开游标失败:" << mdb_strerror(rc);
        return false;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);   // 从最大 seq 开始
    while (rc == MDB_SUCCESS) {
        QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
        QStringList parts = keyStr.split(':');
        if (parts.size() == 3) {
            QString appidFromKey = parts[1];
            QString groupIdFromKey = parts[2];
            if (appidFromKey == appid && groupIdFromKey == groupId) {
                QByteArray blob((const char*)value.mv_data, value.mv_size);
                if (deserializeMessage(blob, msg)) {
                    mdb_cursor_close(cursor);
                    return true;
                }
            }
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }

    mdb_cursor_close(cursor);
    return false;   // 未找到
}

// ---------- 公共接口 ----------
uint64_t LogDB::appendLog(const QString &appid, const QString &groupId, const Message &msg)
{
    QMutexLocker locker(&m_mutex);   // 整个写入过程加锁
    if (!m_env) return 0;

    // 原子递增获取唯一序号
    uint64_t seq = m_nextId.fetch_add(1, std::memory_order_relaxed);

    QString keyStr = makeKey(appid, groupId, seq);
    QByteArray keyBytes = keyStr.toUtf8();
    QByteArray valueBlob;
    if (!serializeMessage(msg, valueBlob)) return 0;

    const int MAX_RETRY = 3;
    for (int retry = 0; retry < MAX_RETRY; ++retry) {
        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
        if (rc != MDB_SUCCESS) {
            qWarning() << "appendLog: 开始事务失败" << mdb_strerror(rc);
            return 0;
        }

        MDB_val key, value;
        key.mv_data = keyBytes.data();
        key.mv_size = keyBytes.size();
        value.mv_data = valueBlob.data();
        value.mv_size = valueBlob.size();

        rc = mdb_put(txn, m_dbi_main, &key, &value, 0);
        if (rc == MDB_MAP_FULL) {
            mdb_txn_abort(txn);
            qWarning() << "appendLog: 空间不足，尝试扩容...";
            if (!expandAndReopen()) {
                return 0;   // 扩容失败
            }
            continue;       // 重试
        } else if (rc != MDB_SUCCESS) {
            mdb_txn_abort(txn);
            qWarning() << "appendLog: mdb_put 失败" << mdb_strerror(rc);
            return 0;
        }

        rc = mdb_txn_commit(txn);
        if (rc == MDB_MAP_FULL) {
            // 提交时也可能返回 FULL（罕见），同样处理
            qWarning() << "appendLog: 提交时空间不足，尝试扩容...";
            if (!expandAndReopen()) {
                return 0;
            }
            continue;
        } else if (rc != MDB_SUCCESS) {
            qWarning() << "appendLog: 提交事务失败" << mdb_strerror(rc);
            return 0;
        }

        // 写入成功
        auto now = std::chrono::steady_clock::now();
        qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        setBufferDurationAndStatus(seq, now_us, 0);
        return seq;
    }

    qWarning() << "appendLog: 重试次数耗尽，写入失败";
    return 0;
}
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
                if (deserializeMessage(blob, msg)) {
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
                                    int seq, int N) const {
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
                    if (deserializeMessage(blob, msg)) {
                        // 你的原有处理（保留 isSelf、hf 等逻辑）
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
        }
        // 向前移动（key 更小）
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    std::reverse(result.begin(), result.end());
    return result;
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
                    if (deserializeMessage(blob, msg)) {
                        result.append(qMakePair(keyStr, msg));
                        fetched++;
                    }
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
    if (!m_env) return false;


    QByteArray keyBytes = keyStr.toUtf8();
    QByteArray valueBlob;
    if (!serializeMessage(msg, valueBlob)) return false;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return false;

    MDB_val key, value;
    key.mv_data = keyBytes.data();
    key.mv_size = keyBytes.size();
    value.mv_data = valueBlob.data();
    value.mv_size = valueBlob.size();

    rc = mdb_put(txn, m_dbi_main, &key, &value, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qWarning() << "LogDB: updateLog 失败:" << mdb_strerror(rc);
        return false;
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: updateLog 提交失败:" << mdb_strerror(rc);
        return false;
    }
    return true;
}
bool LogDB::updateLog(const QString &appid, const QString &groupId, uint64_t seq, const Message &msg)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    QString keyStr = makeKey(appid, groupId, seq);
    QByteArray keyBytes = keyStr.toUtf8();
    QByteArray valueBlob;
    if (!serializeMessage(msg, valueBlob)) return false;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return false;

    MDB_val key, value;
    key.mv_data = keyBytes.data();
    key.mv_size = keyBytes.size();
    value.mv_data = valueBlob.data();
    value.mv_size = valueBlob.size();

    rc = mdb_put(txn, m_dbi_main, &key, &value, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qWarning() << "LogDB: updateLog 失败:" << mdb_strerror(rc);
        return false;
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: updateLog 提交失败:" << mdb_strerror(rc);
        return false;
    }
    return true;
}

bool LogDB::readLog(const QString &appid, const QString &groupId, uint64_t seq, Message &msg) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    QString keyStr = makeKey(appid, groupId, seq);
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
        if (rc != MDB_NOTFOUND)
            qWarning() << "LogDB: readLog 失败:" << mdb_strerror(rc);
        return false;
    }

    QByteArray blob((const char*)value.mv_data, value.mv_size);
    bool ok = deserializeMessage(blob, msg);
    mdb_txn_abort(txn);
    return ok;
}
bool LogDB::readLog(const QString &keyStr, Message &msg) const
{
    QMutexLocker locker(&m_mutex);
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
        if (rc != MDB_NOTFOUND)
            qWarning() << "LogDB: readLog 失败:" << mdb_strerror(rc);
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

// 在 LogDB.cpp 中实现
bool LogDB::reopenEnv(size_t minSize) {
    // 1. 获取当前数据文件大小（data.mdb）
    QFileInfo fileInfo(m_dbPath + "/data.mdb");
    size_t fileSize = fileInfo.exists() ? (size_t)fileInfo.size() : 0;

    // 2. 计算目标 mapsize：至少为 minSize，且大于文件大小，留有余量
    size_t targetSize = std::max(minSize, fileSize * 2);          // 翻倍
    targetSize = std::max(targetSize, fileSize + 10ULL * 1024 * 1024); // 至少多 10MB
    targetSize = std::min(targetSize, MAX_MAPSIZE);               // 不超过上限

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
    qDebug() << "LogDB 打开/扩容完成，实际 mapsize =" << targetSize
             << "，文件大小 =" << fileSize << "，下一个序号 =" << m_nextId.load();
    return true;
}

bool LogDB::expandAndReopen()
{
    // 必须在持有 m_mutex 时调用
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