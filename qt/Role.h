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