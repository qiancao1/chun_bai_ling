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
#include <QDebug>


// ---------- ScheduleConfigWidget ----------
ScheduleConfigWidget::ScheduleConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    initTable();
    refreshRobotList();
    loadAllTasksFromFile();
}

ScheduleConfigWidget::~ScheduleConfigWidget()
{
}

void ScheduleConfigWidget::setupUI()
{
    mainSplitter = new QSplitter(Qt::Horizontal, this);

    // 左侧：机器人列表
    QWidget *leftWidget = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *robotLabel = new QLabel("机器人昵称列表");
    robotListWidget = new QListWidget;
    refreshRobotBtn = new QPushButton("刷新列表");
    leftLayout->addWidget(robotLabel);
    leftLayout->addWidget(robotListWidget);
    leftLayout->addWidget(refreshRobotBtn);


    // 中间：定时任务表格（只有一列：启用+备注）
    QWidget *middleWidget = new QWidget;
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

    // 右侧：详细信息面板
    detailPanel = new QWidget;
    QVBoxLayout *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    QGroupBox *detailGroup = new QGroupBox("定时详细信息");
    QFormLayout *formLayout = new QFormLayout(detailGroup);
    formLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // 创建所有控件
    scheduleTimeEdit = new QLineEdit;
    scheduleTimeEdit->setPlaceholderText("规则 年,月,日,时,分|分割 如:8,10|-3,10 -3代表 每隔3小时发一次 -4 -5同理");

    executeCountSpin = new QSpinBox;
    executeCountSpin->setRange(-1, 2147483000);
    executeCountSpin->setSpecialValueText("无限次");
    executeCountSpin->setValue(-1);

    executeTypeCombo = new QComboBox;
    executeTypeCombo->addItems({"每个群执行一次", "只执行一次"});

    // 新增：触发类型下拉框
    triggerTypeCombo = new QComboBox;
    triggerTypeCombo->addItems({"开发者", "管理员", "所有人"});

    // 新增：已执行次数标签
    executedCountLabel = new QLabel("0");

    // 回复内容使用多行文本框
    replyContentEdit = new QTextEdit;
    replyContentEdit->setPlaceholderText(R"(#python 不带这行就是普通信息 注意这里只有appid有值
api.outlog(f"收到来自 {msg.appid} 的消息")
__result__ = f"收到来自 {msg.appid} 的消息"
"""
[image,path=路径,x=10,y=10] xy可不传 路径可以是链接
[audio,path=路径] [video,path=路径] [file,path=路径]

蓝字：蓝字请使用 []() 如 [测试](你点击了蓝字)
同时也是支持 [链接](https://example.com)

按钮挂载：#b:#按钮json#b:# "#b:#"包起来即可
"""
)");
    replyContentEdit->setMinimumHeight(250);


    addSubscribeEdit = new QLineEdit;
    addSubscribeEdit->setPlaceholderText("订阅测试");
    cancelSubscribeEdit = new QLineEdit;
    cancelSubscribeEdit->setPlaceholderText("取消订阅测试");
    addSubscribeReplyEdit = new QTextEdit;
    addSubscribeReplyEdit->setPlaceholderText("添加订阅后自动回复的内容\n支持py代码");
    cancelSubscribeReplyEdit = new QTextEdit;
    cancelSubscribeReplyEdit->setPlaceholderText("取消订阅后自动回复的内容\n支持py代码");

    formLayout->addRow("定时时间:", scheduleTimeEdit);

    QWidget *execCountWidget = new QWidget;
    QHBoxLayout *execCountLayout = new QHBoxLayout(execCountWidget);

    QLabel *La = new QLabel("已执行次数:");
    La->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    execCountLayout->setContentsMargins(0, 0, 0, 0);
    execCountLayout->addWidget(executeCountSpin);
    execCountLayout->addWidget(La);
    execCountLayout->addWidget(executedCountLabel);
    formLayout->addRow("执行次数:", execCountWidget);

    QWidget *execTypeWidget = new QWidget;
    QHBoxLayout *execTypeLayout = new QHBoxLayout(execTypeWidget);
    execTypeLayout->setContentsMargins(0, 0, 0, 0);
    execTypeLayout->addWidget(executeTypeCombo);

    execTypeLayout->addWidget(new QLabel("触发类型:"));
    execTypeLayout->addWidget(triggerTypeCombo);
    formLayout->addRow("执行类型:", execTypeWidget);

    QWidget *subscribeWidget = new QWidget;
    QHBoxLayout *subscribeLayout = new QHBoxLayout(subscribeWidget);
    subscribeLayout->setContentsMargins(0, 0, 0, 0);
    subscribeLayout->addWidget(new QLabel("添加订阅指令:"));
    subscribeLayout->addWidget(addSubscribeEdit);
    subscribeLayout->addWidget(new QLabel("取消订阅指令:"));
    subscribeLayout->addWidget(cancelSubscribeEdit);
    QWidget *subscribeWidget2 = new QWidget;
    QHBoxLayout *subscribeLayout2 = new QHBoxLayout(subscribeWidget2);
    subscribeLayout2->setContentsMargins(0, 0, 0, 0);
    subscribeLayout2->addWidget(addSubscribeReplyEdit);
    subscribeLayout2->addWidget(cancelSubscribeReplyEdit);



    // 该行自带标签，无需额外的行标签
    formLayout->addRow("", subscribeWidget);
    formLayout->addRow("", subscribeWidget2);
    // 5. 定时回复内容（多行，移到订阅指令下方）
    formLayout->addRow("定时回复内容:", replyContentEdit);

    // 6. 更新任务按钮
    updateTaskBtn = new QPushButton("更新当前任务");
    //formLayout->addRow("", updateTaskBtn);

    detailLayout->addWidget(detailGroup);

    // 添加到分割器
    mainSplitter->addWidget(leftWidget);
    mainSplitter->addWidget(middleWidget);
    mainSplitter->addWidget(detailPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);
    mainSplitter->setStretchFactor(2, 3);

    // 底部按钮栏
    QHBoxLayout *bottomLayout = new QHBoxLayout;
    addBtn = new QPushButton("添加");
    deleteBtn = new QPushButton("删除");
    copyRowBtn = new QPushButton("复制");
    copyAllBtn = new QPushButton("复制全部");
    pasteBtn = new QPushButton("粘贴");
    saveBtn = new QPushButton("保存配置");
    bottomLayout->addWidget(refreshRobotBtn);  // 底部保留刷新按钮（左侧也有，但保留）
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

    // 信号连接（保持不变）
    connect(refreshRobotBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::refreshRobotList);
    connect(robotListWidget, &QListWidget::currentRowChanged, this, &ScheduleConfigWidget::onRobotSelectionChanged);
    connect(taskTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        onTableRowSelected(row);
    });
    connect(addBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onDeleteRow);
    connect(copyRowBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onCopyRow);
    connect(copyAllBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onCopyAllRows);
    connect(pasteBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onPasteFromClipboard);
    connect(saveBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onSaveToFile);
    connect(updateTaskBtn, &QPushButton::clicked, this, &ScheduleConfigWidget::onUpdateTask);

    // 表格项修改同步
    connect(taskTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (!item) return;
        int row = item->row();
        if (currentAppId == 0 || row < 0) return;
        if (!tasksMap.contains(currentAppId)) return;
        QList<ScheduleTask> &tasks = tasksMap[currentAppId];
        if (row >= tasks.size()) return;
        tasks[row].enabled = (item->checkState() == Qt::Checked);
        tasks[row].remark = item->text();
    });
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

void ScheduleConfigWidget::refreshRobotList()
{
    if (currentAppId != 0)
        saveCurrentTasksToMap();

    robotListWidget->clear();
    for (const auto &acc : std::as_const(m_accounts)) {
        if (!acc->nickname.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem(acc->nickname);
            item->setData(Qt::UserRole, acc->appid_int);
            robotListWidget->addItem(item);
        }
    }
    if (robotListWidget->count() > 0)
        robotListWidget->setCurrentRow(0);
    else {
        currentAppId = 0;
        taskTable->setRowCount(0);
        clearDetailPanel();
    }
}

void ScheduleConfigWidget::onRobotSelectionChanged()
{
    if (currentAppId != 0)
        saveCurrentTasksToMap();

    QListWidgetItem *cur = robotListWidget->currentItem();
    if (cur) {
        currentAppId = cur->data(Qt::UserRole).toInt();
        loadTasksForRobot(currentAppId);
    } else {
        currentAppId = 0;
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

void ScheduleConfigWidget::saveCurrentTasksToMap()
{
    if (currentAppId == 0) return;
    QList<ScheduleTask> tasks;
    for (int row = 0; row < taskTable->rowCount(); ++row)
        tasks.append(getTaskFromRow(row));
    tasksMap[currentAppId] = tasks;
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

ScheduleTask ScheduleConfigWidget::getTaskFromRow(int row) const
{
    ScheduleTask task;
    if (row < 0 || row >= taskTable->rowCount()) return task;

    QTableWidgetItem *item = taskTable->item(row, COL_ENABLED_REMARK);
    if (item) {
        task.enabled = (item->checkState() == Qt::Checked);
        task.remark = item->text();
    }

    // 从缓存的完整数据中获取其他字段（因为表格中只存了启用和备注）
    if (currentAppId != 0 && tasksMap.contains(currentAppId)) {
        const auto &tasks = tasksMap[currentAppId];
        if (row < tasks.size()) {
            const ScheduleTask &full = tasks[row];
            task.scheduleTime = full.scheduleTime;
            task.executeCount = full.executeCount;
            task.executeType = full.executeType;
            task.replyContent = full.replyContent;
            task.addSubscribeCmd = full.addSubscribeCmd;
            task.cancelSubscribeCmd = full.cancelSubscribeCmd;
        }
    }
    return task;
}

void ScheduleConfigWidget::addRowFromTask(const ScheduleTask &task)
{
    int row = taskTable->rowCount();
    taskTable->insertRow(row);
    setTaskToRow(row, task);
    if (currentAppId != 0) {
        if (!tasksMap.contains(currentAppId))
            tasksMap[currentAppId] = QList<ScheduleTask>();
        tasksMap[currentAppId].append(task);
    }
    taskTable->selectRow(row);
}

void ScheduleConfigWidget::onTableRowSelected(int row)
{
    if (row < 0 || currentAppId == 0) {
        clearDetailPanel();
        return;
    }
    ScheduleTask task = getTaskFromRow(row);
    updateDetailPanelFromTask(task);
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
    if (currentAppId == 0 || !tasksMap.contains(currentAppId)) return;
    QList<ScheduleTask> &tasks = tasksMap[currentAppId];
    if (row >= tasks.size()) return;

    // 更新完整任务
    applyDetailPanelToTask(tasks[row]);
    // 同步表格中的启用和备注（可能被修改过，但此处以面板为准？我们保持面板更新不改变启用备注，但用户可能在表格中直接修改）
    // 为了安全，我们将表格中的启用和备注也同步，但面板中没有这两个字段，所以保留表格现有值。
    // 因此我们只更新除启用备注外的其他字段。
    // 但更好的做法是：先保存表格的启用备注，然后重新设置行。
    bool wasBlocked = taskTable->blockSignals(true);
    setTaskToRow(row, tasks[row]); // 刷新显示
    taskTable->blockSignals(wasBlocked);
    taskTable->selectRow(row);
    //QMessageBox::information(this, "提示", "任务已更新");
}

void ScheduleConfigWidget::onAddRow()
{
    ScheduleTask newTask;
    newTask.enabled = true;
    newTask.remark = "新定时任务";
    for( auto &acc:m_accounts)
    {
        if(acc->appid_int==currentAppId)
        {
            acc->dyindex++;
            newTask.mark = acc->dyindex;
            accountPage->saveAccounts();
            break;
        }
    }
    if(newTask.mark<=0)
    {
        QMessageBox::warning(this,"添加失败","没有找到选中的机器人 请刷新列表重新选择机器人");
        return;
    }
    newTask.scheduleTime.clear();
    newTask.executeCount = -1;
    newTask.executeType = 0;
    newTask.replyContent = "";
    newTask.addSubscribeCmd = "";
    newTask.cancelSubscribeCmd = "";
    addRowFromTask(newTask);
}

void ScheduleConfigWidget::onDeleteRow()
{
    int row = taskTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要删除的行");
        return;
    }
    if (currentAppId == 0 || !tasksMap.contains(currentAppId)) return;
    if(QMessageBox::warning(this,"确认删除","确认删除吗？ 删除将清空订阅的使用群数据")!=QMessageBox::Yes) return;
    QList<ScheduleTask> &t = tasksMap[currentAppId];
    if(!g_botdb.contains(currentAppId))
    {
        BotDB *client = new BotDB(QString("botdb/%1_db").arg(currentAppId));
        client->open();
        g_botdb[currentAppId] = client;
    }
    auto *db =g_botdb[currentAppId];
    db->clearSubscriptionsByMark(t[row].mark);
    taskTable->removeRow(row);
    tasksMap[currentAppId].removeAt(row);
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
    ScheduleTask task = getTaskFromRow(row);
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
    if (currentAppId == 0 || !tasksMap.contains(currentAppId)) {
        QMessageBox::information(this, "提示", "没有可复制的内容");
        return;
    }
    const auto &tasks = tasksMap[currentAppId];
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

void ___dsrw(){
    QDateTime now = QDateTime::currentDateTime();
    const QTime time = now.time();
    const QDate date = now.date();

    // ① 把 constBegin/constEnd 改成 begin/end（去掉 const）
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

        // ② 这里取非 const 引用（因为迭代器已经不是 const 了）
        QList<ScheduleTask> &list = it.value();

        // ③ 内部循环也去掉 const
        for(auto &t : list) {   // 注意 auto &t 而不是 const auto &t
            if(!t.enabled) continue;
            for (const TimeRule &rule : std::as_const(t.scheduleTime)) {
                if (!rule.matches(date, time)) continue;

                // ④ 现在可以修改了

                if(t.jis>t.executeCount && t.executeCount!=-1) break;
                t.jis++;
                QString text =  "[订阅推送:" + t.remark + "]";
                auto *db = g_botdb[appid];
                QStringList fullList = db->listSubscriptions(t.mark);
                AppendEventLog(text+" 群数："+QString::number(fullList.size()),0x35E496);
                const int chunkSize = 50;
                for (int i = 0; i < fullList.size(); i += chunkSize) {
                    QStringList subList = fullList.mid(i, chunkSize);
                    auto *task = new api_dsrw(appid, subList, t.replyContent,text, t.executeType);
                    QThreadPool::globalInstance()->start(task);
                }
            }
        }
    }
}


class ___dtrw : public QRunnable {
public:
    // 通过构造函数把需要的数据传进来（如果有的话）
    ___dtrw() {}

    void run() override {
        ___dsrw();
    }
};
void ScheduleConfigWidget::检查定时列表()
{
    QDateTime now = QDateTime::currentDateTime();

    const QTime time = now.time();
    int dqsj=time.minute();
    if (定时检查变量 == dqsj) return;
    定时检查变量 = dqsj;
    qDebug() <<"开始执行订阅任务";
    auto *task = new ___dtrw();
    QThreadPool::globalInstance()->start(task);

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
            BotDB *bot = g_botdb[ev.appid];
            if(bot->addSubscription(t.mark,ev.type,ev.groupId))
            {
                if(t.addSubscribe_text.isEmpty()) return "添加订阅成功(这是一串默认内容 证明你没设置成功文本)";
                return t.addSubscribe_text;
            }
            return "添加订阅|定时失败 添加数据库返回失败 可能已经添加了不是吗？";
        }else if(t.cancelSubscribeCmd==ev.msg)
        {
            if(!g_botdb.contains(ev.appid)) return "机器人数据库不存在 请反馈开发者后试试";
            订阅名 = "[删除订阅：" + t.remark + "|%1ms]";
            BotDB *bot = g_botdb[ev.appid];
            if(bot->removeSubscription(t.mark,ev.type,ev.groupId)){
                if(t.addSubscribe_text.isEmpty()) return "取消订阅成功(这是一串默认内容 证明你没设置取消文本)";
                return t.cancelSubscribe_text;
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

void ScheduleConfigWidget::onSaveToFile()
{
    if (currentAppId != 0)
        saveCurrentTasksToMap();
    saveAllTasksToFile();
    emit scheduleConfigChanged();
    QMessageBox::information(this, "保存成功", "定时任务配置已保存到 schedule_tasks.json");
}

void ScheduleConfigWidget::saveAllTasksToFile(const QString &filePath)
{
    QJsonObject root;
    for (auto it = tasksMap.begin(); it != tasksMap.end(); ++it) {
        QJsonArray arr;
        for (const ScheduleTask &task : it.value())
            arr.append(task.toJson());
        root[QString::number(it.key())] = arr;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save schedule tasks to" << filePath;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ScheduleConfigWidget::loadAllTasksFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    tasksMap.clear();
    for (auto it = root.begin(); it != root.end(); ++it) {
        int appid = it.key().toInt();
        const QJsonArray arr = it.value().toArray();
        QList<ScheduleTask> tasks;
        for (const QJsonValue &val : arr) {
            if (val.isObject())
                tasks.append(ScheduleTask::fromJson(val.toObject()));
        }
        tasksMap[appid] = tasks;
    }
    // 刷新当前显示的机器人
    if (currentAppId != 0)
        loadTasksForRobot(currentAppId);
}