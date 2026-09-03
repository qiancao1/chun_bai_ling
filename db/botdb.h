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

#ifndef BOTDB_H
#define BOTDB_H

#include "qqbotclient.h"
#include <QString>
#include <QByteArray>
#include <QMutex>
#include <QList>
#include <lmdb.h>

struct UserRecord {
    uint32_t seq_id;
    uint32_t bitmap;
    uint32_t record_time;
    uint32_t invited_group_count; //邀请请群数
    char nickname[64];
    char name[64];
};

struct GroupRecord {
    uint32_t create_time=0; //加群时间
    uint32_t inviter_seq_id=0; //邀请人id
    uint32_t bitmap=0;
    qint64 xychy_time=0;
    qint64 tq_CD=0;
    qint64 ncgx=0;   //昵称获取时间 一个月刷新一次
    char name[64];
    uint32_t qid[20];
    char autoref[128];
};


struct GroupRecord2 {
    uint32_t bitmap=0;
    uint32_t create_time=0; //加群时间
    uint32_t inviter_seq_id=0; //邀请人id
    qint64 ncgx=0;   //昵称获取时间 一个月刷新一次
    qint64 xychy_time=0;
    qint64 tq_CD=0;

    QString name;
    QString autoref;
    uint32_t qid[20];
    QList<int> jojnyz;//入群验证 id
    QList<int> jojntime;
};


#include <QHash>
#include <cstring>
#include <QDataStream>
// 1. 定义 BinKey 结构体
struct BinKey {
    unsigned char data[16];

    // 必须实现 operator==，QHash 需要用它来比较键
    inline bool operator==(const BinKey& other) const {
        return memcmp(data, other.data, 16) == 0;
    }
};

// 2. 为 BinKey 提供 qHash 重载（放在全局命名空间，与 BinKey 同一作用域）
inline uint qHash(const BinKey& key, uint seed = 0) {
    // 直接使用 Qt 提供的 qHashBits 来哈希 16 字节内存
    return qHashBits(key.data, sizeof(key.data), seed);
}


// 为 QDataStream 提供序列化支持
inline QDataStream &operator<<(QDataStream &out, const BinKey &key) {
    out.writeRawData(reinterpret_cast<const char*>(key.data), sizeof(key.data));
    return out;
}

inline QDataStream &operator>>(QDataStream &in, BinKey &key) {
    in.readRawData(reinterpret_cast<char*>(key.data), sizeof(key.data));
    return in;
}



class BotDB : public QObject {
    Q_OBJECT
public:
    // 增加 initialMapSizeMB 参数，默认 64MB，用户可自行调整
    explicit BotDB(const QString& path, size_t initialMapSizeMB = 8);
    ~BotDB();

    bool open();
    void close();
    void updateUserCache(const QByteArray &openidBin, const UserRecord &record);
    void updateGroupCache(const QByteArray &groupIdBin, const GroupRecord2 &record);
    // 订阅（添加）：标记 + 群ID
    bool addSubscription(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &data);
    //获取单个配置信息
    QString getSubscriptions(const QString &mark, uint8_t param, const QString &groupId) const;
    // 取消订阅（删除）：标记 + 群ID
    bool removeSubscription(const QString &mark, uint8_t param, const QString &groupId, const QList<QString> &data);
    bool clearSubscriptionsByMark(const QString &mark);
    // 获取某个标记下的所有群ID列表
    QStringList listSubscriptions(const QString &mark);
    static uint32_t nowMinutes();
    QList<QString> getAllGroupIds();
    uint32_t getOrUpdateUser(QQBotClient *qqbot, MessageEvent &ev, bool hc=false);
    uint32_t getOrUpdateUser(const QString &openid, QString &name);//不能删
    bool getUserBySeqId(uint32_t seq_id, UserRecord &outRecord);

    bool updateUserBySeqId(uint32_t seq_id, const UserRecord &newRecord);
    bool updateUserBySeqId(uint32_t seq_id, std::function<void(UserRecord&)> updater);
    bool addGroup(const QString &groupIdHex, uint32_t createTimeMinutes, uint32_t inviterSeqId, uint32_t bitmap, const QString &name);
    bool addGroup(const QString &groupIdHex,const GroupRecord2 &record);
    bool getUsersByPage(bool onlyNameEmpty, int offset, int limit,
                        QList<UserRecord>& outUsers, int& totalCount);
    bool getGroupInfo(const QString &groupIdHex, GroupRecord2 &outRecord);
    template<typename Func>
    void withGroupInfo(const BinKey& key, Func&& func);
    bool getTodayDiff(uint32_t appid, const AccountStats &currentStats, AccountStats &diff);
    bool saveAccountStats(uint32_t appid, uint32_t minuteIndex, const AccountStats &stats);
    bool getAccountStats(uint32_t appid, uint32_t minuteIndex, AccountStats &outStats);
    bool deleteGroup(const QString &groupIdHex);
    bool getOpenIdBySeqId(uint32_t seqId, QString &outOpenidHex);
    bool addFriend(uint32_t userSeqId, uint32_t addTimeMinutes);
    bool removeFriend(uint32_t userSeqId);
    bool isFriend(uint32_t userSeqId);
    QList<int> getFriendList();

    quint64 getUserTodayMsgCount(const QByteArray &openidBin);
    quint64 getGroupTodayMsgCount(const QByteArray &groupIdBin);
    bool batchAddGroups(const QList<QString>& groupIdHexList, uint32_t createTimeMinutes);
    bool batchAddFriends(const QList<uint32_t>& userSeqIds, uint32_t addTimeMinutes);
    void cleanExpiredJojiyzCache(int expireMinutes = 5, int appid=0);
    bool addJojiyzRecord(const QString &groupIdHex, int userId);


    QHash<BinKey, UserRecord> m_userCache;
    QHash<BinKey, GroupRecord2> m_groupCache;
    QHash<BinKey, quint64> m_userDailyMsg;   // 用户今日消息数
    QHash<BinKey, quint64> m_groupDailyMsg;  // 群今日消息数
    QMutex m_msgMutex;
private:
    // 内部辅助函数（原有）
    uint32_t getNextSeqId(MDB_txn *txn);
    int putRecord(MDB_txn *txn, MDB_dbi dbi, const QByteArray &keyData, const void *data, size_t size);
    bool getRecord(MDB_txn *txn, MDB_dbi dbi, const QByteArray &keyData, void *outData, size_t size);
    int delRecord(MDB_txn *txn, MDB_dbi dbi, const QByteArray &keyData);
    bool saveSeqToOpenId(MDB_txn *txn, uint32_t seqId, const QByteArray &openidBin);
    bool getOpenIdBySeq(MDB_txn *txn, uint32_t seqId, QByteArray &outOpenidBin);
    static uint64_t makeFriendKey(uint32_t a, uint32_t b);

    // 新增：自动扩容相关
    bool increaseMapSize();                      // 翻倍 mapsize，返回是否成功
    bool reopenEnvironment();                    // 重新打开环境（扩容后调用）
    bool ensureSparseFile();                     // Windows 下设置稀疏文件

    // 泛型写入重试器：执行一个 lambda（接受 MDB_txn*，返回 int），若返回 MDB_MAP_FULL 则自动扩容重试
    template<typename Func>
    bool retryWrite(Func writeFunc, int maxRetries = 3);

    QString m_path;
    size_t m_initialMapSize;    // 用户指定的初始大小（字节）
    size_t m_currentMapSize;    // 当前生效的 mapsize（字节）
    MDB_env* m_env = nullptr;
    MDB_dbi  m_dbi_users;
    MDB_dbi  m_dbi_seq_idx;
    MDB_dbi  m_dbi_groups;
    MDB_dbi  m_dbi_friends;
    MDB_dbi m_dbi_subscriptions;  // 订阅数据库

    QMutex   m_mutex;
    QMutex m_cacheMutex;  // 如果多线程调用，需要加锁

    MDB_dbi m_dbi_account_stats;

    QString m_todayDate;                     // 当前日期字符串，用于判断日期切换
    QTimer *m_saveTimer;                     // 定时保存（例如每60秒）
    QTimer *m_cacheTimer;


    static GroupRecord toGroupRecord(const GroupRecord2& src);
    static GroupRecord2 toGroupRecord2(const GroupRecord& src);


    void loadDailyStats();                   // 加载当日统计文件
    void saveDailyStats();                   // 保存当日统计文件
    void checkDayChange();                   // 检查是否跨天，跨天则重置并加载新一天;


    void cleanUserCache();
    void cleanGroupCache();

    void checkCleanup();
};

#endif // BOTDB_H