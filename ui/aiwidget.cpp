#include "AiWidget.h"
#include "global.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
bool 不加载=false;
AiWidget::AiWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();

    // 连接信号槽&QListWidget::itemClicked
    connect(refreshButton, &QPushButton::clicked, this, &AiWidget::on_refreshButton_clicked);
    connect(robotListWidget, &QListWidget::itemClicked, this, &AiWidget::on_robotListWidget_currentRowChanged);
    connect(btnSaveRobot, &QPushButton::clicked, this, &AiWidget::on_btnSaveRobot_clicked);

    connect(settingListWidget, &QListWidget::currentRowChanged, this, &AiWidget::on_settingListWidget_currentRowChanged);
    connect(btnAddSetting, &QPushButton::clicked, this, &AiWidget::on_btnAddSetting_clicked);
    connect(btnDeleteSetting, &QPushButton::clicked, this, &AiWidget::on_btnDeleteSetting_clicked);

    // 加载数据并刷新界面
    loadFromFile();
    loadFromFile2();
    loadFromFile3();
    refreshSettingList();
    refreshSettingCombo();
    refreshRobotList();
}

void AiWidget::setupUi()
{
    // ---------- 主布局 ----------
    QGridLayout *mainLayout = new QGridLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ==============================================
    // ---- 左侧：全局机器人列表 + 刷新按钮 ----
    // (从 tab1 抽离出来，放到主布局的最左边)
    // ==============================================
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(0);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // 注意：这里父对象改为 this，不再是 tab1
    robotListWidget = new QListWidget(this);
    robotListWidget->setMaximumWidth(150);
    leftLayout->addWidget(robotListWidget);

    refreshButton = new QPushButton("刷新列表", this);
    leftLayout->addWidget(refreshButton);

    // 把左侧布局加到主布局的第 0 列
    mainLayout->addLayout(leftLayout, 0, 0);
    // 【关键】左侧列 stretch 为 0，不拉伸；右侧列 stretch 为 1，拉伸填满
    mainLayout->setColumnStretch(0, 0);
    mainLayout->setColumnStretch(1, 1);

    // ==============================================
    // ---- Tab 控件放到主布局的第 1 列 ----
    // ==============================================
    tabWidget = new QTabWidget(this);
    mainLayout->addWidget(tabWidget, 0, 1);

    // ==================== 首页 (tab 1) ====================
    QWidget *tab1 = new QWidget(this);
    tabWidget->addTab(tab1, "首页");

    QGridLayout *grid1 = new QGridLayout(tab1);
    grid1->setContentsMargins(0, 0, 0, 0);
    grid1->setSpacing(0);

    // ---- 右侧：详细信息区域 ----
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->setSpacing(2);
    rightLayout->setContentsMargins(2, 2, 2, 2);

    // 第一行：复选框 + 保存机器人按钮
    QHBoxLayout *hboxChecks = new QHBoxLayout();
    hboxChecks->setSpacing(2);

    chkGroupChat     = new QCheckBox("启用群聊", tab1);
    chkGroupPersonal = new QCheckBox("启用群个人", tab1);
    chkPrivateChat   = new QCheckBox("启用私聊", tab1);
    chkNameTrigger   = new QCheckBox("名字触发", tab1);
    chkChannel       = new QCheckBox("启用频道", tab1);
    chkAtTrigger     = new QCheckBox("艾特触发", tab1);
    chkFunction      = new QCheckBox("启用函数", tab1);
    chkImageRec      = new QCheckBox("启用识图", tab1);

    hboxChecks->addWidget(chkGroupChat);
    hboxChecks->addWidget(chkGroupPersonal);
    hboxChecks->addWidget(chkPrivateChat);
    hboxChecks->addWidget(chkNameTrigger);
    hboxChecks->addWidget(chkChannel);
    hboxChecks->addWidget(chkAtTrigger);
    hboxChecks->addWidget(chkFunction);
    hboxChecks->addWidget(chkImageRec);

    btnSaveRobot = new QPushButton("保存机器人", tab1);
    hboxChecks->addWidget(btnSaveRobot);
    rightLayout->addLayout(hboxChecks);

    // 第二行：机器人详细信息（网格）
    QGridLayout *gridDetails = new QGridLayout();
    gridDetails->setSpacing(2);
    gridDetails->setContentsMargins(2, 2, 2, 2);

    lblRobotName = new QLabel("昵称：", tab1);
    lblRobotName->setMaximumSize(50, 16777215);
    editRobotName = new QLineEdit(tab1);
    lblModel = new QLabel("模型：", tab1);
    comboModel = new QComboBox(tab1);
    lblSetting = new QLabel("设定：", tab1);
    comboSetting = new QComboBox(tab1);

    gridDetails->addWidget(lblRobotName, 0, 0);
    gridDetails->addWidget(editRobotName, 0, 1);
    gridDetails->addWidget(lblModel, 0, 2);
    gridDetails->addWidget(comboModel, 0, 3);
    gridDetails->addWidget(lblSetting, 0, 4);
    gridDetails->addWidget(comboSetting, 0, 5);

    lblContext = new QLabel("上下文：", tab1);
    editContext = new QLineEdit(tab1);
    lblNoReplySeconds = new QLabel("N秒没回复：", tab1);
    editNoReplySeconds = new QLineEdit(tab1);
    lblNoReplyMinutes = new QLabel("N分钟没回复", tab1);
    editNoReplyMinutes = new QLineEdit(tab1);
    lblDelayReply = new QLabel("延迟回复(秒)：", tab1);
    editDelayReply = new QLineEdit(tab1);

    // 标签右对齐
    lblRobotName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblModel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblSetting->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblContext->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    gridDetails->addWidget(lblContext, 1, 0);
    gridDetails->addWidget(editContext, 1, 1);
    gridDetails->addWidget(lblNoReplySeconds, 1, 2);
    gridDetails->addWidget(editNoReplySeconds, 1, 3);
    gridDetails->addWidget(lblNoReplyMinutes, 1, 4);
    gridDetails->addWidget(editNoReplyMinutes, 1, 5);
    gridDetails->addWidget(lblDelayReply, 1, 6);
    gridDetails->addWidget(editDelayReply, 1, 7);

    rightLayout->addLayout(gridDetails);

    // 第三行：全局设定列表 + 编辑框
    QHBoxLayout *hboxSettings = new QHBoxLayout();
    hboxSettings->setSpacing(2);
    settingListWidget = new QListWidget(tab1);
    settingListWidget->setMaximumWidth(150);
    settingTextEdit = new QTextEdit(tab1);
    hboxSettings->addWidget(settingListWidget);
    hboxSettings->addWidget(settingTextEdit);
    rightLayout->addLayout(hboxSettings);

    // 第四行：设定名 + 添加/删除按钮
    QHBoxLayout *hboxButtons = new QHBoxLayout();
    hboxButtons->setSpacing(2);
    hboxButtons->setContentsMargins(2, 2, 2, 2);

    lblSettingName = new QLabel("设定名：", tab1);
    editSettingName = new QLineEdit(tab1);
    btnAddSetting = new QPushButton("保存|添加", tab1);
    btnDeleteSetting = new QPushButton("删除", tab1);

    hboxButtons->addWidget(lblSettingName);
    hboxButtons->addWidget(editSettingName);
    hboxButtons->addWidget(btnAddSetting);
    hboxButtons->addWidget(btnDeleteSetting);
    rightLayout->addLayout(hboxButtons);

    // 因为左侧列表已经移走，现在 tab1 的网格只需要放右半部分即可
    grid1->addLayout(rightLayout, 0, 0);

    // ==================== 模型配置 (tab 2) ====================
    QWidget *tab2 = new QWidget(this);
    tabWidget->addTab(tab2, "模型配置");

    // 使用水平布局分三列
    QHBoxLayout *hLayoutTab2 = new QHBoxLayout(tab2);
    hLayoutTab2->setSpacing(2);
    hLayoutTab2->setContentsMargins(2, 2, 2, 2);

    // ----- 左侧：模型名列表 -----
    QVBoxLayout *vLayoutLeft = new QVBoxLayout();
    vLayoutLeft->setSpacing(2);

    modelListTable = new QTableWidget(tab2);
    modelListTable->setColumnCount(1);
    modelListTable->setHorizontalHeaderLabels(QStringList() << "模型名");
    modelListTable->verticalHeader()->setVisible(false);
    modelListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    modelListTable->setMaximumWidth(180);
    modelListTable->setColumnWidth(0, 160);
    vLayoutLeft->addWidget(modelListTable);

    QHBoxLayout *hBtnLeft = new QHBoxLayout();
    modelListAddBtn = new QPushButton("添加新行", tab2);
    modelListDelBtn = new QPushButton("删除选中", tab2);
    modelListAddBtn->setMaximumWidth(100);
    modelListDelBtn->setMaximumWidth(100);
    hBtnLeft->addWidget(modelListAddBtn);
    hBtnLeft->addWidget(modelListDelBtn);
    vLayoutLeft->addLayout(hBtnLeft);

    // ----- 中间：接口列表 -----
    QVBoxLayout *vLayoutMid = new QVBoxLayout();
    vLayoutMid->setSpacing(2);

    interfaceTable = new QTableWidget(tab2);
    interfaceTable->setColumnCount(2);
    interfaceTable->setHorizontalHeaderLabels(QStringList() << "备注" << "接口");
    interfaceTable->verticalHeader()->setVisible(false);
    interfaceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    interfaceTable->setColumnWidth(0, 140);
    interfaceTable->setColumnWidth(1, 110);

    vLayoutMid->addWidget(interfaceTable);

    QHBoxLayout *hBtnMid = new QHBoxLayout();
    interfaceAddBtn = new QPushButton("添加新行", tab2);
    interfaceDelBtn = new QPushButton("删除选中", tab2);
    hBtnMid->addWidget(interfaceAddBtn);
    hBtnMid->addWidget(interfaceDelBtn);
    vLayoutMid->addLayout(hBtnMid);

    // ----- 右侧：Key 列表 -----
    QVBoxLayout *vLayoutRight = new QVBoxLayout();
    vLayoutRight->setSpacing(2);

    keyTable = new QTableWidget(tab2);
    keyTable->setColumnCount(3);
    keyTable->setHorizontalHeaderLabels(QStringList() << "key" << "使用次数" << "最后错误");
    keyTable->verticalHeader()->setVisible(false);
    keyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    keyTable->horizontalHeader()->setStretchLastSection(true);
    keyTable->setColumnWidth(0, 150);
    keyTable->setColumnWidth(1, 100);
    keyTable->setColumnWidth(2, 300);
    vLayoutRight->addWidget(keyTable);

    QHBoxLayout *hBtnRight = new QHBoxLayout();
    keyAddBtn = new QPushButton("添加新行", tab2);
    keyDelBtn = new QPushButton("删除选中", tab2);
    hBtnRight->addWidget(keyAddBtn);
    hBtnRight->addWidget(keyDelBtn);
    vLayoutRight->addLayout(hBtnRight);

    // 将三列按比例加入布局 (为了确保左边不拉伸，也可以在这里写死)
    hLayoutTab2->addLayout(vLayoutLeft, 0); // 权重 0，不拉伸
    hLayoutTab2->addLayout(vLayoutMid, 1);  // 权重 1
    hLayoutTab2->addLayout(vLayoutRight, 1); // 权重 1

    // 设置表格选择模式
    modelListTable->setSelectionMode(QAbstractItemView::SingleSelection);
    interfaceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    keyTable->setSelectionMode(QAbstractItemView::SingleSelection);

    // 中间表格第一列设置复选框
    interfaceTable->setColumnCount(2);
    interfaceTable->setHorizontalHeaderLabels(QStringList() << "备注" << "接口");

    interfaceTable->setColumnWidth(0, 140);
    interfaceTable->setColumnWidth(0, 140);



    // 信号连接
    connect(modelListTable, &QTableWidget::currentCellChanged,
            this, &AiWidget::onModelCurrentCellChanged);

    connect(modelListTable, &QTableWidget::cellChanged,
            this, &AiWidget::onmodelListTableCellChanged);

    connect(interfaceTable, &QTableWidget::currentCellChanged,
            this, &AiWidget::onInterfaceCurrentCellChanged);
    connect(interfaceTable, &QTableWidget::itemChanged,
            this, &AiWidget::onInterfaceItemChanged);

    connect(modelListAddBtn, &QPushButton::clicked, this, &AiWidget::onModelAdd);
    connect(modelListDelBtn, &QPushButton::clicked, this, &AiWidget::onModelDelete);
    connect(interfaceAddBtn, &QPushButton::clicked, this, &AiWidget::onInterfaceAdd);
    connect(interfaceDelBtn, &QPushButton::clicked, this, &AiWidget::onInterfaceDelete);
    connect(keyAddBtn, &QPushButton::clicked, this, &AiWidget::onKeyAdd);
    connect(keyDelBtn, &QPushButton::clicked, this, &AiWidget::onKeyDelete);

    connect(interfaceTable, &QTableWidget::cellChanged,
            this, &AiWidget::onInterfaceTableCellChanged);

    connect(keyTable, &QTableWidget::cellChanged,
            this, &AiWidget::onKeyTableCellChanged);








    // ==================== 工具/函数配置 (tab 3) ====================
    QWidget *tab3 = new QWidget(this);
    tabWidget->addTab(tab3, "工具");

    QHBoxLayout *hLayoutTab3 = new QHBoxLayout(tab3);
    hLayoutTab3->setSpacing(2);
    hLayoutTab3->setContentsMargins(2, 2, 2, 2);

    // ----- 左侧：函数备注列表 -----
    QVBoxLayout *vLayoutFuncList = new QVBoxLayout();
    vLayoutFuncList->setSpacing(2);

    funcListTable = new QTableWidget(tab3);
    funcListTable->setColumnCount(1);
    funcListTable->setHorizontalHeaderLabels(QStringList() << "备注");
    funcListTable->verticalHeader()->setVisible(false);
    funcListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    funcListTable->setMaximumWidth(210);
    funcListTable->setColumnWidth(0, 200);
    vLayoutFuncList->addWidget(funcListTable);

    QHBoxLayout *hBtnFuncList = new QHBoxLayout();
    funcListDelBtn = new QPushButton("删除选中", tab3);
    funcListAddBtn = new QPushButton("添加行", tab3);
    hBtnFuncList->addWidget(funcListAddBtn);
    hBtnFuncList->addWidget(funcListDelBtn);
    funcListDelBtn->setMaximumWidth(100);
    funcListAddBtn->setMaximumWidth(100);
    vLayoutFuncList->addLayout(hBtnFuncList);

    // 设置左边不拉伸
    hLayoutTab3->addLayout(vLayoutFuncList, 0);

    // ----- 右侧：代码及参数配置 -----
    QVBoxLayout *vLayoutCode = new QVBoxLayout();
    vLayoutCode->setSpacing(2);

    // 上半部：Python 代码编写框
    funcCodeEdit = new QTextEdit(tab3);
    funcCodeEdit->setPlaceholderText("在此处编写 Python 代码...");
    vLayoutCode->addWidget(funcCodeEdit);

    // 下半部：函数名和参数配置（采用 QGridLayout）
    QGridLayout *gridParams = new QGridLayout();
    gridParams->setSpacing(2);

    // 第一行：函数名、保存、中断
    QLabel *lblFuncName = new QLabel("函数名:", tab3);
    lblFuncName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    funcNameEdit = new QLineEdit(tab3);
    funcNameEdit->setPlaceholderText("只能英文数字");
    funcSaveBtn = new QPushButton("保存", tab3);
    funcInterruptCheck = new QCheckBox("触发后中断", tab3);

    gridParams->addWidget(lblFuncName, 0, 0);
    gridParams->addWidget(funcNameEdit, 0, 1);
    gridParams->addWidget(funcSaveBtn, 0, 2);
    gridParams->addWidget(funcInterruptCheck, 0, 3, 1, 2); // 跨2列

    // 参数 1 ~ 8 (每行两个参数)
    QLabel *lblParam1 = new QLabel("参数1:", tab3); lblParam1->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param1Edit = new QLineEdit(tab3);
    QLabel *lblParam2 = new QLabel("参数2:", tab3); lblParam2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param2Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam1, 1, 0); gridParams->addWidget(param1Edit, 1, 1);
    gridParams->addWidget(lblParam2, 1, 2); gridParams->addWidget(param2Edit, 1, 3);

    QLabel *lblParam3 = new QLabel("参数3:", tab3); lblParam3->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param3Edit = new QLineEdit(tab3);
    QLabel *lblParam4 = new QLabel("参数4:", tab3); lblParam4->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param4Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam3, 2, 0); gridParams->addWidget(param3Edit, 2, 1);
    gridParams->addWidget(lblParam4, 2, 2); gridParams->addWidget(param4Edit, 2, 3);

    QLabel *lblParam5 = new QLabel("参数5:", tab3); lblParam5->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param5Edit = new QLineEdit(tab3);
    QLabel *lblParam6 = new QLabel("参数6:", tab3); lblParam6->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param6Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam5, 3, 0); gridParams->addWidget(param5Edit, 3, 1);
    gridParams->addWidget(lblParam6, 3, 2); gridParams->addWidget(param6Edit, 3, 3);

    QLabel *lblParam7 = new QLabel("参数7:", tab3); lblParam7->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param7Edit = new QLineEdit(tab3);
    QLabel *lblParam8 = new QLabel("参数8:", tab3); lblParam8->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    param8Edit = new QLineEdit(tab3);
    gridParams->addWidget(lblParam7, 4, 0); gridParams->addWidget(param7Edit, 4, 1);
    gridParams->addWidget(lblParam8, 4, 2); gridParams->addWidget(param8Edit, 4, 3);

    vLayoutCode->addLayout(gridParams);
    hLayoutTab3->addLayout(vLayoutCode, 1); // 右侧拉伸填满

    // 设置表格第一列可勾选
    funcListTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    funcListTable->setSelectionMode(QAbstractItemView::SingleSelection);

    // 连接信号槽
    connect(funcListTable, &QTableWidget::currentCellChanged,
            this, &AiWidget::onFuncListCurrentCellChanged);
    connect(funcListAddBtn, &QPushButton::clicked,
            this, &AiWidget::onFuncListAdd);
    connect(funcListDelBtn, &QPushButton::clicked,
            this, &AiWidget::onFuncListDelete);
    connect(funcSaveBtn, &QPushButton::clicked,
            this, &AiWidget::onFuncSave);

    connect(funcListTable, &QTableWidget::itemChanged,this, &AiWidget::onFuncListItemChanged);






}
// ====== 数据读写 ======

void AiWidget::loadFromFile()
{
    QFile file("roles.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();

    // 读取全局设定
    m_globalSettings.clear();
    QJsonArray settingsArr = root["global_settings"].toArray();
    for (const auto &v : std::as_const(settingsArr)) {
        QJsonObject obj = v.toObject();
        RoleSetting s;
        s.name = obj["name"].toString();
        s.content = obj["content"].toString();
        m_globalSettings.append(s);
    }

    // 读取工具配置
    QJsonObject toolObj = root["tool_config"].toObject();
    m_toolConfig.enableAutoReply = toolObj["enable_auto_reply"].toBool();
    m_toolConfig.enableScheduledTask = toolObj["enable_scheduled_task"].toBool();
    m_toolConfig.enableCustomLog = toolObj["enable_custom_log"].toBool();

    QJsonArray tableArr = toolObj["table_data"].toArray();
    m_toolConfig.tableData.clear();
    for (const auto &v : std::as_const(tableArr)) {
        QJsonArray pair = v.toArray();
        if (pair.size() == 2) {
            m_toolConfig.tableData.append(qMakePair(pair[0].toString(), pair[1].toString()));
        }
    }
}
void AiWidget::saveToFile() const
{
    QJsonObject root;

    // 写入全局设定
    QJsonArray settingsArr;
    for (const auto &s : m_globalSettings) {
        QJsonObject obj;
        obj["name"] = s.name;
        obj["content"] = s.content;
        settingsArr.append(obj);
    }
    root["global_settings"] = settingsArr;

    QJsonObject toolObj;
    toolObj["enable_auto_reply"] = m_toolConfig.enableAutoReply;
    toolObj["enable_scheduled_task"] = m_toolConfig.enableScheduledTask;
    toolObj["enable_custom_log"] = m_toolConfig.enableCustomLog;

    QJsonArray tableArr;
    for (const auto &pair : m_toolConfig.tableData) {
        QJsonArray item;
        item.append(pair.first);
        item.append(pair.second);
        tableArr.append(item);
    }
    toolObj["table_data"] = tableArr;
    root["tool_config"] = toolObj;

    QJsonDocument doc(root);
    QFile file("roles.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

// ====== 刷新机器人列表 ======
void AiWidget::refreshRobotList()
{
    robotListWidget->clear();
    for (const auto &acc : std::as_const(m_accounts)) {
        if (!acc->nickname.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem(acc->nickname);
            item->setData(Qt::UserRole, acc->appid_int ) ;
            robotListWidget->addItem(item);

            if(m_currentRobotIndex<=0)
            {
                不加载= 1;
                addtoui(acc);

                for (int row = 0; row < funcListTable->rowCount(); ++row) {
                    QTableWidgetItem *item = funcListTable->item(row, 0);
                    if (item) {
                        item->setCheckState(Qt::Unchecked);
                    }
                }

                QHash<QString, int> nameToRow;
                for (int row = 0; row < functionList.size(); ++row) {
                    nameToRow[functionList[row].funcName] = row;
                }


                for (int i = 0; i < acc->tools.size(); ++i) {
                    const QString &toolName = acc->tools[i];  // 或者用 QString toolName = acc->tools[i];
                    auto it = nameToRow.find(toolName);
                    if (it != nameToRow.end()) {
                        int row = *it;
                        QTableWidgetItem *item = funcListTable->item(row, 0);
                        if (item) {
                            item->setCheckState(Qt::Checked);
                        }
                    }
                }
                不加载= 0;
            }
        }
    }
    if (robotListWidget->count() > 0)
        robotListWidget->setCurrentRow(0);
    else {
        m_currentRobotIndex = -1;
        // 清空机器人信息
        editRobotName->clear();
        comboModel->setCurrentIndex(-1);
        comboSetting->setCurrentIndex(-1);
        editContext->clear();
        editNoReplySeconds->clear();
        editNoReplyMinutes->clear();
        editDelayReply->clear();
        chkGroupChat->setChecked(false);
        chkGroupPersonal->setChecked(false);
        chkPrivateChat->setChecked(false);
        chkNameTrigger->setChecked(false);
        chkChannel->setChecked(false);
        chkAtTrigger->setChecked(false);
        chkFunction->setChecked(false);
        chkImageRec->setChecked(false);
    }
}
void AiWidget::addtoui(const std::shared_ptr<AccountInfo> acc)
{
    editRobotName->setText(acc->Ai_nickname);
    comboModel->setCurrentText(acc->model);
    int idx = comboSetting->findText(acc->setting);
    comboSetting->setCurrentIndex(idx >= 0 ? idx : -1);
    editContext->setText(acc->context);
    editNoReplySeconds->setText(QString::number(acc->nSecondsNoReply));
    editNoReplyMinutes->setText(QString::number(acc->nMinutesNoReply));
    editDelayReply->setText(QString::number(acc->delayReplySeconds));
    chkGroupChat->setChecked(acc->enableGroupChat);
    chkGroupPersonal->setChecked(acc->enableGroupPersonal);
    chkPrivateChat->setChecked(acc->enablePrivateChat);
    chkNameTrigger->setChecked(acc->nameTrigger);
    chkChannel->setChecked(acc->enableChannel);
    chkAtTrigger->setChecked(acc->atTrigger);
    chkFunction->setChecked(acc->enableFunction);
    chkImageRec->setChecked(acc->enableImageRec);
    m_currentRobotIndex = acc->appid_int;
}
void AiWidget::refreshSettingList()
{
    settingListWidget->clear();
    for (const auto &s : std::as_const(m_globalSettings)) {
        QListWidgetItem *item = new QListWidgetItem(s.name);
        item->setData(Qt::UserRole, s.name);
        settingListWidget->addItem(item);
    }
    if (settingListWidget->count() > 0)
        settingListWidget->setCurrentRow(0);
    else
        settingTextEdit->clear();
}

void AiWidget::refreshSettingCombo()
{
    comboSetting->clear();
    for (const auto &s : std::as_const(m_globalSettings)) {
        comboSetting->addItem(s.name);
    }
}

void AiWidget::on_refreshButton_clicked()
{
    refreshRobotList();
}
//列表被单击
void AiWidget::on_robotListWidget_currentRowChanged(QListWidgetItem *item)
{
    int currentRow = item->data(Qt::UserRole).toInt();
    if(currentRow == m_currentRobotIndex) return;
    for (const auto &acc : std::as_const(m_accounts))
    {
        if(acc->appid_int != currentRow) continue;

        if(tabWidget->currentIndex()==0)
            addtoui(acc);
        else
        {
            m_currentRobotIndex= acc->appid_int;
            // 1. 先全部取消勾选
            不加载= 1;
            for (int row = 0; row < funcListTable->rowCount(); ++row) {
                QTableWidgetItem *item = funcListTable->item(row, 0);
                if (item) {
                    item->setCheckState(Qt::Unchecked);
                }
            }

            QHash<QString, int> nameToRow;
            for (int row = 0; row < functionList.size(); ++row) {
                nameToRow[functionList[row].funcName] = row;
            }



            // 使用索引循环遍历 acc->tools
            for (int i = 0; i < acc->tools.size(); ++i) {
                const QString &toolName = acc->tools[i];  // 或者用 QString toolName = acc->tools[i];
                auto it = nameToRow.find(toolName);
                if (it != nameToRow.end()) {
                    int row = *it;
                    QTableWidgetItem *item = funcListTable->item(row, 0);
                    if (item) {
                        item->setCheckState(Qt::Checked);
                    }
                }
            }
            不加载= 0;
        }
        return;
    }
    QMessageBox::warning(this,"打开失败","当前绑定的机器人 不在列表 请刷新列表重写选择");
}

void AiWidget::on_btnSaveRobot_clicked()
{
    for (const auto &acc : std::as_const(m_accounts))
    {
        if(acc->appid_int != m_currentRobotIndex) continue;
        acc->Ai_nickname = editRobotName->text().trimmed();
        acc->model = comboModel->currentText();
        acc->setting = comboSetting->currentText();
        acc->context = editContext->text();
        acc->nSecondsNoReply = editNoReplySeconds->text().toInt();
        acc->nMinutesNoReply = editNoReplyMinutes->text().toInt();
        acc->delayReplySeconds = editDelayReply->text().toInt();
        acc->enableGroupChat = chkGroupChat->isChecked();
        acc->enableGroupPersonal = chkGroupPersonal->isChecked();
        acc->enablePrivateChat = chkPrivateChat->isChecked();
        acc->nameTrigger = chkNameTrigger->isChecked();
        acc->enableChannel = chkChannel->isChecked();
        acc->atTrigger = chkAtTrigger->isChecked();
        acc->enableFunction = chkFunction->isChecked();
        acc->enableImageRec = chkImageRec->isChecked();
        accountPage->saveAccounts();
        return;
    }
    QMessageBox::warning(this,"保存失败","未找对对应 appid 机器人："+QString::number(m_currentRobotIndex));
}

// ====== 全局设定相关槽 ======

void AiWidget::on_settingListWidget_currentRowChanged(int currentRow)
{
    if (currentRow < 0 || currentRow >= m_globalSettings.size()) {
        settingTextEdit->clear();
        editSettingName->clear();
        return;
    }
    const auto &s = m_globalSettings[currentRow];
    editSettingName->setText(s.name);
    settingTextEdit->setPlainText(s.content);
}

void AiWidget::on_btnAddSetting_clicked()
{
    QString name = editSettingName->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "警告", "设定名不能为空！");
        return;
    }
    // 检查同名
    for (auto &s : m_globalSettings) {
        if (s.name == name) {
            s.content = settingTextEdit->toPlainText();
            saveToFile();
            return;
        }
    }

    RoleSetting newSetting;
    newSetting.name = name;
    newSetting.content = settingTextEdit->toPlainText();
    m_globalSettings.append(newSetting);

    refreshSettingList();
    refreshSettingCombo();

    // 选中新增的设定
    for (int i = 0; i < settingListWidget->count(); ++i) {
        if (settingListWidget->item(i)->data(Qt::UserRole).toString() == name) {
            settingListWidget->setCurrentRow(i);
            break;
        }
    }
    saveToFile();
}

void AiWidget::on_btnDeleteSetting_clicked()
{
    int row = settingListWidget->currentRow();
    if (row < 0 || row >= m_globalSettings.size()) {
        QMessageBox::information(this, "提示", "请先选择一个设定！");
        return;
    }
    QString name = m_globalSettings[row].name;

    // 检查是否有机器人正在使用该设定
    for (const auto &acc : std::as_const(m_accounts)) {
        if (acc->setting == name) {
            QMessageBox::warning(this, "警告",
                                 QString("设定「%1」正被机器人「%2」使用，无法删除！").arg(name, acc->nickname));
            return;
        }
    }

    m_globalSettings.removeAt(row);
    refreshSettingList();
    refreshSettingCombo();
    saveToFile();
}



void AiWidget::onFuncListAdd() {
    FunctionData newData;
    newData.remark = QString("新函数 %1").arg(functionList.size() + 1);

    newData.params.clear();
    for (int i = 0; i < 8; ++i) {
        newData.params.append("");
    }
    functionList.append(newData);

    int row = functionList.size() - 1;
    funcListTable->insertRow(row);

    // 设置第一列的 Item（带复选框和备注）
    QTableWidgetItem *item = new QTableWidgetItem(newData.remark);

    funcListTable->setItem(row, 0, item);

    // 选中新行
    funcListTable->selectRow(row);
    // currentCellChanged 会自动触发加载数据
    saveToFile();  // 自动保存到文件
}
void AiWidget::onFuncListDelete() {
    int row = funcListTable->currentRow();
    if (row < 0 || row >= functionList.size()) {
        QMessageBox::warning(this, "提示", "请先选中要删除的行");
        return;
    }

    // 从列表中移除
    functionList.removeAt(row);
    funcListTable->removeRow(row);

    // 如果还有行，选中第一行；否则清空右侧面板
    if (!functionList.isEmpty()) {
        funcListTable->selectRow(0);
    } else {
        clearRightPanel();  // 清空所有编辑框
        currentRow = -1;
    }
    saveToFile();
}
void AiWidget::saveCurrentRowData() {
    int row = funcListTable->currentRow();
    if (row < 0 || row >= functionList.size()) return;

    // 从右侧面板读取数据
    FunctionData &data = functionList[row];
    data.funcName = funcNameEdit->text();
    data.code = funcCodeEdit->toPlainText();
    data.params.clear();
    data.params << param1Edit->text() << param2Edit->text()
                << param3Edit->text() << param4Edit->text()
                << param5Edit->text() << param6Edit->text()
                << param7Edit->text() << param8Edit->text();
    data.interrupt = funcInterruptCheck->isChecked();


}
void AiWidget::onFuncListCurrentCellChanged(int currentRow, int currentCol,
                                             int previousRow, int previousCol) {


    if (currentRow < 0 || currentRow >= functionList.size()) {
        clearRightPanel();
        this->currentRow = -1;
        return;
    }

    const FunctionData &data = functionList[currentRow];
    funcNameEdit->setText(data.funcName);
    funcCodeEdit->setPlainText(data.code);
    param1Edit->setText(data.params.value(0));
    param2Edit->setText(data.params.value(1));
    param3Edit->setText(data.params.value(2));
    param4Edit->setText(data.params.value(3));
    param5Edit->setText(data.params.value(4));
    param6Edit->setText(data.params.value(5));
    param7Edit->setText(data.params.value(6));
    param8Edit->setText(data.params.value(7));
    funcInterruptCheck->setChecked(data.interrupt);

    this->currentRow = currentRow;
}

void AiWidget::clearRightPanel() {
    funcNameEdit->clear();
    funcCodeEdit->clear();
    param1Edit->clear();
    param2Edit->clear();
    param3Edit->clear();
    param4Edit->clear();
    param5Edit->clear();
    param6Edit->clear();
    param7Edit->clear();
    param8Edit->clear();
    funcInterruptCheck->setChecked(false);
}
void AiWidget::onFuncSave() {
    int row = funcListTable->currentRow();
    if (row < 0 || row >= functionList.size()) {
        QMessageBox::warning(this, "提示", "请先选中要保存的行");
        return;
    }
    saveCurrentRowData();   // 保存到内存
    saveToFile();           // 写入文件

}

void AiWidget::saveToFile() {
    QJsonArray jsonArray;
    for (const FunctionData &data : std::as_const(functionList)) {
        QJsonObject obj;

        obj["remark"] = data.remark;
        obj["funcName"] = data.funcName;
        obj["code"] = data.code;
        obj["params"] = QJsonArray::fromStringList(data.params);
        obj["interrupt"] = data.interrupt;
        jsonArray.append(obj);
    }

    QJsonDocument doc(jsonArray);
    QFile file(configFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法保存配置文件");
        return;
    }
    file.write(doc.toJson());
    file.close();
}

void AiWidget::loadFromFile2() {
    QFile file(configFilePath);
    if (!file.exists()) return;  // 首次运行没有文件

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {

        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) return;

    QJsonArray jsonArray = doc.array();
    functionList.clear();
    funcListTable->setRowCount(0);
    不加载= true;
    for (const QJsonValue &val : std::as_const(jsonArray)) {
        QJsonObject obj = val.toObject();
        FunctionData d;

        d.remark = obj["remark"].toString();
        d.funcName = obj["funcName"].toString();
        d.code = obj["code"].toString();
        QJsonArray paramsArray = obj["params"].toArray();
        for (int i = 0; i < paramsArray.size() && i < 8; ++i) {
            d.params.append(paramsArray[i].toString());
        }
        while (d.params.size() < 8) d.params.append("");  // 补齐8个
        d.interrupt = obj["interrupt"].toBool(false);

        functionList.append(d);

        // 在表格中添加行
        int row = functionList.size() - 1;
        funcListTable->insertRow(row);
        QTableWidgetItem *item = new QTableWidgetItem(d.remark);
        funcListTable->setItem(row, 0, item);
    }

    // 若加载后有数据，选中第一行
    if (!functionList.isEmpty()) {
        funcListTable->selectRow(0);
        onFuncListCurrentCellChanged(0, 0, -1, -1);
    } else {
        clearRightPanel();
        currentRow = -1;
    }
    不加载= 0;
}


void AiWidget::onFuncListItemChanged(QTableWidgetItem *item) {
    if(不加载) return;
    if (!item) return;
    int row = item->row();
    if (row < 0 || row >= functionList.size()) return;
    QString remark=item->text();
    if(remark != functionList[row].remark)
    {
        functionList[row].remark = remark;
        saveToFile();
        return;
    }
    for (auto &acc : m_accounts)
    {
        if(acc->appid_int!= m_currentRobotIndex) continue;
        acc->tools.clear();
        bool ok=false;
        for (int i=0; i< functionList.size();++i) {
            QTableWidgetItem *item = funcListTable->item(i, 0);
            bool en = item->checkState();
            if(functionList[i].funcName.isEmpty())
            {
                if(en) item->setCheckState(Qt::Unchecked);
                continue;
            }
            if(en)
            {
                ok=true;
                acc->tools.append(functionList[i].funcName);
            }
        }
        if(ok) accountPage->saveAccounts();
        break;
    }

}

void AiWidget::onmodelListTableCellChanged(int row, int column) {
    if(不加载) return;
    if (row < 0 || row >= modelList.size()) return;  // 全局接口列表
    QTableWidgetItem *item = modelListTable->item(row, column);
    if (!item) return;
    QString newText = item->text();
    auto &iface = modelList[row];
    iface.name = newText;
    saveToFile2();
}

//==============================================

void AiWidget::onModelAdd() {
    ModelData newModel;
    newModel.name = QString("新模型 %1").arg(modelList.size() + 1);
    // 默认不启用任何接口（空列表）
    modelList.append(newModel);
    int row = modelList.size() - 1;
    modelListTable->insertRow(row);
    modelListTable->setItem(row, 0, new QTableWidgetItem(newModel.name));
    modelListTable->selectRow(row);
    saveToFile2();
}

void AiWidget::onModelDelete() {
    int row = modelListTable->currentRow();
    if (row < 0 || row >= modelList.size()) {
        QMessageBox::warning(this, "提示", "请先选中要删除的模型");
        return;
    }
    modelList.removeAt(row);
    modelListTable->removeRow(row);

    if (!modelList.isEmpty()) {
        modelListTable->selectRow(0);
    } else {
        interfaceTable->setRowCount(0);
        keyTable->setRowCount(0);
        currentModelRow = -1;
        currentInterfaceRow = -1;
    }
    saveToFile2();
}
void AiWidget::onModelCurrentCellChanged(int currentRow, int currentCol,
                                         int previousRow, int previousCol) {
    Q_UNUSED(currentCol); Q_UNUSED(previousRow); Q_UNUSED(previousCol);
    if (currentRow < 0 || currentRow >= modelList.size()) {
        interfaceTable->setRowCount(0);
        keyTable->setRowCount(0);
        currentModelRow = -1;
        return;
    }
    currentModelRow = currentRow;

    refreshInterfaceTableForModel(currentRow);
    // 清空右侧密钥表
    keyTable->setRowCount(0);
    currentInterfaceRow = -1;
}

void AiWidget::refreshInterfaceTableForModel(int modelIndex) {
    const ModelData &model = modelList.at(modelIndex);
    interfaceTable->setRowCount(globalInterfaces.size());
    不加载=1;
    for (int i = 0; i < globalInterfaces.size(); ++i) {
        const InterfaceData &iface = globalInterfaces.at(i);
        // 第一列复选框
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        bool enabled = model.enabledInterfaceIndices.contains(i);
        checkItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
        checkItem->setText(iface.remark);
        interfaceTable->setItem(i, 0, checkItem);
        // 第三列接口
        interfaceTable->setItem(i, 1, new QTableWidgetItem(iface.url));
    }
    不加载=0;
}



void AiWidget::onInterfaceAdd() {
    InterfaceData newIface;
    newIface.remark = "新接口";
    newIface.url = "https://";
    globalInterfaces.append(newIface);
    // 刷新当前模型的接口表格（如果模型选中）
    if (currentModelRow >= 0 && currentModelRow < modelList.size()) {
        refreshInterfaceTableForModel(currentModelRow);
        // 自动选中新添加的行
        int row = globalInterfaces.size() - 1;
        interfaceTable->selectRow(row);
    }
    saveToFile2();
}

void AiWidget::onInterfaceDelete() {
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) {
        QMessageBox::warning(this, "提示", "请先选择模型");
        return;
    }
    int row = interfaceTable->currentRow();
    if (row < 0 || row >= globalInterfaces.size()) {
        QMessageBox::warning(this, "提示", "请选中要删除的接口");
        return;
    }
    // 删除全局接口
    globalInterfaces.removeAt(row);
    // 从所有模型的 enabledInterfaceIndices 中移除该索引，并调整索引（因为删除后，后面的索引会减1）
    for (ModelData &model : modelList) {
        // 移除所有等于 row 的索引
        model.enabledInterfaceIndices.removeAll(row);
        // 将大于 row 的索引减1
        for (int i = 0; i < model.enabledInterfaceIndices.size(); ++i) {
            if (model.enabledInterfaceIndices[i] > row) {
                model.enabledInterfaceIndices[i]--;
            }
        }
    }
    // 刷新当前模型视图
    refreshInterfaceTableForModel(currentModelRow);
    if (globalInterfaces.isEmpty()) {
        keyTable->setRowCount(0);
        currentInterfaceRow = -1;
    } else {
        // 选中同一行或上一行
        int newRow = qMin(row, globalInterfaces.size() - 1);
        interfaceTable->selectRow(newRow);
    }
    saveToFile2();
}


void AiWidget::onInterfaceTableCellChanged(int row, int column) {
    // 只处理第二列（备注）和第三列（URL）
    if(不加载) return;
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) return;
    if (row < 0 || row >= globalInterfaces.size()) return;  // 全局接口列表

    QTableWidgetItem *item = interfaceTable->item(row, column);
    if (!item) return;

    // 获取当前文本
    QString newText = item->text();

    // 更新全局接口列表中的数据
    InterfaceData &iface = globalInterfaces[row];
    if (column == 0) {
        iface.remark = newText;
    } else if (column == 1) {
        iface.url = newText;
    }

    // 保存到文件（自动保存）
    saveToFile2();
}
void AiWidget::onInterfaceCurrentCellChanged(int currentRow, int currentCol,
                                             int previousRow, int previousCol) {
    Q_UNUSED(previousRow); Q_UNUSED(previousCol);

    if(不加载) return;
    if (currentRow < 0 || currentRow >= globalInterfaces.size()) {
        keyTable->setRowCount(0);
        currentInterfaceRow = -1;
        return;
    }
    currentInterfaceRow = currentRow;
    loadKeysForInterface(currentRow);
}

void AiWidget::loadKeysForInterface(int interfaceIndex) {
    const InterfaceData &iface = globalInterfaces.at(interfaceIndex);
    const QList<KeyData> &keys = iface.keys;
    keyTable->setRowCount(keys.size());
    for (int i = 0; i < keys.size(); ++i) {
        const KeyData &k = keys.at(i);
        keyTable->setItem(i, 0, new QTableWidgetItem(k.key));
        keyTable->setItem(i, 1, new QTableWidgetItem(QString::number(k.usageCount)));
        keyTable->setItem(i, 2, new QTableWidgetItem(k.lastUsed));
    }
}



void AiWidget::onInterfaceItemChanged(QTableWidgetItem *item) {
    if(不加载) return;
    if (!item) return;
    int row = item->row();
    int col = item->column();
    if (col != 0) return;
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) return;
    if (row < 0 || row >= globalInterfaces.size()) return;

    bool enabled = (item->checkState() == Qt::Checked);
    ModelData &model = modelList[currentModelRow];

    if (enabled) {
        if (!model.enabledInterfaceIndices.contains(row))
            model.enabledInterfaceIndices.append(row);
    } else {
        model.enabledInterfaceIndices.removeAll(row);
    }
    saveToFile2();
}




void AiWidget::onKeyAdd() {
    if (currentInterfaceRow < 0 || currentInterfaceRow >= globalInterfaces.size()) {
        QMessageBox::warning(this, "提示", "请先点击选中一个接口（备注或接口列）");
        return;
    }
    KeyData newKey;
    newKey.key = "新密钥";
    newKey.usageCount = 0;
    newKey.lastUsed = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    globalInterfaces[currentInterfaceRow].keys.append(newKey);
    int row = globalInterfaces[currentInterfaceRow].keys.size() - 1;
    keyTable->insertRow(row);
    keyTable->setItem(row, 0, new QTableWidgetItem(newKey.key));
    keyTable->setItem(row, 1, new QTableWidgetItem(QString::number(newKey.usageCount)));
    keyTable->setItem(row, 2, new QTableWidgetItem(newKey.lastUsed));
    saveToFile2();
}

void AiWidget::onKeyDelete() {
    if (currentInterfaceRow < 0 || currentInterfaceRow >= globalInterfaces.size()) {
        QMessageBox::warning(this, "提示", "请先选中接口");
        return;
    }
    int row = keyTable->currentRow();
    if (row < 0 || row >= globalInterfaces[currentInterfaceRow].keys.size()) {
        QMessageBox::warning(this, "提示", "请选中要删除的密钥");
        return;
    }
    globalInterfaces[currentInterfaceRow].keys.removeAt(row);
    keyTable->removeRow(row);
    saveToFile2();
}

void AiWidget::saveToFile2() {
    QJsonObject root;

    // 保存全局接口列表
    QJsonArray interfacesArray;
    for (const InterfaceData &iface : std::as_const(globalInterfaces)) {
        QJsonObject ifaceObj;
        ifaceObj["enabled"] = iface.enabled;   // 默认启用，但可以不用
        ifaceObj["remark"] = iface.remark;
        ifaceObj["url"] = iface.url;

        QJsonArray keysArray;
        for (const KeyData &key : iface.keys) {
            QJsonObject keyObj;
            keyObj["key"] = key.key;
            keyObj["usageCount"] = key.usageCount;
            keyObj["lastUsed"] = key.lastUsed;
            keysArray.append(keyObj);
        }
        ifaceObj["keys"] = keysArray;
        interfacesArray.append(ifaceObj);
    }
    root["interfaces"] = interfacesArray;

    // 保存模型列表（每个模型包含名称和启用的接口索引）
    QJsonArray modelsArray;
    for (const ModelData &model : std::as_const(modelList)) {
        QJsonObject modelObj;
        modelObj["name"] = model.name;
        QJsonArray enabledIndices;
        for (int idx : model.enabledInterfaceIndices) {
            enabledIndices.append(idx);
        }
        modelObj["enabledInterfaces"] = enabledIndices;
        modelsArray.append(modelObj);
    }
    root["models"] = modelsArray;

    QJsonDocument doc(root);
    QFile file(configFilePath2);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法保存模型配置文件");
        return;
    }
    file.write(doc.toJson());
    file.close();
}

void AiWidget::loadFromFile3() {
    QFile file(configFilePath2);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {

        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();

    // 加载全局接口
    globalInterfaces.clear();
    QJsonArray interfacesArray = root["interfaces"].toArray();
    for (const QJsonValue &ifaceVal : std::as_const(interfacesArray)) {
        QJsonObject ifaceObj = ifaceVal.toObject();
        InterfaceData iface;
        iface.remark = ifaceObj["remark"].toString();
        iface.url = ifaceObj["url"].toString();
        const QJsonArray keysArray = ifaceObj["keys"].toArray();
        for (const QJsonValue &keyVal : keysArray) {
            QJsonObject keyObj = keyVal.toObject();
            KeyData key;
            key.key = keyObj["key"].toString();
            key.usageCount = keyObj["usageCount"].toInt();
            key.lastUsed = keyObj["lastUsed"].toString();
            iface.keys.append(key);
        }
        globalInterfaces.append(iface);
    }

    // 加载模型
    modelList.clear();

    QJsonArray modelsArray = root["models"].toArray();
    for (const QJsonValue &modelVal : std::as_const(modelsArray)) {
        QJsonObject modelObj = modelVal.toObject();
        ModelData model;
        model.name = modelObj["name"].toString();
        QJsonArray enabledArray = modelObj["enabledInterfaces"].toArray();
        for (const QJsonValue &idxVal : std::as_const(enabledArray)) {
            model.enabledInterfaceIndices.append(idxVal.toInt());
        }
        modelList.append(model);
        int row = modelList.size() - 1;
        modelListTable->insertRow(row);
        modelListTable->setItem(row, 0, new QTableWidgetItem(model.name));
    }

    // 刷新中间表格显示（根据当前选中的模型）
    if (!modelList.isEmpty()) {
        modelListTable->selectRow(0);
    } else {
        interfaceTable->setRowCount(0);
        keyTable->setRowCount(0);
        currentModelRow = -1;
        currentInterfaceRow = -1;
    }
}
void AiWidget::onKeyTableCellChanged(int row, int column) {
    if (currentModelRow < 0 || currentModelRow >= modelList.size()) return;
    if (currentInterfaceRow < 0 || currentInterfaceRow >= globalInterfaces.size()) return;

    // 获取当前选中的接口
    InterfaceData &iface = globalInterfaces[currentInterfaceRow];
    if (row < 0 || row >= iface.keys.size()) return;

    QTableWidgetItem *item = keyTable->item(row, column);
    if (!item) return;

    QString newText = item->text();
    KeyData &keyData = iface.keys[row];

    switch (column) {
    case 0: // key
        keyData.key = newText;
        break;
    case 1: // 使用次数
        keyData.usageCount = newText.toInt();
        break;
    case 2: // 最后使用时间
        keyData.lastUsed = newText;
        break;
    default:
        return;
    }
    saveToFile2();
}