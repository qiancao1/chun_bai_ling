#ifndef JJM_H
#define JJM_H


#include <QString>
#include <QHostInfo>
#include <QCryptographicHash>
#include <QDir>

namespace MachineKey {
static QByteArray generateKey(const QString &appid) {
    QString machineInfo;
    machineInfo += QHostInfo::localHostName();
    machineInfo += QSysInfo::kernelType();
    machineInfo += QSysInfo::currentCpuArchitecture();
    machineInfo += QDir::home().absolutePath();
    machineInfo += appid;
    return QCryptographicHash::hash(machineInfo.toUtf8(), QCryptographicHash::Sha256);
}


static QString encryptDecrypt(const QString &text, const QByteArray &key, bool doEncrypt) {
    QByteArray data;
    if (doEncrypt) {
        data = text.toUtf8();
    } else {
        data = QByteArray::fromBase64(text.toLatin1());
    }

    for (int i = 0; i < data.size(); ++i) {
        data[i] = data[i] ^ key[i % key.size()];
    }

    if (doEncrypt) {
        return QString::fromLatin1(data.toBase64());
    } else {
        return QString::fromUtf8(data);
    }
}

inline QString encrypt(const QString &plain, const QByteArray &key) {
    return encryptDecrypt(plain, key, true);
}

inline QString decrypt(const QString &cipherBase64, const QByteArray &key) {
    return encryptDecrypt(cipherBase64, key, false);
}
}
#endif // ACCOUNTINFO_H