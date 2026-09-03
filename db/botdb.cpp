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

#include "botdb.h"
#include "global.h"
#include <QDir>
#include <QDebug>
#include <cstring>
#include <ctime>
#include <QTimer>


BotDB::BotDB(const QString& path, size_t initialMapSizeMB)
    : m_path(path), m_initialMapSize(initialMapSizeMB * 1024ULL * 1024ULL),
    m_currentMapSize(0), m_env(nullptr), m_dbi_users(0), m_dbi_seq_idx(0), m_dbi_groups(0), m_dbi_friends(0)
{
    if (m_initialMapSize < 1ULL * 1024 * 1024)   // 最小 1MB
        m_initialMapSize = 1ULL * 1024 * 1024;
    // 在 open() 成功后
    m_cacheTimer = new QTimer ;
    connect(m_cacheTimer, &QTimer::timeout, this, &BotDB::checkCleanup);
    m_cacheTimer->start(5 * 60 * 1000);  // 5分钟检查一次

    // 计数保存定时器：每10分钟保存一次
    m_saveTimer = new QTimer ;
    connect(m_saveTimer, &QTimer::timeout, this, &BotDB::saveDailyStats);
    m_saveTimer->start(10 * 60 * 1000); // 10分钟
    loadDailyStats();            // 加载当日统计
}

BotDB::~BotDB()
{
    close();
}
void BotDB::checkCleanup()
{
    static qint64 lastUserClean = 0;
    static qint64 lastGroupClean = 0;
    qint64 now = QDateTime::currentSecsSinceEpoch();

    // 每4小时清理用户缓存
    if (now - lastUserClean >= 4 * 3600) {
        cleanUserCache();
        lastUserClean = now;
    }
    // 每24小时清理群缓存
    if (now - lastGroupClean >= 24 * 3600) {
        cleanGroupCache();
        lastGroupClean = now;
    }
}

void BotDB::cleanUserCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_userCache.clear();     // 直接清空，下次访问会从LMDB重新加载

}

void BotDB::cleanGroupCache()
{
    QMutexLocker locker(&m_cacheMutex);
    for (auto it = m_groupCache.begin(); it != m_groupCache.end(); ) {
        const GroupRecord2& rec = it.value();
        if (rec.jojnyz.isEmpty() && rec.jojntime.isEmpty()) {
            it = m_groupCache.erase(it);
        } else {
            ++it;
        }
    }
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
    rc = mdb_env_open(m_env, pathBytes.constData(), MDB_WRITEMAP | MDB_NOMETASYNC, 0664);
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


    rc = mdb_dbi_open(txn, "account_stats", MDB_CREATE, &m_dbi_account_stats);
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
    if (m_saveTimer) {
        m_saveTimer->stop();
        saveDailyStats();
    }

    if (m_dbi_account_stats) mdb_dbi_close(m_env, m_dbi_account_stats);
}

// 重新打开环境（扩容后调用），保持原有的 m_currentMapSize
bool BotDB::reopenEnvironment()
{
    if (m_env) close();

    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) return false;

    mdb_env_set_mapsize(m_env, m_currentMapSize);
    QByteArray pathBytes = m_path.toUtf8();
    rc = mdb_env_open(m_env, pathBytes.constData(), MDB_WRITEMAP | MDB_NOMETASYNC, 0664);
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

        if (rc != MDB_SUCCESS) return false;  // begin失败，无事务需释放

        bool needAbort = true;   // 标记是否需要手动abort
        try {
            int opResult = writeFunc(txn);

            if (opResult == MDB_SUCCESS) {
                rc = mdb_txn_commit(txn);
                if (rc == MDB_SUCCESS) {
                    needAbort = false;   // 提交成功，不再abort
                    return true;
                }
                // commit失败，根据LMDB文档事务已自动回滚，不应再调用abort
                // 但为了释放事务资源，我们仍需要将txn置为无效（无需调用abort）
                // 所以这里保持 needAbort = true，但不调用abort？不行，因为事务已回滚，调用abort是未定义行为。
                // 正确做法：对于commit失败，我们既不能abort，但资源已由LMDB释放，所以直接将needAbort设为false。
                needAbort = false;   // 关键：commit失败后事务已终止，不应再abort
                if (rc == MDB_MAP_FULL) {
                    if (!increaseMapSize()) return false;
                    retry++;
                    continue;
                }
                return false;
            } else if (opResult == MDB_MAP_FULL) {
                // writeFunc返回MAP_FULL，事务仍活跃，需要abort
                mdb_txn_abort(txn);
                needAbort = false;   // 已abort，不再重复
                if (!increaseMapSize()) return false;
                retry++;
                continue;
            } else {
                // 其他错误，手动abort
                mdb_txn_abort(txn);
                needAbort = false;
                return false;
            }
        } catch (...) {
            // 异常发生，事务仍活跃，必须abort
            mdb_txn_abort(txn);
            throw;   // 或 return false，取决于您的错误处理策略
        }

        // 如果needAbort为true（理论上不会到达这里，但以防万一）
        if (needAbort) {
            mdb_txn_abort(txn);
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
    if (rc == MDB_SUCCESS) {
        size_t copySize = std::min<size_t>(value.mv_size, size);
        memcpy(outData, value.mv_data, copySize);
        if (copySize < size) {
            memset(static_cast<char*>(outData) + copySize, 0, size - copySize);
        }
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

static QString fetchGroupNameFromApi(QQBotClient *qqbot, const QString &groupIdHex)
{
    QString json = qqbot->get_groups_info(groupIdHex);
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError) {
        QJsonObject obj = doc.object();
        return obj["group_name"].toString();
    }
    return QString();
}
uint32_t BotDB::getOrUpdateUser(const QString &openid, QString &name)
{
    uint32_t resultSeq = 0;

    QByteArray userKeyBytes = QByteArray::fromHex(openid.toUtf8());
    BinKey userKey;
    memcpy(userKey.data, userKeyBytes.constData(), 16);
    {
        QMutexLocker locker(&m_cacheMutex);
        auto itUser = m_userCache.find(userKey);
        if (itUser != m_userCache.end()) {
            UserRecord cachedUser = itUser.value();
            return cachedUser.seq_id;
        }
    }


    bool success = retryWrite([&](MDB_txn *txn) -> int {
        QByteArray keyData = QByteArray::fromHex(openid.toUtf8());
        if (keyData.isEmpty()) return -1;

        MDB_val key, value;
        key.mv_data = keyData.data();
        key.mv_size = keyData.size();

        int rc = mdb_get(txn, m_dbi_users, &key, &value);
        if (rc == MDB_NOTFOUND) {

            return -1;
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
uint32_t BotDB::getOrUpdateUser(QQBotClient *qqbot, MessageEvent &ev, bool hc)
{

    bool isGroup = (ev.type == 0);
    if (!isGroup) isGroup = (ev.type == 18);

    QByteArray userKeyBytes = QByteArray::fromHex(ev.user.toUtf8());
    BinKey userKey;
    memcpy(userKey.data, userKeyBytes.constData(), 16);

    BinKey groupKey;
    if (isGroup) {
        QByteArray groupKeyBytes = QByteArray::fromHex(ev.groupId.toUtf8());
        memcpy(groupKey.data, groupKeyBytes.constData(), 16);
    }

    // 每日消息计数（不变）
    {
        QMutexLocker locker(&m_msgMutex);
        checkDayChange();
        m_userDailyMsg[userKey] = m_userDailyMsg.value(userKey, 0) + 1;
        if (isGroup) {
            m_groupDailyMsg[groupKey] = m_groupDailyMsg.value(groupKey, 0) + 1;
        }
        auto *info = qqbot-> m_info;
        info->message_received++;
        info->received++;
        info->received_day++;
    }

    // 缓存相关
    bool userCacheHit = false;
    bool groupCacheHit = false;
    UserRecord cachedUser{};
    GroupRecord2 cachedGroup{}; // [修改] 原为 GroupRecord cachedGroup{};

    // LMDB 读取结果
    bool userExists = false;
    bool groupExists = false;
    UserRecord oldUser{};
    GroupRecord oldGroup{};
    uint32_t oldSeqId = 0;

    // 决策变量
    bool needUpdateGroup = false;
    bool needUpdateUser = false;
    bool needUpdateBitmap = false;
    QString newGroupName;

    // 写事务结果
    uint32_t resultSeq = 0;
    QString finalGroupName;
    int finalBitmap = 0;
    QString finalUserNickname,finalUserNickname2;
    UserRecord finalUser{};
    GroupRecord finalGroup{};

    // ======================== 2. 尝试从缓存读取 ========================
    {
        QMutexLocker locker(&m_cacheMutex);
        auto itUser = m_userCache.find(userKey);
        if (itUser != m_userCache.end()) {
            cachedUser = itUser.value();
            userCacheHit = true;
        }
        if (isGroup) {
            auto itGroup = m_groupCache.find(groupKey);
            if (itGroup != m_groupCache.end()) {
                cachedGroup = itGroup.value();   // GroupRecord2
                groupCacheHit = true;
            }
        }
    }


    // ======================== 3. 如果缓存命中，基于缓存数据做决策 ========================
    if (userCacheHit) {
        // ---------- 用户缓存命中 ----------
        oldUser = cachedUser;          // UserRecord 是 POD，拷贝一次
        userExists = true;
        oldSeqId = cachedUser.seq_id;

        // ---------- 群缓存处理 ----------
        if (isGroup) {
            if (groupCacheHit) {
                groupExists = true;
                // 注意：cachedGroup 是 GroupRecord2，直接使用其字段
            } else {
                groupExists = false;
            }
        }

        // ---------- 决策：群名是否需要更新 ----------
        if (isGroup && hc == false) {
            if (!groupExists) {
                needUpdateGroup = true;
                newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
            } else {
                const qint64 ONE_MONTH = 30 * 24 * 3600;
                qint64 now = time(nullptr);
                // 直接使用 cachedGroup.ncgx（无需转换）
                if (cachedGroup.ncgx == 0 || (now - cachedGroup.ncgx > ONE_MONTH)) {
                    needUpdateGroup = true;
                    newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
                }
            }
        }

        // ---------- 决策：管理员位是否需要更新 ----------
        if (isGroup && groupExists && ev.at_you) {
            bool currentAdmin = (cachedGroup.bitmap & BIT_ADMIN) != 0;
            if (currentAdmin != ev.bot_admin) {
                needUpdateBitmap = true;
            }
        }

        // ---------- 决策：用户昵称是否需要更新 ----------
        if (!ev.nickname.isEmpty()) {
            QByteArray newName = ev.nickname.toUtf8();
            if (strcmp(oldUser.nickname, newName.constData()) != 0)
                needUpdateUser = true;
        }

        // ---------- 如果无需任何更新，直接返回缓存数据（零转换） ----------
        if (!needUpdateGroup && !needUpdateUser && !needUpdateBitmap) {
            if (isGroup && groupExists) {
                ev.groupname = cachedGroup.name;          // QString 共享，不复制字符
                ev.bitmap = cachedGroup.bitmap;
                memcpy(ev.qid, cachedGroup.qid, sizeof(ev.qid));
            }
            // 用户昵称
            ev.nickname = QString::fromUtf8(oldUser.nickname);
            ev.nickname2 = QString::fromUtf8(oldUser.name);
            if (!isGroup) {
                ev.bitmap = 0;
                memset(ev.qid, 0, sizeof(ev.qid));
            }
            if(ev.type == 0 && ev.subType==1)
            {
                for(int i=0;i<cachedGroup.jojnyz.size();++i)
                {
                    if(oldSeqId == cachedGroup.jojnyz[i])
                    {
                        cachedGroup.jojnyz.removeAt(i);
                        cachedGroup.jojntime.removeAt(i);

                        auto *c = m_botClients[ev.appid];
                        c->setGroupRestrictChatSetting(ev.groupId,ev.user,0,[](auto,auto){});
                        QString text = QString("<@%1> 入群验证已通过").arg(ev.user);
                        c->send_msgAsync(ev.type,ev.groupId,"入群验证",text,ev.msgId);

                    }
                }
            }
            return oldSeqId;
        }

        // ---------- 需要更新：将缓存的 GroupRecord2 转换为 GroupRecord（只做一次） ----------
        if (isGroup && groupExists) {
            oldGroup = toGroupRecord(cachedGroup);  // 转换，包含字符串复制到 char[]
        }

    }

    else {
        // ======================== 4. 缓存未命中：执行 LMDB 只读事务 ========================
        MDB_txn *txn = nullptr;
        try {
            if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
                return 0;
            if (isGroup && !ev.groupId.isEmpty()) {
                QByteArray groupKey = QByteArray::fromHex(ev.groupId.toUtf8());
                if (!groupKey.isEmpty())
                    groupExists = getRecord(txn, m_dbi_groups, groupKey, &oldGroup, sizeof(oldGroup));
            }

            QByteArray userKey = QByteArray::fromHex(ev.user.toUtf8());
            if (!userKey.isEmpty()) {
                userExists = getRecord(txn, m_dbi_users, userKey, &oldUser, sizeof(oldUser));
                if (userExists) oldSeqId = oldUser.seq_id;
            }
            mdb_txn_abort(txn);
        } catch (const std::exception &e) {
            qWarning() << "BotDB: 只读事务异常" << e.what();
            if (txn) mdb_txn_abort(txn);
            return 0;
        } catch (...) {
            if (txn) mdb_txn_abort(txn);
            return 0;
        }

        // ----- 决策（群） -----
        if (isGroup && hc == false) {
            if (!groupExists) {
                needUpdateGroup = true;
                newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
            } else {
                const qint64 ONE_MONTH = 30 * 24 * 3600;
                qint64 now = time(nullptr);
                if (oldGroup.ncgx == 0 || (now - oldGroup.ncgx > ONE_MONTH)) {
                    needUpdateGroup = true;
                    newGroupName = fetchGroupNameFromApi(qqbot, ev.groupId);
                }
            }
        }

        // ----- 决策（bitmap 管理员位）-----
        if (isGroup && groupExists && ev.at_you) {
            bool currentAdmin = (oldGroup.bitmap & BIT_ADMIN) != 0;
            if (currentAdmin != ev.bot_admin) {
                needUpdateBitmap = true;
            }
        }

        // ----- 决策（用户） -----
        if (userExists && !ev.nickname.isEmpty()) {
            QByteArray newName = ev.nickname.toUtf8();
            if (strcmp(oldUser.nickname, newName.constData()) != 0)
                needUpdateUser = true;
        }
    }

    // ======================== 5. 写事务 ========================
    bool success = retryWrite([&](MDB_txn *txn) -> int {
        int finalRc = MDB_SUCCESS;

        // 5a. 处理群记录
        if (isGroup) {
            QByteArray groupKey = QByteArray::fromHex(ev.groupId.toUtf8());
            if (groupKey.isEmpty()) return -1;

            GroupRecord currentGroup = oldGroup;
            if (!groupExists) {
                // 新建群
                memset(&currentGroup, 0, sizeof(currentGroup));
                currentGroup.create_time = static_cast<uint32_t>(time(nullptr));
                if (!newGroupName.isEmpty()) {
                    size_t copyLen = std::min<size_t>(newGroupName.toUtf8().size(), sizeof(currentGroup.name)-1);
                    memcpy(currentGroup.name, newGroupName.toUtf8().constData(), copyLen);
                    currentGroup.name[copyLen] = '\0';
                    currentGroup.ncgx = time(nullptr);
                } else {
                    currentGroup.name[0] = '\0';
                    currentGroup.ncgx = 0;
                }
                if (ev.at_you) {
                    if (ev.bot_admin)
                        currentGroup.bitmap |= BIT_ADMIN;
                    else
                        currentGroup.bitmap &= ~BIT_ADMIN;
                }
                int rc = putRecord(txn, m_dbi_groups, groupKey, &currentGroup, sizeof(currentGroup));
                if (rc != MDB_SUCCESS) finalRc = rc;
            } else {
                // 群已存在，处理各类更新
                if (needUpdateGroup && !newGroupName.isEmpty()) {
                    size_t copyLen = std::min<size_t>(newGroupName.toUtf8().size(), sizeof(currentGroup.name)-1);
                    memcpy(currentGroup.name, newGroupName.toUtf8().constData(), copyLen);
                    currentGroup.name[copyLen] = '\0';
                    currentGroup.ncgx = time(nullptr);
                }
                if (needUpdateBitmap) {
                    if (ev.bot_admin)
                        currentGroup.bitmap |= BIT_ADMIN;
                    else
                        currentGroup.bitmap &= ~BIT_ADMIN;
                }
                if (needUpdateGroup || needUpdateBitmap) {
                    int rc = putRecord(txn, m_dbi_groups, groupKey, &currentGroup, sizeof(currentGroup));
                    if (rc != MDB_SUCCESS) finalRc = rc;
                }
            }
            // 保存最终群记录
            finalGroup = currentGroup;
            finalGroupName = QString::fromUtf8(currentGroup.name, strnlen(currentGroup.name, sizeof(currentGroup.name)));
            finalBitmap = currentGroup.bitmap;
        }

        // 5b. 处理用户记录（保持不变）
        QByteArray userKey = QByteArray::fromHex(ev.user.toUtf8());
        if (userKey.isEmpty()) return -1;

        if (!userExists) {
            UserRecord newRec = {};
            uint32_t newSeq = getNextSeqId(txn);
            if (newSeq == 0) return -1;
            newRec.seq_id = newSeq;
            newRec.record_time = nowMinutes();
            newRec.invited_group_count = 0;

            QByteArray nameBytes = ev.nickname.toUtf8();
            size_t copyLen = std::min<size_t>(nameBytes.size(), sizeof(newRec.nickname)-1);
            memcpy(newRec.nickname, nameBytes.constData(), copyLen);
            newRec.nickname[copyLen] = '\0';
            newRec.name[0] = '\0';

            int rc = putRecord(txn, m_dbi_users, userKey, &newRec, sizeof(newRec));
            if (rc != MDB_SUCCESS) {
                finalRc = rc;
            } else if (!saveSeqToOpenId(txn, newSeq, userKey)) {
                finalRc = -1;
            } else {
                resultSeq = newSeq;
                finalUser = newRec;
                finalUserNickname = ev.nickname;

            }
        } else {
            UserRecord updatedUser = oldUser;
            if (needUpdateUser) {
                QByteArray newNameBytes = ev.nickname.toUtf8();
                size_t copyLen = std::min<size_t>(newNameBytes.size(), sizeof(updatedUser.nickname)-1);
                memcpy(updatedUser.nickname, newNameBytes.constData(), copyLen);
                updatedUser.nickname[copyLen] = '\0';
                updatedUser.name[copyLen] = '\0';
                updatedUser.record_time = nowMinutes();
                int rc = putRecord(txn, m_dbi_users, userKey, &updatedUser, sizeof(updatedUser));
                if (rc != MDB_SUCCESS) finalRc = rc;
            }
            if (finalRc == MDB_SUCCESS) {
                resultSeq = oldSeqId;
                finalUser = updatedUser;
                finalUserNickname = QString::fromUtf8(updatedUser.nickname);
                finalUserNickname2 = QString::fromUtf8(updatedUser.name);
            }

        }

        return finalRc;
    });

    // ======================== 6. 事务成功后，更新缓存 ========================
    if (success && resultSeq != 0) {
        {
            QMutexLocker locker(&m_cacheMutex);
            m_userCache[userKey] = finalUser;
        }
        if (isGroup) {
            QMutexLocker locker(&m_cacheMutex);

            // 将 finalGroup 转换为 GroupRecord2
            GroupRecord2 cacheRecord = toGroupRecord2(finalGroup);
            // 加载 jojnyz 数据（如果存在）
            QList<int> ids, times;

            if(ev.type == 0 && ev.subType==1)
            {
                for(int i=0;i<cacheRecord.jojnyz.size();++i)
                {
                    if(resultSeq == cacheRecord.jojnyz[i])
                    {
                        cacheRecord.jojnyz.removeAt(i);
                        cacheRecord.jojntime.removeAt(i);

                        auto *c = m_botClients[ev.appid];
                        c->setGroupRestrictChatSetting(ev.groupId,ev.user,0,[](auto,auto){});
                         QString text = QString("<@%1> 入群验证已通过").arg(ev.user);
                        c->send_msgAsync(ev.type,ev.groupId,"入群验证",text,ev.msgId);

                    }
                }
            }
            // 存入缓存
            m_groupCache[groupKey] = cacheRecord;

            ev.groupname = finalGroupName;
            ev.bitmap = finalBitmap;
            memcpy(ev.qid, finalGroup.qid, sizeof(ev.qid));
        }
        if (ev.nickname.isEmpty()) {
            ev.nickname = finalUserNickname;

        }
        if(!finalUserNickname2.isEmpty())
        {
            ev.nickname2 = finalUserNickname2;
        }

        return resultSeq;
    }

    return 0;
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
    outRecord.name[0] = '\0';

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
bool BotDB::getUsersByPage(bool onlyNameEmpty, int offset, int limit,
                           QList<UserRecord>& outUsers, int& totalCount)
{
    outUsers.clear();
    totalCount = 0;
    if (!m_env) return false;

    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return false;

    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_users, &cursor) != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return false;
    }

    // 定义新旧记录大小
    const size_t OLD_RECORD_SIZE = 80;   // 无 name 字段
    const size_t NEW_RECORD_SIZE = sizeof(UserRecord);

    MDB_val key, value;
    int rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    int skipped = 0;
    int collected = 0;

    while (rc == MDB_SUCCESS) {
        UserRecord rec;
        bool valid = false;

        // 尝试新格式
        if (value.mv_size == NEW_RECORD_SIZE) {
            memcpy(&rec, value.mv_data, NEW_RECORD_SIZE);
            valid = true;
        }
        // 尝试旧格式（无 name 字段）
        else if (value.mv_size == OLD_RECORD_SIZE) {
            // 旧格式布局：前 80 字节与 UserRecord 前 80 字节一致（seq_id, bitmap, record_time, invited_group_count, nickname）
            const char* data = (const char*)value.mv_data;
            memcpy(&rec.seq_id, data, 4);
            memcpy(&rec.bitmap, data + 4, 4);
            memcpy(&rec.record_time, data + 8, 4);
            memcpy(&rec.invited_group_count, data + 12, 4);
            memcpy(rec.nickname, data + 16, 64);
            rec.nickname[63] = '\0';
            // 新字段 name 置空
            rec.name[0] = '\0';
            valid = true;
        }

        if (valid) {
            bool nameEmpty = (rec.name[0] == '\0');
            if(rec.name[0] != '\0' || rec.nickname[0] != '\0')  {
                if ((onlyNameEmpty && nameEmpty) || (!onlyNameEmpty && !nameEmpty)) {
                    totalCount++;
                    if (skipped < offset) {
                        skipped++;
                    } else if (collected < limit) {
                        outUsers.append(rec);
                        collected++;
                    }
                }
            }
        }
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return true;
}

// 更新用户缓存（传入 openid 二进制和 UserRecord）
void BotDB::updateUserCache(const QByteArray &openidBin, const UserRecord &record) {
    BinKey key;
    memcpy(key.data, openidBin.constData(), 16);
    QMutexLocker locker(&m_cacheMutex);
    m_userCache[key] = record;

}


void BotDB::updateGroupCache(const QByteArray &groupIdBin, const GroupRecord2 &record) {
    BinKey key;
    memcpy(key.data, groupIdBin.constData(), 16);

    QMutexLocker locker(&m_cacheMutex);

    m_groupCache[key] = record;
}


bool BotDB::updateUserBySeqId(uint32_t seq_id, const UserRecord &newRecord)
{
    // 拷贝一份，确保 record_time 被刷新
    UserRecord toWrite = newRecord;
    toWrite.record_time = nowMinutes();   // 强制更新时间
    return updateUserBySeqId(seq_id, [&](UserRecord &r) {
        r = toWrite;

    });
}
bool BotDB::updateUserBySeqId(uint32_t seq_id, std::function<void(UserRecord&)> updater)
{
    if (!m_env) return false;

    UserRecord finalRecord;
    QByteArray finalOpenidBin;
    bool recordChanged = false;

    bool success = retryWrite([&](MDB_txn *txn) -> int {
        QByteArray openidBin;
        if (!getOpenIdBySeq(txn, seq_id, openidBin))
            return MDB_NOTFOUND;

        MDB_val key, value;
        key.mv_data = openidBin.data();
        key.mv_size = openidBin.size();
        int rc = mdb_get(txn, m_dbi_users, &key, &value);
        if (rc != MDB_SUCCESS) return rc;

        UserRecord record;
        bool valid = false;

        // 尝试新格式（完整记录）
        if (value.mv_size == sizeof(UserRecord)) {
            memcpy(&record, value.mv_data, sizeof(UserRecord));
            valid = true;
        }
        // 尝试旧格式（80字节，无 name 字段）
        else if (value.mv_size == 80) {
            const char* data = (const char*)value.mv_data;
            memcpy(&record.seq_id, data, 4);
            memcpy(&record.bitmap, data + 4, 4);
            memcpy(&record.record_time, data + 8, 4);
            memcpy(&record.invited_group_count, data + 12, 4);
            memcpy(record.nickname, data + 16, 64);
            record.nickname[63] = '\0';
            record.name[0] = '\0';   // 旧记录 name 为空
            valid = true;
        }

        if (!valid)
            return -1;

        // 调用 updater 修改记录（可修改 name 等字段）
        updater(record);
        record.record_time = nowMinutes();

        // 以新格式写回（升级为完整记录）
        rc = putRecord(txn, m_dbi_users, openidBin, &record, sizeof(record));
        if (rc == MDB_SUCCESS) {
            finalRecord = record;
            finalOpenidBin = openidBin;
            recordChanged = true;
        }
        return rc;
    });

    if (success && recordChanged) {
        updateUserCache(finalOpenidBin, finalRecord);
    }
    return success;
}



bool BotDB::getOpenIdBySeqId(uint32_t seqId, QString &outOpenidHex) {
    if (!m_env) return false;
    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return false;
    QByteArray bin;
    bool ok = getOpenIdBySeq(txn, seqId, bin);  // 复用私有方法
    mdb_txn_abort(txn);
    if (ok) {
        outOpenidHex = bin.toHex(); // 转为32位Hex字符串
        return true;
    }
    return false;
}
void BotDB::loadDailyStats()
{
    QString dateStr = QDate::currentDate().toString("yyyy-MM-dd");
    if (m_todayDate == dateStr) return;  // 日期未变，保留当前数据
    m_todayDate = dateStr;

    // 清空旧数据
    QMutexLocker locker(&m_msgMutex);
    m_userDailyMsg.clear();
    m_groupDailyMsg.clear();

    // 加载用户统计
    QFile userFile(  m_path+ "/msgstat_user_" + dateStr + ".dat");
    if (userFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&userFile);
        in >> m_userDailyMsg;
    }

    // 加载群统计
    QFile groupFile(m_path+"/msgstat_group_" + dateStr + ".dat");
    if (groupFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&groupFile);
        in >> m_groupDailyMsg;
    }
}
bool BotDB::saveAccountStats(uint32_t appid, uint32_t minuteIndex, const AccountStats &stats)
{
    // 构造 key: appid + minute_index
    QByteArray keyData;
    keyData.append((char*)&appid, sizeof(appid));
    keyData.append((char*)&minuteIndex, sizeof(minuteIndex));

    return retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_account_stats, keyData, &stats, sizeof(stats));
    });
}
bool BotDB::getAccountStats(uint32_t appid, uint32_t minuteIndex, AccountStats &outStats)
{
    QByteArray keyData;
    keyData.append((char*)&appid, sizeof(appid));
    keyData.append((char*)&minuteIndex, sizeof(minuteIndex));

    MDB_txn *txn = nullptr;
    if (mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn) != MDB_SUCCESS)
        return false;

    bool ok = getRecord(txn, m_dbi_account_stats, keyData, &outStats, sizeof(outStats));
    mdb_txn_abort(txn);
    return ok;
}
bool BotDB::getTodayDiff(uint32_t appid, const AccountStats &currentStats, AccountStats &diff)
{
    uint32_t nowMinute = QDateTime::currentSecsSinceEpoch() / 60;
    uint32_t yesterdayMinute = nowMinute - 1440;

    AccountStats yesterdayStats;
    if (!getAccountStats(appid, yesterdayMinute, yesterdayStats)) {
        // 如果昨天同一分钟没有数据，说明昨天这个时间还没有记录，无法对比
        return false;
    }

    // 今日数据使用传入的 currentStats（来自内存）
    diff.minute_index = nowMinute;
    diff.appid = appid;

    diff.message_received = currentStats.message_received - yesterdayStats.message_received;
    diff.message_sent = currentStats.message_sent - yesterdayStats.message_sent;
    diff.今日加群数量 = currentStats.今日加群数量 - yesterdayStats.今日加群数量;
    diff.今日退群数量 = currentStats.今日退群数量 - yesterdayStats.今日退群数量;
    diff.今日好友数量 = currentStats.今日好友数量 - yesterdayStats.今日好友数量;
    diff.今日删除好友数量 = currentStats.今日删除好友数量 - yesterdayStats.今日删除好友数量;
    diff.今日频道数量 = currentStats.今日频道数量 - yesterdayStats.今日频道数量;
    diff.今日退出频道数量 = currentStats.今日退出频道数量 - yesterdayStats.今日退出频道数量;
    diff.active_users = currentStats.active_users - yesterdayStats.active_users;
    diff.active_groups = currentStats.active_groups - yesterdayStats.active_groups;

    return true;
}

void BotDB::saveDailyStats()
{
    QMutexLocker locker(&m_msgMutex);
    QString dateStr = QDate::currentDate().toString("yyyy-MM-dd");

    QFile userFile(m_path+"/msgstat_user_" + dateStr + ".dat");
    if (userFile.open(QIODevice::WriteOnly)) {
        QDataStream out(&userFile);
        out << m_userDailyMsg;
    }
    QFile groupFile(m_path+ "/msgstat_group_" + dateStr + ".dat");
    if (groupFile.open(QIODevice::WriteOnly)) {
        QDataStream out(&groupFile);
        out << m_groupDailyMsg;
    }
}

void BotDB::checkDayChange()
{
    QString now = QDate::currentDate().toString("yyyy-MM-dd");
    if (m_todayDate != now) {
        m_userDailyMsg.clear();
        m_groupDailyMsg.clear();
        m_todayDate = now;



    }
}

quint64 BotDB::getUserTodayMsgCount(const QByteArray &openidBin) {
    BinKey key;
    memcpy(key.data, openidBin.constData(), 16);
    QMutexLocker locker(&m_msgMutex);
    return m_userDailyMsg.value(key, 0);
}
GroupRecord BotDB::toGroupRecord(const GroupRecord2& src) {
    GroupRecord dst{};
    dst.create_time = src.create_time;
    dst.inviter_seq_id = src.inviter_seq_id;
    dst.bitmap = src.bitmap;
    dst.xychy_time = src.xychy_time;
    dst.tq_CD = src.tq_CD;
    dst.ncgx = src.ncgx;

    // 复制 name
    QByteArray nameBytes = src.name.toUtf8();
    size_t copyLen = std::min<size_t>(nameBytes.size(), sizeof(dst.name) - 1);
    memcpy(dst.name, nameBytes.constData(), copyLen);
    dst.name[copyLen] = '\0';

    // 复制 autoref
    QByteArray autorefBytes = src.autoref.toUtf8();
    copyLen = std::min<size_t>(autorefBytes.size(), sizeof(dst.autoref) - 1);
    memcpy(dst.autoref, autorefBytes.constData(), copyLen);
    dst.autoref[copyLen] = '\0';

    // 复制 qid 数组
    memcpy(dst.qid, src.qid, sizeof(dst.qid));

    // jojnyz 和 jojntime 被忽略
    return dst;
}
bool BotDB::addJojiyzRecord(const QString &groupIdHex, int userId) {
    QByteArray groupKeyBin = QByteArray::fromHex(groupIdHex.toUtf8());
    if (groupKeyBin.size() != 16) return false;
    BinKey key;
    memcpy(key.data, groupKeyBin.constData(), 16);
    QMutexLocker locker(&m_cacheMutex);  // 确保线程安全
    auto it = m_groupCache.find(key);
    if (it != m_groupCache.end()) {
        it->jojnyz.append(userId);
        uint32_t nowMin = static_cast<uint32_t>(std::time(nullptr) / 60);
        it->jojntime.append(nowMin);
        return true;
    }


    return false;
}

void BotDB::cleanExpiredJojiyzCache(int expireMinutes,int appid)
{
    if(!m_botClients.contains(appid)) return;
    QMutexLocker locker(&m_cacheMutex);
    qint64 nowMin = QDateTime::currentSecsSinceEpoch() / 60;

    auto *c = m_botClients[appid];
    for (auto it = m_groupCache.begin(); it != m_groupCache.end(); ++it) {
        GroupRecord2& rec = it.value();
        QList<int>& ids = rec.jojnyz;
        QList<int>& times = rec.jojntime;
        if(ids.size()==0) continue;
        QByteArray byteArray(reinterpret_cast<const char*>(it.key().data), 16);

        // 转为 32 位长度的十六进制字符串（小写）
        QString hexStr = byteArray.toHex();
        QString user_list;

        for (int i = times.size() - 1; i >= 0; --i) {
            if (nowMin - static_cast<qint64>(times[i]) > expireMinutes) {

                QString user;
                getOpenIdBySeqId(ids[i],user);

                user_list.append(user).append(",");
                ids.removeAt(i);
                times.removeAt(i);

            }
        }
        if(!user_list.isEmpty()){
            c->del_members(hexStr,user_list,false,[c,hexStr,user_list](const QString &resp,auto){
                QString text = QString("入群验证移除 部分未完成验证成员..%1\n结果：%2").arg(user_list,resp);
                c->send_msgAsync(0,hexStr,"[入群验证]",text,QString());
            });
        }
    }
}
GroupRecord2 BotDB::toGroupRecord2(const GroupRecord& src) {
    GroupRecord2 dst{};
    dst.create_time = src.create_time;
    dst.inviter_seq_id = src.inviter_seq_id;
    dst.bitmap = src.bitmap;
    dst.xychy_time = src.xychy_time;
    dst.tq_CD = src.tq_CD;
    dst.ncgx = src.ncgx;

    dst.name = QString::fromUtf8(src.name, strnlen(src.name, sizeof(src.name)));
    dst.autoref = QString::fromUtf8(src.autoref, strnlen(src.autoref, sizeof(src.autoref)));
    memcpy(dst.qid, src.qid, sizeof(dst.qid));


    return dst;
}
quint64 BotDB::getGroupTodayMsgCount(const QByteArray &groupIdBin) {
    BinKey key;
    memcpy(key.data, groupIdBin.constData(), 16);
    QMutexLocker locker(&m_msgMutex);
    return m_groupDailyMsg.value(key, 0);
}



// 重载1：传入各字段，构造 GroupRecord 并调用核心函数
bool BotDB::addGroup(const QString &groupIdHex, uint32_t createTimeMinutes,
                     uint32_t inviterSeqId, uint32_t bitmap, const QString &name)
{
    QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
    if (keyData.isEmpty()) return false;
    GroupRecord record;
    record.create_time = static_cast<uint32_t>(time(nullptr)); // 注意：原代码如此，未使用 createTimeMinutes
    record.inviter_seq_id = inviterSeqId;
    record.bitmap = bitmap;
    record.xychy_time = 0;
    record.tq_CD = 0;
    record.ncgx = time(nullptr);
    QByteArray nameData = name.toUtf8();
    size_t copyLen = std::min<size_t>(nameData.size(), sizeof(record.name) - 1);
    memcpy(record.name, nameData.constData(), copyLen);
    record.name[copyLen] = '\0';
    memset(record.autoref, 0, sizeof(record.autoref));
    memset(record.qid, 0, sizeof(record.qid));

    bool success = retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_groups, keyData, &record, sizeof(record));
    });
    if(success)
    {
        GroupRecord2 record2 = toGroupRecord2(record);
        updateGroupCache(keyData, record2);
        return true;
    }
    return false;
}

// 重载2：接受 GroupRecord2，转换为 GroupRecord 后调用核心函数
bool BotDB::addGroup(const QString &groupIdHex, const GroupRecord2 &record2)
{
    QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
    if (keyData.isEmpty()) return false;
    GroupRecord record = toGroupRecord(record2);
    bool success = retryWrite([&](MDB_txn *txn) -> int {
        return putRecord(txn, m_dbi_groups, keyData, &record, sizeof(record));
    });
    if(success)
    {
        updateGroupCache(keyData, record2);
        return true;
    }
    return false;

}


QList<QString> BotDB::getAllGroupIds()
{
    QList<QString> result;
    if (!m_env) return result;

    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return result;

    MDB_cursor *cursor = nullptr;
    if (mdb_cursor_open(txn, m_dbi_groups, &cursor) != MDB_SUCCESS) {
        mdb_txn_abort(txn);
        return result;
    }

    MDB_val key, value;
    rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
    while (rc == MDB_SUCCESS) {
        // key 存储的是群 ID 的二进制数据（hex 解码后的字节）
        QByteArray keyData(static_cast<const char*>(key.mv_data), key.mv_size);
        // 转为十六进制字符串（与数据库中存储格式一致）
        QString hexId = keyData.toHex();
        result.append(hexId);
        rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    return result;
}
template<typename Func>
void BotDB::withGroupInfo(const BinKey& key, Func&& func) {
    QMutexLocker locker(&m_cacheMutex);
    auto it = m_groupCache.find(key);
    if (it != m_groupCache.end())
        func(it.value()); // 传递 const GroupRecord2&
}
bool BotDB::getGroupInfo(const QString &groupIdHex, GroupRecord2 &outRecord)
{
    if (!m_env) return false;
    QByteArray keyData = QByteArray::fromHex(groupIdHex.toUtf8());
    if (keyData.isEmpty()) return false;
    BinKey groupKey;
    memcpy(groupKey.data, keyData.constData(), 16);
    {
        QMutexLocker locker(&m_cacheMutex);
        auto itGroup = m_groupCache.find(groupKey);
        if (itGroup != m_groupCache.end()) {
            outRecord = itGroup.value();
            return true;
        }
    }
    MDB_txn *txn = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc != MDB_SUCCESS) return false;
    GroupRecord Record;
    bool ok = getRecord(txn, m_dbi_groups, keyData, &Record, sizeof(Record));
    mdb_txn_abort(txn);
    outRecord = toGroupRecord2(Record);
    updateGroupCache(keyData, outRecord);
    return ok;
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

    int putRc = putRecord(txn, m_dbi_friends, keyData, &addTimeMinutes, sizeof(addTimeMinutes));
    if (putRc == MDB_SUCCESS) {
        rc = mdb_txn_commit(txn);
        return (rc == MDB_SUCCESS);
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
    int putRc = delRecord(txn, m_dbi_friends, keyData);
    if (putRc == MDB_SUCCESS) {
        rc = mdb_txn_commit(txn);
        return (rc == MDB_SUCCESS);
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

QList<int> BotDB::getFriendList()
{
    QList<int> result;
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


// 构造 Key
static QByteArray makeSubKey(const QString &mark, uint8_t param, const QString &groupId)
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


bool BotDB::addSubscription(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
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
bool BotDB::removeSubscription(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &dataList)
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
QString BotDB::getSubscriptions(const QString &mark, uint8_t param, const QString &groupId) const
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
QStringList BotDB::listSubscriptions(const QString &mark)
{
    QStringList result;
    if (!m_env) return result;
    QString ke=mark+":";
    QByteArray prefix = ke.toUtf8();
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
bool BotDB::clearSubscriptionsByMark(const QString &mark)
{
    return retryWrite([&](MDB_txn *txn) -> int {
        QString k = mark+":";
        QByteArray prefix =  k.toUtf8();  // "标记:"
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
bool BotDB::batchAddGroups(const QList<QString>& groupIdHexList, uint32_t createTimeMinutes)
{
    if (groupIdHexList.isEmpty())
        return true;

    return retryWrite([&](MDB_txn *txn) -> int {
        for (const QString& hex : groupIdHexList) {
            QByteArray keyData = QByteArray::fromHex(hex.toUtf8());
            if (keyData.isEmpty())
                return -1;   // 无效 key

            MDB_val key, value;
            key.mv_data = keyData.data();
            key.mv_size = keyData.size();

            int rc = mdb_get(txn, m_dbi_groups, &key, &value);
            if (rc == MDB_NOTFOUND) {
                // 构造记录：邀请者和 bitmap 均为 0
                GroupRecord record{ createTimeMinutes, 0, 0 };
                rc = putRecord(txn, m_dbi_groups, keyData, &record, sizeof(record));
                if (rc != MDB_SUCCESS)
                    return rc;
            } else if (rc != MDB_SUCCESS) {
                return rc;   // 其他错误（如磁盘问题）
            }
            // 若已存在，跳过
        }
        return MDB_SUCCESS;
    });
}
bool BotDB::batchAddFriends(const QList<uint32_t>& userSeqIds, uint32_t addTimeMinutes)
{
    if (userSeqIds.isEmpty())
        return true;

    return retryWrite([&](MDB_txn *txn) -> int {
        for (uint32_t seq : userSeqIds) {
            QByteArray keyData(reinterpret_cast<const char*>(&seq), sizeof(seq));
            MDB_val key, value;
            key.mv_data = keyData.data();
            key.mv_size = keyData.size();

            int rc = mdb_get(txn, m_dbi_friends, &key, &value);
            if (rc == MDB_NOTFOUND) {
                rc = putRecord(txn, m_dbi_friends, keyData, &addTimeMinutes, sizeof(addTimeMinutes));
                if (rc != MDB_SUCCESS)
                    return rc;
            } else if (rc != MDB_SUCCESS) {
                return rc;
            }
        }
        return MDB_SUCCESS;
    });
}