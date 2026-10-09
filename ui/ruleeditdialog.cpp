#include "ruleeditdialog.h"
#include "pluginpage.h"

#include <QListWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMessageBox>
#include <utility>

// 匹配方式 → 中文（与「指令」列表弹窗里的叫法保持一致）
static QString matchTypeName(int t)
{
    switch (static_cast<MatchType>(t)) {
    case MatchType::Equals:     return "等于";
    case MatchType::StartsWith: return "开头";
    case MatchType::EndsWith:   return "结尾";
    case MatchType::Contains:   return "包含";
    case MatchType::Regex:      return "正则";
    case MatchType::event:      return "事件";
    }
    return "未知";
}

namespace {
enum Col { ColEnable = 0, ColKey, ColNewKey, ColType, ColCount };
}

RuleEditDialog::RuleEditDialog(PluginPage *page, int preselectIndex, QWidget *parent)
    : QDialog(parent), m_page(page)
{
    setWindowTitle("编辑指令");
    setMinimumSize(780, 470);
    setStyleSheet(
        "QDialog { background: #F5F5F5; }"
        "QLabel { background: transparent; color: #222222; }"
        "QPushButton { background: #FFF0DE; border: 1px solid #cccccc; border-radius: 4px;"
        "              padding: 5px 12px; color: #111111; }"
        "QPushButton:hover { background: #FFE3C0; }"
        "QListWidget, QTableWidget { background: #FFFFFF; border: 1px solid #cccccc;"
        "                            border-radius: 4px; color: #111111; }"
        "QTableWidget::item { color: #111111; }"
        "QHeaderView::section { background: #EFEFEF; color: #222222; border: 0px; padding: 5px; }"
        );

    QSplitter *split = new QSplitter(Qt::Horizontal, this);

    // ---------- 左：插件列表 ----------
    QWidget *left = new QWidget;
    QVBoxLayout *lv = new QVBoxLayout(left);
    lv->setContentsMargins(3, 3, 3, 3);
    lv->setSpacing(6);
    QLabel *listTitle = new QLabel("插件列表");
    listTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #222222;");
    lv->addWidget(listTitle);
    m_pluginList = new QListWidget;
    m_pluginList->setFixedWidth(240);
    m_pluginList->setSpacing(2);
    lv->addWidget(m_pluginList);

    // ---------- 右：指令表 ----------
    QWidget *right = new QWidget;
    QVBoxLayout *rv = new QVBoxLayout(right);
    rv->setContentsMargins(3, 3, 3, 3);
    rv->setSpacing(6);

    m_tip = new QLabel;
    m_tip->setWordWrap(true);
    m_tip->setStyleSheet("font-size: 12px; color: #555555;");
    rv->addWidget(m_tip);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels(QStringList() << "启用" << "指令名" << "新指令名" << "匹配方式");
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setSectionResizeMode(ColEnable, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColKey, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColNewKey, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    rv->addWidget(m_table, 1);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    QPushButton *saveBtn = new QPushButton("保存修改");
    btnRow->addWidget(saveBtn);
    rv->addLayout(btnRow);

    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->addWidget(split);

    connect(m_pluginList, &QListWidget::currentRowChanged, this, &RuleEditDialog::onPluginRowChanged);
    connect(saveBtn, &QPushButton::clicked, this, &RuleEditDialog::onSave);

    reloadPluginList(preselectIndex);
}

void RuleEditDialog::reloadPluginList(int preselectIndex)
{
    m_indexes = m_page ? m_page->ruleConfigPluginIndexes() : QList<int>();
    m_pluginList->clear();
    for (int idx : std::as_const(m_indexes))
        m_pluginList->addItem(m_page->ruleConfigPluginLabel(idx));

    if (m_indexes.isEmpty()) {
        m_table->setRowCount(0);
        m_curPlugin = -1;
        m_tip->setText("当前没有任何插件注册了指令。");
        return;
    }

    const int at = m_indexes.indexOf(preselectIndex);
    m_pluginList->setCurrentRow(at >= 0 ? at : 0);   // 触发 currentRowChanged → 载入右侧
}

void RuleEditDialog::onPluginRowChanged(int row)
{
    loadRowsForListRow(row);
}

void RuleEditDialog::loadRowsForListRow(int listRow)
{
    m_table->setRowCount(0);
    m_curPlugin = -1;
    if (!m_page || listRow < 0 || listRow >= m_indexes.size()) return;

    m_curPlugin = m_indexes[listRow];
    const bool allowRename = m_page->ruleConfigAllowRename(m_curPlugin);
    const QList<RuleConfigRow> rows = m_page->ruleConfigRows(m_curPlugin);
    m_table->setRowCount(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const RuleConfigRow &r = rows[i];

        // 0 启用：勾选 = 该指令参与匹配（默认全部启用）
        QTableWidgetItem *chk = new QTableWidgetItem;
        chk->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        chk->setCheckState(r.enabled ? Qt::Checked : Qt::Unchecked);
        chk->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, ColEnable, chk);

        // 1 指令名：插件注册的原名，只读（重命名走右边那一列；这个原名是「命中原指令」的来源）
        QTableWidgetItem *k = new QTableWidgetItem(r.key);
        k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        k->setToolTip("插件注册的原始指令（只读）。改这里没意义，请在右边填新指令名。");
        m_table->setItem(i, ColKey, k);

        // 2 新指令名：可编辑，留空 = 不改名。旧版插件 → 这一列只读。
        QTableWidgetItem *n = new QTableWidgetItem(r.newKey);
        if (allowRename) {
            n->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            n->setToolTip("留空 = 沿用原指令名。填了之后机器人用新名字触发；正文原样透传，"
                          "插件从「命中原指令」参数/字段里判断该走哪条逻辑。");
        } else {
            n->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            n->setToolTip("这个插件是旧版（原生库没导出 onMessagev3；32 位 / Python / JS 是插件信息里没带 sdk），"
                          "框架没有把指令名传给它的通道，所以不支持重命名；勾选启用仍然有效。");
        }
        m_table->setItem(i, ColNewKey, n);

        // 3 匹配方式：只读（真要改匹配方式请改插件源码重新注册）
        QTableWidgetItem *t = new QTableWidgetItem(matchTypeName(r.type));
        t->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        t->setData(Qt::UserRole, r.type);   // 保存时从这里取回枚举值
        m_table->setItem(i, ColType, t);
    }

    m_tip->setText(allowRename
                       ? QString("共 %1 条指令；勾选 = 启用，新指令名留空 = 不改名。保存后立即生效，无需重载插件。")
                             .arg(rows.size())
                       : QString("共 %1 条指令；该插件是旧版（原生库没有 v3 入口，或插件信息里没带 sdk），只能勾选启用 / 停用，不支持重命名。")
                             .arg(rows.size()));
}

void RuleEditDialog::onSave()
{
    if (m_curPlugin < 0) {
        QMessageBox::warning(this, "", "请先在左侧选择一个插件");
        return;
    }

    // 旧版插件不允许改名：即使列只读了，这里也再兜一层，防止脏值写进配置
    const bool allowRename = m_page && m_page->ruleConfigAllowRename(m_curPlugin);

    QList<RuleConfigRow> rows;
    rows.reserve(m_table->rowCount());
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem *c0 = m_table->item(i, ColEnable);
        QTableWidgetItem *c1 = m_table->item(i, ColKey);
        QTableWidgetItem *c2 = m_table->item(i, ColNewKey);
        QTableWidgetItem *c3 = m_table->item(i, ColType);
        if (!c1 || !c3) continue;

        RuleConfigRow r;
        r.type    = c3->data(Qt::UserRole).toInt();
        r.key     = c1->text();
        r.newKey  = (allowRename && c2) ? c2->text().trimmed() : QString();
        r.enabled = c0 ? (c0->checkState() == Qt::Checked) : true;
        if (r.newKey == r.key) r.newKey.clear();   // 填成和原指令一样 = 没改名，别白写进配置
        rows.append(r);
    }

    m_page->applyRuleConfigRows(m_curPlugin, rows);

    const int keepRow = m_pluginList->currentRow();
    loadRowsForListRow(keepRow);   // 回显规整后的值
    m_tip->setText(QString("已保存并立即生效，共 %1 条指令。").arg(rows.size()));
}
