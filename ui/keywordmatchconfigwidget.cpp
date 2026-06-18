#include "KeywordMatchConfigWidget.h"
#include "global.h"  // 提供 extern QList<AccountInfo> m_accounts;

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QComboBox>
#include <QDebug>
#include <QQueue>
std::string g_keyuuid;
char* g_keyuuid2 = nullptr;
// ---------- KeywordMatchRule 序列化 ----------
QJsonObject KeywordMatchRule::toJson() const {
    QJsonObject obj;
    obj["enabled"] = enabled;
    obj["keywords"] = keywords.join("|||");
    obj["matchType"] = matchType;
    obj["replyContent"] = replyContent;
    obj["forbiddenWords"] = forbiddenWords.join("|||");
    return obj;
}

KeywordMatchRule KeywordMatchRule::fromJson(const QJsonObject &obj) {
    KeywordMatchRule rule;
    rule.enabled = obj["enabled"].toBool(true);
    rule.keywords = obj["keywords"].toString("").split("|||", Qt::SkipEmptyParts);
    rule.matchType = obj["matchType"].toInt(0);
    rule.replyContent = obj["replyContent"].toString("");
    rule.forbiddenWords = obj["forbiddenWords"].toString("").split("|||", Qt::SkipEmptyParts);
    return rule;
}

// ---------- MovableKeywordTable ----------
MovableKeywordTable::MovableKeywordTable(QWidget *parent) : QTableWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::InternalMove);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
}

void MovableKeywordTable::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectionModel()->selectedIndexes();
    if (!indexes.isEmpty()) dragStartRow = indexes.first().row();
    QTableWidget::startDrag(supportedActions);
}

void MovableKeywordTable::dropEvent(QDropEvent *event) {
    if (event->source() != this || dragStartRow == -1) {
        QTableWidget::dropEvent(event);
        return;
    }
    QPoint dropPos = event->pos();
    int targetRow = indexAt(dropPos).row();
    if (targetRow == -1) targetRow = rowCount();
    int fromRow = dragStartRow;
    int toRow = targetRow;
    if (fromRow == toRow) {
        dragStartRow = -1;
        event->accept();
        return;
    }
    if (fromRow < toRow) toRow--;
    emit rowsSwapped(fromRow, toRow);
    dragStartRow = -1;
    event->accept();
}

// ---------- Aho‑Corasick 实现 ----------
void AhoCorasick::insert(const QString &pattern, int patternIndex) {
    int node = 0;
    for (QChar ch : pattern) {
        if (!nodes[node].next.contains(ch)) {
            nodes[node].next[ch] = nodes.size();
            nodes.emplace_back();
        }
        node = nodes[node].next[ch];
    }
    nodes[node].output.append(patternIndex);
}

void AhoCorasick::build() {
    QQueue<int> q;
    for (auto it = nodes[0].next.begin(); it != nodes[0].next.end(); ++it) {
        int child = it.value();
        nodes[child].fail = 0;
        q.enqueue(child);
    }
    while (!q.isEmpty()) {
        int v = q.dequeue();
        for (auto it = nodes[v].next.begin(); it != nodes[v].next.end(); ++it) {
            QChar ch = it.key();
            int u = it.value();
            int f = nodes[v].fail;
            while (f != 0 && !nodes[f].next.contains(ch))
                f = nodes[f].fail;
            if (nodes[f].next.contains(ch))
                nodes[u].fail = nodes[f].next[ch];
            else
                nodes[u].fail = 0;
            nodes[u].output.append(nodes[nodes[u].fail].output);
            q.enqueue(u);
        }
    }
}

QSet<int> AhoCorasick::scan(const QString &text) const {
    QSet<int> matchedIndices;
    int state = 0;
    for (QChar ch : text) {
        while (state != 0 && !nodes[state].next.contains(ch))
            state = nodes[state].fail;
        if (nodes[state].next.contains(ch))
            state = nodes[state].next[ch];
        for (int idx : nodes[state].output)
            matchedIndices.insert(idx);
    }
    return matchedIndices;
}

// ---------- KeywordMatchConfigWidget 静态成员 ----------
QMap<int, AhoCorasick> KeywordMatchConfigWidget::s_acMatchers;
QMap<int, QList<KeywordMatchConfigWidget::RuleIndex>> KeywordMatchConfigWidget::s_rulesList;
QMap<int, QHash<QString, int>> KeywordMatchConfigWidget::s_exactMaps;
QMap<int, bool> KeywordMatchConfigWidget::s_matcherBuilt;

// ---------- KeywordMatchConfigWidget 实现 ----------
KeywordMatchConfigWidget::KeywordMatchConfigWidget(QWidget *parent) : QWidget(parent) {
    setupUI();
    initTable();
    refreshRobotList();
    loadAllRulesFromFile();
    connect(this, &KeywordMatchConfigWidget::needLoadRules,
            this, &KeywordMatchConfigWidget::loadRulesForRobot,
            Qt::QueuedConnection);
}

KeywordMatchConfigWidget::~KeywordMatchConfigWidget() {}

void KeywordMatchConfigWidget::setupUI() {
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    QWidget *leftWidget = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    QLabel *robotLabel = new QLabel("机器人昵称列表");
    robotListWidget = new QListWidget;
    refreshRobotBtn = new QPushButton("刷新列表");
    leftLayout->addWidget(robotLabel);
    leftLayout->addWidget(robotListWidget);
    leftLayout->addWidget(refreshRobotBtn);

    QWidget *rightWidget = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    QLabel *ruleLabel = new QLabel("关键词匹配规则表 (可拖拽行首移动)");
    ruleTable = new MovableKeywordTable;
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
    connect(refreshRobotBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::refreshRobotList);
    connect(robotListWidget, &QListWidget::currentRowChanged, this, &KeywordMatchConfigWidget::onRobotSelectionChanged);
    connect(addBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onDeleteRow);
    connect(copyRowBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onCopyRow);
    connect(copyAllBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onCopyAllRows);
    connect(pasteBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onPasteFromClipboard);
    connect(moveUpBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onMoveRowUp);
    connect(moveDownBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onMoveRowDown);
    connect(saveBtn, &QPushButton::clicked, this, &KeywordMatchConfigWidget::onSaveToFile);
    connect(ruleTable, &MovableKeywordTable::rowsSwapped, this, &KeywordMatchConfigWidget::onRowsSwapped);
    connect(ruleTable, &QTableWidget::itemChanged, this, &KeywordMatchConfigWidget::onTableDataChanged);
}

void KeywordMatchConfigWidget::initTable() {
    QStringList headers = {"关键词 (|||分隔多个)", "匹配类型", "回复内容", "禁止词 (|||分隔多个)"};
    ruleTable->setColumnCount(headers.size());
    ruleTable->setHorizontalHeaderLabels(headers);
    ruleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // 启用列固定宽度30，其他列自动拉伸
    //ruleTable->horizontalHeader()->setSectionResizeMode(COL_ENABLED, QHeaderView::Fixed);
    //ruleTable->setColumnWidth(COL_ENABLED, 30);
    ruleTable->verticalHeader()->setVisible(true);
}

void KeywordMatchConfigWidget::refreshRobotList() {
    if (currentRobotId != 0) saveCurrentRulesToMap();
    robotListWidget->clear();
    for (const auto &acc : std::as_const(m_accounts)) {
        if (!acc->nickname.isEmpty()) {
            QListWidgetItem *item = new QListWidgetItem(acc->nickname);
            item->setData(Qt::UserRole, acc->appid_int);
            robotListWidget->addItem(item);
        }
    }
    if (robotListWidget->count() > 0) robotListWidget->setCurrentRow(0);
    else {
        currentRobotId = 0;
        ruleTable->setRowCount(0);
    }
}

void KeywordMatchConfigWidget::onRobotSelectionChanged() {
    if (currentRobotId != 0) saveCurrentRulesToMap();
    QListWidgetItem *cur = robotListWidget->currentItem();
    if (cur) {
        currentRobotId = cur->data(Qt::UserRole).toInt();
        emit needLoadRules(currentRobotId);
    } else {
        currentRobotId = 0;
        ruleTable->setRowCount(0);
    }
}

void KeywordMatchConfigWidget::saveCurrentRulesToMap() {
    if (currentRobotId == 0) return;
    QList<KeywordMatchRule> rules;
    for (int row = 0; row < ruleTable->rowCount(); ++row)
        rules.append(getRuleItemFromRow(row));
    rulesMap[currentRobotId] = rules;
}

void KeywordMatchConfigWidget::loadRulesForRobot(int robotId) {
    static bool isLoading = false;
    if (isLoading) return;
    isLoading = true;

    bool wasBlocked = ruleTable->blockSignals(true);
    const QList<KeywordMatchRule> &rules = rulesMap[robotId];
    ruleTable->setRowCount(rules.size());
    for (int i = 0; i < rules.size(); ++i)
        setRuleItemToRow(i, rules[i]);
    ruleTable->blockSignals(wasBlocked);
    isLoading = false;

    // 构建该机器人的高性能匹配器
    buildMatcherForRobot(robotId);
}

void KeywordMatchConfigWidget::setRuleItemToRow(int row, const KeywordMatchRule &rule) {
    while (row >= ruleTable->rowCount()) ruleTable->insertRow(ruleTable->rowCount());

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
    checkItem->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);

    ensureItem(COL_KEYWORDS)->setText(rule.keywords.join("|||"));
    ensureItem(COL_REPLY)->setText(rule.replyContent);
    ensureItem(COL_FORBIDDEN)->setText(rule.forbiddenWords.join("|||"));

    QComboBox *combo = new QComboBox(ruleTable);
    combo->addItem("0精确", 0);
    combo->addItem("1指令头(首个为头,其余必含)", 1);
    combo->addItem("2包含(全部须出现)", 2);
    combo->setCurrentIndex(combo->findData(rule.matchType));
    combo->setStyleSheet("QComboBox { border: none; background: transparent; }");
    ruleTable->setCellWidget(row, COL_MATCH_TYPE, combo);
    delete ruleTable->takeItem(row, COL_MATCH_TYPE);
}

KeywordMatchRule KeywordMatchConfigWidget::getRuleItemFromRow(int row) const {
    KeywordMatchRule rule;
    if (row < 0 || row >= ruleTable->rowCount()) return rule;

    QTableWidgetItem *checkItem = ruleTable->item(row, COL_ENABLED);
    rule.enabled = checkItem && (checkItem->checkState() == Qt::Checked);

    auto text = [this, row](int col) -> QString {
        QTableWidgetItem *it = ruleTable->item(row, col);
        return it ? it->text() : "";
    };
    rule.keywords = text(COL_KEYWORDS).split("|||", Qt::SkipEmptyParts);
    rule.replyContent = text(COL_REPLY);
    rule.forbiddenWords = text(COL_FORBIDDEN).split("|||", Qt::SkipEmptyParts);

    QWidget *widget = ruleTable->cellWidget(row, COL_MATCH_TYPE);
    if (auto *combo = qobject_cast<QComboBox*>(widget))
        rule.matchType = combo->currentData().toInt();
    else
        rule.matchType = text(COL_MATCH_TYPE).toInt();

    return rule;
}

void KeywordMatchConfigWidget::addRowFromRuleItem(const KeywordMatchRule &rule) {
    int row = ruleTable->rowCount();
    ruleTable->insertRow(row);
    setRuleItemToRow(row, rule);
    ruleTable->selectRow(row);
}

void KeywordMatchConfigWidget::onAddRow() {
    KeywordMatchRule newRule;
    newRule.enabled = true;
    newRule.keywords << "新关键词";
    newRule.matchType = 2;
    newRule.replyContent = R"(#python 不带这行就是普通信息
api.outlog(f"收到来自 {msg.appid} 的消息")
__result__ = f"收到来自 {msg.appid} 的消息"
)";
    newRule.forbiddenWords.clear();
    addRowFromRuleItem(newRule);
    onTableDataChanged();
}

void KeywordMatchConfigWidget::onDeleteRow() {
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

void KeywordMatchConfigWidget::onCopyRow() {
    int row = ruleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要复制的行");
        return;
    }
    KeywordMatchRule rule = getRuleItemFromRow(row);
    QString line = QString("%1\t%2\t%3\t%4\t%5")
                       .arg(rule.enabled ? "1" : "0",rule.keywords.join("|||"))
                       .arg(rule.matchType)
                       .arg(rule.replyContent, rule.forbiddenWords.join("|||"));
    QApplication::clipboard()->setText(line);
    QMessageBox::information(this, "提示", "已复制当前行到剪贴板");
}

void KeywordMatchConfigWidget::onCopyAllRows() {
    QStringList tsv = getTableAsTSV();
    if (tsv.isEmpty()) {
        QMessageBox::information(this, "提示", "没有可复制的内容");
        return;
    }
    QApplication::clipboard()->setText(tsv.join("\n"));
    QMessageBox::information(this, "提示", QString("已复制 %1 行").arg(tsv.size()));
}

void KeywordMatchConfigWidget::onPasteFromClipboard() {
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        QMessageBox::information(this, "提示", "剪贴板为空");
        return;
    }
    addRowsFromTSV(text);
    onTableDataChanged();
}

void KeywordMatchConfigWidget::onMoveRowUp() {
    int row = ruleTable->currentRow();
    if (row <= 0) {
        QMessageBox::information(this, "提示", "无法上移");
        return;
    }
    saveCurrentRulesToMap();
    if (!rulesMap.contains(currentRobotId)) return;
    QList<KeywordMatchRule> &rules = rulesMap[currentRobotId];
    if (row >= rules.size()) return;
    qSwap(rules[row], rules[row-1]);
    loadRulesForRobot(currentRobotId);
    ruleTable->selectRow(row-1);
}

void KeywordMatchConfigWidget::onMoveRowDown() {
    int row = ruleTable->currentRow();
    if (row < 0 || row >= ruleTable->rowCount() - 1) {
        QMessageBox::information(this, "提示", "无法下移");
        return;
    }
    saveCurrentRulesToMap();
    if (!rulesMap.contains(currentRobotId)) return;
    QList<KeywordMatchRule> &rules = rulesMap[currentRobotId];
    if (row+1 >= rules.size()) return;
    qSwap(rules[row], rules[row+1]);
    loadRulesForRobot(currentRobotId);
    ruleTable->selectRow(row+1);
}

void KeywordMatchConfigWidget::onRowsSwapped(int fromRow, int toRow) {
    if (currentRobotId == 0) return;
    if (rulesMap.contains(currentRobotId)) {
        QList<KeywordMatchRule> &rules = rulesMap[currentRobotId];
        if (fromRow >= 0 && fromRow < rules.size() && toRow >= 0 && toRow < rules.size()) {
            KeywordMatchRule moving = rules.takeAt(fromRow);
            int insertPos = toRow;
            if (toRow >= fromRow && insertPos < rules.size()) insertPos++;
            rules.insert(insertPos, moving);
        }
    }
    disconnect(ruleTable, &QTableWidget::itemChanged, this, &KeywordMatchConfigWidget::onTableDataChanged);
    loadRulesForRobot(currentRobotId);
    ruleTable->selectRow(toRow);
}

void KeywordMatchConfigWidget::onTableDataChanged() {
    if (currentRobotId == 0) return;
    saveCurrentRulesToMap();
    buildMatcherForRobot(currentRobotId);
}

void KeywordMatchConfigWidget::onSaveToFile() {
    if (currentRobotId == 0) return;
    saveCurrentRulesToMap();
    saveAllRulesToFile();
    buildMatcherForRobot(currentRobotId);
    QMessageBox::information(this, "保存成功", "关键词匹配规则已保存到 keyword_match_rules.json");
}

QStringList KeywordMatchConfigWidget::getTableAsTSV() const {
    QStringList lines;
    for (int row = 0; row < ruleTable->rowCount(); ++row) {
        KeywordMatchRule rule = getRuleItemFromRow(row);
        QStringList fields;
        fields << (rule.enabled ? "1" : "0");
        fields << rule.keywords.join("|||");
        fields << QString::number(rule.matchType);
        fields << rule.replyContent;
        fields << rule.forbiddenWords.join("|||");
        lines << fields.join("\t");
    }
    return lines;
}

void KeywordMatchConfigWidget::addRowsFromTSV(const QString &tsv) {
    const QStringList lines = tsv.split("\n", Qt::SkipEmptyParts);
    int added = 0;
    for (const QString &line : lines) {
        QStringList parts = line.split("\t");
        if (parts.size() >= 5) {
            KeywordMatchRule rule;
            rule.enabled = (parts[0] == "1");
            rule.keywords = parts[1].split("|||", Qt::SkipEmptyParts);
            rule.matchType = parts[2].toInt();
            rule.replyContent = parts[3];
            rule.forbiddenWords = parts[4].split("|||", Qt::SkipEmptyParts);
            addRowFromRuleItem(rule);
            added++;
        } else if (parts.size() >= 1 && !line.trimmed().isEmpty()) {
            KeywordMatchRule rule;
            rule.keywords << line.trimmed();
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

void KeywordMatchConfigWidget::saveAllRulesToFile(const QString &filePath) {
    QJsonObject root;
    for (auto it = rulesMap.begin(); it != rulesMap.end(); ++it) {
        QJsonArray arr;
        for (const KeywordMatchRule &rule : it.value())
            arr.append(rule.toJson());
        root[QString::number(it.key())] = arr;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save keyword match rules to" << filePath;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void KeywordMatchConfigWidget::loadAllRulesFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    rulesMap.clear();
    for (auto it = root.begin(); it != root.end(); ++it) {
        int robotId = it.key().toInt();
        const QJsonArray arr = it.value().toArray();
        QList<KeywordMatchRule> rules;
        for (const QJsonValue &val : arr) {
            if (val.isObject())
                rules.append(KeywordMatchRule::fromJson(val.toObject()));
        }
        rulesMap[robotId] = rules;
        buildMatcherForRobot(robotId);
    }

    if (currentRobotId != 0)
        loadRulesForRobot(currentRobotId);
}

// ---------- 高性能匹配器构建 ----------
void KeywordMatchConfigWidget::buildMatcherForRobot(int appid) {
    if (!rulesMap.contains(appid)) return;
    const auto &rules = rulesMap[appid];

    QList<RuleIndex> ruleList;
    QHash<QString, int> exactMap;
    AhoCorasick ac;

    for (int i = 0; i < rules.size(); ++i) {
        const auto &rule = rules[i];
        if (!rule.enabled) continue;   // 只加入启用的规则

        RuleIndex ri;
        ri.ruleIdx = i;
        ri.isHeaderMode = (rule.matchType == 1);
        ri.isExactMode = (rule.matchType == 0);
        ri.reply = rule.replyContent;
        ri.keywords = rule.keywords;
        ri.keywordSet = QSet<QString>(rule.keywords.begin(), rule.keywords.end());
        ri.forbiddenSet = QSet<QString>(rule.forbiddenWords.begin(), rule.forbiddenWords.end());

        // 精确匹配模式（只有一个关键词）单独用哈希表
        if (ri.isExactMode && rule.keywords.size() == 1) {
            QString kw = rule.keywords.first();
            if (!exactMap.contains(kw))   // 保留顺序第一个
                exactMap[kw] = ruleList.size();
            ruleList.append(ri);
            continue;
        }

        // 包含匹配或指令头匹配：将所有正向关键词加入 AC 自动机
        for (const QString &kw : rule.keywords) {
            ac.insert(kw, ruleList.size());
        }
        ruleList.append(ri);
    }

    ac.build();
    s_acMatchers[appid] = ac;
    s_rulesList[appid] = ruleList;
    s_exactMaps[appid] = exactMap;
    s_matcherBuilt[appid] = true;
}

// ---------- 静态匹配接口 ----------
QString KeywordMatchConfigWidget::match(int appid, const QString &msg) {
    if (!s_matcherBuilt.value(appid, false)) return QString();
    const auto &ac = s_acMatchers[appid];
    const auto &ruleList = s_rulesList[appid];
    const auto &exactMap = s_exactMaps[appid];

    // 精确匹配优先
    if (exactMap.contains(msg)) {
        int idx = exactMap[msg];
        const auto &ri = ruleList[idx];
        // 检查禁止词
        bool forbidden = false;
        for (const QString &fw : ri.forbiddenSet) {
            if (msg.contains(fw)) { forbidden = true; break; }
        }
        if (!forbidden) return ri.reply;
    }

    // AC 自动机扫描，获取所有命中的模式索引（规则列表中的位置）
    QSet<int> candidateSet = ac.scan(msg);
    QList<int> candidates = candidateSet.values();
    std::sort(candidates.begin(), candidates.end());  // 按规则顺序处理

    for (int idx : std::as_const(candidates)) {
        const auto &ri = ruleList[idx];
        if (ri.isExactMode) continue;   // 精确模式已在上面处理

        // 检查禁止词
        bool forbidden = false;
        for (const QString &fw : ri.forbiddenSet) {
            if (msg.contains(fw)) { forbidden = true; break; }
        }
        if (forbidden) continue;

        // 根据模式匹配
        if (!ri.isHeaderMode) {   // 包含模式：所有关键词必须全部出现
            bool allHit = true;
            for (const QString &kw : ri.keywords) {
                if (!msg.contains(kw)) { allHit = false; break; }
            }
            if (allHit) return ri.reply;
        } else {                  // 指令头模式
            if (ri.keywords.isEmpty()) continue;
            QString header = ri.keywords.first();
            if (!msg.startsWith(header)) continue;
            QString remaining = msg.mid(header.length());
            bool allSubHit = true;
            for (int i = 1; i < ri.keywords.size(); ++i) {
                if (!remaining.contains(ri.keywords[i])) { allSubHit = false; break; }
            }
            if (allSubHit) return ri.reply;
        }
    }
    return QString();
}

