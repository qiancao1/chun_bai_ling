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

#include "logdb.h"
#include <QDir>
#include <QDebug>
#include <cstring>
#include <ctime>

// 默认初始 mapsize 64MB
static const size_t DEFAULT_MAPSIZE = 64ULL * 1024 * 1024;
// 最大 mapsize 限制 16GB
static const size_t MAX_MAPSIZE = 16ULL * 1024 * 1024 * 1024;

LogDB::LogDB(const QString &dbPath, int flushIntervalMs, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_flushIntervalMs(flushIntervalMs)
    , m_env(nullptr)
    , m_dbi_logs(0)
    , m_dbi_meta(0)
    , m_timer(nullptr)
    , m_nextId(1)
    , m_currentMapSize(DEFAULT_MAPSIZE)
{
}

LogDB::~LogDB()
{
    close();
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
        qCritical() << "LogDB: mdb_env_create 失败" << mdb_strerror(rc);
        return false;
    }

    mdb_env_set_maxdbs(m_env, 2);   // logs 和 meta
    mdb_env_set_mapsize(m_env, m_currentMapSize);

    QByteArray pathBytes = m_dbPath.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), 0, 0664);
    if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: mdb_env_open 失败" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {

        return false;
    }
    const char *nextIdKey = "next_log_id";
    rc = mdb_dbi_open(txn, "logs", MDB_CREATE, &m_dbi_logs);
    if (rc != MDB_SUCCESS) goto fail;
    rc = mdb_dbi_open(txn, "meta", MDB_CREATE, &m_dbi_meta);
    if (rc != MDB_SUCCESS) goto fail;

    // 读取下一个自增ID

    MDB_val key, value;
    key.mv_data = (void*)nextIdKey;
    key.mv_size = strlen(nextIdKey) + 1;
    rc = mdb_get(txn, m_dbi_meta, &key, &value);
    if (rc == MDB_SUCCESS && value.mv_size == sizeof(uint64_t)) {
        memcpy(&m_nextId, value.mv_data, sizeof(m_nextId));
    } else if (rc != MDB_NOTFOUND) {
        qWarning() << "LogDB: 读取 next_id 失败" << mdb_strerror(rc);
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: 提交事务失败";
        return false;
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &LogDB::onFlushTimer);
    m_timer->start(m_flushIntervalMs);

    qDebug() << "LogDB 已打开，目录:" << m_dbPath << "next_id =" << m_nextId;
    return true;

fail:
    mdb_txn_abort(txn);
    qCritical() << "LogDB: 打开子数据库失败";
    return false;
}

void LogDB::close()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    flush();  // 最后刷一次盘

    if (m_env) {
        if (m_dbi_logs) mdb_dbi_close(m_env, m_dbi_logs);
        if (m_dbi_meta) mdb_dbi_close(m_env, m_dbi_meta);
        mdb_env_close(m_env);
        m_env = nullptr;
    }
    {
        QMutexLocker locker(&m_mutex);
        m_writeBuf.clear();
        m_readBuf.clear();
    }
    m_nextId = 1;
}

LogRecord& LogDB::appendLog()
{
    QMutexLocker locker(&m_mutex);
    m_writeBuf.append(LogRecord()); // 添加一个默认构造的 LogRecord
    return m_writeBuf.last();       // 返回最后一个元素的引用

}

void LogDB::flush()
{
    // 交换缓冲区
    swapBuffers();

    // 如果读缓冲区有数据，尝试写入（最多重试 3 次，非递归） //因为数据库的 限制 写入失败时将进行扩容 然后重试 一般来说2次即可
    if (!m_readBuf.isEmpty()) {
        int retry = 0;
        const int MAX_RETRY = 3;
        while (retry < MAX_RETRY && !writeReadBuffer()) {
            retry++;
            qWarning() << "LogDB: 写入失败，重试" << retry;
        }
        if (retry == MAX_RETRY) {
            qCritical() << "LogDB: 多次写入失败，数据保留在 m_readBuf 中，等待下次定时重试";
        }
    }
}

void LogDB::swapBuffers()
{
    QMutexLocker locker(&m_mutex);
    m_readBuf.swap(m_writeBuf);   // 将当前写缓冲换到读缓冲，写缓冲变空
}

bool LogDB::writeReadBuffer()
{
    if (m_readBuf.isEmpty()) return true;
    if (!m_env) return false;

    // 注意：这里直接使用 m_readBuf，因为它在交换后只被当前线程访问（其他线程不会触碰）
    // 但仍然需要确保在写入过程中不会与 swapBuffers 冲突——不会，因为只有定时器线程调用 flush，而 flush 在交换后没有重新加锁，
    // 而且 swapBuffers 只在 flush 开始时调用一次，之后 m_readBuf 完全归当前线程所有。

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: 开始事务失败" << mdb_strerror(rc);
        return false;
    }

    uint64_t newNextId = m_nextId;   // 局部副本，成功后再提交
    bool hasError = false;
    for (const LogRecord &rec : std::as_const(m_readBuf)) {
        // 构造 key: 自增 64 位整数
        uint64_t keyVal = newNextId++;
        MDB_val key;
        key.mv_data = &keyVal;
        key.mv_size = sizeof(keyVal);

        // 序列化 value: [time:4][appid:4][sourceId:16][groupId:16][msgLen:4][messageUTF8]
        QByteArray msgUtf8 = rec.msg.toUtf8();
        uint32_t msgLen = msgUtf8.size();
        size_t totalSize = 4 + 4 + 16 + 16 + 4 + msgLen;
        QByteArray blob;
        blob.reserve(totalSize);
        blob.append((const char*)&rec.time, 4);
        blob.append((const char*)&rec.appid, 4);
        // sourceId 固定16字节
        if (rec.user.size() >= 16)
            blob.append(rec.user.left(16));
        else {
            blob.append(rec.user);
            blob.append(QByteArray(16 - rec.user.size(), 0));
        }
        // groupId 固定16字节
        if (rec.groupId.size() >= 16)
            blob.append(rec.groupId.left(16));
        else {
            blob.append(rec.groupId);
            blob.append(QByteArray(16 - rec.groupId.size(), 0));
        }
        blob.append((const char*)&msgLen, 4);
        blob.append(msgUtf8);

        MDB_val value;
        value.mv_data = blob.data();
        value.mv_size = blob.size();

        rc = mdb_put(txn, m_dbi_logs, &key, &value, 0);
        if (rc == MDB_MAP_FULL) {
            // 需要扩容
            mdb_txn_abort(txn);
            if (increaseMapSize()) {
                // 递归重试（非递归实现，改为循环调用外层 flush 的重试逻辑）
                // 这里直接返回 false，让外层 flush 重试
                return false;
            } else {
                qCritical() << "LogDB: 扩容失败";
                return false;
            }
        } else if (rc != MDB_SUCCESS) {
            qCritical() << "LogDB: mdb_put 失败" << mdb_strerror(rc);
            hasError = true;
            break;
        }
    }

    if (hasError) {
        mdb_txn_abort(txn);
        return false;
    }

    // 更新元数据中的 next_id
    const char *nextIdKey = "next_log_id";
    MDB_val key, value;
    key.mv_data = (void*)nextIdKey;
    key.mv_size = strlen(nextIdKey) + 1;
    value.mv_data = &newNextId;
    value.mv_size = sizeof(newNextId);
    rc = mdb_put(txn, m_dbi_meta, &key, &value, 0);
    if (rc == MDB_MAP_FULL) {
        mdb_txn_abort(txn);
        if (increaseMapSize()) return false;
        else return false;
    } else if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qCritical() << "LogDB: 更新 next_id 失败" << mdb_strerror(rc);
        return false;
    }

    rc = mdb_txn_commit(txn);
    if (rc == MDB_MAP_FULL) {
        if (increaseMapSize()) return false;
        else return false;
    } else if (rc != MDB_SUCCESS) {
        qCritical() << "LogDB: 提交事务失败" << mdb_strerror(rc);
        return false;
    }

    // 成功，更新成员变量并清空读缓冲
    m_nextId = newNextId;
    m_readBuf.clear();
    return true;
}

bool LogDB::increaseMapSize()
{
    if (!m_env) return false;
    size_t newSize = m_currentMapSize * 2;
    if (newSize > MAX_MAPSIZE) {
        qCritical() << "LogDB: 已达到最大 mapsize 限制" << (MAX_MAPSIZE>>20) << "MB";
        return false;
    }
    qDebug() << "LogDB: 扩容 mapsize 从" << (m_currentMapSize>>20) << "MB 到" << (newSize>>20) << "MB";
    m_currentMapSize = newSize;
    return reopenEnvironment();
}

bool LogDB::reopenEnvironment()
{
    // 关闭当前环境
    if (m_env) {
        // 注意：需要关闭所有 DBI
        if (m_dbi_logs) mdb_dbi_close(m_env, m_dbi_logs);
        if (m_dbi_meta) mdb_dbi_close(m_env, m_dbi_meta);
        mdb_env_close(m_env);
        m_env = nullptr;
    }

    // 重新创建环境
    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) return false;
    mdb_env_set_maxdbs(m_env, 2);
    mdb_env_set_mapsize(m_env, m_currentMapSize);
    QByteArray pathBytes = m_dbPath.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), 0, 0664);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }
    const char *nextIdKey = "next_log_id";
    // 重新打开数据库
    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return false;
    rc = mdb_dbi_open(txn, "logs", MDB_CREATE, &m_dbi_logs);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "meta", MDB_CREATE, &m_dbi_meta);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    // 恢复 m_nextId（从元数据）

    MDB_val key, value;
    key.mv_data = (void*)nextIdKey;
    key.mv_size = strlen(nextIdKey) + 1;
    rc = mdb_get(txn, m_dbi_meta, &key, &value);
    if (rc == MDB_SUCCESS && value.mv_size == sizeof(uint64_t)) {
        memcpy(&m_nextId, value.mv_data, sizeof(m_nextId));
    } else {
        m_nextId = 1;
    }
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    return true;

reopen_fail:
    mdb_txn_abort(txn);
    mdb_env_close(m_env);
    m_env = nullptr;
    return false;
}

QList<LogRecord> LogDB::getRecentLogs(int limit) const
{
    QList<LogRecord> result;
    if (!m_env) return result;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return result;

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi_logs, &cursor);
    if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return result;
    }

    // 移动到最后一个 key（最大 ID）
    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_LAST);
    int fetched = 0;
    while (rc == MDB_SUCCESS && fetched < limit) {
        if (key.mv_size == sizeof(uint64_t) && value.mv_size > 4+4+16+16+4) {
            const uchar *data = (const uchar*)value.mv_data;
            LogRecord rec;
            memcpy(&rec.time, data, 4);
            memcpy(&rec.appid, data+4, 4);
            rec.user = QByteArray((const char*)data+8, 16);
            rec.groupId  = QByteArray((const char*)data+24, 16);
            uint32_t msgLen;
            memcpy(&msgLen, data+40, 4);
            if (msgLen <= value.mv_size - 44) {
                rec.msg = QString::fromUtf8((const char*)data+44, msgLen);
            }
            // 由于每个 LogDB 实例只对应一个账号，可以不过滤 appid，但结构体中保留了
            result.append(rec);
            fetched++;
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_PREV);
    }
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return result;
}

void LogDB::onFlushTimer()
{
    flush();
}