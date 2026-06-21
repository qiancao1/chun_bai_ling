#ifndef BOTDB_H
#define BOTDB_H

#include <QString>
#include <QByteArray>
#include <QMutex>
#include <QList>
#include <lmdb.h>

struct UserRecord {
    uint32_t seq_id;
    uint32_t reserved_qq;
    uint32_t record_time;
    uint32_t invited_group_count;
    char nickname[64];
};

struct GroupRecord {
    uint32_t create_time;
    uint32_t inviter_seq_id;
    uint32_t bitmap=0;
};

class BotDB {
public:
    // 增加 initialMapSizeMB 参数，默认 64MB，用户可自行调整
    explicit BotDB(const QString& path, size_t initialMapSizeMB = 8);
    ~BotDB();

    bool open();
    void close();
    // 订阅（添加）：标记 + 群ID
    bool addSubscription(uint32_t mark, uint8_t param, const QString &groupId,const QList<QString> &data);
    //获取单个配置信息
    QString getSubscriptions(uint32_t mark, uint8_t param, const QString &groupId) const;
    // 取消订阅（删除）：标记 + 群ID
    bool removeSubscription(uint32_t mark, uint8_t param, const QString &groupId,const QList<QString> &data);
    bool clearSubscriptionsByMark(uint32_t mark);
    // 获取某个标记下的所有群ID列表
    QStringList listSubscriptions(uint32_t mark);
    static uint32_t nowMinutes();
    QList<QString> getAllGroupIds();
    uint32_t getOrUpdateUser(const QString &openid, QString &name);
    bool getUserBySeqId(uint32_t seq_id, UserRecord &outRecord);
    bool incrementInvitedGroupCount(uint32_t seq_id, int delta = 1);

    bool addGroup(const QString &groupIdHex, uint32_t createTimeMinutes, uint32_t inviterSeqId);
    bool getGroupInfo(const QString &groupIdHex, GroupRecord &outRecord);
    bool isGroupExist(const QString &groupIdHex);
    bool deleteGroup(const QString &groupIdHex);
    bool getOpenIdBySeqId(uint32_t seqId, QString &outOpenidHex);
    bool addFriend(uint32_t userSeqId, uint32_t addTimeMinutes);
    bool removeFriend(uint32_t userSeqId);
    bool isFriend(uint32_t userSeqId);
    QList<int> getFriendList();
    bool getFriendAddTime(uint32_t userSeqId, uint32_t &outAddTimeMinutes);

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
    MDB_env* m_env;
    MDB_dbi  m_dbi_users;
    MDB_dbi  m_dbi_seq_idx;
    MDB_dbi  m_dbi_groups;
    MDB_dbi  m_dbi_friends;
    MDB_dbi m_dbi_subscriptions;  // 订阅数据库

    QMutex   m_mutex;
};

#endif // BOTDB_H