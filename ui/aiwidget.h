#ifndef AIWIDGET_H
#define AIWIDGET_H

#include <QWidget>
#include <QList>
#include <qcheckbox.h>
#include <qlabel.h>
#include <qlistwidget.h>
#include <qpushbutton.h>
#include <qtablewidget.h>
#include "AccountInfo.h"
#include "qqbotclient.h"
#include "PlaceholderLineEdit.h"
#include "PlaceholderTextEdit.h"

#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit


struct KeyData {
    bool enabled=false;
    QString key;
    int usageCount = 0;
    QString lastUsed;
};

struct InterfaceData {
    int key_index=0;
    QString remark;
    QString url;
    QList<KeyData> keys;
};

struct ModelData {
    QString name;
    QList<int> enabledInterfaceIndices; // 全局接口列表中的索引
};

struct FunctionData {

    QString remark;               // 备注文本（显示在第一列）
    QString funcName;             // 函数名
    QString code;                 // Python 代码
    QStringList params;           // 8 个参数，索引 0~7
    bool interrupt = false;       // 中断复选框
};
struct PendingMessage {
    QString text;           // 纯文本（[image,path=...] 已替换为 [image]）
    QStringList imagePaths; // 本地缓存图片路径（绝对路径）
};
struct Ai_Fun {

    QString p1,p2,p3,p4,p5,p6,p7,p8;

};


struct SessionContext {
    QTimer* timer = nullptr;
    int dslx=0; //0常规对话 1定时N秒 1定时N分钟
    QJsonObject baseContext;              // 存储上下文（包含本地路径）
    QList<PendingMessage> pendingMessages; // 等待合并的消息
    bool isProcessing = false;
    // 发送参数
    int appid;
    int type;
    QString groupId;
    QString msgId;
    QString openid;
    AccountInfo* accountInfo = nullptr;
};
class AiWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AiWidget(QWidget *parent = nullptr);
    ~AiWidget();
    QString Ai_post(AccountInfo *info, const MessageEvent &ev);

    QString Ai_post(const QString &model, const QString &msg, int type);
    QString Ai_posts(const MessageEvent &ev, int &模型开始下标, int model_index, QJsonObject &sxw, int timeoutMs);
    QString Ai_post(const MessageEvent &ev, const QString &url, const QString &key, QJsonObject &sxw, QString &err, int timeoutMs);
    QByteArray Ai_post(const QString &url, const QString &key, QJsonObject &sxw, int timeoutMs);
    QJsonArray get_tools(const AccountInfo *info);
    QString Ai_qx(AccountInfo *info, const MessageEvent &ev);
    void 列表行被单击(); // 切换机器人
    QMap<QString, SessionContext> m_sessions;   // 以 openid 为键

public slots:
    void onNewMessage(AccountInfo* info, MessageEvent ev, bool send, bool notime);


signals:
    void newMessageArrived(AccountInfo* info, MessageEvent ev, bool send,bool notime);
    void asyncReplyReceived(const QString &openid, const QString &reply,
                            const QJsonObject &updatedContext,       // mutableContext（含 AI 回复）
                            int oldMsgCount,
                            int newStartIndex);

private slots:


    void onAsyncReply(const QString &openid, const QString &reply,
                      const QJsonObject &updatedContext,    // baseContextCopy
                      int oldMsgCount,
                      int newStartIndex);
    // --- 机器人列表相关 ---


    void on_btnSaveRobot_clicked();                  // 保存当前机器人信息（新增/更新）

    // --- 全局设定相关 ---
    void on_settingListWidget_currentRowChanged(int currentRow);
    void on_btnAddSetting_clicked();
    void on_btnDeleteSetting_clicked();



private:
    void setupUi();                          // 纯代码构建全部 UI
    void loadFromFile();                     // 加载 roles.json
    void saveToFile1() const;                 // 保存到 roles.json
    void addtoui(const std::shared_ptr<AccountInfo> acc);
    void trimToolResponses(QJsonObject &context, int maxToolMessages, int truncateLimit);
    void refreshSettingList();               // 刷新右侧全局设定列表
    void refreshSettingCombo();              // 刷新“设定”下拉框
    void onFuncListItemChanged(QTableWidgetItem *item);
    void loadFromFile2();
    void onFuncListAdd();
    void onFuncListDelete() ;
    void saveCurrentRowData();
    void onFuncListCurrentCellChanged(int currentRow, int currentCol,int previousRow, int previousCol);
    void clearRightPanel();
    void onFuncSave();
    void saveToFile();


    QString generateHash(const QString &url);

    QString downloadImage(const QString &url, const QString &hash);
    PendingMessage parseImageTagsAndDownload(const QString &msg);

    void appendPendingMessageToContext(QJsonObject &context, const PendingMessage &pm);

    void trimContextImages(QJsonObject &context, int maxImageMessages = 3);
    void convertContextImagesToBase64(QJsonObject &context);


    // --- UI 控件指针 ---
    QTabWidget *tabWidget;

    QCheckBox *feibaimd,*chkGroupChat, *chkGroupPersonal, *chkPrivateChat;
    QCheckBox *chkNameTrigger, *chkChannel, *chkAtTrigger, *chkChannelPersonal, *chkImageRec,*chkniren;

    QLabel *lblRobotName, *lblModel, *lblSetting, *lblContext;
    QLabel *lblNoReplySeconds, *lblNoReplyMinutes, *lblDelayReply,*lblPplx;
    QLineEdit *editRobotName, *editContext, *editNoReplySeconds, *editNoReplyMinutes, *editDelayReply;
    QLineEdit *set_zl, *set_sc,*set_qy;
    QComboBox *comboModel, *comboSetting,*comboPplx;

    QListWidget *settingListWidget;    // 全局设定列表
    QTextEdit *settingTextEdit;

    QLabel *lblSettingName;
    QLineEdit *editSettingName;
    QPushButton *btnAddSetting;        // 添加/保存设定
    QPushButton *btnDeleteSetting;     // 删除设定

    QPushButton *btnSaveRobot;         // 保存机器人按钮（位于首页）

    // ============== 模型配置 (tab 2) ==============
    QTableWidget *modelListTable;      // 左侧：模型名
    QPushButton *modelListAddBtn;      // 左侧：添加新行
    QPushButton *modelListDelBtn;      // 左侧：删除选中

    QTableWidget *interfaceTable;      // 中间：备注、接口
    QPushButton *interfaceAddBtn;      // 中间：添加新行
    QPushButton *interfaceDelBtn;      // 中间：删除选中

    QTableWidget *keyTable;            // 右侧：key、使用次数、最后
    QPushButton *keyAddBtn;            // 右侧：添加新行
    QPushButton *keyDelBtn;            // 右侧：删除选中

    QList<ModelData> modelList;
    QList<InterfaceData> globalInterfaces;
    int currentModelRow = -1;
    int currentInterfaceRow = -1; // 当前选中的全局接口索引
    QString configFilePath2 = "data/model_config.json";

    void onModelAdd();
    void onModelDelete();
    void onModelCurrentCellChanged(int currentRow, int currentCol,int previousRow, int previousCol);
    void refreshInterfaceTableForModel(int modelIndex);
    void onInterfaceAdd();
    void onInterfaceDelete();
    void onInterfaceCurrentCellChanged(int currentRow, int currentCol,int previousRow, int previousCol);
    void loadKeysForInterface(int modelIndex, int interfaceIndex);
    void onInterfaceItemChanged(QTableWidgetItem *item);
    void onKeyAdd();
    void onKeyDelete();
    void saveToFile2();
    void loadFromFile3();
    void loadKeysForInterface(int interfaceIndex) ;
    void onInterfaceTableCellChanged(int row, int column);
    void onKeyTableCellChanged(int row, int column);
    void onmodelListTableCellChanged(int row, int column) ;
    void 刷新模型();
    void 内置函数();
    void 内置函数(const QString &Nmae,const QString &remark,const QStringList &params);
    // ============== 工具/函数配置 (tab 3) ==============
    QTableWidget *funcListTable;       // 左侧：备注列表
    QPushButton *funcListDelBtn;       // 左侧：删除选中
    QPushButton *funcListAddBtn;       // 左侧：添加行

    QTextEdit *funcCodeEdit;           // 右侧：上半部 Python 代码输入框

    QLineEdit *funcNameEdit;           // 函数名
    QPushButton *funcSaveBtn;          // 保存按钮
    QCheckBox *funcInterruptCheck;     // 触发后中断

    QLineEdit *param1Edit, *param2Edit, *param3Edit, *param4Edit;
    QLineEdit *param5Edit, *param6Edit, *param7Edit, *param8Edit; // 8个参数

    // --- 数据 ---
    QList<FunctionData> functionList;   // 所有函数数据
    int currentRow = -1;                // 当前选中的行索引
    QString configFilePath = "data/functions.json"; // 配置文件路径

    QList<RoleSetting> m_globalSettings;     // 全局设定库
    ToolConfig m_toolConfig;                 // 工具配置

    QHash<QString,QJsonObject> m_fun;


    QJsonObject buildBaseContext(AccountInfo* info, const QString &Gid, const QString& openid, int type);
    void flushPendingMessages(const QString& openid,bool send);
    void trimContextByMessageCount(QJsonObject &context, int maxMessages);


    int m_modelStartIndex;                      // 全局轮询下标
};

#endif // AIWIDGET_H