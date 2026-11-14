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

#ifndef KVDB_H
#define KVDB_H

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QMutex>

struct sqlite3;
struct sqlite3_stmt;

// ════════════════════════════════════════════════════════════════════════════
// 纯 KV 缓存库 —— 2026-11-13 从 LMDB 换成 SQLite。
//
// 类名沿用历史上的 LmdbKV（db/lmdbkv.h 现在是 `using LmdbKV = KvDB;` 的兼容头），
// 所以 api.cpp / ailog.h / aisxw.cpp / aiwidget*.cpp / nickreviewwidget.cpp
// **一行都不用改** —— 它们的 LmdbKV* 会自动指向本类。
//
// 为什么非换不可 —— LMDB 这条路有两个绕不开的坑：
//   ① 16MB mapsize 起步，写满就 MDB_MAP_FULL → increaseMapSize() 必须
//      mdb_env_close + mdb_env_open，等于把 lock.mdb 和命名互斥量整个重建。
//      此时若还有另一个进程开着同一个库（崩溃自重启 + 全项目没有单实例保护），
//      两边的锁各自独立 → 同时改 free-list → **库文件结构被写坏** → 之后每次
//      mdb_get / mdb_put 都直接 AV，只能删库重来。
//   ② 没有页校验、没有修复工具。坏一次就是整库报废，且报的是野指针 AV，
//      不是 MDB_CORRUPTED —— 现象就是"莫名其妙闪退"。
//
// SQLite 这边：
//   · 单文件 + WAL，容量按需增长 —— **没有 MAP_FULL，也就不存在"运行中重建锁"**；
//   · 进程崩了由 WAL 自动回滚，不会留下半个页；
//   · 读到坏页返回 SQLITE_CORRUPT 错误码而不是 AV（最坏是缓存失效，不会闪退）；
//   · 多进程打开同一个库有文件锁 + busy_timeout，不会静默互相覆盖；
//   · 内存只有 cache_size 那一块**硬上限**（2MB），mmap_size=0 完全不映射文件。
//
// 表结构（1:1 对应原来的 KV 子库，序列化格式一个字没动）：
//   kv(k BLOB PRIMARY KEY, v BLOB NOT NULL) WITHOUT ROWID   ← 就是一棵 B-tree，跟 LMDB 同构
//   meta(k TEXT PRIMARY KEY, v TEXT)                        ← 只放迁移标记
//
// 旧数据：首次打开时如果同目录还有 LMDB 的 data.mdb，**只读**打开它、全量导进
// kv 表一次（aidb 里是 AI 会话上下文，不能白丢），导完打标记；旧文件不删，留着当备份。
// ════════════════════════════════════════════════════════════════════════════
class KvDB
{
public:
    // 第二个参数只为兼容旧的 `new LmdbKV(path, nullptr)` 写法，本类不用 QObject 生命周期。
    explicit KvDB(const QString &dbPath, void *parent = nullptr);
    ~KvDB();

    bool isOpen() const;

    bool put(const QString &key, const QString &value);
    bool put(const QByteArray &key, const QByteArray &value);

    QString get(const QString &key) const;
    QByteArray get(const QByteArray &key) const;

    bool remove(const QString &key);
    bool remove(const QByteArray &key);
    bool removeBatch(const QList<QByteArray> &keys);

    QList<QByteArray> getAllKeysByteArray() const;
    QStringList getAllKeys() const;

    // 扫全库删掉**值已过期**的条目（值的前缀惯例 "<过期秒>||||<内容>"，兼容早期
    // 媒体缓存的 "[…,Time=<过期秒>]"）。认不出格式的值一律不动。
    // 返回删除条数；-1 = 库没打开 / SQL 失败。
    int removeExpired(qint64 nowSec);

    // 幂等关闭：关掉之后 get/put/remove 全部变成**安全空操作**（返回空 / false）。
    // 用途还是「双库按日轮换」：Windows 下还有句柄开着就删不掉目录，必须先关；
    // 而旧对象不能立刻 delete（api.cpp 里有 std::thread 上传路径可能还攥着旧指针）。
    void close();

private:
    bool openInternal();
    bool ensureSchema();
    bool prepareStatements();        // 三个热点 SQL 只解析一次（见 kvdb.cpp 顶部说明）
    void finalizeStatements();
    bool hasMigrated();
    void markMigrated();
    void migrateFromLmdb();          // 只在首次打开时跑一次

    // 底层实现：各自内部持 m_mutex（openInternal 里调用它们之前必须先放锁）
    bool putUnlocked(const QByteArray &key, const QByteArray &value);
    QByteArray getUnlocked(const QByteArray &key) const;
    bool removeInternal(const QByteArray &key);

    sqlite3 *m_db;
    QString  m_dbPath;      // 目录（构造函数参数原样保留，用于迁移时找 data.mdb）
    QString  m_filePath;    // 实际库文件 = <dir>/kv.db

    // 常驻的 prepared statement：put/get/remove 是热点（每条带媒体的消息都走），
    // 常驻下来就只做 reset + bind + step，省掉每次的 SQL 解析（sqlite3_prepare_v2）。
    // ⚠ 全部只在持 m_mutex 时使用；close()/重开前必须 finalize。
    sqlite3_stmt *m_stmtPut;
    sqlite3_stmt *m_stmtGet;
    sqlite3_stmt *m_stmtDel;

    mutable QMutex m_mutex;
};

#endif // KVDB_H
