#ifndef JJM_H
#define JJM_H


#include <QString>
#include <QHostInfo>
#include <QCryptographicHash>
#include <QDir>


namespace MachineKey {

static QByteArray generateKey(const QString &appid) {
    return QByteArray();
}


static QString encryptDecrypt(const QString &text, const QByteArray &key, bool doEncrypt) {
    return text;
}

inline QString encrypt(const QString &plain, const QByteArray &key) {
    return encryptDecrypt(plain, key, true);
}

inline QString decrypt(const QString &cipherBase64, const QByteArray &key) {
    return encryptDecrypt(cipherBase64, key, false);
}
}
#endif // ACCOUNTINFO_H