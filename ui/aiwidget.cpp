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
#include <qnetworkreply.h>
bool 不加载=false;
AiWidget::AiWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();


    connect(btnSaveRobot, &QPushButton::clicked, this, &AiWidget::on_btnSaveRobot_clicked);
    connect(settingListWidget, &QListWidget::currentRowChanged, this, &AiWidget::on_settingListWidget_currentRowChanged);
    connect(btnAddSetting, &QPushButton::clicked, this, &AiWidget::on_btnAddSetting_clicked);
    connect(btnDeleteSetting, &QPushButton::clicked, this, &AiWidget::on_btnDeleteSetting_clicked);
    connect(this, &AiWidget::newMessageArrived,this, &AiWidget::onNewMessage, Qt::QueuedConnection);
    connect(this, &AiWidget::asyncReplyReceived,this, &AiWidget::onAsyncReply);

    loadFromFile();
    loadFromFile2();
    loadFromFile3();
    refreshSettingList();
    refreshSettingCombo();

}

AiWidget::~AiWidget()
{
    for (auto &session : m_sessions) {
        delete session.timer;
    }
    m_sessions.clear();
}


void AiWidget::setupUi()
{
    // ---------- 主布局 ----------
    QGridLayout *mainLayout = new QGridLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);


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

    chkGroupChat     = new QCheckBox("群聊", tab1);
    chkGroupPersonal = new QCheckBox("群个人", tab1);
    chkPrivateChat   = new QCheckBox("私聊", tab1);
    chkNameTrigger   = new QCheckBox("名字触发", tab1);
    chkChannel       = new QCheckBox("频道", tab1);
    chkAtTrigger     = new QCheckBox("艾特触发", tab1);
    chkChannelPersonal= new QCheckBox("频道个人", tab1);
    chkImageRec      = new QCheckBox("启用识图", tab1);

    hboxChecks->addWidget(chkGroupChat);
    hboxChecks->addWidget(chkGroupPersonal);
    hboxChecks->addWidget(chkPrivateChat);
    hboxChecks->addWidget(chkChannel);
    hboxChecks->addWidget(chkChannelPersonal);
    hboxChecks->addWidget(chkNameTrigger);
    hboxChecks->addWidget(chkAtTrigger);
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
    lblPplx = new QLabel("匹配类型：", tab1);
    comboPplx = new QComboBox(tab1);
    comboPplx->addItems(QStringList() << "不匹配昵称" << "信息包含" << "信息头");

    gridDetails->addWidget(lblRobotName, 0, 0);
    gridDetails->addWidget(editRobotName, 0, 1);
    gridDetails->addWidget(lblModel, 0, 2);
    gridDetails->addWidget(comboModel, 0, 3);
    gridDetails->addWidget(lblSetting, 0, 4);
    gridDetails->addWidget(comboSetting, 0, 5);
    gridDetails->addWidget(lblPplx, 0, 6);
    gridDetails->addWidget(comboPplx, 0, 7);

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
    QFile file("data/roles.json");
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
void AiWidget::saveToFile1() const
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
    QFile file("data/roles.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}



void AiWidget::addtoui(const std::shared_ptr<AccountInfo> acc)
{
    editRobotName->setText(acc->Ai_nickname);
    comboModel->setCurrentText(acc->model);
    comboPplx->setCurrentIndex(acc->pplx);
    int idx = comboSetting->findText(acc->setting);
    comboSetting->setCurrentIndex(idx >= 0 ? idx : -1);
    editContext->setText(QString::number(acc->context_len));
    editNoReplySeconds->setText(QString::number(acc->nSecondsNoReply));
    editNoReplyMinutes->setText(QString::number(acc->nMinutesNoReply));
    editDelayReply->setText(QString::number(acc->delayReplySeconds));
    chkGroupChat->setChecked(acc->enableGroupChat);
    chkGroupPersonal->setChecked(acc->enableGroupPersonal);
    chkPrivateChat->setChecked(acc->enablePrivateChat);
    chkNameTrigger->setChecked(acc->nameTrigger);
    chkChannel->setChecked(acc->enableChannel);
    chkAtTrigger->setChecked(acc->atTrigger);
    chkChannelPersonal->setChecked(acc->enableChannelPersonal);
    chkImageRec->setChecked(acc->enableImageRec);



    m_currentRobotIndex = acc->appid_int;


}
void AiWidget::刷新模型()
{

    QString currentText = comboModel->currentText();  // 假设 comboModel 是 QComboBox*


    comboModel->clear();
    for (const auto &m : std::as_const(modelList))
    {
        comboModel->addItem(m.name);
    }
    int index = comboModel->findText(currentText);
    if (index != -1)
        comboModel->setCurrentIndex(index);

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


//列表被单击
void AiWidget::列表行被单击(QListWidgetItem *item)
{
    int currentRow = item->data(Qt::UserRole).toInt();
    if(currentRow == m_currentRobotIndex) return;
    for (const auto &acc : std::as_const(m_accounts))
    {
        if(acc->appid_int != currentRow) continue;

        addtoui(acc);
        m_currentRobotIndex= acc->appid_int;
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
        acc->pplx = comboPplx->currentIndex();
        acc->setting = comboSetting->currentText();
        acc->context_len = editContext->text().toInt();
        acc->nSecondsNoReply = editNoReplySeconds->text().toInt();
        acc->nMinutesNoReply = editNoReplyMinutes->text().toInt();
        acc->delayReplySeconds = editDelayReply->text().toInt();
        acc->enableGroupChat = chkGroupChat->isChecked();
        acc->enableGroupPersonal = chkGroupPersonal->isChecked();
        acc->enablePrivateChat = chkPrivateChat->isChecked();
        acc->nameTrigger = chkNameTrigger->isChecked();
        acc->enableChannel = chkChannel->isChecked();
        acc->atTrigger = chkAtTrigger->isChecked();
        acc->enableChannelPersonal = chkChannelPersonal->isChecked();
        acc->enableImageRec = chkImageRec->isChecked();
        accountPage->saveAccounts();
        return;
    }
    QMessageBox::warning(this,"保存失败","未找对对应 appid 机器人："+QString::number(m_currentRobotIndex));
}

// ====== 全局设定相关槽 ======

void AiWidget::on_settingListWidget_currentRowChanged(int currentRow)
{
    if(不加载) return;
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
            saveToFile1();
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
    saveToFile1();
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
    saveToFile1();
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
    刷新模型();
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
    刷新模型();
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
    刷新模型();
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


QString _tools(const QString &code,const QString &args,const MessageEvent &ev)
{
    Ai_Fun aifun;
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(args.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {

        return "调用函数时 参数错误\n";
    }
    QJsonObject obj=doc.object();

    //{"p2":"苹果","p1":"512x512"}
    aifun.p1=obj["p1"].toString();
    aifun.p2=obj["p2"].toString();
    aifun.p3=obj["p3"].toString();
    aifun.p4=obj["p4"].toString();
    aifun.p5=obj["p5"].toString();
    aifun.p6=obj["p6"].toString();
    aifun.p7=obj["p7"].toString();
    aifun.p8=obj["p8"].toString();


    py::gil_scoped_acquire gil;
    try {
        py::module_ qiancao = py::module_::import("qiancao_sdk");
        py::object api = qiancao.attr("QQApi")(g_keyuuid);
        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");
        exec_globals["msg"] = py::cast(ev);
        exec_globals["args"] = py::cast(aifun);
        exec_globals["api"] = api;
        py::exec(code.toStdString(), exec_globals);
        QString ret;
        if (exec_globals.contains("__result__"))
            ret = QString::fromStdString(py::str(exec_globals["__result__"]));

        return ret;
    } catch (const py::error_already_set &e) {
        return "[Python] Execute code error: " + QString::fromUtf8(e.what());
    } catch (const std::exception &e) {
        return "[Python] Execute code error: " + QString::fromUtf8(e.what());
    }
    return QString();

}
QJsonArray AiWidget::get_tools(AccountInfo *info)
{

    QJsonArray toolsArray;
    for (const auto &fun : std::as_const(functionList)) {
        if(fun.funcName.isEmpty()) continue;
        if(!info->tools.contains(fun.funcName)) continue;
        QJsonObject functionObj;
        functionObj["name"] = fun.funcName;
        functionObj["description"] = fun.remark;

        QJsonObject parameters;
        parameters["type"] = "object";
        QJsonObject properties;
        QJsonArray required;

        int paramIndex = 1;
        for (const QString &paramName : fun.params) {
            if (paramName.isEmpty()) {
                break;
            }
            QString pKey = QString("p%1").arg(paramIndex);
            QJsonObject paramDef;
            paramDef["type"] = "string";
            paramDef["description"] = paramName;
            properties[pKey] = paramDef;
            required.append(pKey);
            ++paramIndex;
        }

        if (!properties.isEmpty()) {
            parameters["properties"] = properties;
            parameters["required"] = required;
            functionObj["parameters"] = parameters;
        } else {
            functionObj["parameters"] = QJsonObject(); // 无参数时留空对象
        }

        QJsonObject toolObj;
        toolObj["type"] = "function";
        toolObj["function"] = functionObj;
        toolsArray.append(toolObj);
    }

    return toolsArray;

}


QString AiWidget::generateHash(const QString &url)
{
    return QString(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString AiWidget::downloadImage(const QString &url, const QString &hash)
{
    QString localPath = "tmp/image/" + hash + ".png";
    if (QFile::exists(localPath))
        return localPath;

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(url)));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(5000); // 5秒超时
    loop.exec();

    if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return QString();
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(data);
    file.close();
    return localPath;
}

PendingMessage AiWidget::parseImageTagsAndDownload(const QString &msg)
{
    PendingMessage result;
    QString text = msg;
    QRegularExpression re("\\[image,([^\\]]*)\\]");
    QRegularExpressionMatchIterator it = re.globalMatch(msg);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString tagContent = match.captured(1); // 如 "height=1080,width=1080,url=https://..."
        QString url;
        // 按逗号分割键值对
        QStringList pairs = tagContent.split(',', Qt::SkipEmptyParts);
        for (const QString &pair : std::as_const(pairs)) {
            int eqPos = pair.indexOf('=');
            if (eqPos != -1) {
                QString key = pair.left(eqPos).trimmed();
                QString value = pair.mid(eqPos + 1).trimmed();
                if (key == "url") {
                    url = value;
                    break;
                }
            }
        }
        if (!url.isEmpty()) {
            QString hash = generateHash(url);
            QString localPath = downloadImage(url, hash);
            if (!localPath.isEmpty()) {
                result.imagePaths.append(localPath);
                // 替换整个标签为 [image,path=本地路径]
                text.replace(match.captured(0), "[image,path=" + localPath + "]");
            } else {
                // 下载失败，保留错误标记（可保留原标签或替换为 [image,error]）
                text.replace(match.captured(0), "[image,error]");
            }
        } else {
            // 没有 url，保持原标签不变
        }
    }

    result.text = text.trimmed();
    return result;
}

void AiWidget::appendPendingMessageToContext(QJsonObject &context, const PendingMessage &pm)
{
    if (!context.contains("messages") || !context["messages"].isArray()) {
        context["messages"] = QJsonArray();
    }
    QJsonArray messages = context["messages"].toArray();

    QJsonObject msgObj;
    msgObj["role"] = "user";
    QJsonArray content;

    // 文本部分
    if (!pm.text.isEmpty()) {
        QJsonObject textItem;
        textItem["type"] = "text";
        textItem["text"] = pm.text;
        content.append(textItem);
    }

    // 图片部分（存本地路径）
    for (const QString &path : pm.imagePaths) {
        QJsonObject imageItem;
        imageItem["type"] = "image_url";
        QJsonObject imageUrlObj;
        imageUrlObj["url"] = path; // 绝对路径
        imageItem["image_url"] = imageUrlObj;
        content.append(imageItem);
    }

    msgObj["content"] = content;
    messages.append(msgObj);
    context["messages"] = messages;
}

void AiWidget::trimContextImages(QJsonObject &context, int maxUserMessages)
{
    if (!context.contains("messages") || !context["messages"].isArray())
        return;

    QJsonArray messages = context["messages"].toArray();
    QList<int> userMsgIndices;

    // 从后向前收集最近 maxUserMessages 条 user 消息的索引
    for (int i = messages.size() - 1; i >= 0; --i) {
        QJsonObject msg = messages[i].toObject();
        if (msg["role"].toString() == "user") {
            userMsgIndices.append(i);
            if (userMsgIndices.size() >= maxUserMessages)
                break;
        }
    }

    // 如果 user 消息不足 maxUserMessages，则全部保留
    if (userMsgIndices.isEmpty())
        return;

    // 将最近 maxUserMessages 条 user 消息的索引转为 QSet 便于快速查找
    QSet<int> keepIndices;
    for (int idx : userMsgIndices) {
        keepIndices.insert(idx);
    }

    // 遍历所有消息，如果该消息不是最近保留的 user 消息，则移除其 content 中的 image_url 元素
    for (int i = 0; i < messages.size(); ++i) {
        if (keepIndices.contains(i))
            continue; // 保留

        QJsonObject msg = messages[i].toObject();
        if (!msg.contains("content") || !msg["content"].isArray())
            continue;

        QJsonArray content = msg["content"].toArray();
        QJsonArray newContent;
        for (const QJsonValue &val : std::as_const(content)) {
            QJsonObject item = val.toObject();
            if (item["type"].toString() != "image_url") {
                newContent.append(item);
            }
        }
        if (newContent.size() != content.size()) {
            msg["content"] = newContent;
            messages[i] = msg;
            qDebug() << "已移除消息索引" << i << "中的图片（非最近3轮对话）";
        }
    }

    context["messages"] = messages;
}

void AiWidget::convertContextImagesToBase64(QJsonObject &context)
{
    if (!context.contains("messages") || !context["messages"].isArray())
        return;

    QJsonArray messages = context["messages"].toArray();
    for (int i = 0; i < messages.size(); ++i) {
        QJsonObject msg = messages[i].toObject();
        if (!msg.contains("content") || !msg["content"].isArray())
            continue;
        QJsonArray content = msg["content"].toArray();
        for (int j = 0; j < content.size(); ++j) {
            QJsonObject item = content[j].toObject();
            if (item["type"].toString() == "image_url" &&
                item.contains("image_url") && item["image_url"].isObject()) {
                QJsonObject imageUrlObj = item["image_url"].toObject();
                QString path = imageUrlObj["url"].toString();
                // 如果是本地路径（非 data: 开头）且文件存在
                if (!path.startsWith("data:") && QFile::exists(path)) {
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)) {
                        QByteArray fileData = file.readAll();
                        file.close();

                        // 判断是否为 GIF (GIF87a 或 GIF89a)
                        bool isGif = false;
                        if (fileData.size() > 6) {
                            QByteArray header = fileData.left(6);
                            if (header == "GIF87a" || header == "GIF89a") {
                                isGif = true;
                            }
                        }

                        QByteArray imageData;
                        QString mimeType;

                        if (isGif) {
                            // 提取第一帧并转为 PNG
                            QBuffer buffer(&fileData);
                            buffer.open(QIODevice::ReadOnly);
                            QImageReader reader(&buffer);
                            QImage firstFrame = reader.read();
                            if (!firstFrame.isNull()) {
                                QByteArray pngData;
                                QBuffer pngBuffer(&pngData);
                                pngBuffer.open(QIODevice::WriteOnly);
                                if (firstFrame.save(&pngBuffer, "PNG")) {
                                    imageData = pngData;
                                    mimeType = "image/png";
                                }
                            }
                            buffer.close();
                        }

                        // 如果不是 GIF 或转换失败，使用原始数据
                        if (imageData.isEmpty()) {
                            imageData = fileData;
                            // 根据扩展名猜测 MIME
                            if (path.endsWith(".png", Qt::CaseInsensitive))
                                mimeType = "image/png";
                            else if (path.endsWith(".gif", Qt::CaseInsensitive))
                                mimeType = "image/gif"; // 但实际不会走这里，因为上面已处理
                            else if (path.endsWith(".webp", Qt::CaseInsensitive))
                                mimeType = "image/webp";
                            else if (path.endsWith(".bmp", Qt::CaseInsensitive))
                                mimeType = "image/bmp";
                            else
                                mimeType = "image/jpeg"; // 默认
                        }

                        // 编码 base64
                        QString base64 = QString::fromLatin1(imageData.toBase64());
                        imageUrlObj["url"] = "data:" + mimeType + ";base64," + base64;
                        item["image_url"] = imageUrlObj;
                        content[j] = item;
                    }
                }
            }
        }
        msg["content"] = content;
        messages[i] = msg;
    }
    context["messages"] = messages;
}
// ========== 构建基础上下文（从数据库读取） ==========
QJsonObject AiWidget::buildBaseContext(AccountInfo* info, const QString& openid)
{
    QJsonObject context;
    QString sxw = aidb->get(openid);
    if (!sxw.isEmpty()) {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(sxw.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
            context = doc.object();
    }
    context["model"] = info->model;

    QJsonArray arr = get_tools(info);
    if (!arr.isEmpty())
        context["tools"] = arr;

    QString setting;
    for (const auto &sd : std::as_const(m_globalSettings)) {
        if (sd.name == info->setting) {
            setting = sd.content;
            break;
        }
    }

    QJsonArray msgs;
    if (context.contains("messages"))
        msgs = context["messages"].toArray();

    if (msgs.isEmpty()) {
        QJsonObject systemMsg;
        systemMsg["role"] = "system";
        systemMsg["content"] = setting;
        msgs.append(systemMsg);
    } else {
        QJsonObject first = msgs[0].toObject();
        if (first["role"].toString() != "system") {
            QJsonObject systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = setting;
            msgs.insert(0, systemMsg);
        } else {
            first["content"] = setting;
            msgs[0] = first;
        }
    }
    context["messages"] = msgs;
    return context;
}

// ========== 入口函数（只做检查，发射信号到主线程） ==========
QString AiWidget::Ai_post(AccountInfo *info, const MessageEvent &ev)
{
    // 清除记忆（保持原样）
    if (ev.msg == "清除记忆") {
        QString openid;
        switch (ev.type) {
        case 0: openid = info->enableGroupPersonal ? ev.groupId : ev.user; break;
        case 1: openid = info->enableChannelPersonal ? ev.groupId : ev.user; break;
        case 2: openid = ev.groupId; break;
        default: return "不支持Ai指令  请在 群 私聊 频道 发送本指令";
        }
        aidb->put(openid, "{}");
        return "清空记忆完成";
    }
    if(ev.type==0 && info->enableGroupChat){}
    else if(ev.type==1 && info->enableChannel){}
    else if(ev.type==2 && info->enablePrivateChat){}
    else return QString();

    if(info->atTrigger && ev.at_you){}
    else if(info->pplx == 1 && ev.msg.contains(info->Ai_nickname)){}
    else if(info->pplx == 2 && ev.msg.startsWith(info->Ai_nickname)){}
    else return QString();


    // 模型检查
    int index = -1;
    for (int i = 0; i < modelList.size(); ++i) {
        if (modelList[i].name == info->model) {
            index = i;
            break;
        }
    }
    if (index == -1)
        return "触发AI:" + info->Ai_nickname + " 但是设置的模型【" + info->model + "】 在模型列表不存在";
    if (modelList[index].enabledInterfaceIndices.isEmpty())
        return "触发AI:" + info->Ai_nickname + " 但是设置的模型【" + info->model + "】 未设置接口";

    // 发射信号到主线程处理（确保定时器安全）
    emit newMessageArrived(info, ev);
    return QString(); // 立即返回
}

// ========== 主线程处理消息（延迟合并） ==========
void AiWidget::onNewMessage(AccountInfo *info, MessageEvent ev)
{
    // 计算 openid
    QString openid;
    switch (ev.type) {
    case 0: openid = info->enableGroupPersonal ? ev.groupId : ev.user; break;
    case 1: openid = info->enableChannelPersonal ? ev.groupId : ev.user; break;
    case 2: openid = ev.groupId; break;
    default: return;
    }

    auto &session = m_sessions[openid];
    if (!session.timer) {
        session.timer = new QTimer(this);
        session.timer->setSingleShot(true);
        connect(session.timer, &QTimer::timeout, this, [this, openid]() {
            flushPendingMessages(openid);
        });
    }

    if(info->enableImageRec)
    {
        PendingMessage pm = parseImageTagsAndDownload(ev.msg);
        session.pendingMessages.append(pm);
    }else{
        PendingMessage pm;
        pm.text=ev.msg;
        pm.imagePaths.clear();
        session.pendingMessages.append(pm);
    }

    session.baseContext = buildBaseContext(info, openid);
    session.appid = ev.appid;
    session.type = ev.type;
    session.groupId = ev.groupId;
    session.msgId = ev.msgId;
    session.accountInfo = info;

    if (session.isProcessing) {
        return;
    }
    int delayMs = info->delayReplySeconds * 1000;
    if (delayMs <= 0) delayMs = 1000;
    session.timer->start(delayMs);
    session.dslx=0;
    qDebug() << "[AiWidget] 定时器已启动，延迟" << delayMs << "ms，openid:" << openid;
}

void AiWidget::trimContextByMessageCount(QJsonObject &context, int maxMessages)
{
    if (!context.contains("messages") || !context["messages"].isArray())
        return;

    QJsonArray msgs = context["messages"].toArray();
    if (msgs.size() <= 1)
        return;
    QJsonObject systemMsg;
    if (!msgs.isEmpty() && msgs[0].toObject()["role"].toString() == "system") {
        systemMsg = msgs[0].toObject();
    }
    QJsonArray nonSystem;
    for (int i = 0; i < msgs.size(); ++i) {
        if (i == 0 && !systemMsg.isEmpty()) continue;  // 跳过 system
        nonSystem.append(msgs[i]);
    }
    while (nonSystem.size() > maxMessages) {
        nonSystem.removeAt(0);   // 删除最早的一条
    }
    while (!nonSystem.isEmpty() && nonSystem[0].toObject()["role"].toString() != "user") {
        nonSystem.removeAt(0);
    }

    QJsonArray finalMsgs;
    if (!systemMsg.isEmpty())
        finalMsgs.append(systemMsg);
    for (const QJsonValue &val : std::as_const(nonSystem))
        finalMsgs.append(val);

    context["messages"] = finalMsgs;
}

void AiWidget::flushPendingMessages(const QString &openid)
{
    auto &session = m_sessions[openid];
    if (session.pendingMessages.isEmpty())
        return;

    for (const PendingMessage &pm : std::as_const(session.pendingMessages)) {
        appendPendingMessageToContext(session.baseContext, pm);
    }
    session.pendingMessages.clear();

    trimContextImages(session.baseContext, 6);
    if(session.accountInfo->context_len<5)
        session.accountInfo->context_len=5;
    trimContextByMessageCount(session.baseContext, session.accountInfo->context_len);
    QJsonObject baseContextCopy = session.baseContext;

    QJsonObject requestContext = session.baseContext;
    convertContextImagesToBase64(requestContext);

    int oldMsgCount = 0;
    if (baseContextCopy.contains("messages") && baseContextCopy["messages"].isArray()) {
        oldMsgCount = baseContextCopy["messages"].toArray().size();
    }

    session.isProcessing = true;

    // 构造空 MessageEvent
    MessageEvent ev;
    ev.appid = session.appid;
    ev.type = session.type;
    ev.groupId = session.groupId;
    ev.msgId = session.msgId;
    ev.msg = "";

    AccountInfo* info = session.accountInfo;
    if (!info) {
        session.isProcessing = false;
        return;
    }

    // 查找模型索引
    int model_index = -1;
    for (int i = 0; i < modelList.size(); ++i) {
        if (modelList[i].name == info->model) {
            model_index = i;
            break;
        }
    }
    if (model_index == -1) {
        session.isProcessing = false;
        return;
    }

    int timeoutMs = 30000;

    QThreadPool::globalInstance()->start([this, ev, model_index, requestContext, baseContextCopy, oldMsgCount, timeoutMs, openid]() {
        QJsonObject mutableContext = requestContext;
        int startIndex = m_modelStartIndex;
        QString reply = Ai_posts(ev, startIndex, model_index, mutableContext, timeoutMs);
        int newStartIndex = startIndex;

        // 传回 mutableContext（已含 AI 回复）和 oldMsgCount，以便主线程合并
        emit asyncReplyReceived(openid, reply, mutableContext, baseContextCopy, oldMsgCount, newStartIndex);
    });
}


void AiWidget::onAsyncReply(const QString &openid, const QString &reply,
                            const QJsonObject &updatedContext,      // mutableContext
                            const QJsonObject &originalContext,    // baseContextCopy
                            int oldMsgCount,
                            int newStartIndex)
{
    m_modelStartIndex = newStartIndex;

    auto it = m_sessions.find(openid);
    if (it == m_sessions.end())
        return;

    auto &session = *it;
    session.isProcessing = false;

    QThreadPool::globalInstance()->start([this, appid = session.appid, type = session.type,
                                          groupId = session.groupId, msgId = session.msgId,
                                          replyText = reply]() {
        if (m_botClients.contains(appid)) {
            QString text = replyText;
            QString pname = "[Ai系统：%1ms]";
            QString response =  m_botClients[appid]->send_messages(type, groupId, pname,text , msgId, false, false);
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError) {
                m_botClients[appid]->send_messages(type, groupId, pname,text , msgId, false, false);
            }else{
                QJsonObject obj = doc.object();
                QString id = obj["id"].toString();
                if(id.isEmpty())
                {
                    m_botClients[appid]->send_messages(type, groupId, pname,text , QString(), false, false);
                }
            }
        }
    });
    if (updatedContext.contains("messages") && updatedContext["messages"].isArray()) {
        QJsonArray newMsgs = updatedContext["messages"].toArray();
        QJsonArray baseMsgs = session.baseContext["messages"].toArray();
        if (newMsgs.size() > oldMsgCount) {

            for (int i = oldMsgCount; i < newMsgs.size(); ++i) {
                baseMsgs.append(newMsgs[i]);
            }
            session.baseContext["messages"] = baseMsgs;
        }
    }

    // ---- 保存上下文到数据库（使用 session.baseContext，含本地路径） ----
    QJsonObject savedContext = session.baseContext;
    if (savedContext.contains("tools"))
        savedContext.remove("tools");
    if (savedContext.contains("messages") && savedContext["messages"].isArray()) {
        QJsonArray msgs = savedContext["messages"].toArray();
        if (!msgs.isEmpty()) {
            QJsonObject first = msgs[0].toObject();
            if (first["role"].toString() == "system") {
                first["content"] = "";
                msgs[0] = first;
                savedContext["messages"] = msgs;
            }
        }
    }
    aidb->put(openid, QJsonDocument(savedContext).toJson(QJsonDocument::Compact));

    if (!session.pendingMessages.isEmpty()) {
        flushPendingMessages(openid);
    }else if(session.dslx==0 && session.accountInfo->nSecondsNoReply>0){
        session.dslx=1;
        PendingMessage pm;
        pm.imagePaths.clear();
        pm.text="[定时器]本条信息为Ai主动信息 用户在"+QString::number(session.accountInfo->nSecondsNoReply)+"秒内没找你对话触发 请无视本条信息 请参考上下文对话";
        session.pendingMessages.append(pm);
        session.timer->start(session.accountInfo->nSecondsNoReply*1000);
    }else if(session.dslx==1 && session.accountInfo->nMinutesNoReply>0){
        session.dslx=2;
        PendingMessage pm;
        pm.imagePaths.clear();
        pm.text="[定时器]本条信息为Ai主动信息 用户在"+QString::number(session.accountInfo->nSecondsNoReply)+"分钟内没找你对话触发 请无视本条信息 请参考上下文对话";
        session.pendingMessages.append(pm);
        session.timer->start(session.accountInfo->nMinutesNoReply*60*1000);
    }
}
QString AiWidget::Ai_post(const MessageEvent &ev, const QString &url, const QString &key, QJsonObject &sxw, QString &err, int timeoutMs)
{

    if (!ev.msg.isEmpty()) {
        if (sxw.contains("messages") && sxw["messages"].isArray()) {
            QJsonArray msgs = sxw["messages"].toArray();
            QJsonObject userMsg;
            userMsg["role"] = "user";
            userMsg["content"] = ev.msg;
            msgs.append(userMsg);
            sxw["messages"] = msgs;
        }
    }

    for(int i=0; i<3; ++i) {
        QJsonObject obj;
        for(int i2=0; i2<3; ++i2) {
            //qDebug() << "上下文：" << sxw;
            QByteArray response = Ai_post(url, key, sxw, timeoutMs);
            if(response.isEmpty()) {
                err += "接口返回空\n";
                return QString();
            }
            qDebug() << "AI返回：" << response;
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(response, &error);
            if (error.error != QJsonParseError::NoError) {
                err += "接口返回错误json:" + error.errorString()+"\n";
                if(err.contains(key)) err = subTextReplace(err, key, "...");
                return QString();
            }
            obj = doc.object();

            QJsonObject obj2 = obj["error"].toObject();
            QString error_mes = obj2["message"].toString();
            if(error_mes.contains("token")) {
                if (sxw.contains("messages") && sxw["messages"].isArray()) {
                    QJsonArray msgs = sxw["messages"].toArray();
                    if (msgs.size() > 1) {
                        msgs.removeAt(1);
                        sxw["messages"] = msgs;
                    }
                }
                continue;
            }
            break;
        }

        QJsonArray arr = obj["choices"].toArray();
        if (arr.isEmpty()) {
            err = "返回的 choices 为空\n";
            return QString();
        }
        QJsonObject obj2 = arr.at(0).toObject();
        QJsonObject obj3 = obj2["message"].toObject();
        QString text = obj3["content"].toString();
        const QJsonArray arr2 = obj3["tool_calls"].toArray();

        // 将 AI 响应加入上下文
        if (sxw.contains("messages") && sxw["messages"].isArray()) {
            QJsonArray msgs = sxw["messages"].toArray();
            msgs.append(obj3);
            sxw["messages"] = msgs;
        }

        bool ok = false;
        if (!arr2.isEmpty()) {
            //不传递appid 也不会传递 函数所以这里是调不到的
            if (!text.isEmpty() && m_botClients.contains(ev.appid)) {
                auto &bot = m_botClients[ev.appid];
                QString pname = "[Ai|%1ms]";
                bot->send_messages(ev.type, ev.groupId, pname, text, ev.msgId, false, false);
                text = QString();
            }

            for (const QJsonValue &value : arr2) {
                QJsonObject a = value.toObject();
                QJsonObject function = a["function"].toObject();
                QString tool_name = function["name"].toString();
                QString args = function["arguments"].toString();
                QString callID = a["id"].toString();

                for (const auto &fun : std::as_const(functionList)) {
                    if (fun.funcName != tool_name) continue;
                    QString data = _tools(fun.code, args, ev);
                    if (!data.isEmpty()) {
                        if (sxw.contains("messages") && sxw["messages"].isArray()) {
                            QJsonArray msgs = sxw["messages"].toArray();
                            QJsonObject toolMsg;
                            toolMsg["role"] = "tool";
                            toolMsg["content"] = data;
                            toolMsg["tool_call_id"] = callID;
                            toolMsg["name"] = tool_name;
                            msgs.append(toolMsg);
                            sxw["messages"] = msgs;
                        }
                        ok = true;
                    }
                    break;
                }
            }
            if (ok) continue;
        }
        return text;
    }
    return QString();
}
//============
QString AiWidget::Ai_post(const QString &model,const QString &msg,int timeoutMs)
{
    int index=-1;
    for (int i =0;i<modelList.size();++i)
    {
        if(modelList[i].name==model)
        {
            index=i;
            break;
        }
    }
    if(index==-1) return "【"+model+"】 在模型列表不存在 请配置模型后试试";
    if(modelList[index].enabledInterfaceIndices.isEmpty()) return "【"+model+"】 未设置接口 请配置接口后试试";
    QJsonObject obj;
    obj["model"]=model;
    QJsonArray msgs;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = msg;
    msgs.append(userMsg);
    obj["messages"] = msgs;
    int index2=0;
    if(timeoutMs<=0) timeoutMs=30000;
    if(timeoutMs<=5000) timeoutMs=5000;
    return Ai_posts(MessageEvent(),index2,index,obj,timeoutMs);
}

QString AiWidget::Ai_posts(const MessageEvent &ev,int &模型开始下标,int model_index,QJsonObject &sxw,int timeoutMs) //内部使用请勿公开
{
    QString err;
    int kswz = modelList[model_index].enabledInterfaceIndices.size();
    for(int i = 0;i<kswz;++i)//接口循环
    {
        //模型开始下标 原子+1
        int jk= 模型开始下标 % modelList[model_index].enabledInterfaceIndices.size();//实时获取
        模型开始下标++;
        auto &key = globalInterfaces[jk].keys;
        int len = key.size();
        for(int i2=0;i2< len;++i2)
        {
            int index = globalInterfaces[jk].key_index++;
            index = index % len;
            QString text =  Ai_post(ev,globalInterfaces[jk].url,key[index].key,sxw,err,timeoutMs);
            if(text.isEmpty()) continue;
            return text;
        }

    }
    return err;
}


QByteArray AiWidget::Ai_post(const QString &url,const QString &key, QJsonObject &sxw,int timeoutMs)
{

    QByteArray jsonData = QJsonDocument(sxw).toJson(QJsonDocument::Compact);
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", QString("Bearer " + key).toUtf8());
    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.post(request, jsonData);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();   // 阻塞直到请求完成或超时
    QByteArray response= reply->readAll();
    reply->deleteLater();
    return response;
}



