#include "ruleeditdialog.h"
#include "pluginpage.h"

#include <QListWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QComboBox>
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
// 第 0 列现在是一个下拉框（QComboBox 作为 cellWidget），不再用勾选框：
// 「启用类型」= 这条指令对谁能生效（禁用 / 全部人 / 超管 / 机器人管理 / 群主 / 群管理 / 代管）
// 最后一列是每行一个「测试」按钮：只跑这一条（见 PluginPage::testPluginCommand）。
enum Col { ColRole = 0, ColKey, ColNewKey, ColType, ColTest, ColCount };
}

RuleEditDialog::RuleEditDialog(PluginPage *page, int preselectIndex, QWidget *parent)
    : QDialog(parent), m_page(page)
{
    setWindowTitle("编辑指令");
    setMinimumSize(860, 480);   // 多了一列「测试」（66px），宽度跟着放宽一点
    setStyleSheet(
        "QDialog { background: #F5F5F5; }"
        "QLabel { background: transparent; color: #222222; }"
        "QPushButton { background: #FFF0DE; border: 1px solid #cccccc; border-radius: 4px;"
        "              padding: 5px 12px; color: #111111; }"
        "QPushButton:hover { background: #FFE3C0; }"
        "QListWidget, QTableWidget { background: #FFFFFF; border: 1px solid #cccccc;"
        "                            border-radius: 4px; color: #111111; }"
        "QTableWidget::item { color: #111111; }"
        "QComboBox { background: #FFFFFF; border: 1px solid #cccccc; border-radius: 3px;"
        "            padding: 1px 6px; margin: 2px; color: #111111; }"
        "QComboBox QAbstractItemView { background: #FFFFFF; color: #111111;"
        "                              selection-background-color: #FFE3C0; selection-color: #111111; }"
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

    // 机器人下拉：默认（全部机器人） / 每个账号。切换就是切「改谁的配置」
    QHBoxLayout *botRow = new QHBoxLayout;
    botRow->setSpacing(6);
    QLabel *botLabel = new QLabel("机器人：");
    botLabel->setStyleSheet("font-size: 13px; color: #222222;");
    m_botCombo = new QComboBox;
    m_botCombo->setMinimumWidth(220);
    botRow->addWidget(botLabel);
    botRow->addWidget(m_botCombo, 1);
    rv->addLayout(botRow);

    m_tip = new QLabel;
    m_tip->setWordWrap(true);
    m_tip->setStyleSheet("font-size: 12px; color: #555555;");
    rv->addWidget(m_tip);

    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels(QStringList() << "启用类型" << "指令名" << "新指令名" << "匹配方式" << "测试");
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(32);   // 要给下拉框留出行高
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setSectionResizeMode(ColRole, QHeaderView::Fixed);
    m_table->setColumnWidth(ColRole, 118);
    m_table->horizontalHeader()->setSectionResizeMode(ColKey, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColNewKey, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColType, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColTest, QHeaderView::Fixed);
    m_table->setColumnWidth(ColTest, 66);
    rv->addWidget(m_table, 1);

    QHBoxLayout *btnRow = new QHBoxLayout;
    m_followBtn = new QPushButton("跟随默认");
    m_followBtn->setToolTip("删掉这个机器人的独立配置，重新跟随「默认（全部机器人）」。");
    m_followBtn->setVisible(false);
    btnRow->addWidget(m_followBtn);
    btnRow->addStretch();
    // 一键测试：把**当前插件**的每一条指令都假跑一遍（每行的小「测试」只跑一条）
    QPushButton *testAllBtn = new QPushButton("一键测试");
    testAllBtn->setToolTip("把这个插件注册的每一条指令都假跑一遍，看哪条会把用户输入原样复读回去。\n"
                           "匹配方式是「等于」的指令会跳过（那种正文只能是命令词本身），要验它就点该行的「测试」。\n"
                           "启用类型是「禁用」的指令不会跑 —— 想单独关掉某条的测试，就把它设成「禁用」并保存。\n"
                           "⚠ 会真的执行每一条指令（发话题 / 请求 AI / 写库都照做），"
                           "只是它要说的话在发送入口被扣下，一条都发不出去。");
    btnRow->addWidget(testAllBtn);
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
    // ⚠ currentIndexChanged 在 Qt5 是重载的（int / QString）→ 必须显式选 int 那个重载
    connect(m_botCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RuleEditDialog::onBotChanged);
    connect(m_followBtn, &QPushButton::clicked, this, &RuleEditDialog::onFollowDefault);
    connect(saveBtn, &QPushButton::clicked, this, &RuleEditDialog::onSave);
    // 一键测试：测的是左边当前选中的那个插件（整插件全测，结果在独立窗口里滚动显示）
    connect(testAllBtn, &QPushButton::clicked, this, [this]() {
        if (!m_page || m_curPlugin < 0) {
            QMessageBox::information(this, "一键测试", "请先在左边选一个插件。");
            return;
        }
        m_page->testPluginCommands(m_curPlugin, m_curAppid, disabledRuleKeys());
    });

    // 机器人下拉：第 0 项「默认（全部机器人）」= appid 0
    if (m_page) {
        m_botAppids = m_page->ruleConfigBotAppids();
        m_botCombo->addItems(m_page->ruleConfigBotLabels());
    } else {
        m_botAppids << 0;
        m_botCombo->addItem("默认（全部机器人）");
    }
    m_curAppid = 0;

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

void RuleEditDialog::onBotChanged(int comboRow)
{
    if (comboRow < 0 || comboRow >= m_botAppids.size()) return;
    m_curAppid = m_botAppids.at(comboRow);
    // 切换机器人时把「另一个机器人上一秒还没保存的改动」直接丢掉 —— 表格是只读视图，
    // 重新从配置里载入当前这一档，避免把 A 的改动顺手存成 B 的。
    loadRowsForListRow(m_pluginList->currentRow());
}

void RuleEditDialog::loadRowsForListRow(int listRow)
{
    m_table->setRowCount(0);
    m_curPlugin = -1;
    // 「跟随默认」只对具体机器人有意义；先按档位定可见性，具体插件有没有配过后面再定能不能点
    m_followBtn->setVisible(m_curAppid > 0);
    m_followBtn->setEnabled(false);
    if (!m_page || listRow < 0 || listRow >= m_indexes.size()) return;

    m_curPlugin = m_indexes[listRow];
    const bool allowRename = m_page->ruleConfigAllowRename(m_curPlugin);
    // 具体机器人档：启用类型 / 新指令名都只管这个号（新指令名按「跟默认的差异」存）
    const bool botScoped   = (m_curAppid > 0);
    // 是否已经有自己的一份配置（决定「跟随默认」按钮亮不亮）
    m_followBtn->setEnabled(botScoped && m_page->hasBotRuleConfig(m_curPlugin, m_curAppid));

    const QList<RuleConfigRow> rows = m_page->ruleConfigRows(m_curPlugin, m_curAppid);
    m_table->setRowCount(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const RuleConfigRow &r = rows[i];

        // 0 启用类型：下拉框。禁用 = 该指令不参与匹配；其余按权限放行
        //   （全部人 / 超管 / 机器人管理 / 群主 / 群管理 / 代管，默认全部人）
        QComboBox *role = new QComboBox;
        role->addItems(ruleRoleComboItems());
        role->setCurrentIndex(ruleRoleToComboIndex(r.role));
        role->setToolTip("这条指令对谁能生效。数值越小权限越高：例如选「群主」，则超管 / 机器人管理 / 群主都能用；"
                         "选「代管」则 超管 / 机器人管理 / 群主 / 群管理 / 代管 都能用。\n"
                         "私聊 / 频道消息没有成员身份，只有选「全部人」以及超管 / 机器人管理能用。");
        m_table->setCellWidget(i, ColRole, role);

        // 1 指令名：插件注册的原名，只读（重命名走右边那一列；这个原名是「命中原指令」的来源）
        QTableWidgetItem *k = new QTableWidgetItem(r.key);
        k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        k->setToolTip("插件注册的原始指令（只读）。改这里没意义，请在右边填新指令名。");
        m_table->setItem(i, ColKey, k);

        // 2 新指令名：可编辑，留空 = 不改名。旧版插件 → 这一列只读。
        //   具体机器人档也能改：跟默认档一样就是「跟随默认」，不一样就是该号专用。
        QTableWidgetItem *n = new QTableWidgetItem(r.newKey);
        if (allowRename) {
            n->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            n->setToolTip(botScoped
                              ? "只对上面这个机器人生效。留空 = 这个号仍用**原指令名**（哪怕默认档改过名）；"
                                "填成跟默认档一样 = 继续跟随默认（以后默认改名它也自动跟着改）。"
                              : "留空 = 沿用原指令名。填了之后机器人用新名字触发；正文原样透传，"
                                "插件从「命中原指令」参数/字段里判断该走哪条逻辑。");
        } else {
            n->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            n->setToolTip("这个插件是旧版（原生库没导出 onMessagev3；32 位 / Python / JS 是插件信息里没带 sdk），"
                          "框架没有把指令名传给它的通道，所以不支持重命名；改「启用类型」仍然有效。");
        }
        m_table->setItem(i, ColNewKey, n);

        // 3 匹配方式：只读（真要改匹配方式请改插件源码重新注册）
        QTableWidgetItem *t = new QTableWidgetItem(matchTypeName(r.type));
        t->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        t->setData(Qt::UserRole, r.type);   // 保存时从这里取回枚举值
        m_table->setItem(i, ColType, t);

        // 4 测试：只跑这一条（不去碰别的指令，免得一堆有副作用的指令一起被打一遍）
        //   正文 = 命令词 +「测试测试测试」，插件想发出去的话在发送入口被扣下不发，
        //   回复里出现这串 = 它把用户输入原样复读回去了。
        QPushButton *testBtn = new QPushButton("测试");
        testBtn->setCursor(Qt::PointingHandCursor);
        testBtn->setStyleSheet("QPushButton { padding: 0px 8px; font-size: 12px; }");
        testBtn->setToolTip("只测这一条：正文 = 指令 +「测试测试测试」，插件想发出去的话会被扣下不发，"
                            "看回复里有没有把这串原样吐回来（有 = 该指令会复读用户输入）。\n"
                            "⚠ 会真的执行一次（发话题 / 请求 AI / 写库都会照做，只是话发不出去）。\n"
                            "这条的启用类型是「禁用」→ 不会跑（这就是「单独关掉某条指令测试」的开关）。\n"
                            "用的是**已生效**的配置，表格里没点「保存修改」的改动不参与。");
        {
            const QString ruleKey  = r.key;
            const int     ruleType = r.type;
            connect(testBtn, &QPushButton::clicked, this, [this, ruleKey, ruleType]() {
                if (!m_page || m_curPlugin < 0) return;
                // m_curAppid 就是上面下拉选的那一档；disabledRuleKeys() 是表格此刻标成「禁用」的
                // → 这一行要是被设成「禁用」，按了也不会跑（想测就先把它改回「全部人」）
                m_page->testPluginCommand(m_curPlugin, ruleType, ruleKey, m_curAppid, disabledRuleKeys());
            });
        }
        m_table->setCellWidget(i, ColTest, testBtn);
    }

    // 顶部提示：明确「现在改的是谁」
    const QString scope = botScoped
        ? QString("机器人「%1」：只管这个机器人的启用类型和新指令名，改完保存不影响别的机器人。")
              .arg(m_botCombo->currentText())
        : QString("默认（全部机器人）：所有没有单独配置过的机器人都跟这一套。");
    if (!allowRename) {
        m_tip->setText(scope + QString("\n共 %1 条指令；该插件是旧版（原生库没有 v3 入口，或插件信息里没带 sdk），"
                                       "只能改「启用类型」，不支持重命名。").arg(rows.size())
                       + QStringLiteral("\n每行右边的「测试」只跑那一条，底部「一键测试」跑整个插件"
                                        "（「等于」的指令一键测试时跳过；启用类型为「禁用」的一律不跑）："
                                        "都会真的执行，但插件要说的话被扣下不会发出去。"));
    } else {
        m_tip->setText(scope + QString("\n共 %1 条指令；「启用类型」决定谁能用这条指令（默认全部人），"
                                       "新指令名留空 = 不改名。保存后立即生效，无需重载插件。").arg(rows.size())
                       + QStringLiteral("\n每行右边的「测试」只跑那一条，底部「一键测试」跑整个插件"
                                        "（「等于」的指令一键测试时跳过；启用类型为「禁用」的一律不跑）："
                                        "都会真的执行，但插件要说的话被扣下不会发出去。"));
    }
}

void RuleEditDialog::onFollowDefault()
{
    if (!m_page || m_curPlugin < 0 || m_curAppid <= 0) return;
    m_page->clearBotRuleConfig(m_curPlugin, m_curAppid);
    loadRowsForListRow(m_pluginList->currentRow());
    m_tip->setText(QString("已删掉机器人「%1」的独立配置，重新跟随默认。")
                       .arg(m_botCombo->currentText()));
}

QStringList RuleEditDialog::disabledRuleKeys() const
{
    QStringList out;
    if (!m_table) return out;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QComboBox *roleBox = qobject_cast<QComboBox *>(m_table->cellWidget(i, ColRole));
        QTableWidgetItem *c1 = m_table->item(i, ColKey);
        QTableWidgetItem *c3 = m_table->item(i, ColType);
        if (!roleBox || !c1 || !c3) continue;
        if (ruleRoleFromComboIndex(roleBox->currentIndex()) > kRuleRoleOff) continue;   // 只有「禁用」才算
        out << QString::number(c3->data(Qt::UserRole).toInt()) + QChar(0x1f) + c1->text();
    }
    return out;
}

void RuleEditDialog::onSave()
{
    if (m_curPlugin < 0) {
        QMessageBox::warning(this, "", "请先在左侧选择一个插件");
        return;
    }

    // 旧版插件不允许改名：即使列只读了，这里也再兜一层，防止脏值写进配置。
    // （具体机器人档现在也允许改名 —— 存的是「跟默认档的差异」，见 applyRuleConfigRows。）
    const bool allowRename = m_page && m_page->ruleConfigAllowRename(m_curPlugin);

    QList<RuleConfigRow> rows;
    rows.reserve(m_table->rowCount());
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem *c1 = m_table->item(i, ColKey);
        QTableWidgetItem *c2 = m_table->item(i, ColNewKey);
        QTableWidgetItem *c3 = m_table->item(i, ColType);
        if (!c1 || !c3) continue;

        RuleConfigRow r;
        r.type    = c3->data(Qt::UserRole).toInt();
        r.key     = c1->text();
        r.newKey  = (allowRename && c2) ? c2->text().trimmed() : QString();
        // 启用类型：读第 0 列的「启用类型」下拉（下拉下标 → role：0 禁用=-1，1 全部人=0，… 6 代管=5）
        QComboBox *roleBox = qobject_cast<QComboBox *>(m_table->cellWidget(i, ColRole));
        r.role    = roleBox ? ruleRoleFromComboIndex(roleBox->currentIndex()) : kRuleRoleAll;
        if (r.newKey == r.key) r.newKey.clear();   // 填成和原指令一样 = 没改名，别白写进配置
        rows.append(r);
    }

    m_page->applyRuleConfigRows(m_curPlugin, rows, m_curAppid);

    const int keepRow = m_pluginList->currentRow();
    loadRowsForListRow(keepRow);   // 回显规整后的值
    m_tip->setText(m_curAppid > 0
                       ? QString("已保存并立即生效：机器人「%1」共 %2 条指令。其他机器人不受影响。")
                             .arg(m_botCombo->currentText()).arg(rows.size())
                       : QString("已保存并立即生效（默认，全部机器人），共 %1 条指令。").arg(rows.size()));
}
