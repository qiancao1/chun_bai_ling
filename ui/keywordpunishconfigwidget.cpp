#include "keywordpunishconfigwidget.h"
#include "global.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QJsonDocument>
#include <QFile>
#include <QQueue>

// ---------- 序列化 ----------
QJsonObject KeywordPunishRule::toJson() const {
    QJsonObject obj;
    obj["enabled"] = enabled;
    obj["keywords"] = keywords.join("|||");
    obj["punishType"] = punishType;
    obj["param"] = param;
    obj["extra"] = extra;
    return obj;
}

KeywordPunishRule KeywordPunishRule::fromJson(const QJsonObject &obj) {
    KeywordPunishRule rule;
    rule.enabled = obj["enabled"].toBool(true);
    rule.keywords = obj["keywords"].toString("").split("|||", Qt::SkipEmptyParts);
    rule.punishType = obj["punishType"].toInt(0);
    rule.param = obj["param"].toInt(0);
    rule.extra = obj["extra"].toString("");
    return rule;
}

// ---------- 静态成员 ----------
QMap<int, AhoCorasick> KeywordPunishConfigWidget::s_acMatchers;


// ---------- 构造/析构 ----------
KeywordPunishConfigWidget::KeywordPunishConfigWidget(QWidget *parent)
    : QWidget(parent) {
    setupUI();
    initTable();
    loadAllRulesFromFile();
}

KeywordPunishConfigWidget::~KeywordPunishConfigWidget() {}

// ---------- UI ----------
void KeywordPunishConfigWidget::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(3,3,3,3);
    QLabel *label = new QLabel("关键词惩罚规则 (第一列带启用复选框)");
    ruleTable = new QTableWidget;
    ruleTable->setAlternatingRowColors(true);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    saveBtn = new QPushButton("保存");
    addBtn = new QPushButton("添加行");
    deleteBtn = new QPushButton("删除行");
    copyRowBtn = new QPushButton("复制一行");
    copyAllBtn = new QPushButton("复制全部");
    pasteBtn = new QPushButton("从剪贴板添加");
    btnLayout->addWidget(saveBtn);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(deleteBtn);
    btnLayout->addWidget(copyRowBtn);
    btnLayout->addWidget(copyAllBtn);
    btnLayout->addWidget(pasteBtn);
    btnLayout->addStretch();

    mainLayout->addWidget(label);
    mainLayout->addWidget(ruleTable);
    mainLayout->addLayout(btnLayout);
    setLayout(mainLayout);

    connect(addBtn, &QPushButton::clicked, this, &KeywordPunishConfigWidget::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &KeywordPunishConfigWidget::onDeleteRow);
    connect(copyRowBtn, &QPushButton::clicked, this, &KeywordPunishConfigWidget::onCopyRow);
    connect(copyAllBtn, &QPushButton::clicked, this, &KeywordPunishConfigWidget::onCopyAllRows);
    connect(pasteBtn, &QPushButton::clicked, this, &KeywordPunishConfigWidget::onPasteFromClipboard);
    connect(saveBtn, &QPushButton::clicked, this, &KeywordPunishConfigWidget::onSaveToFile);
    connect(ruleTable, &QTableWidget::itemChanged, this, &KeywordPunishConfigWidget::onTableDataChanged);
}

void KeywordPunishConfigWidget::initTable() {
    QStringList headers = {"关键词 (|||分割)", "惩罚类型", "禁言(秒)", "处理后回复"};
    ruleTable->setColumnCount(headers.size());
    ruleTable->setHorizontalHeaderLabels(headers);
    ruleTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ruleTable->setColumnWidth(0, 250);
    ruleTable->setColumnWidth(1, 150);
    ruleTable->setColumnWidth(2, 150);
    ruleTable->setColumnWidth(3, 150);
    ruleTable->verticalHeader()->setVisible(true);
}

// ---------- 行数据存取 ----------
void KeywordPunishConfigWidget::setRuleItemToRow(int row, const KeywordPunishRule &rule) {
    while (row >= ruleTable->rowCount()) ruleTable->insertRow(ruleTable->rowCount());

    QTableWidgetItem *keyItem = ruleTable->item(row, 0);
    if (!keyItem) {
        keyItem = new QTableWidgetItem;
        ruleTable->setItem(row, 0, keyItem);
    }
    keyItem->setText(rule.keywords.join("|||"));
    keyItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    keyItem->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);

    QComboBox *combo = new QComboBox(ruleTable);
    combo->addItem("撤回", 0);
    combo->addItem("禁言", 1);
    combo->addItem("踢出", 2);
    combo->addItem("撤回+禁言", 3);
    combo->addItem("踢出+撤回", 4);
    combo->setCurrentIndex(combo->findData(rule.punishType));
    combo->setStyleSheet("QComboBox { border: none; background: transparent; }");
    ruleTable->setCellWidget(row, 1, combo);
    delete ruleTable->takeItem(row, 1);

    QTableWidgetItem *paramItem = ruleTable->item(row, 2);
    if (!paramItem) {
        paramItem = new QTableWidgetItem;
        ruleTable->setItem(row, 2, paramItem);
    }
    paramItem->setText(QString::number(rule.param));

    QTableWidgetItem *extraItem = ruleTable->item(row, 3);
    if (!extraItem) {
        extraItem = new QTableWidgetItem;
        ruleTable->setItem(row, 3, extraItem);
    }
    extraItem->setText(rule.extra);
}

KeywordPunishRule KeywordPunishConfigWidget::getRuleItemFromRow(int row) const {
    KeywordPunishRule rule;
    if (row < 0 || row >= ruleTable->rowCount()) return rule;

    QTableWidgetItem *keyItem = ruleTable->item(row, 0);
    if (keyItem) {
        rule.enabled = (keyItem->checkState() == Qt::Checked);
        rule.keywords = keyItem->text().split("|||", Qt::SkipEmptyParts);
    }

    QWidget *widget = ruleTable->cellWidget(row, 1);
    if (auto *combo = qobject_cast<QComboBox*>(widget)) {
        rule.punishType = combo->currentData().toInt();
    }

    QTableWidgetItem *paramItem = ruleTable->item(row, 2);
    if (paramItem) rule.param = paramItem->text().toInt();

    QTableWidgetItem *extraItem = ruleTable->item(row, 3);
    if (extraItem) rule.extra = extraItem->text();

    return rule;
}

void KeywordPunishConfigWidget::addRowFromRuleItem(const KeywordPunishRule &rule) {
    int row = ruleTable->rowCount();
    ruleTable->insertRow(row);
    setRuleItemToRow(row, rule);
    ruleTable->selectRow(row);
}

// ---------- 数据同步 ----------
void KeywordPunishConfigWidget::saveCurrentRulesToMap() {
    if (g_appid == 0) return;
    QList<KeywordPunishRule> rules;
    for (int row = 0; row < ruleTable->rowCount(); ++row)
        rules.append(getRuleItemFromRow(row));
    rulesMap[g_appid] = rules;
}

void KeywordPunishConfigWidget::refreshTable() {
    if (g_appid == 0 || !rulesMap.contains(g_appid)) return;
    bool wasBlocked = ruleTable->blockSignals(true);
    const auto &rules = rulesMap[g_appid];
    ruleTable->setRowCount(rules.size());
    for (int i = 0; i < rules.size(); ++i)
        setRuleItemToRow(i, rules[i]);
    ruleTable->blockSignals(wasBlocked);
}

void KeywordPunishConfigWidget::loadRulesForRobot(int robotId) {
    refreshTable();
    buildMatcherForRobot(robotId);
}

// ---------- 槽函数 ----------
void KeywordPunishConfigWidget::onAddRow() {
    KeywordPunishRule newRule;
    newRule.enabled = true;
    newRule.keywords << "新关键词";
    newRule.punishType = 0;
    if (g_appid != 0) {
        rulesMap[g_appid].append(newRule);
        disconnect(ruleTable, &QTableWidget::itemChanged, this, &KeywordPunishConfigWidget::onTableDataChanged);
        refreshTable();
        ruleTable->selectRow(rulesMap[g_appid].size() - 1);
    }
}

void KeywordPunishConfigWidget::onDeleteRow() {
    int row = ruleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要删除的行");
        return;
    }
    if (g_appid == 0 || !rulesMap.contains(g_appid)) return;
    QList<KeywordPunishRule> &rules = rulesMap[g_appid];
    if (row >= rules.size()) return;
    rules.removeAt(row);
    disconnect(ruleTable, &QTableWidget::itemChanged, this, &KeywordPunishConfigWidget::onTableDataChanged);
    refreshTable();
    if (row < rules.size()) ruleTable->selectRow(row);
    else if (!rules.isEmpty()) ruleTable->selectRow(rules.size() - 1);
}

void KeywordPunishConfigWidget::onCopyRow() {
    int row = ruleTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请先选中要复制的行");
        return;
    }
    KeywordPunishRule rule = getRuleItemFromRow(row);
    QString line = QString("%1\t%2\t%3\t%4\t%5")
                       .arg(rule.enabled ? "1" : "0",rule.keywords.join("|||"))
                       .arg(rule.punishType)
                       .arg(rule.param)
                       .arg(rule.extra);
    QApplication::clipboard()->setText(line);
    QMessageBox::information(this, "提示", "已复制当前行");
}

void KeywordPunishConfigWidget::onCopyAllRows() {
    QStringList tsv = getTableAsTSV();
    if (tsv.isEmpty()) {
        QMessageBox::information(this, "提示", "没有可复制的内容");
        return;
    }
    QApplication::clipboard()->setText(tsv.join("\n"));
    QMessageBox::information(this, "提示", QString("已复制 %1 行").arg(tsv.size()));
}

void KeywordPunishConfigWidget::onPasteFromClipboard() {
    QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        QMessageBox::information(this, "提示", "剪贴板为空");
        return;
    }
    addRowsFromTSV(text);
    onTableDataChanged();
}

void KeywordPunishConfigWidget::onTableDataChanged() {
    if (g_appid == 0) return;
    saveCurrentRulesToMap();
    buildMatcherForRobot(g_appid);
}

void KeywordPunishConfigWidget::onSaveToFile() {
    if (g_appid == 0) return;
    saveCurrentRulesToMap();
    saveAllRulesToFile();
    buildMatcherForRobot(g_appid);

}

// ---------- TSV 导入导出 ----------
QStringList KeywordPunishConfigWidget::getTableAsTSV() const {
    QStringList lines;
    for (int row = 0; row < ruleTable->rowCount(); ++row) {
        KeywordPunishRule rule = getRuleItemFromRow(row);
        QStringList fields;
        fields << (rule.enabled ? "1" : "0");
        fields << rule.keywords.join("|||");
        fields << QString::number(rule.punishType);
        fields << QString::number(rule.param);
        fields << rule.extra;
        lines << fields.join("\t");
    }
    return lines;
}

void KeywordPunishConfigWidget::addRowsFromTSV(const QString &tsv) {
    const QStringList lines = tsv.split("\n", Qt::SkipEmptyParts);
    int added = 0;
    for (const QString &line : lines) {
        QStringList parts = line.split("\t");
        if (parts.size() >= 5) {
            KeywordPunishRule rule;
            rule.enabled = (parts[0] == "1");
            rule.keywords = parts[1].split("|||", Qt::SkipEmptyParts);
            rule.punishType = parts[2].toInt();
            rule.param = parts[3].toInt();
            rule.extra = parts[4];
            addRowFromRuleItem(rule);
            added++;
        } else if (parts.size() >= 1 && !line.trimmed().isEmpty()) {
            KeywordPunishRule rule;
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

// ---------- 文件 I/O ----------
void KeywordPunishConfigWidget::saveAllRulesToFile(const QString &filePath) {
    QJsonObject root;
    for (auto it = rulesMap.begin(); it != rulesMap.end(); ++it) {
        QJsonArray arr;
        for (const auto &rule : it.value()) arr.append(rule.toJson());
        root[QString::number(it.key())] = arr;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save" << filePath;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void KeywordPunishConfigWidget::loadAllRulesFromFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;
    QJsonObject root = doc.object();
    rulesMap.clear();
    for (auto it = root.begin(); it != root.end(); ++it) {
        int robotId = it.key().toInt();
        QList<KeywordPunishRule> rules;
        for (const QJsonValue &val : it.value().toArray()) {
            if (val.isObject()) rules.append(KeywordPunishRule::fromJson(val.toObject()));
        }
        rulesMap[robotId] = rules;
        buildMatcherForRobot(robotId);
    }
}

// ---------- 匹配器构建 ----------
void KeywordPunishConfigWidget::buildMatcherForRobot(int appid) {
    if (!rulesMap.contains(appid)) return;
    const auto &rules = rulesMap[appid];
    AhoCorasick ac;
    for (int i = 0; i < rules.size(); ++i) {
        const auto &rule = rules[i];
        if (!rule.enabled) continue;
        for (const QString &kw : rule.keywords) {
            ac.insert(kw, i); // 关联到规则索引
        }
    }
    ac.build();
    s_acMatchers[appid] = ac;
}

// ---------- 静态匹配 ----------
bool KeywordPunishConfigWidget::match(const MessageEvent &ev) {


    if(!(ev.bitmap & BIT_PUNISH)) return false; //没开启违禁词撤回
    if(ev.member_role<2) return false; //管理员
    //if(!m_botClients.contains(ev.appid))  return false; //机器人没登录 算了注释吧
    int appid = ev.appid;
    if (!s_acMatchers.contains(appid)) return false;


    const auto &ac = s_acMatchers[appid];
    const auto &rules = rulesMap[appid];


    QSet<int> candidateSet = ac.scan(ev.msg);
    if (candidateSet.isEmpty())
        return false;

    QList<int> candidates = candidateSet.values();
    std::sort(candidates.begin(), candidates.end()); // 升序保证最早添加的优先
    int idx = candidates.first();
    const auto &ri = rules[idx];
    auto *client = m_botClients[ev.appid];
    QString text = ri.extra;
    if(ri.punishType==0 || ri.punishType==3 || ri.punishType==4)
    {
        client->delete_messages(ev.type,ev.groupId,ev.msgId,[](auto,auto){});
    }
    if(ri.punishType==1 || ri.punishType==3)
    {
        client->setGroupRestrictChatSetting(ev.groupId,ev.user,ri.param,[](auto,auto){});
    }
    if(ri.punishType==2 || ri.punishType==4)
    {
        //client->del_members(ev.type,ev.groupId,ev.user,false,0,[](auto,auto){});
    }
    if(text.startsWith("#python")) text = python_code(text,ev);
    if(!text.isEmpty())
    {
        client->send_msgAsync(ev.type,ev.groupId,QString("[违禁词处理|%1]").arg(ri.keywords.join("|||")),text,ev.msgId);
    }

    return true;
    // return PunishAction{ri.punishType, ri.param, ri.extra};
}