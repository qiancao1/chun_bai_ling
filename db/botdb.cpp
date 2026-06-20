#include "botdb.h"
#include "global.h"
#include <QDir>
#include <QDebug>
#include <cstring>
#include <ctime>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

BotDB::BotDB(const QString& path, size_t initialMapSizeMB)
    : m_path(path), m_initialMapSize(initialMapSizeMB * 1024ULL * 1024ULL),
    m_currentMapSize(0), m_env(nullptr), m_dbi_users(0), m_dbi_seq_idx(0), m_dbi_groups(0), m_dbi_friends(0)
{
    if (m_initialMapSize < 1ULL * 1024 * 1024)   // 最小 1MB
        m_initialMapSize = 1ULL * 1024 * 1024;
}

BotDB::~BotDB()
{
    close();
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

bool BotDB::open()
{
    if (m_env) close();

    QDir dir;
    if (!dir.mkpath(m_path)) {
        qCritical() << "无法创建数据库目录:" << m_path;
        return false;
    }

    m_currentMapSize = m_initialMapSize;

    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) {
        qCritical() << "mdb_env_create 失败:" << mdb_strerror(rc);
        return false;
    }
    mdb_env_set_maxdbs(m_env, 10);
    mdb_env_set_mapsize(m_env, m_currentMapSize);
    QByteArray pathBytes = m_path.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), 0, 0664);
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
    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        qCritical() << "提交事务失败:" << mdb_strerror(rc);
        return false;
    }
    AppendEventLog( "数据库已打开，目录:" + m_path + "初始mapsize:" + QString::number((m_currentMapSize >> 20)) + "MB");


    return true;

fail:
    mdb_txn_abort(txn);
    qCritical() << "打开子数据库失败:" << mdb_strerror(rc);
    return false;
}

// 关闭环境并释放所有句柄
void BotDB::close()
{
    if (m_env) {
        // 关闭所有已打开的 DBI
        if (m_dbi_users) mdb_dbi_close(m_env, m_dbi_users);
        if (m_dbi_seq_idx) mdb_dbi_close(m_env, m_dbi_seq_idx);
        if (m_dbi_groups) mdb_dbi_close(m_env, m_dbi_groups);
        if (m_dbi_friends) mdb_dbi_close(m_env, m_dbi_friends);
        if (m_dbi_subscriptions) mdb_dbi_close(m_env, m_dbi_subscriptions);
        m_dbi_subscriptions = 0;
        mdb_env_close(m_env);
        m_env = nullptr;
    }
    m_dbi_users = m_dbi_seq_idx = m_dbi_groups = m_dbi_friends = 0;
}

// 重新打开环境（扩容后调用），保持原有的 m_currentMapSize
bool BotDB::reopenEnvironment()
{
    if (m_env) close();

    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) return false;

    mdb_env_set_mapsize(m_env, m_currentMapSize);
    QByteArray pathBytes = m_path.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), 0, 0664);
    if (rc != MDB_SUCCESS) {
        mdb_env_close(m_env);
        m_env = nullptr;
        return false;
    }

    ensureSparseFile();

    // 重新打开所有子数据库
    MDB_txn *txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) return false;

    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &m_dbi_users);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "seq_to_openid", MDB_CREATE, &m_dbi_seq_idx);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "groups", MDB_CREATE, &m_dbi_groups);
    if (rc != MDB_SUCCESS) goto reopen_fail;
    rc = mdb_dbi_open(txn, "friends", MDB_CREATE, &m_dbi_friends);
    if (rc != MDB_SUCCESS) goto reopen_fail;

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) goto reopen_fail;

    qDebug() << "扩容后重新打开环境成功，新 mapsize:" << (m_currentMapSize >> 20) << "MB";
    return true;

reopen_fail:
    mdb_txn_abort(txn);
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
        if (rc != MDB_SUCCESS) return false;

        // 执行写入操作，它应该返回 0 表示成功，非 0 表示错误（可能是 MDB_MAP_FULL）
        int opResult = writeFunc(txn);

        if (opResult == MDB_SUCCESS) {
            rc = mdb_txn_commit(txn);
            if (rc == MDB_SUCCESS)
                return true;
            else if (rc == MDB_MAP_FULL) {
                // 提交时也可能 MAP_FULL，需要回滚并扩容
                mdb_txn_abort(txn);
                // 尝试扩容
                if (!increaseMapSize()) return false;
                retry++;
                continue;
            } else {
                mdb_txn_abort(txn);
                return false;
            }
        } else if (opResult == MDB_MAP_FULL) {
            mdb_txn_abort(txn);
            if (!increaseMapSize()) return false;
            retry++;
            continue;
        } else {
            mdb_txn_abort(txn);
            return false;
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

bool BotDB::getRecord(MDB_txn *txn, MDB_dbi dbi, const QByteArray &keyData, void *outData, size_t size)
{
    MDB_val key, value;
    key.mv_data = (void*)keyData.constData();
    key.mv_size = keyData.size();
    int rc = mdb_get(txn, dbi, &key, &value);
    if (rc == MDB_SUCCESS && value.mv_size == size) {
        memcpy(outData, value.mv_data, size);
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

uint32_t BotDB::getOrUpdateUser(const QString &openid, QString &name)
{
    uint32_t resultSeq = 0;
    bool success = retryWrite([&](MDB_txn *txn) -> int {
        QByteArray keyData = QByteArray::fromHex(openid.toUtf8());
        if (keyData.isEmpty()) return -1;

        MDB_val key, value;
        key.mv_data = keyData.data();
        key.mv_size = keyData.size();

        int rc = mdb_get(txn, m_dbi_users, &key, &value);
        if (rc == MDB_NOTFOUND) {
            // 初次写入：清零整个结构体
            UserRecord record = {};
            uint32_t newSeq = getNextSeqId(txn);
            if (newSeq == 0) return -1;
            record.seq_id = newSeq;
            record.reserved_qq = 0;
            record.record_time = nowMinutes();
            record.invited_group_count = 0;

            QByteArray nameBytes = name.toUtf8();
            size_t copyLen = std::min<size_t>(63, (size_t)nameBytes.size());
            memcpy(record.nickname, nameBytes.constData(), copyLen);
            record.nickname[copyLen] = '\0';

            rc = putRecord(txn, m_dbi_users, keyData, &record, sizeof(record));
            if (rc != MDB_SUCCESS) return rc;
            if (!saveSeqToOpenId(txn, newSeq, keyData)) return -1;
            resultSeq = newSeq;
            return MDB_SUCCESS;
        } else if (rc == MDB_SUCCESS) {
            UserRecord record;
            memcpy(&record, value.mv_data, sizeof(UserRecord));

            if (name.isEmpty()) {
                name = QString::fromUtf8(record.nickname);   // 数据库中的 nickname 保证是干净的 UTF-8
                resultSeq = record.seq_id;
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
            return MDB_SUCCESS;
        }
        return rc;
    });
    return success ? resultSeq : 0;
}

bool BotDB::getUserBySeqId(uint32_t seq_id, UserRecord &outRecord)
{
    // 只读操作无需扩容，不加锁也可以（但为了安全可以使用读事务）
    if (!m_env) return false;
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    QByteArray openidBin;
    if (!getOpenIdBySeq(txn, seq_id, openidBin)) {
        mdb_txn_abort(txn);
        return false;
    }
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

bool BotDB::incrementInvitedGroupCount(uint32_t seq_id, int delta)
{
    return retryWrite([&](MDB_txn *txn) -> int {
        QByteArray openidBin;
        if (!getOpenIdBySeq(txn, seq_id, openidBin))
            return -1;
        MDB_val key, value;
        key.mv_data = openidBin.data();
        key.mv_size = openidBin.size();
        int rc = mdb_get(txn, m_dbi_users, &key, &value);
        if (rc != MDB_SUCCESS) return rc;
        UserRecord record;
        memcpy(&record, value.mv_data, sizeof(record));
        record.invited_group_count += delta;
        return putRecord(txn, m_dbi_users, openidBin, &record, sizeof(record));
    });
}

bool BotDB::addGroup(const QString &groupIdHex, uint32_t createTimeMinutes, uint32_t inviterSeqId)
{
    return retryWrite([&](MDB_txn *txn) -> int {
        QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
        if (keyData.isEmpty()) return -1;
        GroupRecord record{ createTimeMinutes, inviterSeqId };
        return putRecord(txn, m_dbi_groups, keyData, &record, sizeof(record));
    });
}

bool BotDB::getGroupInfo(const QString &groupIdHex, GroupRecord &outRecord)
{
    if (!m_env) return false;
    QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
    if (keyData.isEmpty()) return false;
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    bool ok = getRecord(txn, m_dbi_groups, keyData, &outRecord, sizeof(outRecord));
    mdb_txn_abort(txn);
    return ok;
}

bool BotDB::isGroupExist(const QString &groupIdHex)
{
    GroupRecord dummy;
    return getGroupInfo(groupIdHex, dummy);
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
    bool ok = putRecord(txn, m_dbi_friends, keyData, &addTimeMinutes, sizeof(addTimeMinutes));
    if (ok) {
        rc = mdb_txn_commit(txn);
        return rc == MDB_SUCCESS;
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
    bool ok = delRecord(txn, m_dbi_friends, keyData);
    if (ok) {
        rc = mdb_txn_commit(txn);
        return rc == MDB_SUCCESS;
    } else {
        mdb_txn_abort(txn);
        return false;
    }
}

bool BotDB::isFriend(uint32_t userSeqId)
{
    if (!m_env) return false;
    QByteArray keyData((char*)&userSeqId, sizeof(userSeqId));
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    uint32_t dummy;
    bool ok = getRecord(txn, m_dbi_friends, keyData, &dummy, sizeof(dummy));
    mdb_txn_abort(txn);
    return ok;
}

QList<uint32_t> BotDB::getFriendList()
{

    QList<uint32_t> result;
    if (!m_env) return result;
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

bool BotDB::getFriendAddTime(uint32_t userSeqId, uint32_t &outAddTimeMinutes)
{
    if (!m_env) return false;
    QByteArray keyData((char*)&userSeqId, sizeof(userSeqId));
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    bool ok = getRecord(txn, m_dbi_friends, keyData, &outAddTimeMinutes, sizeof(outAddTimeMinutes));
    mdb_txn_abort(txn);
    return ok;
}
// 构造 Key
static QByteArray makeSubKey(uint32_t mark, uint8_t param, const QString &groupId)
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


bool BotDB::addSubscription(uint32_t mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
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
bool BotDB::removeSubscription(uint32_t mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
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
QString BotDB::getSubscriptions(uint32_t mark, uint8_t param, const QString &groupId) const
{
    QString result;
    if (groupId.isEmpty() || param > 3)
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
QStringList BotDB::listSubscriptions(uint32_t mark)
{
    QStringList result;
    if (!m_env) return result;

    QByteArray prefix = makeSubPrefix(mark);
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
bool BotDB::clearSubscriptionsByMark(uint32_t mark)
{
    return retryWrite([&](MDB_txn *txn) -> int {
        QByteArray prefix = makeSubPrefix(mark);  // "标记:"
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