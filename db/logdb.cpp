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
    , m_dbi_meta(0)
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
    if (m_env) {
        // 如果存在未提交的事务，回滚
        if (m_currentTxn) {
            mdb_txn_abort(m_currentTxn);
            m_currentTxn = nullptr;
        }
        if (m_dbi_main) mdb_dbi_close(m_env, m_dbi_main);
        if (m_dbi_meta) mdb_dbi_close(m_env, m_dbi_meta);
        mdb_env_close(m_env);
        m_env = nullptr;
        m_dbi_main = 0;
        m_dbi_meta = 0;
    }
    delete[] m_buffer;
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

    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: mdb_env_create 失败:" << mdb_strerror(rc);
        return false;
    }

    mdb_env_set_maxdbs(m_env, 2);
    mdb_env_set_mapsize(m_env, DEFAULT_MAPSIZE);

    QByteArray pathBytes = m_dbPath.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), 0, 0664);
    if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: mdb_env_open 失败:" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: 开始事务失败:" << mdb_strerror(rc);
        return false;
    }

    rc = mdb_dbi_open(txn, "logs", MDB_CREATE, &m_dbi_main);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qCritical() << "LogDB: 打开 logs 子库失败:" << mdb_strerror(rc);
        return false;
    }
    rc = mdb_dbi_open(txn, "meta", MDB_CREATE, &m_dbi_meta);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qCritical() << "LogDB: 打开 meta 子库失败:" << mdb_strerror(rc);
        return false;
    }

    // 如果 meta 中没有 next_id，初始化为 1
    const char *nextIdKey = "next_log_id";
    MDB_val key, value;
    key.mv_data = (void*)nextIdKey;
    key.mv_size = strlen(nextIdKey) + 1;
    rc = mdb_get(txn, m_dbi_meta, &key, &value);
    if (rc == MDB_NOTFOUND) {
        uint64_t initId = 1;
        value.mv_data = &initId;
        value.mv_size = sizeof(initId);
        rc = mdb_put(txn, m_dbi_meta, &key, &value, 0);
        if (rc != MDB_SUCCESS) {
            mdb_txn_abort(txn);
            qCritical() << "LogDB: 初始化 next_id 失败:" << mdb_strerror(rc);
            return false;
        }
    } else if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qCritical() << "LogDB: 读取 next_id 失败:" << mdb_strerror(rc);
        return false;
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: 提交事务失败:" << mdb_strerror(rc);
        return false;
    }

    qDebug() << "LogDB 已打开，目录:" << m_dbPath;
    return true;
}


QString LogDB::makeKey(const QString &appid, const QString &groupId, uint64_t seq) const
{
    QString seqStr = QString("%1").arg(seq, SEQ_DIGITS, 10, QChar('0'));
    return seqStr + ":" +appid + ":" + groupId  ;
}

bool LogDB::getNextId(uint64_t &nextId) const
{
    if (!m_env) return false;
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;

    const char *keyStr = "next_log_id";
    MDB_val key, value;
    key.mv_data = (void*)keyStr;
    key.mv_size = strlen(keyStr) + 1;
    rc = mdb_get(txn, m_dbi_meta, &key, &value);
    if (rc == MDB_SUCCESS && value.mv_size == sizeof(uint64_t)) {
        memcpy(&nextId, value.mv_data, sizeof(nextId));
    } else {
        nextId = 1;
    }
    mdb_txn_abort(txn);
    return true;
}

bool LogDB::updateNextId(uint64_t nextId)
{
    if (!m_env) return false;
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return false;

    const char *keyStr = "next_log_id";
    MDB_val key, value;
    key.mv_data = (void*)keyStr;
    key.mv_size = strlen(keyStr) + 1;
    value.mv_data = &nextId;
    value.mv_size = sizeof(nextId);
    rc = mdb_put(txn, m_dbi_meta, &key, &value, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return false;
    }
    rc = mdb_txn_commit(txn);
    return rc == MDB_SUCCESS;
}

bool LogDB::serializeMessage(const Message &msg, QByteArray &data) const
{
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << msg.user << msg.msg << msg.isSelf << msg.timestamp
           << msg.name << msg.hf << msg.ch<< msg.plugin_ch << msg.direction << msg.Color_0;
    return stream.status() == QDataStream::Ok;
}

bool LogDB::deserializeMessage(const QByteArray &data, Message &msg) const
{
    QDataStream stream(data);
    stream.setVersion(QDataStream::Qt_5_15);
    stream >> msg.user >> msg.msg >> msg.isSelf >> msg.timestamp
        >> msg.name >> msg.hf >> msg.ch >> msg.plugin_ch >> msg.direction >> msg.Color_0;

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
    QMutexLocker locker(&m_mutex);
    if (!m_env) return 0;

    uint64_t nextId;
    if (!getNextId(nextId)) return 0;

    QString keyStr = makeKey(appid, groupId, nextId);
    QByteArray keyBytes = keyStr.toUtf8();

    QByteArray valueBlob;
    if (!serializeMessage(msg, valueBlob)) return 0;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return 0;

    MDB_val key, value;
    key.mv_data = keyBytes.data();
    key.mv_size = keyBytes.size();
    value.mv_data = valueBlob.data();
    value.mv_size = valueBlob.size();

    rc = mdb_put(txn, m_dbi_main, &key, &value, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qWarning() << "LogDB: mdb_put 失败:" << mdb_strerror(rc);
        return 0;
    }

    // 更新 next_id
    uint64_t newId = nextId + 1;
    const char *metaKey = "next_log_id";
    MDB_val mk, mv;
    mk.mv_data = (void*)metaKey;
    mk.mv_size = strlen(metaKey) + 1;
    mv.mv_data = &newId;
    mv.mv_size = sizeof(newId);
    rc = mdb_put(txn, m_dbi_meta, &mk, &mv, 0);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qWarning() << "LogDB: 更新 next_id 失败:" << mdb_strerror(rc);
        return 0;
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qWarning() << "LogDB: 提交事务失败:" << mdb_strerror(rc);
        return 0;
    }
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    setBufferTimeAndStatus(nextId,now_us,0);
    return nextId;
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
QList<Message> LogDB::getRecentLogs(const QString &appid, const QString &groupId, int N) const
{
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

    MDB_val key, value;
    // 从数据库末尾（最大 seq）开始向前遍历
    rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);
    int fetched = 0;
    while (rc == MDB_SUCCESS && fetched < N) {
        QString keyStr = QString::fromUtf8((const char*)key.mv_data, key.mv_size);
        QStringList parts = keyStr.split(':');
        if (parts.size() == 3) {
            // 新格式：parts[0]=seq, parts[1]=appid, parts[2]=groupId
            QString appidFromKey = parts[1];
            QString groupIdFromKey = parts[2];
            if (appidFromKey == appid && groupIdFromKey == groupId) {
                QByteArray blob((const char*)value.mv_data, value.mv_size);
                Message msg;
                if (deserializeMessage(blob, msg)) {
                    if(!msg.direction.isEmpty() && !msg.msg.isEmpty())
                    {
                        msg.isSelf=true;
                        QString hf = msg.hf;
                        msg.hf = QString();
                        result.append(msg);
                        msg.isSelf=false;
                        msg.hf = hf;
                    }
                    result.append(msg);

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