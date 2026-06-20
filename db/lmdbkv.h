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