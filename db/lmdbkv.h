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

#ifndef LMDBKV_H
#define LMDBKV_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QMutex>
#include <lmdb.h>

class LmdbKV : public QObject
{
    Q_OBJECT
public:
    explicit LmdbKV(const QString &dbPath, QObject *parent = nullptr);
    ~LmdbKV();

    bool put(const QString &key, const QString &value);
    bool put(const QByteArray &key, const QByteArray &value);

    QString get(const QString &key) const;
    QByteArray get(const QByteArray &key) const;

    bool remove(const QString &key);
    bool remove(const QByteArray &key);
    QList<QByteArray> getAllKeysByteArray() const;
    QStringList getAllKeys() const;
    bool removeBatch(const QList<QByteArray>& keys);
    // 扫描全库，删掉**值已过期**的条目（本库缓存统一是 "<过期秒>||||<内容>" 的前缀惯例，
    // 兼容早期媒体缓存的 "[…,Time=<过期秒>]"）。认不出格式的值一律不动。
    // 返回删除条数；-1 = 事务失败 / 库没打开。
    int removeExpired(qint64 nowSec);
    // 幂等关闭底层 env：关掉之后 get/put/remove 全部变成**安全空操作**（直接返回空/false）。
    // 用途是「双库按日轮换」：Windows 下 env 还映射着 data.mdb 就删不掉目录，
    // 所以必须先把库关掉才能删文件；而旧 LmdbKV 对象又**不能立刻 delete**
    // （api.cpp 里有 std::thread 上传路径，可能还有线程攥着旧指针），
    // 关掉后不用 delete 也不会崩 —— 残留调用只会落到空操作上。析构里重复调用无害。
    void close();
    size_t   m_currentMapSize;
     bool increaseMapSize();

private:
    MDB_env *m_env;
    MDB_dbi  m_dbi;
    QString  m_dbPath;

    mutable QMutex m_mutex;

    bool putInternal(const QByteArray &key, const QByteArray &value);
    QByteArray getInternal(const QByteArray &key) const;
    bool removeInternal(const QByteArray &key);

};

#endif // LMDBKV_H