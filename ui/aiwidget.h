#ifndef AIWIDGET_H
#define AIWIDGET_H

#include <QWidget>
#include <QList>
#include <qlistwidget.h>
#include <qtablewidget.h>
#include "AccountInfo.h"

class QTabWidget;
class QListWidget;
class QPushButton;
class QCheckBox;
class QLabel;
class QLineEdit;
class QComboBox;
class QTextEdit;

struct KeyData {
    QString key;
    int usageCount = 0;
    QString lastUsed;
};

struct InterfaceData {
    bool enabled=false;
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


class AiWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AiWidget(QWidget *parent = nullptr);

private slots:
    // --- 机器人列表相关 ---
    void on_refreshButton_clicked();                 // 刷新机器人列表（从 m_accounts 读取）
    void on_robotListWidget_currentRowChanged(QListWidgetItem *item); // 切换机器人
    void on_btnSaveRobot_clicked();                  // 保存当前机器人信息（新增/更新）

    // --- 全局设定相关 ---
    void on_settingListWidget_currentRowChanged(int currentRow);
    void on_btnAddSetting_clicked();
    void on_btnDeleteSetting_clicked();



private:
    void setupUi();                          // 纯代码构建全部 UI
    void loadFromFile();                     // 加载 roles.json
    void saveToFile() const;                 // 保存到 roles.json
    void addtoui(const std::shared_ptr<AccountInfo> acc);
    void refreshRobotList();                 // 刷新左侧机器人列表
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

    // --- UI 控件指针 ---
    QTabWidget *tabWidget;

    // ---------- 首页 (tab 1) ----------
    QListWidget *robotListWidget;      // 左侧机器人列表
    QPushButton *refreshButton;

    QCheckBox *chkGroupChat, *chkGroupPersonal, *chkPrivateChat;
    QCheckBox *chkNameTrigger, *chkChannel, *chkAtTrigger, *chkFunction, *chkImageRec;

    QLabel *lblRobotName, *lblModel, *lblSetting, *lblContext;
    QLabel *lblNoReplySeconds, *lblNoReplyMinutes, *lblDelayReply;
    QLineEdit *editRobotName, *editContext, *editNoReplySeconds, *editNoReplyMinutes, *editDelayReply;
    QComboBox *comboModel, *comboSetting;

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
    QString configFilePath2 = "model_config.json";

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
    QString configFilePath = "functions.json"; // 配置文件路径

    QList<RoleSetting> m_globalSettings;     // 全局设定库
    ToolConfig m_toolConfig;                 // 工具配置
    int m_currentRobotIndex = -1;            // 当前选中的机器人appid
};

#endif // AIWIDGET_H