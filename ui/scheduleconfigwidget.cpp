#include "ScheduleConfigWidget.h"
#include "global.h"  // extern QList<AccountInfo*> m_accounts;

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>



// ---------- ScheduleConfigWidget ----------
ScheduleConfigWidget::ScheduleConfigWidget(QWidget *parent)
    : QWidget(parent)
{

    setupUI();
    initTable();

    loadAllTasksFromFile();
}

ScheduleConfigWidget::~ScheduleConfigWidget()
{
}

void ScheduleConfigWidget::setupUI()
{
    mainSplitter = new QSplitter(Qt::Horizontal, this);

    // ---------- 左侧：定时任务表格 ----------
    QWidget *middleWidget = new QWidget;
    middleWidget->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *middleLayout = new QVBoxLayout(middleWidget);
    middleLayout->setContentsMargins(0, 0, 0, 0);

    taskTable = new QTableWidget;
    taskTable->setAlternatingRowColors(true);
    taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    taskTable->setSelectionMode(QAbstractItemView::SingleSelection);
    taskTable->setStyleSheet(
        "QTableWidget::item:selected { background-color: #cce8cf; color: #1e3c2c; }"
        );
    middleLayout->addWidget(taskTable);

    // ---------- 右侧：详细信息面板 ----------
    detailPanel = new QWidget;
    detailPanel->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);

    QGroupBox *detailGroup = new QGroupBox("定时详细信息");
    detailGroup->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout *groupLayout = new QVBoxLayout(detailGroup);
    groupLayout->setSpacing(6);

    // ---------- 1. 创建所有控件 ----------
    scheduleTimeEdit = new QLineEdit;
    scheduleTimeEdit->setPlaceholderText("规则 年,月,日,时,分|分割 如:8,10|-3,10 -3代表 每隔3小时发一次 -4 -5同理");

    executeCountSpin = new QSpinBox;
    executeCountSpin->setRange(-1, 2147483000);
    executeCountSpin->setSpecialValueText("无限次");
    executeCountSpin->setValue(-1);

    executeTypeCombo = new QComboBox;
    executeTypeCombo->addItems({"每个群执行一次", "只执行一次"});

    triggerTypeCombo = new QComboBox;
    triggerTypeCombo->addItems({"开发者", "管理员", "所有人"});

    executedCountLabel = new QLabel("0");

    replyContentEdit = new QTextEdit;
    replyContentEdit->setPlaceholderText(R"(#python 不带这行就是普通信息 注意这里只有appid有值
api.outlog(f"收到来自 {msg.appid} 的消息")
__result__ = f"收到来自 {msg.appid} 的消息"

#msg.msg #你添加到数据库的 内容 使用|||分割 注意 触发类型必须是"每个群执行一次" 这个保留才他值
#msg.groupid #注意 触发类型必须是"每个群执行一次" 这个保留才他值
#![#100px #100px](路径) #100px是设置高宽可不传 路径可以是链接
#[audio,path=路径] [video,path=路径] [file,path=路径]
#蓝字：蓝字请使用 []() 如 [测试](你点击了蓝字)
#同时也是支持 [链接](https://example.com)
#按钮挂载：#b:#按钮json#b:# "#b:#"包起来即可
)");
    replyContentEdit->setMinimumHeight(200);

    addSubscribeEdit = new QLineEdit;
    addSubscribeEdit->setPlaceholderText("订阅测试");
    cancelSubscribeEdit = new QLineEdit;
    cancelSubscribeEdit->setPlaceholderText("取消订阅测试");

    addSubscribeReplyEdit = new QTextEdit;
    addSubscribeReplyEdit->setPlaceholderText(R"(只支持pyhton代码下面例子
__result__="添加订阅成功 在特定时间将会推送 订阅内容 记得打开主动功能"
__ok__="1"#返回1代表成功
__data__="要添加到数据库内容" #使用|||分割添加多个
)");
    cancelSubscribeReplyEdit = new QTextEdit;
    cancelSubscribeReplyEdit->setPlaceholderText(R"(只支持pyhton代码下面例子
__result__="取消订阅成功"
__ok__="1" #返回1代表成功
__data__="要从数据库删除的内容" #使用|||分割删除多个
#__ok__ 为1时 __data__="" 返回空将清空所有数据
)");

    updateTaskBtn = new QPushButton("更新当前任务");

    // ---------- 2. 组装布局到右侧面板 ----------

    // 第一行：定时时间
    QHBoxLayout *rowTimeLayout = new QHBoxLayout;
    rowTimeLayout->addWidget(new QLabel("定时时间:"));
    rowTimeLayout->addWidget(scheduleTimeEdit);
    groupLayout->addLayout(rowTimeLayout);

    // 【核心修改 1】：将红框三行整合进一个 QGridLayout
    QGridLayout *gridLayout = new QGridLayout;
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setVerticalSpacing(6); // 行间距
    gridLayout->setHorizontalSpacing(10); // 列间距

    // 第 1 行：执行次数
    gridLayout->addWidget(new QLabel("执行次数:"), 0, 0, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
    gridLayout->addWidget(executeCountSpin, 0, 1);
    QLabel *la = new QLabel("已执行次数:");
    la->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gridLayout->addWidget(la, 0, 2);
    gridLayout->addWidget(executedCountLabel, 0, 3, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);

    // 第 2 行：执行类型 + 触发类型
    gridLayout->addWidget(new QLabel("执行类型:"), 1, 0, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
    gridLayout->addWidget(executeTypeCombo, 1, 1);
    QLabel *la2 = new QLabel("触发类型:");
    la2->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gridLayout->addWidget(la2, 1, 2);
    gridLayout->addWidget(triggerTypeCombo, 1, 3);

    // 第 3 行：添加/取消订阅指令
    gridLayout->addWidget(new QLabel("添加订阅指令:"), 2, 0, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
    gridLayout->addWidget(addSubscribeEdit, 2, 1);
    gridLayout->addWidget(new QLabel("取消订阅指令:"), 2, 2, 1, 1, Qt::AlignRight | Qt::AlignVCenter);
    gridLayout->addWidget(cancelSubscribeEdit, 2, 3);

    // 重要：设置列伸缩，让第1列和第3列的输入框自动平分填满宽度
    gridLayout->setColumnStretch(0, 0);
    gridLayout->setColumnStretch(1, 1);
    gridLayout->setColumnStretch(2, 0);
    gridLayout->setColumnStretch(3, 1);

    // 把这个网格布局加进主垂直布局中
    groupLayout->addLayout(gridLayout);


    // 第五行：订阅指令回复框 (两个并排)
    QWidget *subscribeWidget2 = new QWidget;
    QHBoxLayout *subscribeLayout2 = new QHBoxLayout(subscribeWidget2);
    subscribeLayout2->setContentsMargins(0, 0, 0, 0);
    subscribeLayout2->addWidget(addSubscribeReplyEdit);
    subscribeLayout2->addWidget(cancelSubscribeReplyEdit);
    groupLayout->addWidget(subscribeWidget2);

    // 第六行：定时回复内容
    QVBoxLayout *replyWrapLayout = new QVBoxLayout;
    replyWrapLayout->addWidget(replyContentEdit);
    groupLayout->addLayout(replyWrapLayout);

    // 第七行：更新按钮
    groupLayout->addWidget(updateTaskBtn);

    detailLayout->addWidget(detailGroup);

    // ---------- 3. 添加到分割器和底部按钮 ----------
    mainSplitter->addWidget(middleWidget);
    mainSplitter->addWidget(detailPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);

    QHBoxLayout *bottomLayout = new QHBoxLayout;
    addBtn = new QPushButton("添加");
    deleteBtn = new QPushButton("删除");
    copyRowBtn = new QPushButton("复制");
    copyAllBtn = new QPushButton("复制全部");
    pasteBtn = new QPushButton("粘贴");
    saveBtn = new QPushButton("保存配置");

    bottomLayout->addWidget(addBtn);
    bottomLayout->addWidget(deleteBtn);
    bottomLayout->addWidget(copyRowBtn);
    bottomLayout->addWidget(copyAllBtn);
    bottomLayout->addWidget(pasteBtn);
    bottomLayout->addStretch();
    bottomLayout->addWidget(updateTaskBtn);
    bottomLayout->addWidget(saveBtn);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(mainSplitter);
    mainLayout->addLayout(bottomLayout);
    setLayout(mainLayout);

    // ---------- 4. 信号槽连接 ----------
    // 如果需要恢复监听选中行，解开下面这行注释：
     connect(taskTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
         onTableRowSelected(row);
     });
    connect(taskTable, &QTableWidget::itemChanged, this, &ScheduleConfigWidget::onTableItemChanged);


    connect(addBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onDeleteRow);
    connect(copyRowBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onCopyRow);
    connect(copyAllBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onCopyAllRows);
    connect(pasteBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onPasteFromClipboard);
    connect(updateTaskBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onUpdateTask);
}
void ScheduleConfigWidget::onTableItemChanged(QTableWidgetItem *item)
{
    if (!item || g_appid == 0 || !tasksMap.contains(g_appid))
        return;

    int row = item->row();
    int col = item->column();
    if (row < 0 || row >= taskTable->rowCount() || col != COL_ENABLED_REMARK)
        return;

    QList<ScheduleTask> &tasks = tasksMap [g_appid];
    if (row >= tasks.size())
        return;

    ScheduleTask &task = tasks[row];
    task.enabled = (item->checkState() == Qt::Checked);
    task.remark = item->text();

    // 立即写入数据库
    if (dsdb) {
        QString key = QString("task_%1_%2").arg(g_appid).arg(task.mark);
        QByteArray jsonData = QJsonDocument(task.toJson()).toJson(QJsonDocument::Indented);
        if (!dsdb->put(key, jsonData)) {
            qWarning() << "Failed to update task" << key << "to LMDB";
        }
    }
}
void ScheduleConfigWidget::initTable()
{
    // 只有一列：启用+备注
    QStringList headers = {"启用 / 备注"};
    taskTable->setColumnCount(1);
    taskTable->setHorizontalHeaderLabels(headers);
    taskTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    taskTable->verticalHeader()->setVisible(true);
}



void ScheduleConfigWidget::列表行被单击()
{


    if (g_appid!=0) {

        loadTasksForRobot(g_appid);
    } else {

        taskTable->setRowCount(0);
        clearDetailPanel();
    }
}

void ScheduleConfigWidget::clearDetailPanel()
{
    scheduleTimeEdit->clear();
    executeCountSpin->setValue(-1);
    executeTypeCombo->setCurrentIndex(0);
    replyContentEdit->clear();
    addSubscribeEdit->clear();
    cancelSubscribeEdit->clear();
}


void ScheduleConfigWidget::loadTasksForRobot(int appid)
{

    bool wasBlocked = taskTable->blockSignals(true);
    const QList<ScheduleTask> &tasks = tasksMap[appid];

    taskTable->setRowCount(tasks.size());
    for (int i = 0; i < tasks.size(); ++i) {
        setTaskToRow(i, tasks[i]);
    }
    taskTable->blockSignals(wasBlocked);

    // 选中第一行
    if (taskTable->rowCount() > 0) {
        taskTable->selectRow(0);
        onTableRowSelected(0);
    } else {
        clearDetailPanel();
    }
}

void ScheduleConfigWidget::setTaskToRow(int row, const ScheduleTask &task)
{
    while (row >= taskTable->rowCount())
        taskTable->insertRow(taskTable->rowCount());

    QTableWidgetItem *item = taskTable->item(row, COL_ENABLED_REMARK);
    if (!item) {
        item = new QTableWidgetItem;
        taskTable->setItem(row, COL_ENABLED_REMARK, item);
    }
    item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    item->setCheckState(task.enabled ? Qt::Checked : Qt::Unchecked);
    item->setText(task.remark);
}

const ScheduleTask& ScheduleConfigWidget::getTaskFromRow(int row) const
{

    if (g_appid != 0 && tasksMap.contains(g_appid)) {
        const auto &tasks = tasksMap[g_appid];
        if (row < tasks.size()) {
            return tasks[row];
        }
    }
    static const ScheduleTask EMPTY; // 返回一个静态空对象
    return EMPTY;
}

void ScheduleConfigWidget::addRowFromTask(const ScheduleTask &task)
{
    int row = taskTable->rowCount();
    taskTable->insertRow(row);
    setTaskToRow(row, task);
    if (g_appid != 0) {
        if (!tasksMap.contains(g_appid))
            tasksMap[g_appid] = QList<ScheduleTask>();
        tasksMap[g_appid].append(task);
    }
    taskTable->selectRow(row);
}

void ScheduleConfigWidget::onTableRowSelected(int row)
{
    if (row < 0 || g_appid == 0) {
        clearDetailPanel();
        return;
    }

    updateDetailPanelFromTask(getTaskFromRow(row));
}

void ScheduleConfigWidget::updateDetailPanelFromTask(const ScheduleTask &task)
{

    scheduleTimeEdit->setText(task.TimetoString());
    executeCountSpin->setValue(task.executeCount);
    int idx = executeTypeCombo->findText(task.executeType==0 ? "每个群执行一次":"只执行一次");
    if (idx < 0) idx = 0;
    executeTypeCombo->setCurrentIndex(idx);
    replyContentEdit->setPlainText(task.replyContent);
    addSubscribeEdit->setText(task.addSubscribeCmd);
    cancelSubscribeEdit->setText(task.cancelSubscribeCmd);
    addSubscribeReplyEdit->setText(task.addSubscribe_text);
    cancelSubscribeReplyEdit->setText(task.cancelSubscribe_text);
    triggerTypeCombo->setCurrentIndex(task.role);
    executedCountLabel->setText(QString::number(task.jis));
    if (task.executeCount != -1 && task.jis + 10 >= task.executeCount) {
        executedCountLabel->setStyleSheet("color: red;");
    } else {
        executedCountLabel->setStyleSheet("");
    }
}

void ScheduleConfigWidget::applyDetailPanelToTask(ScheduleTask &task)
{
    QString time = scheduleTimeEdit->text();
    task.scheduleTime.clear();
    task.StringToTime(time);
    task.executeCount = executeCountSpin->value();
    task.executeType = executeTypeCombo->currentIndex();
    task.replyContent = replyContentEdit->toPlainText();
    task.addSubscribeCmd = addSubscribeEdit->text();
    task.cancelSubscribeCmd = cancelSubscribeEdit->text();
    task.addSubscribe_text = addSubscribeReplyEdit->toPlainText();
    task.cancelSubscribe_text = cancelSubscribeReplyEdit->toPlainText();
    task.role = triggerTypeCombo->currentIndex();
}

void ScheduleConfigWidget::onUpdateTask()
{
    int row = taskTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要更新的任务行");
        return;
    }
    if (g_appid == 0 || !tasksMap.contains(g_appid)) return;
    QList<ScheduleTask> &tasks = tasksMap[g_appid];
    if (row >= tasks.size()) return;

    // 更新完整任务
    applyDetailPanelToTask(tasks[row]);
    // 同步表格中的启用和备注（可能被修改过，但此处以面板为准？我们保持面板更新不改变启用备注，但用户可能在表格中直接修改）
    // 为了安全，我们将表格中的启用和备注也同步，但面板中没有这两个字段，所以保留表格现有值。
    // 因此我们只更新除启用备注外的其他字段。
    // 但更好的做法是：先保存表格的启用备注，然后重新设置行。
    bool wasBlocked = taskTable->blockSignals(true);
    setTaskToRow(row, tasks[row]); // 刷新显示

    QString key = QString("task_%1_%2").arg(g_appid).arg(tasks[row].mark);
    QByteArray jsonData = QJsonDocument(tasks[row].toJson()).toJson(QJsonDocument::Indented);
    dsdb->put(key, jsonData);

    taskTable->blockSignals(wasBlocked);
    taskTable->selectRow(row);
    //QMessageBox::information(this, "提示", "任务已更新");
}

void ScheduleConfigWidget::onAddRow()
{
    if(g_appid==0) return ;
    ScheduleTask newTask;
    newTask.enabled = true;
    newTask.remark = "新定时任务";
    int mark = 0;
    QSet<int> usedMarks;
    if (tasksMap.contains(g_appid)) {
        for (const ScheduleTask &t : tasksMap[g_appid]) {
            usedMarks.insert(t.mark);
        }
    }
    // 最多尝试 100 次
    for (int i = 0; i < 100; ++i) {
        quint64 now = QDateTime::currentMSecsSinceEpoch();
        int candidate = static_cast<int>(now & 0x7FFFFFFF) + (rand() % 10000);
        if (!usedMarks.contains(candidate)) {
            mark = candidate;
            break;
        }
        // 如果冲突，等待 1ms 再试（避免连续碰撞）
        QThread::msleep(1);
    }
    if (mark == 0) {
        // 极端情况：仍然冲突，用时间戳+循环递增
        quint64 base = QDateTime::currentMSecsSinceEpoch();
        for (int offset = 0; offset < 1000; ++offset) {
            int candidate = static_cast<int>((base + offset) & 0x7FFFFFFF);
            if (!usedMarks.contains(candidate)) {
                mark = candidate;
                break;
            }
        }
    }
    newTask.mark=mark;
    if(newTask.mark<=0)
    {
        QMessageBox::warning(this,"添加失败","没有找到选中的机器人 请刷新列表重新选择机器人");
        return;
    }
    newTask.scheduleTime.clear();
    newTask.executeCount = -1;
    newTask.executeType = 0;
    newTask.addSubscribeCmd = "";
    newTask.cancelSubscribeCmd = "";
    newTask.replyContent = R"(#python 不带这行就是普通信息 注意这里只有appid有值
api.outlog(f"收到来自 {msg.appid} 的消息")
__result__ = f"收到来自 {msg.appid} 的消息"

#msg.msg #你添加到数据库的 内容 使用|||分割 注意 触发类型必须是"每个群执行一次" 这个保留才他值
#msg.groupid #注意 触发类型必须是"每个群执行一次" 这个保留才他值
#![#100px #100px](路径) #100px是设置高宽可不传 路径可以是链接
#[audio,path=路径] [video,path=路径] [file,path=路径]
#蓝字：蓝字请使用 []() 如 [测试](你点击了蓝字)
#同时也是支持 [链接](https://example.com)
#按钮挂载：#b:#按钮json#b:# "#b:#"包起来即可)";


    newTask.addSubscribe_text=R"(#只支持pyhton代码下面例子
__result__="添加订阅成功 在特定时间将会推送 订阅内容 记得打开主动功能"
__ok__="1"  #返回1代表成功
__data__="要添加到数据库内容" #使用|||分割添加多个
)";
    newTask.cancelSubscribe_text= R"(只支持pyhton代码下面例子
__result__="取消订阅成功"
__ok__="1"  #返回1代表成功
__data__="要从数据库删除的内容"  #使用|||分割删除多个
#__ok__ 为1时 __data__="" 返回空将清空所有数据
)";


    if (!tasksMap.contains(g_appid))
        tasksMap[g_appid] = QList<ScheduleTask>();
    bool shua_xin =tasksMap[g_appid].size()!=taskTable->rowCount();
    tasksMap[g_appid].append(newTask);

    QString key = QString("task_%1_%2").arg(g_appid).arg(newTask.mark);
    QByteArray jsonData = QJsonDocument(newTask.toJson()).toJson(QJsonDocument::Indented);
    dsdb->put(key, jsonData);
    if(shua_xin) loadTasksForRobot(g_appid);
}

void ScheduleConfigWidget::onDeleteRow()
{
    int row = taskTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要删除的行");
        return;
    }
    if (g_appid == 0 || !tasksMap.contains(g_appid)) return;

    if(tasksMap[g_appid].size()!=taskTable->rowCount())//在下面必定弹窗前增加
    {
        loadTasksForRobot(g_appid);
        QMessageBox::warning(this,"失败","检查到 ui变量 和 内存变量数据不一致 请重新刷新列表"
                                           "\n为什么会这样子 因为内部定时自动删除类变量 在定时任务完成时 被删除了"
                                           "\n目前来说强制删除会删错误数据或者报错\n现在为你重新加载列表 如果你还找得到就可以删");
        return;
    }
    if(QMessageBox::warning(this,"确认删除","确认删除吗？ 删除将清空订阅的使用群数据",QMessageBox::Yes | QMessageBox::No)!=QMessageBox::Yes) return;

    if(tasksMap[g_appid].size()!=taskTable->rowCount())//在下面必定弹窗前增加
    {
        loadTasksForRobot(g_appid);
        QMessageBox::warning(this,"失败","检查到 ui变量 和 内存变量数据不一致 请重新刷新列表"
                                           "\n为什么会这样子 因为内部定时自动删除类变量 在定时任务完成时 被删除了"
                                           "\n目前来说强制删除会删错误数据或者报错\n现在为你重新加载列表 如果你还找得到就可以删");
        return;
    }



    QList<ScheduleTask> &t = tasksMap [g_appid];
    if(!g_botdb.contains(g_appid))
    {
        BotDB *client = new BotDB(QString("botdb/%1_db").arg(g_appid));
        client->open();
        g_botdb[g_appid] = client;
    }

    int mark = tasksMap[g_appid][row].mark;
    auto *db =g_botdb[g_appid];
    db->clearSubscriptionsByMark(QString("t_%1_%2").arg(g_appid).arg(t[row].mark));
    taskTable->removeRow(row);
    tasksMap[g_appid].removeAt(row);


    QString key = QString("task_%1_%2").arg(g_appid).arg(mark);
    dsdb->remove(key);

    if (taskTable->rowCount() > 0) {
        int newRow = qMin(row, taskTable->rowCount() - 1);
        taskTable->selectRow(newRow);
        onTableRowSelected(newRow);
    } else {
        clearDetailPanel();
    }
}

void ScheduleConfigWidget::onCopyRow()
{
    int row = taskTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要复制的行");
        return;
    }
    const ScheduleTask task = getTaskFromRow(row);
    // TSV格式：启用(1/0), 备注, 定时时间, 执行次数, 执行类型, 回复内容(可能含换行，用转义), 添加订阅, 取消订阅
    // 对于多行内容，我们用\n转义，或者用Base64？为简单，用特殊分隔符，但这里推荐使用JSON格式复制，但用户习惯TSV，我们用制表符分隔，但回复内容可能包含制表符和换行，所以建议用JSON或者Base64。
    // 为了简单，我们采用JSON单行格式，但之前代码用TSV，我们保持TSV，但将回复内容中的换行替换为\\n，制表符替换为\\t，防止破坏格式。
    QString escapedReply = task.replyContent;
    escapedReply.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t");

    QString line = QString("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t&10")
                       .arg(task.enabled ? "1" : "0",task.remark,task.TimetoString())
                       .arg(task.executeCount)
                       .arg(task.executeType)
                       .arg(escapedReply,task.addSubscribeCmd,task.cancelSubscribeCmd,task.addSubscribe_text,task.cancelSubscribe_text);

    QApplication::clipboard()->setText(line);
    QMessageBox::information(this, "提示", "已复制当前任务到剪贴板");
}

void ScheduleConfigWidget::onCopyAllRows()
{
    if (g_appid == 0 || !tasksMap.contains(g_appid)) {
        QMessageBox::information(this, "提示", "没有可复制的内容");
        return;
    }
    const auto &tasks = tasksMap[g_appid];
    QStringList lines;
    for (const ScheduleTask &task : tasks) {
        QString escapedReply = task.replyContent;
        escapedReply.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t");

        lines << QString("%1\t%2\t%3\t%4\t%5\t%6\t%7\t%8\t%9\t&10")
                     .arg(task.enabled ? "1" : "0",task.remark,task.TimetoString())
                     .arg(task.executeCount)
                     .arg(task.executeType)
                     .arg(escapedReply,task.addSubscribeCmd,task.cancelSubscribeCmd,task.addSubscribe_text,task.cancelSubscribe_text);

    }
    QApplication::clipboard()->setText(lines.join("\n"));
    QMessageBox::information(this, "提示", QString("已复制 %1 个任务").arg(lines.size()));
}

void ScheduleConfigWidget::onPasteFromClipboard()
{
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        QMessageBox::information(this, "提示", "剪贴板为空");
        return;
    }
    addRowsFromTSV(text);
}
int accinfo(int appid);
void ___dsrw(){
    QDateTime now = QDateTime::currentDateTime();
    const QTime time = now.time();
    const QDate date = now.date();


    for (auto it = schedule->tasksMap.begin(); it != schedule->tasksMap.end(); ++it) {
        int appid = it.key();
        bool ok = false;
        for (auto &acc : m_accounts) {
            if(acc->appid_int!=appid) continue;
            if(!acc->online) continue;
            ok=true;
            break;   // 这里建议加上 break，既然找到了就没必要继续循环了
        }
        if(!ok) continue;
        if(!m_botClients.contains(appid)) continue;
        if(!g_botdb.contains(appid)) continue;
        QList<ScheduleTask> &list = it.value();
        for(auto &t : list) {
            if(!t.enabled) continue;
            for (const TimeRule &rule : std::as_const(t.scheduleTime)) {
                if (!rule.matches(date, time)) continue;

                if(t.jis>t.executeCount && t.executeCount!=-1) break;
                t.jis++;
                if(t.zdsc)
                {
                    if(t.replyContent.startsWith("code_ai|||"))
                    {
                        int index =accinfo(appid);
                        if(index !=-1)
                        {
                            QMetaObject::invokeMethod(ai_ui, [index, replyContent = t.replyContent]() {

                                MessageEvent ev;
                                QStringList list = replyContent.split("|||");
                                if(list.size()<5) return ;
                                AccountInfo *info = m_accounts[index].get();
                                ev.appid = info->appid_int;
                                ev.user =list[1];
                                ev.groupId =list[2];
                                ev.msg =list[3];
                                ev.type = list[4].toInt();
                                ai_ui->onNewMessage(info, ev,false,true);
                            }, Qt::QueuedConnection);
                        }
                        return;
                    }
                }


                QString text =  "[订阅推送:" + t.remark + "]";
                auto *db = g_botdb[appid];
                QStringList fullList = db->listSubscriptions(QString("t_%1_%2").arg(appid).arg(t.mark));
                AppendEventLog(text+" 群数："+QString::number(fullList.size()),0x35E496);
                const int chunkSize = 50;
                for (int i = 0; i < fullList.size(); i += chunkSize) {
                    QStringList subList = fullList.mid(i, chunkSize);
                    auto *task = new api_dsrw(appid, subList, t.replyContent,text, t.executeType,t.mark);
                    QThreadPool::globalInstance()->start(task);
                }
            }
        }
    }
}

int 计数器=0;
class ___dtrw : public QRunnable {
public:
    // 通过构造函数把需要的数据传进来（如果有的话）
    ___dtrw() {}

    void run() override {
        计数器++;
        ___dsrw();
        计数器--;
        if(计数器<0) 计数器=0;//1分钟才会启动一次的线程
    }
};
void ScheduleConfigWidget::jiancha()
{
    QDateTime now = QDateTime::currentDateTime();

    const QTime time = now.time();
    int dqsj=time.minute();
    if (定时检查变量 == dqsj) return;
    定时检查变量 = dqsj;
    if(计数器==0)
    {
        //for (auto &tasks : schedule->tasksMap)   // tasks 是一个任务容器（如 vector/list）
        for (auto tasks = schedule->tasksMap.begin(); tasks != schedule->tasksMap.end(); )  // 改用迭代器
        {
            int appid = tasks.key();
            for (auto it = tasks->begin(); it != tasks->end(); )  // 改用迭代器
            {
                auto &s = *it;
                bool ok=true;
                if (s.zdsc) // 允许自动删除
                {
                    for(auto & time : s.scheduleTime) //时间大于显示时间
                    {
                        if(!time.isExpired(now))
                        {
                            ok=false;
                            break;
                        }
                    }
                    if(ok)
                    {
                        dsdb->remove(QString("t_%1_%2").arg(appid).arg(it->mark));
                        it = tasks->erase(it);

                        continue;
                    }
                }

                if (s.executeCount == -1)   // 设定值为-1，跳过
                {
                    ++it;
                    continue;
                }
                if (s.executeCount > s.jis) // 设定值 >= 实际次数，未超标，跳过
                {
                    ++it;
                    continue;
                }
                if (s.zdsc)  // 如果允许自动删除
                {
                    dsdb->remove(QString("t_%1_%2").arg(appid).arg(it->mark));
                    it = tasks->erase(it);

                }
                else
                {
                    s.enabled = false;  // 不允许删除，则禁用
                    ++it;
                }
            }
            ++tasks;
        }
    }
    auto *task = new ___dtrw();
    QThreadPool::globalInstance()->start(task);
}

QString ScheduleConfigWidget::add_byAi(const QString &remark, int appid, const QString &时间, int 执行次数, const QString &python_code)
{
    // 1. 解析定时时间
    ScheduleTask newTask;
    newTask.StringToTime(时间);
    if (newTask.scheduleTime.isEmpty())
        return "定时时间解析失败，请确认格式正确（年,月,日,时,分|||...）";
    if (python_code.isEmpty())
        return "定时参数 定时内容 不能为空";
    if (remark.isEmpty())
        return "备注不能为空";
    int mark = 0;
    QSet<int> usedMarks;
    if (tasksMap.contains(appid)) {
        for (const ScheduleTask &t : tasksMap[appid]) {
            usedMarks.insert(t.mark);
        }
    }
    for (int i = 0; i < 100; ++i) {
        quint64 now = QDateTime::currentMSecsSinceEpoch();
        int candidate = static_cast<int>(now & 0x7FFFFFFF) + (rand() % 10000);
        if (!usedMarks.contains(candidate)) {
            mark = candidate;
            break;
        }
        QThread::msleep(1);
    }
    if (mark == 0) {
        quint64 base = QDateTime::currentMSecsSinceEpoch();
        for (int offset = 0; offset < 1000; ++offset) {
            int candidate = static_cast<int>((base + offset) & 0x7FFFFFFF);
            if (!usedMarks.contains(candidate)) {
                mark = candidate;
                break;
            }
        }
    }
    if (mark == 0) {
        return "生成唯一 mark 失败，请稍后重试";
    }
    newTask.enabled = true;
    newTask.remark = remark;
    newTask.mark = mark;
    newTask.executeCount = (执行次数 <= 0) ? 1 : 执行次数;
    newTask.executeType = 0;
    newTask.zdsc = true;
    newTask.replyContent = python_code;

    // 4. 添加到内存 tasksMap
    if (!tasksMap.contains(appid))
        tasksMap[appid] = QList<ScheduleTask>();
    tasksMap[appid].append(newTask);

    // 5. 立即写入数据库（单条存储）
    if (dsdb) {
        QString key = QString("task_%1_%2").arg(appid).arg(mark);
        QByteArray jsonData = QJsonDocument(newTask.toJson()).toJson(QJsonDocument::Indented);
        if (!dsdb->put(key, jsonData)) {
            qWarning() << "Failed to save task" << key << "to LMDB";
            // 可以回滚内存添加，但为了简单，只记录日志
        }
    }

    if (appid == g_appid) {
        QMetaObject::invokeMethod(this, [this, newTask]() {
            int row = taskTable->rowCount();
            taskTable->insertRow(row);
            setTaskToRow(row, newTask);
            taskTable->selectRow(row);
            onTableRowSelected(row);
        }, Qt::QueuedConnection);
    }

    return "添加成功";
}
QString ScheduleConfigWidget::get_aids_list(int appid,const QString &openid)
{
    if(!tasksMap.contains(appid)) return "暂时没有对该用户定时信息";
    const auto &tasks = tasksMap[appid];
    QString tab= QString("[code_ai]%1").arg((openid));
    QString res;
    for (const auto &t : tasks)
    {
        if(t.replyContent.startsWith(tab))
        {
            res+=QString("mark:%1 备注：%2\n").arg(t.mark).arg(t.remark);
        }
    }
    return res;
}

QString ScheduleConfigWidget::remov_ds_byai(int appid, int mark)
{
    if (!tasksMap.contains(appid)) {
        return "删除失败：该机器人没有定时任务";
    }

    QList<ScheduleTask> &tasks = tasksMap[appid];
    int removeIndex = -1;
    for (int i = 0; i < tasks.size(); ++i) {
        if (tasks[i].mark == mark) {
            removeIndex = i;
            break;
        }
    }

    if (removeIndex == -1) {
        return "删除失败：未找到 mark=" + QString::number(mark) + " 的任务";
    }

    // 保存一份副本（用于可能的回滚，但此处直接删除）
    ScheduleTask removedTask = tasks[removeIndex];
    tasks.removeAt(removeIndex);

    // 从数据库删除
    if (dsdb) {
        QString key = QString("task_%1_%2").arg(appid).arg(mark);
        if (!dsdb->remove(key)) {
            // 删除失败，回滚内存并返回错误
            tasks.insert(removeIndex, removedTask);
            qWarning() << "Failed to remove task key" << key << "from LMDB";
            return "删除失败：数据库删除操作出错";
        }
    }

    // 如果当前界面显示的是该机器人，则更新表格（必须在主线程）
    if (appid == g_appid) {
        QMetaObject::invokeMethod(this, [this, removeIndex]() {
            if (removeIndex < taskTable->rowCount()) {
                taskTable->removeRow(removeIndex);
                // 调整选中行
                if (taskTable->rowCount() > 0) {
                    int newRow = qMin(removeIndex, taskTable->rowCount() - 1);
                    taskTable->selectRow(newRow);
                    onTableRowSelected(newRow);
                } else {
                    clearDetailPanel();
                }
            }
        }, Qt::QueuedConnection);
    }

    return "删除定时成功";
}

QString python_code2(const QString &py_code,const MessageEvent &msg,QString &data,bool &ok)
{
    py::gil_scoped_acquire gil;
    try {
        py::module_ qiancao = py::module_::import("qiancao_sdk");
        py::object api = qiancao.attr("QQApi")(g_keyuuid);

        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");
        exec_globals["msg"] = py::cast(msg);
        exec_globals["api"] = api;               // 注入 api 对象

        // 4. 执行用户代码
        py::exec(py_code.toStdString(), exec_globals);

        // 5. 读取返回值
        QString ret;
        if (exec_globals.contains("__result__"))
            ret = QString::fromStdString(py::str(exec_globals["__result__"]));
        if (exec_globals.contains("__data__"))
            data = QString::fromStdString(py::str(exec_globals["__data__"]));
        if (exec_globals.contains("__ok__"))
            ok = QString::fromStdString(py::str(exec_globals["__ok__"]))=="1";
        return ret;
    } catch (const py::error_already_set &e) {
        AppendEventLog("[Python] Execute code error: " + QString::fromUtf8(e.what()) ,0xff);
    } catch (const std::exception &e) {
        AppendEventLog("[Python] Execute code error: " + QString::fromUtf8(e.what()) ,0xff);
    }
    return QString();
}

QString ScheduleConfigWidget::ppzl(const MessageEvent &ev,QString &订阅名)
{
    if(!tasksMap.contains(ev.appid)) return QString();
    const auto &tasks = tasksMap[ev.appid];
    for (auto &t : tasks)
    {
        if(!t.enabled) continue;
        if(!(t.role == 1 && ev.member_role<=1 || t.role == 2 || t.role == 0 && g_admin.contains(ev.user))) continue;
        if(t.addSubscribeCmd == ev.msg)
        {
            if(!g_botdb.contains(ev.appid)) return "机器人数据库不存在 请反馈开发者后试试";
            订阅名 = "[添加订阅：" + t.remark + "|%1ms]";
            QString data;
            bool ok=false;
            QString ret = python_code2(t.addSubscribe_text,ev,data,ok);
            if(!ok) return ret;
            BotDB *bot = g_botdb[ev.appid];
            if(bot->addSubscription(QString("t_%1_%2").arg(ev.appid).arg(t.mark),ev.type,ev.groupId,data.split("|||")))
                return ret;
            return "添加订阅|定时失败 添加数据库返回失败 可能已经添加了不是吗？";
        }else if(t.cancelSubscribeCmd==ev.msg)
        {
            if(!g_botdb.contains(ev.appid)) return "机器人数据库不存在 请反馈开发者后试试";
            订阅名 = "[删除订阅：" + t.remark + "|%1ms]";
            QString data;
            bool ok=false;
            QString ret = python_code2(t.cancelSubscribe_text,ev,data,ok);
            if(!ok) return ret;
            BotDB *bot = g_botdb[ev.appid];
            if(bot->removeSubscription(QString("t_%1_%2").arg(ev.appid).arg(t.mark),ev.type,ev.groupId,data.split("|||"))){
                return ret;
            }
            return "取消订阅|定时失败 添加数据库返回失败 可能已经取消了不是吗？";
        }
    }
    return QString();
}

void ScheduleConfigWidget::addRowsFromTSV(const QString &tsv)
{
    QStringList lines = tsv.split("\n", Qt::SkipEmptyParts);
    int added = 0;
    for (const QString &line : std::as_const(lines)) {
        QStringList parts = line.split("\t");
        if (parts.size() >= 8) {
            ScheduleTask task;
            task.enabled = (parts[0] == "1");
            task.remark = parts[1];
            task.StringToTime(parts[2]);
            task.executeCount = parts[3].toInt();
            task.executeType = parts[4].toInt(0);
            // 反转义
            QString reply = parts[5];
            reply.replace("\\t", "\t").replace("\\n", "\n").replace("\\\\", "\\");
            task.replyContent = reply;
            task.addSubscribeCmd = parts[6];
            task.cancelSubscribeCmd = parts[7];
            task.addSubscribe_text = parts[8];
            task.cancelSubscribe_text = parts[9];
            addRowFromTask(task);
            added++;
        } else if (parts.size() >= 2) {
            // 最少有启用和备注，其余默认
            ScheduleTask task;
            task.enabled = (parts[0] == "1");
            task.remark = parts[1];
            addRowFromTask(task);
            added++;
        }
    }
    if (added > 0) {
        QMessageBox::information(this, "提示", QString("成功添加 %1 个任务").arg(added));
    } else {
        QMessageBox::warning(this, "警告", "剪贴板中没有有效数据");
    }
}





void ScheduleConfigWidget::loadAllTasksFromFile()
{
    if (!dsdb) {
        qWarning() << "LMDB database not initialized, cannot load tasks.";
        return;
    }

    tasksMap.clear();
    const QList<QByteArray> allKeys = dsdb->getAllKeysByteArray();
    const QString prefix = "task_";
    for (const QByteArray &keyBA : allKeys) {
        QString key = QString::fromUtf8(keyBA);
        if (!key.startsWith(prefix))
            continue;
        QByteArray value = dsdb->get(keyBA);
        if (value.isEmpty())
            continue;

        QJsonDocument doc = QJsonDocument::fromJson(value);
        if (!doc.isObject())
            continue;

        ScheduleTask task = ScheduleTask::fromJson(doc.object());
        int appid = key.mid(prefix.size()).split('_').first().toInt();
        tasksMap[appid].append(task);

    }

}