
#include "lmdbkv.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>

LmdbKV::LmdbKV(const QString &dbPath, QObject *parent)
    : QObject(parent), m_env(nullptr), m_dbi(0), m_dbPath(dbPath)
{
    // 确保数据库目录存在
    QDir dir;
    if (!dir.mkpath(dbPath)) {
        qCritical() << "无法创建数据库目录:" << dbPath;
        return;
    }

    // 打开 LMDB 环境
    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_create 失败:" << mdb_strerror(rc);
        m_env = nullptr;
        return;
    }

    // 设置最大数据库数量（这里只需要 1 个）
    rc = mdb_env_set_maxdbs(m_env, 1);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_set_maxdbs 失败:" << mdb_strerror(rc);
    }

    // 设置内存映射大小 (例如 10MB，实际可按需调整)
    rc = mdb_env_set_mapsize(m_env, 10 * 1024 * 1024);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_set_mapsize 失败:" << mdb_strerror(rc);
    }

    // 打开环境，使用 MDB_NOSUBDIR 表示 dbPath 就是一个文件（而非目录）
    // 如果希望 dbPath 是一个目录，去掉 MDB_NOSUBDIR 并确保目录存在
    // 这里为了简单，将 dbPath 作为单一文件路径（例如 ./data.mdb）
    QByteArray pathBytes = dbPath.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), MDB_MAPASYNC, 0664);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_open 失败:" << mdb_strerror(rc);
        mdb_env_close(m_env);
        m_env = nullptr;
        return;
    }

    // 开启一个事务并打开数据库（如果不存在则创建）
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
    if (!m_env) return false;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {

        return false;
    }

    MDB_val k = { (size_t)key.size(), (void*)key.constData() };
    MDB_val v = { (size_t)value.size(), (void*)value.constData() };
    rc = mdb_put(txn, m_dbi, &k, &v, 0); // 0 表示覆盖或插入

    if (rc == MDB_SUCCESS) {
        rc = mdb_txn_commit(txn);
        if (rc != MDB_SUCCESS) {
            qCritical() << "put: 提交事务失败" << mdb_strerror(rc);
            return false;
        }
        return true;
    } else {
        mdb_txn_abort(txn);
        qCritical() << "put: mdb_put 失败" << mdb_strerror(rc);
        return false;
    }
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
    if (!m_env) return QByteArray();

    MDB_txn *txn = nullptr;
    // 只读事务使用 MDB_RDONLY
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) {

        return QByteArray();
    }

    MDB_val k = { (size_t)key.size(), (void*)key.constData() };
    MDB_val v;
    rc = mdb_get(txn, m_dbi, &k, &v);
    if (rc == MDB_NOTFOUND) {
        mdb_txn_abort(txn);
        return QByteArray();   // 键不存在
    } else if (rc != MDB_SUCCESS) {
        mdb_txn_abort(txn);

        return QByteArray();
    }

    QByteArray result((const char*)v.mv_data, v.mv_size);
    mdb_txn_abort(txn);   // 只读事务可以 abort 或 commit
    return result;
}

bool LmdbKV::remove(const QString &key)
{
    return removeInternal(key.toUtf8());
}

bool LmdbKV::remove(const QByteArray &key)
{
    return removeInternal(key);
}

bool LmdbKV::removeInternal(const QByteArray &key)
{
    if (!m_env) return false;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {

        return false;
    }

    MDB_val k = { (size_t)key.size(), (void*)key.constData() };
    rc = mdb_del(txn, m_dbi, &k, nullptr); // 只需提供键即可删除
    if (rc == MDB_NOTFOUND) {
        // 键不存在，视为成功删除（或者返回 false，由调用者决定）
        mdb_txn_abort(txn);
        return true;   // 或者 return false，这里宽容处理
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