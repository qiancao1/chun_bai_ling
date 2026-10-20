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

#include "lmdbkv.h"
#include <QDebug>
#include <QDir>

LmdbKV::LmdbKV(const QString &dbPath, QObject *parent)
    : QObject(parent), m_env(nullptr), m_dbi(0), m_dbPath(dbPath), m_currentMapSize(16 * 1024 * 1024)
{
    QDir dir;
    if (!dir.mkpath(dbPath)) {
        qCritical() << "无法创建数据库目录:" << dbPath;
        return;
    }

    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_create 失败:" << mdb_strerror(rc);
        m_env = nullptr;
        return;
    }

    rc = mdb_env_set_maxdbs(m_env, 1);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_set_maxdbs 失败:" << mdb_strerror(rc);
    }

    rc = mdb_env_set_mapsize(m_env, m_currentMapSize);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_set_mapsize 失败:" << mdb_strerror(rc);
    }

    QByteArray pathBytes = dbPath.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), MDB_NOMETASYNC, 0664);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_open 失败:" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return;
    }

    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_txn_begin 失败:" << mdb_strerror(rc);
        return;
    }

    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &m_dbi);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_dbi_open 失败:" << mdb_strerror(rc);
        mdb_txn_abort(txn);
        return;
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "提交事务失败:" << mdb_strerror(rc);
    }
}

LmdbKV::~LmdbKV()
{
    if (m_env) {
        mdb_dbi_close(m_env, m_dbi);
        mdb_env_close(m_env);
    }
}

// 幂等关闭：关完把 m_env 置空，后续 get/put/remove 一看 m_env 为空就直接返回空/false。
// 这样可以「先关库、再删目录」（Windows 下不关就删不掉映射中的 data.mdb），
// 而不用急着 delete 对象 —— 还攥着旧指针的线程最多做一次空操作。
void LmdbKV::close()
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return;
    mdb_dbi_close(m_env, m_dbi);
    mdb_env_close(m_env);
    m_env = nullptr;
    m_dbi = 0;
}

bool LmdbKV::put(const QString &key, const QString &value)
{
    return putInternal(key.toUtf8(), value.toUtf8());
}

bool LmdbKV::put(const QByteArray &key, const QByteArray &value)
{
    return putInternal(key, value);
}

bool LmdbKV::putInternal(const QByteArray &key, const QByteArray &value)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    const int MAX_RETRIES = 10;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
        if (rc != MDB_SUCCESS) {
            qCritical() << "put: mdb_txn_begin 失败" << mdb_strerror(rc);
            return false;
        }

        MDB_val k = { (size_t)key.size(), (void*)key.constData() };
        MDB_val v = { (size_t)value.size(), (void*)value.constData() };
        rc = mdb_put(txn, m_dbi, &k, &v, 0);

        if (rc == MDB_SUCCESS) {
            rc = mdb_txn_commit(txn);
            if (rc != MDB_SUCCESS) {
                qCritical() << "put: 提交事务失败" << mdb_strerror(rc);
                mdb_txn_abort(txn);
                return false;
            }
            return true;
        } else if (rc == MDB_MAP_FULL) {
            mdb_txn_abort(txn);
            if (!increaseMapSize()) {
                return false;
            }
        } else {
            mdb_txn_abort(txn);
            qCritical() << "put: mdb_put 失败" << mdb_strerror(rc);
            return false;
        }
    }

    qCritical() << "put: 重试次数耗尽，写入失败";
    return false;
}

QString LmdbKV::get(const QString &key) const
{
    QByteArray value = getInternal(key.toUtf8());
    return QString::fromUtf8(value);
}

QByteArray LmdbKV::get(const QByteArray &key) const
{
    return getInternal(key);
}

QByteArray LmdbKV::getInternal(const QByteArray &key) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return QByteArray();

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) {
        return QByteArray();
    }

    MDB_val k = { (size_t)key.size(), (void*)key.constData() };
    MDB_val v;
    rc = mdb_get(txn, m_dbi, &k, &v);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return QByteArray();
    } else if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return QByteArray();
    }

    QByteArray result((const char*)v.mv_data, v.mv_size);
    mdb_txn_abort(txn);
    return result;
}

bool LmdbKV::remove(const QString &key)
{
    return removeInternal(key.toUtf8());
}
bool LmdbKV::removeBatch(const QList<QByteArray>& keys)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env || keys.isEmpty()) return true;

    const int MAX_RETRIES = 3;
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        MDB_txn *txn = nullptr;
        int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
        if (rc != MDB_SUCCESS) {
            qCritical() << "removeBatch: begin failed" << mdb_strerror(rc);
            return false;
        }

        bool ok = true;
        for (const QByteArray &key : keys) {
            MDB_val k = { (size_t)key.size(), (void*)key.constData() };
            rc = mdb_del(txn, m_dbi, &k, nullptr);
            if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
                qCritical() << "removeBatch: mdb_del failed for key" << key.toHex() << mdb_strerror(rc);
                ok = false;
                break;
            }
        }

        if (!ok) {
            mdb_txn_abort(txn);
            return false;
        }

        rc = mdb_txn_commit(txn);
        if (rc == MDB_SUCCESS) {
            return true;
        } else if (rc == MDB_MAP_FULL) {
            if (!increaseMapSize()) {
                qCritical() << "removeBatch: 扩容失败";
                return false;
            }
            continue; // 重试
        } else {
            qCritical() << "removeBatch: commit failed" << mdb_strerror(rc);
            return false;
        }
    }
    return false;
}
bool LmdbKV::remove(const QByteArray &key)
{
    return removeInternal(key);
}

// 从缓存值里取过期秒。本库惯例：值 = "<过期秒>||||<内容>"；
// 早期媒体缓存是 "[<类型>,path=…,md5=…,Time=<过期秒>]"（过期秒在末尾、还带个 ']'）。
// 返回 -1 = 认不出（调用方一律不动它）。
static qint64 cacheExpireSec(const QByteArray &v)
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

int LmdbKV::removeExpired(qint64 nowSec)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return -1;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "removeExpired: mdb_txn_begin 失败" << mdb_strerror(rc);
        return -1;
    }

    // 先扫、后删：不边删边游标（避免游标定位语义的坑）。过期条目一般很少，键留着不占多少内存。
    QList<QByteArray> dead;
    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi, &cursor);
    if (rc == MDB_SUCCESS) {
        MDB_val key, data;
        while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == MDB_SUCCESS) {
            const QByteArray k((const char*)key.mv_data, (int)key.mv_size);
            const QByteArray v((const char*)data.mv_data, (int)data.mv_size);
            const qint64 expire = cacheExpireSec(v);
            if (expire >= 0 && nowSec >= expire)
                dead.append(k);
        }
        mdb_cursor_close(cursor);
    } else {
        qCritical() << "removeExpired: 打开游标失败" << mdb_strerror(rc);
    }

    int removed = 0;
    for (const QByteArray &k : std::as_const(dead)) {
        if (k.isEmpty()) continue;
        MDB_val kv = { (size_t)k.size(), (void*)k.constData() };
        rc = mdb_del(txn, m_dbi, &kv, nullptr);
        if (rc == MDB_SUCCESS) ++removed;
        else if (rc != MDB_NOTFOUND)
            qCritical() << "removeExpired: mdb_del 失败" << mdb_strerror(rc);
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        // ⚠ 提交失败后 txn 已被 LMDB 释放，**不能再 abort**（对已结束的事务 abort 是未定义行为）
        qCritical() << "removeExpired: 提交事务失败" << mdb_strerror(rc);
        return -1;
    }
    return removed;
}

bool LmdbKV::removeInternal(const QByteArray &key)
{
    QMutexLocker locker(&m_mutex);
    if (!m_env) return false;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        return false;
    }

    MDB_val k = { (size_t)key.size(), (void*)key.constData() };
    rc = mdb_del(txn, m_dbi, &k, nullptr);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return true;
    } else if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        qCritical() << "remove: mdb_del 失败" << mdb_strerror(rc);
        return false;
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "remove: 提交事务失败" << mdb_strerror(rc);
        return false;
    }
    return true;
}

bool LmdbKV::increaseMapSize()
{
    if (!m_env) return false;
    size_t newSize = m_currentMapSize * 2; // 翻倍
    if (newSize > 1024ULL * 1024 * 1024) return false; // 限制1GB

    // 关闭当前环境
    mdb_dbi_close(m_env, m_dbi);
    mdb_env_close(m_env);
    m_env = nullptr;

    // 重新创建环境
    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) return false;
    mdb_env_set_mapsize(m_env, newSize);
    rc = mdb_env_open(m_env, m_dbPath.toUtf8().constData(), MDB_NOMETASYNC, 0664);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    // 重新打开 DBI
    MDB_txn *txn;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) { mdb_env_close(m_env); m_env=nullptr; return false; }
    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &m_dbi);
    mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) { mdb_env_close(m_env); m_env=nullptr; return false; }

    m_currentMapSize = newSize;
    qDebug() << "LMDB mapsize increased to" << newSize;
    return true;
}
// 获取所有键（返回 QByteArray 列表）
QList<QByteArray> LmdbKV::getAllKeysByteArray() const
{
    QMutexLocker locker(&m_mutex);
    QList<QByteArray> keys;
    if (!m_env) return keys;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "getAllKeys: 开启只读事务失败" << mdb_strerror(rc);
        return keys;
    }

    MDB_cursor *cursor = nullptr;
    rc = mdb_cursor_open(txn, m_dbi, &cursor);
    if (rc != MDB_SUCCESS) {
        qCritical() << "getAllKeys: 打开游标失败" << mdb_strerror(rc);
        mdb_txn_abort(txn);
        return keys;
    }

    MDB_val key, data;
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == MDB_SUCCESS) {
        keys.append(QByteArray((const char*)key.mv_data, key.mv_size));
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn); // 只读事务 abort 即可
    return keys;
}

// 获取所有键（返回 QString 列表）
QStringList LmdbKV::getAllKeys() const
{
    QList<QByteArray> byteKeys = getAllKeysByteArray();
    QStringList strKeys;
    strKeys.reserve(byteKeys.size());
    for (const QByteArray &ba : std::as_const(byteKeys)) {
        strKeys.append(QString::fromUtf8(ba));
    }
    return strKeys;
}