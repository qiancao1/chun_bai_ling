

#include "BotRuleConfigWidget.h"
#include "global.h"  // 提供 extern QList<AccountInfo> m_accounts;

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QComboBox>          // <-- 新增
#include <QDebug>

// ---------- BotRuleItem 序列化 ----------
QJsonObject BotRuleItem::toJson() const
{
    QJsonObject obj;
    obj["enabled"] = enabled;
    obj["remark"] = remark;
    obj["jzc"] = jzc;
    obj["buttonText"] = buttonText;
    obj["matchType"] = matchType;
    obj["candidateWords"] = candidateWords;
    return obj;
}

BotRuleItem BotRuleItem::fromJson(const QJsonObject &obj)
{
    BotRuleItem item;
    item.enabled = obj["enabled"].toBool(true);
    item.remark = obj["remark"].toString("");
    item.jzc = obj["jzc"].toString("");
    item.buttonText = obj["buttonText"].toString("");
    item.matchType = obj["matchType"].toInt(0);
    item.candidateWords = obj["candidateWords"].toString("");
    return item;
}

// ---------- BotMovableTableWidget ----------
BotMovableTableWidget::BotMovableTableWidget(QWidget *parent)
    : QTableWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void BotMovableTableWidget::startDrag(Qt::DropActions supportedActions)
{
    QModelIndexList indexes = selectionModel()->selectedIndexes();
    if (!indexes.isEmpty())
        dragStartRow = indexes.first().row();
    QTableWidget::startDrag(supportedActions);
}

void BotMovableTableWidget::dropEvent(QDropEvent *event)
{
    if (event->source() != this || dragStartRow == -1) {
        QTableWidget::dropEvent(event);
        return;
    }

    QPoint dropPos = event->pos();
    int targetRow = indexAt(dropPos).row();
    if (targetRow == -1)
        targetRow = rowCount();

    int fromRow = dragStartRow;
    int toRow = targetRow;
    if (fromRow == toRow) {
        dragStartRow = -1;
        event->accept();
        return;
    }
    if (fromRow < toRow)
        toRow--;   // 因为移除源行后索引会前移

    emit rowsSwapped(fromRow, toRow);

    dragStartRow = -1;
    event->accept();
}

// ---------- BotRuleConfigWidget ----------
BotRuleConfigWidget::BotRuleConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    initTable();
    refreshRobotList();
    loadAllRulesFromFile();
    // 队列连接，确保加载发生在事件循环末尾
    connect(this, &BotRuleConfigWidget::needLoadBotRules,
            this, &BotRuleConfigWidget::loadRulesForRobot,
            Qt::QueuedConnection);
}

BotRuleConfigWidget::~BotRuleConfigWidget()
{
}

void BotRuleConfigWidget::setupUI()
{
    mainSplitter = new QSplitter(Qt::Horizontal, this);

    // 左侧机器人列表
    QWidget *leftWidget = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    QLabel *robotLabel = new QLabel("机器人昵称列表");
    robotListWidget = new QListWidget;
    refreshRobotBtn = new QPushButton("刷新列表");
    leftLayout->addWidget(robotLabel);
    leftLayout->addWidget(robotListWidget);
    leftLayout->addWidget(refreshRobotBtn);

    // 右侧规则表格
    QWidget *rightWidget = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    QLabel *ruleLabel = new QLabel("机器人规则配置表 (可拖拽行首移动)");
    ruleTable = new BotMovableTableWidget;
    ruleTable->setAlternatingRowColors(true);
    ruleTable->setStyleSheet(
        "QTableWidget::item:selected { background-color: #cce8cf; color: #1e3c2c; }"
        "QTableWidget::item:selected:focus { background-color: #a8d5aa; }"
        );

    QHBoxLayout *btnLayout = new QHBoxLayout;
    saveBtn = new QPushButton("保存");
    addBtn = new QPushButton("添加行");
    deleteBtn = new QPushButton("删除行");
    copyRowBtn = new QPushButton("复制一行");
    copyAllBtn = new QPushButton("复制全部");
    pasteBtn = new QPushButton("从剪贴板添加");
    moveUpBtn = new QPushButton("上移行");
    moveDownBtn = new QPushButton("下移行");
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(deleteBtn);
    btnLayout->addWidget(copyRowBtn);
    btnLayout->addWidget(copyAllBtn);
    btnLayout->addWidget(pasteBtn);
    btnLayout->addWidget(moveUpBtn);
    btnLayout->addWidget(moveDownBtn);
    btnLayout->addStretch();

    rightLayout->addWidget(ruleLabel);
    rightLayout->addWidget(ruleTable);
    rightLayout->addLayout(btnLayout);

    mainSplitter->addWidget(leftWidget);
    mainSplitter->addWidget(rightWidget);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(mainSplitter);
    setLayout(mainLayout);

    // 信号连接
    connect(refreshRobotBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::refreshRobotList);
    connect(robotListWidget, &QListWidget::currentRowChanged, this, &BotRuleConfigWidget::onRobotSelectionChanged);
    connect(addBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onDeleteRow);
    connect(copyRowBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onCopyRow);
    connect(copyAllBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onCopyAllRows);
    connect(pasteBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onPasteFromClipboard);
    connect(moveUpBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onMoveRowUp);
    connect(moveDownBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onMoveRowDown);
    connect(saveBtn, &QPushButton::clicked, this, &BotRuleConfigWidget::onSaveToFile);
    connect(ruleTable, &BotMovableTableWidget::rowsSwapped, this, &BotRuleConfigWidget::onRowsSwapped);
    connect(ruleTable, &QTableWidget::itemChanged, this, &BotRuleConfigWidget::onTableDataChanged);
}

void BotRuleConfigWidget::initTable()
{
    QStringList headers = {"启用", "备注", "禁止词", "按钮文本", "匹配类型", "候选词"};
    ruleTable->setColumnCount(headers.size());
    ruleTable->setHorizontalHeaderLabels(headers);
    ruleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ruleTable->verticalHeader()->setVisible(true);

    // 让匹配类型列不可直接编辑（实际使用 cell widget 覆盖）
    ruleTable->setItemDelegateForColumn(COL_MATCH_TYPE, nullptr);
}

void BotRuleConfigWidget::refreshRobotList()
{
    if (m_currentRobotId != 0)
        saveCurrentRulesToMap();

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
        m_currentRobotId = 0;
        ruleTable->setRowCount(0);
    }
}

void BotRuleConfigWidget::onRobotSelectionChanged()
{
    if (m_currentRobotId != 0)
        saveCurrentRulesToMap();

    QListWidgetItem *cur = robotListWidget->currentItem();
    if (cur) {
        m_currentRobotId = cur->data(Qt::UserRole).toInt();
        emit needLoadBotRules(m_currentRobotId);   // 队列加载
    } else {
        m_currentRobotId = 0;
        ruleTable->setRowCount(0);
    }
}

void BotRuleConfigWidget::saveCurrentRulesToMap()
{
    if (m_currentRobotId == 0) return;
    QList<BotRuleItem> rules;
    for (int row = 0; row < ruleTable->rowCount(); ++row)
        rules.append(getRuleItemFromRow(row));
    m_ruleMap[m_currentRobotId] = rules;
}

void BotRuleConfigWidget::loadRulesForRobot(int robotId)
{
    static bool isLoading = false;
    if (isLoading) return;
    isLoading = true;



    bool wasBlocked = ruleTable->blockSignals(true);
    const QList<BotRuleItem> &rules = m_ruleMap[robotId];
    ruleTable->setRowCount(rules.size());
    for (int i = 0; i < rules.size(); ++i) {
        setRuleItemToRow(i, rules[i]);
    }
    ruleTable->blockSignals(wasBlocked);
    isLoading = false;
    connect(ruleTable, &QTableWidget::itemChanged, this, &BotRuleConfigWidget::onTableDataChanged);
}

void BotRuleConfigWidget::setRuleItemToRow(int row, const BotRuleItem &item)
{
    while (row >= ruleTable->rowCount())
        ruleTable->insertRow(ruleTable->rowCount());

    auto ensureItem = [this, row](int col) -> QTableWidgetItem* {
        QTableWidgetItem *it = ruleTable->item(row, col);
        if (!it) {
            it = new QTableWidgetItem;
            ruleTable->setItem(row, col, it);
        }
        return it;
    };

    // 启用列
    QTableWidgetItem *checkItem = ensureItem(COL_ENABLED);
    checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    checkItem->setCheckState(item.enabled ? Qt::Checked : Qt::Unchecked);

    // 普通文本列
    ensureItem(COL_REMARK)->setText(item.remark);
    ensureItem(COL_BUTTON_TYPE)->setText(item.jzc);
    ensureItem(COL_BUTTON_TEXT)->setText(item.buttonText);
    ensureItem(COL_CANDIDATE)->setText(item.candidateWords);

    // ----- 匹配类型列：使用 QComboBox 下拉选择 -----
    QComboBox *combo = new QComboBox(ruleTable);
    combo->addItem("0指令", 0);          // 存储数据 0
    combo->addItem("1匹配指令头", 1);
    combo->addItem("2指令包含", 2);
    combo->addItem("3发送内容包含", 3);
    combo->setCurrentIndex(combo->findData(item.matchType));
    // 可选：让 combo 的样式与表格单元格融合
    combo->setStyleSheet("QComboBox { border: none; background: transparent; }");
    ruleTable->setCellWidget(row, COL_MATCH_TYPE, combo);

    // 移除可能已存在的普通 item，避免干扰
    delete ruleTable->takeItem(row, COL_MATCH_TYPE);
}

BotRuleItem BotRuleConfigWidget::getRuleItemFromRow(int row) const
{
    BotRuleItem item;
    if (row < 0 || row >= ruleTable->rowCount()) return item;

    QTableWidgetItem *checkItem = ruleTable->item(row, COL_ENABLED);
    item.enabled = checkItem && (checkItem->checkState() == Qt::Checked);

    auto text = [this, row](int col) -> QString {
        QTableWidgetItem *it = ruleTable->item(row, col);
        return it ? it->text() : "";
    };
    item.remark = text(COL_REMARK);
    item.jzc = text(COL_BUTTON_TYPE);
    item.buttonText = text(COL_BUTTON_TEXT);
    item.candidateWords = text(COL_CANDIDATE);

    // 获取匹配类型：从 cell widget 中读取
    QWidget *widget = ruleTable->cellWidget(row, COL_MATCH_TYPE);
    if (auto *combo = qobject_cast<QComboBox*>(widget)) {
        item.matchType = combo->currentData().toInt();
    } else {
        // 兼容旧数据（如果该列还是普通文本）
        item.matchType = text(COL_MATCH_TYPE).toInt();
    }
    return item;
}

void BotRuleConfigWidget::addRowFromRuleItem(const BotRuleItem &item)
{
    int row = ruleTable->rowCount();
    ruleTable->insertRow(row);
    setRuleItemToRow(row, item);
    ruleTable->selectRow(row);
}

void BotRuleConfigWidget::onAddRow()
{
    BotRuleItem newItem;
    newItem.enabled = true;
    newItem.remark = "新规则";
    newItem.jzc = "";
    newItem.buttonText = "新按钮";
    newItem.matchType = 0;
    newItem.candidateWords = "";
    addRowFromRuleItem(newItem);
    onTableDataChanged();
}

void BotRuleConfigWidget::onDeleteRow()
{
    int row = ruleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要删除的行");
        return;
    }
    ruleTable->removeRow(row);
    onTableDataChanged();
    if (ruleTable->rowCount() > 0) {
        int newRow = qMin(row, ruleTable->rowCount() - 1);
        ruleTable->selectRow(newRow);
    }
}

void BotRuleConfigWidget::onCopyRow()
{
    int row = ruleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要复制的行");
        return;
    }

    BotRuleItem item = getRuleItemFromRow(row);
    QString enab = item.enabled ? "1" : "0";

    QString line = QString("%1\t%2\t%3\t%4\t%5\t%6")
                       .arg(enab,item.remark,item.jzc,item.buttonText).arg(item.matchType).arg(item.candidateWords);


    QApplication::clipboard()->setText(line);
    QMessageBox::information(this, "提示", "已复制当前行到剪贴板");
}
void BotRuleConfigWidget::onCopyAllRows()
{
    QStringList tsv = getTableAsTSV();
    if (tsv.isEmpty()) {
        QMessageBox::information(this, "提示", "没有可复制的内容");
        return;
    }
    QApplication::clipboard()->setText(tsv.join("\n"));
    QMessageBox::information(this, "提示", QString("已复制 %1 行").arg(tsv.size()));
}

void BotRuleConfigWidget::onPasteFromClipboard()
{
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        QMessageBox::information(this, "提示", "剪贴板为空");
        return;
    }
    addRowsFromTSV(text);
    onTableDataChanged();
}

void BotRuleConfigWidget::onMoveRowUp()
{
    int row = ruleTable->currentRow();
    if (row <= 0) {
        QMessageBox::information(this, "提示", "无法上移");
        return;
    }
    saveCurrentRulesToMap();
    if (!m_ruleMap.contains(m_currentRobotId)) return;
    QList<BotRuleItem> &rules = m_ruleMap[m_currentRobotId];
    if (row >= rules.size()) return;
    qSwap(rules[row], rules[row-1]);
    emit needLoadBotRules(m_currentRobotId);
    ruleTable->selectRow(row-1);
}

void BotRuleConfigWidget::onMoveRowDown()
{
    int row = ruleTable->currentRow();
    if (row < 0 || row >= ruleTable->rowCount() - 1) {
        QMessageBox::information(this, "提示", "无法下移");
        return;
    }
    saveCurrentRulesToMap();
    if (!m_ruleMap.contains(m_currentRobotId)) return;
    QList<BotRuleItem> &rules = m_ruleMap[m_currentRobotId];
    if (row+1 >= rules.size()) return;
    qSwap(rules[row], rules[row+1]);
    emit needLoadBotRules(m_currentRobotId);
    ruleTable->selectRow(row+1);
}

void BotRuleConfigWidget::onRowsSwapped(int fromRow, int toRow)
{
    if (m_currentRobotId == 0) return;
    if (m_ruleMap.contains(m_currentRobotId)) {
        QList<BotRuleItem> &rules = m_ruleMap[m_currentRobotId];
        if (fromRow >= 0 && fromRow < rules.size() && toRow >= 0 && toRow < rules.size()) {
            BotRuleItem moving = rules.takeAt(fromRow);
            int insertPos = toRow;
            if(toRow>=fromRow && insertPos<rules.size())
                insertPos++;

            rules.insert(insertPos, moving);
        }
    }
    // 断开 itemChanged 信号，避免加载时触发保存
    disconnect(ruleTable, &QTableWidget::itemChanged, this, &BotRuleConfigWidget::onTableDataChanged);
    emit needLoadBotRules(m_currentRobotId);
    ruleTable->selectRow(toRow);
}

void BotRuleConfigWidget::onTableDataChanged()
{
    if (m_currentRobotId == 0) return;
    saveCurrentRulesToMap();
}

void BotRuleConfigWidget::onSaveToFile()
{
    if (m_currentRobotId != 0)
        saveCurrentRulesToMap();
    saveAllRulesToFile();
    oninitbot();
    QMessageBox::information(this, "保存成功", "机器人规则已保存到 bot_rules.json");
}

QStringList BotRuleConfigWidget::getTableAsTSV() const
{
    QStringList lines;
    for (int row = 0; row < ruleTable->rowCount(); ++row) {
        QStringList fields;
        QTableWidgetItem *check = ruleTable->item(row, COL_ENABLED);
        fields << (check && check->checkState() == Qt::Checked ? "1" : "0");
        BotRuleItem item = getRuleItemFromRow(row);
        fields << item.remark;
        fields << item.jzc;
        fields << item.buttonText;
        fields << QString::number(item.matchType);   // 导出数字
        fields << item.candidateWords;
        lines << fields.join("\t");
    }
    return lines;
}

void BotRuleConfigWidget::addRowsFromTSV(const QString &tsv)
{
    const QStringList lines = tsv.split("\n", Qt::SkipEmptyParts);
    int added = 0;
    for (const QString &line : lines) {
        QStringList parts = line.split("\t");
        if (parts.size() >= 6) {
            BotRuleItem item;
            item.enabled = (parts[0] == "1");
            item.remark = parts[1];
            item.jzc = parts[2];
            item.buttonText = parts[3];
            item.matchType = parts[4].toInt();
            item.candidateWords = parts[5];
            addRowFromRuleItem(item);
            added++;
        } else if (parts.size() >= 1 && !line.trimmed().isEmpty()) {
            BotRuleItem item;
            item.remark = line.trimmed();
            addRowFromRuleItem(item);
            added++;
        }
    }
    if (added > 0) {
        onTableDataChanged();
        QMessageBox::information(this, "提示", QString("成功添加 %1 行").arg(added));
    } else {
        QMessageBox::warning(this, "警告", "剪贴板中没有有效数据");
    }
}

void BotRuleConfigWidget::saveAllRulesToFile(const QString &filePath)
{
    QJsonObject root;
    for (auto it = m_ruleMap.begin(); it != m_ruleMap.end(); ++it) {
        QJsonArray arr;
        for (const BotRuleItem &item : it.value())
            arr.append(item.toJson());
        root[QString::number(it.key())] = arr;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save bot rules to" << filePath;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void BotRuleConfigWidget::oninitbot()
{
    if (m_currentRobotId == 0) return;
    const QList<BotRuleItem>& rules = m_ruleMap[m_currentRobotId];
    if (rules.isEmpty()) return;
    for (auto& account : m_accounts) {
        if (account->appid_int != m_currentRobotId) continue;
        account->mdbtn.clear();
        for (const auto& rule : rules) {
            QJsonParseError parseError;
            QJsonDocument jsonDoc = QJsonDocument::fromJson(rule.buttonText.toUtf8(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                continue;
            }
            if(rule.remark.isEmpty() || jsonDoc.isEmpty()) continue;
            mdbtn bt;
            bt.btnjson = jsonDoc.object();
            bt.zl   = rule.remark.split("|||");//指令
            if(rule.jzc.isEmpty())
                bt.jzc.clear();
            else
                bt.jzc  = rule.jzc.split("|||");//禁止词
            if(rule.jzc.isEmpty())
                bt.hxc.clear();
            else
                bt.hxc  = rule.candidateWords.split("|||"); //候选词
            bt.pplx = rule.matchType;
            account->mdbtn.append(bt);
        }
    }
}

void BotRuleConfigWidget::loadAllRulesFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    m_ruleMap.clear();
    for (auto it = root.begin(); it != root.end(); ++it) {
        int robotId = it.key().toInt();
        const QJsonArray arr = it.value().toArray();
        QList<BotRuleItem> items;
        for (const QJsonValue &val : arr) {
            if (val.isObject())
                items.append(BotRuleItem::fromJson(val.toObject()));
        }
        m_ruleMap[robotId] = items;
        m_currentRobotId=robotId;
        oninitbot();
    }

    if (m_currentRobotId != 0)
        loadRulesForRobot(m_currentRobotId);
}