/*
 * 纯白铃 - QQ 机器人管理平台 - 订阅数据库（SQLite 版）
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

// ════════════════════════════════════════════════════════════════════════════
// 2026-11-12：订阅数据从 botdb（LMDB）搬出来，单独一个 SQLite 库。
//
// 为什么搬：
//   订阅是「定时任务的推送目标名单」，完全是**只增不减**的业务数据 —— 每个群/好友
//   订阅一次就一条记录，取消订阅才删。它原来躺在 botdb 的 subscriptions 子库里，
//   而 botdb 是 LMDB：整个 data.mdb 一次 mmap 进进程，且删条目不缩文件。
//   订阅量一旦涨起来（多账号 × 多任务 × 几百个群），botdb 文件只涨不缩，
//   映射范围跟着涨，任务管理器上就是常驻几百 MB 且永不回落。
//
// 这里换成 SQLite：
//   · cache_size 是**硬上限**（下面 SUBDB_CACHE_KB = 2MB），库多大常驻都这么多；
//   · mmap_size=0，走 read()，不把文件映射进进程；
//   · 按 (mark, ctype, conv) 建主键，WITHOUT ROWID —— 「某个任务下的全部目标」
//     就是一次主键范围扫描，比 LMDB 的前缀游标更直接；
//   · 取消订阅删行即真删，auto_vacuum=INCREMENTAL 能把页原地还回去。
//
// 表结构：
//   subs(mark, ctype, conv, data)   mark = "t_<appid>_<任务mark>"（跟旧 key 里那段一致）
//                                   ctype = 会话类型（0/1 群、2 私聊，同 MessageEvent.type）
//                                   conv  = 群号 / openid
//                                   data  = 该目标下的附加参数，原样 "a|||b|||c"
//   meta(k, v)                      迁移标记用（"已从某个 botdb 目录搬过"）
//
// ⚠ 迁移是**只读旧库、不删旧数据**：BotDB::migrateSubscriptions() 把 LMDB 里的
//   subscriptions 子库读出来灌进来，然后写一条 meta 标记；旧数据留在 data.mdb 里
//   当离线备份，要回退直接删 subscriptions.db 即可（BotDB 会退回 LMDB 实现）。
// ════════════════════════════════════════════════════════════════════════════

#ifndef SUBDB_H
#define SUBDB_H

#include <QString>
#include <QStringList>
#include <QMutex>
#include <memory>

struct sqlite3;   // 只前向声明，不在头文件里拉 sqlite3.h

class SubDB
{
public:
    // dirPath：库所在目录（跟 LogDB 一样传目录，不是文件），实际文件是 <dir>/subscriptions.db
    explicit SubDB(const QString &dirPath);
    ~SubDB();

    bool open();
    void close();
    bool isOpen() const;

    // ==================== 业务接口（签名与 BotDB 那套一一对应）====================
    // 添加：把 dataList 里**没有出现过**的条目追加到该 (mark,ctype,conv) 的 data 后面。
    //   dataList 空 / ctype 非法 / conv 空 → false。
    //   一条都没新增（全是重复）也返回 true（跟原来 LMDB 版一致，不算错）。
    bool addSubscription(const QString &mark, int ctype, const QString &conv, const QStringList &dataList);
    // 删除：dataList 为空 = 删掉整个 (mark,ctype,conv) 记录；
    //       否则只删列表里命中的条目，删空了就把整行删掉。
    bool removeSubscription(const QString &mark, int ctype, const QString &conv, const QStringList &dataList);
    // 读回该目标的附加参数，用 "|||" 拼接（没有记录 = 空串）
    QString getSubscriptions(const QString &mark, int ctype, const QString &conv) const;
    // 某个任务下的全部目标，每项 "<ctype>|<conv>"（调用方就是按 '|' 切的）
    QStringList listSubscriptions(const QString &mark) const;
    // 取消整个任务：删掉该 mark 下所有目标
    bool clearSubscriptionsByMark(const QString &mark);

    // ==================== 迁移辅助 ====================
    // 灌入一条旧记录：已经存在就不覆盖（新库里的数据永远优先）
    bool importRow(const QString &mark, int ctype, const QString &conv, const QStringList &dataList);
    // tag 形如 "botdb:botdb/102072330_db"，用来标记「这个账号库已经搬过了」
    bool isMigrated(const QString &tag) const;
    void markMigrated(const QString &tag);
    // 当前总行数（迁移前后对账用）
    qint64 rowCount() const;

private:
    bool exec(const char *sql);
    // 打开连接 + PRAGMA + 建表
    bool openConnection();

    QString m_dirPath;
    QString m_filePath;                 // <dir>/subscriptions.db
    sqlite3 *m_db = nullptr;
    mutable QMutex m_mutex{QMutex::Recursive};   // 单连接，所有访问串行
};

// 全局单例：main.cpp 的 initDBs() 里创建/打开，打不开就是 nullptr，
// 此时 BotDB 会退回原来的 LMDB 实现（见 botdb.cpp 里 *Legacy 那几个）。
extern std::unique_ptr<SubDB> g_subdb;

#endif // SUBDB_H
