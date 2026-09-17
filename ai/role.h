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

#ifndef ROLE_H
#define ROLE_H

#include <QString>
#include <QList>

// 全局设定项
struct RoleSetting {
    QString name;
    QString content;
};

// 角色信息（不含内部设定列表，只存选中的设定名）
struct Role {
    QString name;               // 角色名
    QString model;              // 模型
    QString setting;            // 选中的全局设定名
    QString context;            // 上下文
    int nSecondsNoReply = 0;    // N秒没回复
    int nMinutesNoReply = 0;    // N分钟没回复
    int delayReplySeconds = 0;  // 延迟回复(秒)

    bool enableGroupChat = false;
    bool enableGroupPersonal = false;
    bool enablePrivateChat = false;
    bool nameTrigger = false;
    bool enableChannel = false;
    bool atTrigger = false;
    bool enableFunction = false;
    bool enableImageRec = false;
};

#endif // ROLE_H