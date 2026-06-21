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