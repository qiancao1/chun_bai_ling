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

private:
    MDB_env *m_env;
    MDB_dbi  m_dbi;
    QString  m_dbPath;
    size_t   m_currentMapSize;
    mutable QMutex m_mutex;

    bool putInternal(const QByteArray &key, const QByteArray &value);
    QByteArray getInternal(const QByteArray &key) const;
    bool removeInternal(const QByteArray &key);
    bool increaseMapSize();
};

#endif // LMDBKV_H