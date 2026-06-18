#include "TextReplaceConfigWidget.h"
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
#include <QDebug>

// ---------- TextReplaceRule 序列化 ----------
QJsonObject TextReplaceRule::toJson() const
{
    QJsonObject obj;
    obj["enabled"] = enabled;
    obj["remark"] = remark;
    obj["replaceRule"] = replaceRule;
    obj["appendText"] = appendText;
    obj["forbiddenWords"] = forbiddenWords;
    return obj;
}

TextReplaceRule TextReplaceRule::fromJson(const QJsonObject &obj)
{
    TextReplaceRule rule;
    rule.enabled = obj["enabled"].toBool(true);
    rule.remark = obj["remark"].toString("");
    rule.replaceRule = obj["replaceRule"].toString("");
    rule.appendText = obj["appendText"].toString("");
    rule.forbiddenWords = obj["forbiddenWords"].toString("");
    return rule;
}

// ---------- RowMovableTableWidget ----------
RowMovableTableWidget::RowMovableTableWidget(QWidget *parent)
    : QTableWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void RowMovableTableWidget::startDrag(Qt::DropActions supportedActions)
{
    QModelIndexList indexes = selectionModel()->selectedIndexes();
    if (!indexes.isEmpty())
        dragStartRow = indexes.first().row();
    QTableWidget::startDrag(supportedActions);
}

void RowMovableTableWidget::dropEvent(QDropEvent *event)
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

// ---------- TextReplaceConfigWidget ----------
TextReplaceConfigWidget::TextReplaceConfigWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    initTable();
    refreshRobotList();
    loadAllRulesFromFile();

    connect(this, &TextReplaceConfigWidget::needLoadRules,
            this, &TextReplaceConfigWidget::loadRulesForRobot,
            Qt::QueuedConnection);
}

TextReplaceConfigWidget::~TextReplaceConfigWidget()
{
}

void TextReplaceConfigWidget::setupUI()
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

    QLabel *ruleLabel = new QLabel("文本替换规则表 (可拖拽行首移动)");
    ruleTable = new RowMovableTableWidget;
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
        mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(mainSplitter);
    setLayout(mainLayout);

    // 信号连接
    connect(refreshRobotBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::refreshRobotList);
    connect(robotListWidget, &QListWidget::currentRowChanged, this, &TextReplaceConfigWidget::onRobotSelectionChanged);
    connect(addBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onDeleteRow);
    connect(copyRowBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onCopyRow);
    connect(copyAllBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onCopyAllRows);
    connect(pasteBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onPasteFromClipboard);
    connect(moveUpBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onMoveRowUp);
    connect(moveDownBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onMoveRowDown);
    connect(saveBtn, &QPushButton::clicked, this, &TextReplaceConfigWidget::onSaveToFile);
    connect(ruleTable, &RowMovableTableWidget::rowsSwapped, this, &TextReplaceConfigWidget::onRowsSwapped);
    connect(ruleTable, &QTableWidget::itemChanged, this, &TextReplaceConfigWidget::onTableDataChanged);
}

void TextReplaceConfigWidget::initTable()
{
    QStringList headers = {"指令|||分割", "文本替换 左边||右边|||分割", "无条件添加文本", "禁止词 |||分割"};
    ruleTable->setColumnCount(headers.size());
    ruleTable->setHorizontalHeaderLabels(headers);
    ruleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ruleTable->verticalHeader()->setVisible(true);
}

void TextReplaceConfigWidget::refreshRobotList()
{
    if (currentAppId != 0)
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
        currentAppId = 0;
        ruleTable->setRowCount(0);
    }
}

void TextReplaceConfigWidget::onRobotSelectionChanged()
{
    if (currentAppId != 0)
        saveCurrentRulesToMap();

    QListWidgetItem *cur = robotListWidget->currentItem();
    if (cur) {
        currentAppId = cur->data(Qt::UserRole).toInt();
        emit needLoadRules(currentAppId);   // 发射信号，稍后加载
    } else {
        currentAppId = 0;
        ruleTable->setRowCount(0);
    }
}
void TextReplaceConfigWidget::saveCurrentRulesToMap()
{
    if (currentAppId == 0) return;
    QList<TextReplaceRule> rules;
    for (int row = 0; row < ruleTable->rowCount(); ++row)
        rules.append(getRuleItemFromRow(row));
    rulesMap[currentAppId] = rules;
}

void TextReplaceConfigWidget::loadRulesForRobot(int appid)
{
    // 断开信号，防止加载过程中触发保存
    static bool isLoading = false;
    if (isLoading) return;
    isLoading = true;

    if (!rulesMap.contains(appid)) {
        TextReplaceRule example;
        example.enabled = true;
        example.remark = "示例指令";
        example.replaceRule = "旧文本||新文本";
        example.appendText = "（自动添加后缀）";
        example.forbiddenWords = "禁止词1|||禁止词2";
        rulesMap[appid] = {example};
    }
    bool wasBlocked = ruleTable->blockSignals(true);
    const QList<TextReplaceRule> &rules = rulesMap[appid];
    ruleTable->setRowCount(rules.size());
    for (int i = 0; i < rules.size(); ++i) {
        setRuleItemToRow(i, rules[i]);
    }
    ruleTable->blockSignals(wasBlocked);
    isLoading = false;
    connect(ruleTable, &QTableWidget::itemChanged, this, &TextReplaceConfigWidget::onTableDataChanged);
}
void TextReplaceConfigWidget::setRuleItemToRow(int row, const TextReplaceRule &item)
{
    // 确保行存在
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

    QTableWidgetItem *checkItem = ensureItem(COL_ENABLED);
    //checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    checkItem->setCheckState(item.enabled ? Qt::Checked : Qt::Unchecked);

    ensureItem(COL_REMARK)->setText(item.remark);
    ensureItem(COL_REPLACE_RULE)->setText(item.replaceRule);
    ensureItem(COL_APPEND_TEXT)->setText(item.appendText);
    ensureItem(COL_FORBIDDEN)->setText(item.forbiddenWords);
}

TextReplaceRule TextReplaceConfigWidget::getRuleItemFromRow(int row) const
{
    TextReplaceRule rule;

    if (row < 0 || row >= ruleTable->rowCount()) return rule;

    QTableWidgetItem *checkItem = ruleTable->item(row, COL_ENABLED);
    rule.enabled = checkItem && (checkItem->checkState() == Qt::Checked);

    auto text = [this, row](int col) -> QString {
        QTableWidgetItem *it = ruleTable->item(row, col);
        return it ? it->text() : "";
    };
    rule.remark = text(COL_REMARK);
    rule.replaceRule = text(COL_REPLACE_RULE);
    rule.appendText = text(COL_APPEND_TEXT);
    rule.forbiddenWords = text(COL_FORBIDDEN);
    return rule;
}

void TextReplaceConfigWidget::addRowFromRuleItem(const TextReplaceRule &item)
{
    int row = ruleTable->rowCount();
    ruleTable->insertRow(row);
    setRuleItemToRow(row, item);
    ruleTable->selectRow(row);
}

void TextReplaceConfigWidget::onAddRow()
{
    TextReplaceRule newItem;
    newItem.enabled = true;
    newItem.remark = "新规则";
    newItem.replaceRule = "left||right";
    newItem.appendText = "";
    newItem.forbiddenWords = "";
    addRowFromRuleItem(newItem);
    onTableDataChanged();  // 保存到 rulesMap
}

void TextReplaceConfigWidget::onDeleteRow()
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

void TextReplaceConfigWidget::onCopyRow()
{
    int row = ruleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要复制的行");
        return;
    }
    TextReplaceRule rule = getRuleItemFromRow(row);
    QString line = QString("%1\t%2\t%3\t%4\t%5")
                       .arg(rule.enabled ? "1" : "0",rule.remark,rule.replaceRule,rule.appendText,rule.forbiddenWords);

    QApplication::clipboard()->setText(line);
    QMessageBox::information(this, "提示", "已复制当前行到剪贴板");
}

void TextReplaceConfigWidget::onCopyAllRows()
{
    QStringList tsv = getTableAsTSV();
    if (tsv.isEmpty()) {
        QMessageBox::information(this, "提示", "没有可复制的内容");
        return;
    }
    QApplication::clipboard()->setText(tsv.join("\n"));
    QMessageBox::information(this, "提示", QString("已复制 %1 行").arg(tsv.size()));
}

void TextReplaceConfigWidget::onPasteFromClipboard()
{
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        QMessageBox::information(this, "提示", "剪贴板为空");
        return;
    }
    addRowsFromTSV(text);
    onTableDataChanged();
}

void TextReplaceConfigWidget::onMoveRowUp()
{
    int row = ruleTable->currentRow();
    if (row <= 0) {
        QMessageBox::information(this, "提示", "无法上移");
        return;
    }
    saveCurrentRulesToMap();   // 先保存当前状态
    if (!rulesMap.contains(currentAppId)) return;
    QList<TextReplaceRule> &rules = rulesMap[currentAppId];
    if (row >= rules.size()) return;
    qSwap(rules[row], rules[row-1]);
    loadRulesForRobot(currentAppId);
    ruleTable->selectRow(row-1);
}

void TextReplaceConfigWidget::onMoveRowDown()
{
    int row = ruleTable->currentRow();
    if (row < 0 || row >= ruleTable->rowCount() - 1) {
        QMessageBox::information(this, "提示", "无法下移");
        return;
    }
    saveCurrentRulesToMap();
    if (!rulesMap.contains(currentAppId)) return;
    QList<TextReplaceRule> &rules = rulesMap[currentAppId];
    if (row+1 >= rules.size()) return;
    qSwap(rules[row], rules[row+1]);
    loadRulesForRobot(currentAppId);
    ruleTable->selectRow(row+1);
}

void TextReplaceConfigWidget::onRowsSwapped(int fromRow, int toRow)
{
    if (currentAppId == 0) return;
    if (rulesMap.contains(currentAppId)) {
        QList<TextReplaceRule> &rules = rulesMap[currentAppId];
        if (fromRow >= 0 && fromRow < rules.size() && toRow >= 0 && toRow < rules.size()) {

            TextReplaceRule movingRule = rules.takeAt(fromRow);
            qDebug() << fromRow << "||" <<  toRow;
            int insertPos = toRow;
            if(toRow>=fromRow && insertPos<rules.size())
                insertPos++;
            rules.insert(insertPos, movingRule);
        }
    }
    disconnect(ruleTable, &QTableWidget::itemChanged, this, &TextReplaceConfigWidget::onTableDataChanged);
    emit needLoadRules(currentAppId);
    ruleTable->selectRow(toRow);
}

void TextReplaceConfigWidget::onTableDataChanged()
{
        if (currentAppId == 0) return;
        saveCurrentRulesToMap();
}

void TextReplaceConfigWidget::onSaveToFile()
{
    if (currentAppId != 0)
        saveCurrentRulesToMap();
    saveAllRulesToFile();
    oninitbot();
    QMessageBox::information(this, "保存成功", "规则已保存到 text_replace_rules.json");
}
void TextReplaceConfigWidget::oninitbot()
{
    if (currentAppId == 0) return;
    const QList<TextReplaceRule>& rules = rulesMap[currentAppId];
    if (rules.isEmpty()) return;
    for (auto& account : m_accounts) {
        if (account->appid_int != currentAppId) continue;
        account->zdywb.clear();
        for (const auto& rule : rules) {
            if(rule.remark.isEmpty() || rule.appendText.isEmpty()) continue;

            zdywb zd;

            zd.data = rule.appendText;
            zd.zl   = rule.remark.split("|||");
            if(rule.forbiddenWords.isEmpty())
                zd.jzc.clear();
            else
                zd.jzc = rule.forbiddenWords.split("|||");
            zd.thck.clear();
            zd.thcv.clear();
            if(rule.replaceRule.isEmpty()) continue;

            const QStringList tripleBarParts=rule.replaceRule.split("|||");
            for (const QString& part : tripleBarParts) {
                const QStringList doubleBarParts = part.split("||");
                if (doubleBarParts.size() >= 3) {
                    zd.thck.append(doubleBarParts[1]);
                    zd.thcv.append(doubleBarParts[2]);
                }
            }
            account->zdywb.append(zd);
        }
    }
}
QStringList TextReplaceConfigWidget::getTableAsTSV() const
{
    QStringList lines;
    for (int row = 0; row < ruleTable->rowCount(); ++row) {
        QStringList fields;
        QTableWidgetItem *check = ruleTable->item(row, COL_ENABLED);
        fields << (check && check->checkState() == Qt::Checked ? "1" : "0");
        fields << getRuleItemFromRow(row).remark;
        fields << getRuleItemFromRow(row).replaceRule;
        fields << getRuleItemFromRow(row).appendText;
        fields << getRuleItemFromRow(row).forbiddenWords;
        lines << fields.join("\t");
    }
    return lines;
}

void TextReplaceConfigWidget::addRowsFromTSV(const QString &tsv)
{
    QStringList lines = tsv.split("\n", Qt::SkipEmptyParts);
    int added = 0;
    for (QString &line : lines) {
        QStringList parts = line.split("\t");
        if (parts.size() >= 5) {
            TextReplaceRule rule;
            rule.enabled = (parts[0] == "1");
            rule.remark = parts[1];
            rule.replaceRule = parts[2];
            rule.appendText = parts[3];
            rule.forbiddenWords = parts[4];
            addRowFromRuleItem(rule);
            added++;
        } else if (parts.size() >= 1 && !line.trimmed().isEmpty()) {
            TextReplaceRule rule;
            rule.remark = line.trimmed();
            addRowFromRuleItem(rule);
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

void TextReplaceConfigWidget::saveAllRulesToFile(const QString &filePath)
{
    QJsonObject root;
    for (auto it = rulesMap.begin(); it != rulesMap.end(); ++it) {
        QJsonArray arr;
        for (const TextReplaceRule &rule : it.value())
            arr.append(rule.toJson());
        root[QString::number(it.key())] = arr;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save text replace rules to" << filePath;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void TextReplaceConfigWidget::loadAllRulesFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    rulesMap.clear();
    for (auto it = root.begin(); it != root.end(); ++it) {
        int appid = it.key().toInt();
        const QJsonArray arr = it.value().toArray();
        QList<TextReplaceRule> rules;
        for (const QJsonValue &val : arr) {
            if (val.isObject())
                rules.append(TextReplaceRule::fromJson(val.toObject()));
        }
        rulesMap[appid] = rules;

    }
    oninitbot();
    if (currentAppId != 0)
        loadRulesForRobot(currentAppId);
}

