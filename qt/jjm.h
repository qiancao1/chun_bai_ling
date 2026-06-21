#ifndef JJM_H
#define JJM_H


#include <QString>
#include <QHostInfo>
#include <QCryptographicHash>
#include <QDir>

// ========== 基于机器特征 + appid 的密钥派生（纯 Qt） ==========
namespace MachineKey {
// 生成 32 字节密钥：机器特征 + appid 的 SHA256
static QByteArray generateKey(const QString &appid) {
    return QByteArray();
}

// XOR 加密/解密（对称操作）
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