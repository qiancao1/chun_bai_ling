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

#ifndef LOGDB_H
#define LOGDB_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QMutex>
#include <QTimer>
#include <QList>
#include <lmdb.h>

// 日志记录结构体（分钟级时间戳 + 变长消息）
struct LogRecord {
    int time;                 // 分钟级时间戳（time(nullptr)/60）
    int appid;                // 机器人账号ID（虽然目录已隔离，仍保留）
    int type=0;
    QByteArray user;      // 固定16字节
    QByteArray groupId;       // 固定16字节
    QString msg;          // 日志内容
};

// 日志数据库类（每个机器人账号独立实例）
class LogDB : public QObject
{
    Q_OBJECT

public:
    // dbPath: 数据库目录路径（例如 "logs/account_123"）
    // flushIntervalMs: 定时刷盘间隔（毫秒），默认5000
    explicit LogDB(const QString &dbPath, int flushIntervalMs = 5000, QObject *parent = nullptr);
    ~LogDB();

    // 打开数据库（必须调用）
    bool open();

    // 关闭数据库（会刷盘）
    void close();

    // 追加一条日志（线程安全）
    LogRecord& appendLog();

    // 强制将缓冲区中所有日志写入磁盘（线程安全）
    void flush();

    // 获取当前账号（appid）的最近 N 条日志（按时间倒序，即最新在前）
    QList<LogRecord> getRecentLogs(int limit = 5000) const;

private slots:
    void onFlushTimer();      // 定时器触发

private:
    // 执行实际的写入（将 m_readBuf 写入 LMDB，并清空 m_readBuf）
    // 返回 true 表示成功，false 表示失败（失败时数据保留在 m_readBuf 中，下次继续重试）
    bool writeReadBuffer();

    // 交换读写缓冲区（加锁）
    void swapBuffers();

    // LMDB 相关辅助
    bool increaseMapSize();          // 翻倍扩容
    bool reopenEnvironment();        // 重新打开环境（扩容后调用）

    QString m_dbPath;
    int m_flushIntervalMs;
    MDB_env *m_env;
    MDB_dbi  m_dbi_logs;      // 日志表
    MDB_dbi  m_dbi_meta;      // 元数据表（存储下一个自增ID）

    // 双缓冲
    QList<LogRecord> m_writeBuf;    // 写缓冲（接收 appendLog）
    QList<LogRecord> m_readBuf;     // 读缓冲（待写入 LMDB）
    mutable QMutex   m_mutex;       // 保护两个缓冲区的交换操作

    QTimer *m_timer;
    uint64_t m_nextId;              // 下一个可用的自增ID
    size_t   m_currentMapSize;      // 当前 mapsize（用于扩容）
};

#endif // LOGDB_H