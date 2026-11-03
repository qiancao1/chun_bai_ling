#include "shoppricedialog.h"
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
#include <QColor>
#include <utility>

namespace {
// 商品 / 插件注册价 / 售价（可编辑）/ 上架（下拉）
enum Col { ColItem = 0, ColBase, ColPrice, ColSale, ColCount };

// 单件上限 10 亿：防手滑打出天文数字（超过 int32 也超出正常运营范围）。
// 另：价格存进 JSON 是 double，2^53 以内不会丢精度，10 亿远低于这条线。
constexpr qint64 kMaxItemPrice = 1000000000LL;
}

ShopPriceDialog::ShopPriceDialog(PluginPage *page, int preselectIndex, QWidget *parent)
    : QDialog(parent), m_page(page)
{
    setWindowTitle("商品定价");
    setMinimumSize(760, 460);
    // 配色跟「编辑指令」窗口保持一致，免得两个页面像两个软件
    setStyleSheet(
        "QDialog { background: #F5F5F5; }"
        "QLabel { background: transparent; color: #222222; }"
        "QPushButton { background: #FFF0DE; border: 1px solid #cccccc; border-radius: 4px;"
        "              padding: 5px 12px; color: #111111; }"
        "QPushButton:hover { background: #FFE3C0; }"
        "QPushButton:disabled { color: #999999; }"
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
    QLabel *listTitle = new QLabel("有商品的插件");
    listTitle->setStyleSheet("font-size: 15px; font-weight: bold; color: #222222;");
    lv->addWidget(listTitle);
    m_pluginList = new QListWidget;
    m_pluginList->setFixedWidth(240);
    m_pluginList->setSpacing(2);
    lv->addWidget(m_pluginList);

    // ---------- 右：价格表 ----------
    QWidget *right = new QWidget;
    QVBoxLayout *rv = new QVBoxLayout(right);
    rv->setContentsMargins(3, 3, 3, 3);
    rv->setSpacing(6);

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
    m_table->setHorizontalHeaderLabels(QStringList() << "商品" << "插件注册价" << "售价" << "上架");
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked
                             | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setSectionResizeMode(ColItem, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColBase, QHeaderView::Fixed);
    m_table->setColumnWidth(ColBase, 100);
    m_table->horizontalHeader()->setSectionResizeMode(ColPrice, QHeaderView::Fixed);
    m_table->setColumnWidth(ColPrice, 110);
    m_table->horizontalHeader()->setSectionResizeMode(ColSale, QHeaderView::Fixed);
    m_table->setColumnWidth(ColSale, 84);
    rv->addWidget(m_table, 1);

    QHBoxLayout *btnRow = new QHBoxLayout;
    m_followBtn = new QPushButton("跟随默认");
    m_followBtn->setToolTip("删掉这个机器人的独立定价，重新跟随「默认（全部机器人）」。");
    m_followBtn->setVisible(false);
    btnRow->addWidget(m_followBtn);
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

    connect(m_pluginList, &QListWidget::currentRowChanged, this, &ShopPriceDialog::onPluginRowChanged);
    // ⚠ Qt5 里 currentIndexChanged 是重载的（int / QString）→ 必须显式指定 int 那个
    connect(m_botCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ShopPriceDialog::onBotChanged);
    connect(m_followBtn, &QPushButton::clicked, this, &ShopPriceDialog::onFollowDefault);
    connect(saveBtn, &QPushButton::clicked, this, &ShopPriceDialog::onSave);

    // 机器人下拉：第 0 项「默认（全部机器人）」= appid 0（与「编辑指令」同一份列表）
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

void ShopPriceDialog::reloadPluginList(int preselectIndex)
{
    m_indexes = m_page ? m_page->shopPricePluginIndexes() : QList<int>();
    m_pluginList->clear();
    for (int idx : std::as_const(m_indexes))
        m_pluginList->addItem(m_page->shopPricePluginLabel(idx));

    if (m_indexes.isEmpty()) {
        m_table->setRowCount(0);
        m_curPlugin = -1;
        m_tip->setText("当前没有任何插件在卖东西。\n"
                       "插件要在 get_plugin_info 里注册 shop 数组才会出现在这里（见 商店系统.md）。");
        return;
    }

    const int at = m_indexes.indexOf(preselectIndex);
    m_pluginList->setCurrentRow(at >= 0 ? at : 0);   // 触发 currentRowChanged → 载入右侧
}

void ShopPriceDialog::onPluginRowChanged(int row)
{
    loadRowsForListRow(row);
}

void ShopPriceDialog::onBotChanged(int comboRow)
{
    if (comboRow < 0 || comboRow >= m_botAppids.size()) return;
    m_curAppid = m_botAppids.at(comboRow);
    // 切机器人时丢掉上一档还没保存的改动：表格是只读视图，重新按当前这一档载入，
    // 免得把 A 号改的价顺手存成 B 号的。
    loadRowsForListRow(m_pluginList->currentRow());
}

void ShopPriceDialog::extracted(const ShopPriceRow &r, QTableWidgetItem *&p) {
    if (r.overridden)
        p->setForeground(QColor("#C0392B"));
}
void ShopPriceDialog::loadRowsForListRow(int listRow) {
    m_table->setRowCount(0);
    m_curPlugin = -1;
    m_followBtn->setVisible(m_curAppid > 0);
    m_followBtn->setEnabled(false);
    if (!m_page || listRow < 0 || listRow >= m_indexes.size())
        return;

    m_curPlugin = m_indexes[listRow];
    const bool botScoped = (m_curAppid > 0);
    m_followBtn->setEnabled(botScoped &&
                            m_page->hasBotShopPrices(m_curPlugin, m_curAppid));

    const QList<ShopPriceRow> rows =
        m_page->shopPriceRows(m_curPlugin, m_curAppid);
    m_table->setRowCount(rows.size());

    for (int i = 0; i < rows.size(); ++i) {
        const ShopPriceRow &r = rows[i];

        // 0 商品名：只读（改价按它认，改了等于换一件商品，所以不让改）
        QTableWidgetItem *it = new QTableWidgetItem(r.item);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        it->setToolTip(
            "插件注册时给的备注（只读）。改价按这一行认，要改名字请改插件源码。");
        m_table->setItem(i, ColItem, it);

        // 1 插件注册价：只读，让你知道「原件」是多少
        QTableWidgetItem *b = new QTableWidgetItem(QString::number(r.basePrice));
        b->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        b->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        b->setToolTip(
            "插件注册的价（只读）。你在这边改过就是覆盖，改回来就继续用它。");
        m_table->setItem(i, ColBase, b);

        // 2 售价：可编辑
        QTableWidgetItem *p = new QTableWidgetItem(QString::number(r.price));
        p->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        p->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        p->setToolTip(botScoped
                          ? "只对这个机器人生效。填 0 = "
                            "免费；填回和「插件注册价」一样 = 这个号不再单独定价。"
                          : "所有机器人共用的基准价。填 0 = "
                            "免费；填回和「插件注册价」一样 = 不覆盖。");
        extracted(r, p); // 红字 = 这一档改过
        m_table->setItem(i, ColPrice, p);

        // 3 上架：下拉。和「原生库崩过自动停用」是同一个 buyable 状态，
        //   只是原因分开写（一个 adminOff、一个
        //   broken），商店里的提示文案才能说清楚。 ⚠ 默认是「下架」（r.allowed ==
        //   false），新商品必须在这里手动改成「上架」。
        QComboBox *sale = new QComboBox;
        sale->addItems(QStringList() << "上架" << "下架");
        sale->setCurrentIndex(r.allowed ? 0 : 1);
        sale->setToolTip(
            "上架 = 能买（会扣真积分）；下架 = 商店列表里标「未上架」、买它直接被拒"
            "（不调插件、不扣钱）。\n"
            "⚠ 默认就是下架：新装插件 / "
            "新加的商品都要在这里手动改成「上架」才会卖。\n"
            "和插件执行崩溃被自动停用是同一个状态，只是原因不同。");
        m_table->setCellWidget(i, ColSale, sale);
    }

    const QString scope =
        botScoped
                              ? QString(
                                    "机器人「%1」：下面改的价只对这个号生效，别的机器人不受影响。")
                                    .arg(m_botCombo->currentText())
                              : QString("默认（全部机器人）：所有没单独定价的号都用这一套价。");
    m_tip->setText(
        scope +
        QString("\n共 %1 "
                "件商品。插件注册的价只是默认值，这里改完点「保存修改」立即生效，"
                        "不用重载插件；红字 = 这一档改过。")
                    .arg(rows.size()) +
        QStringLiteral(
            "\n⚠ "
            "商品默认全部下架：第一次装上插件（或插件新加商品）时一件都不能买，"
            "要在最右「上架」列里逐件改成「上架」才会卖 —— "
            "免得插件随手写的价格没确认就开卖。") +
        QStringLiteral(
            "\n⚠ 改价 / 上下架只影响以后的新订单，已经成交的不补差、不退款。"));
}

void ShopPriceDialog::onFollowDefault()
{
    if (!m_page || m_curPlugin < 0 || m_curAppid <= 0) return;
    m_page->clearBotShopPrices(m_curPlugin, m_curAppid);
    loadRowsForListRow(m_pluginList->currentRow());
    m_tip->setText(QString("已删掉机器人「%1」的独立定价，重新跟随默认。")
                       .arg(m_botCombo->currentText()));
}

void ShopPriceDialog::onSave()
{
    if (!m_page || m_curPlugin < 0) {
        QMessageBox::warning(this, "", "请先在左侧选择一个插件");
        return;
    }

    QHash<QString, qint64> prices;
    QHash<QString, bool>   allowed;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QTableWidgetItem *c0 = m_table->item(i, ColItem);
        QTableWidgetItem *c2 = m_table->item(i, ColPrice);
        if (!c0 || !c2) continue;
        const QString txt = c2->text().trimmed();
        bool ok = false;
        const qint64 v = txt.toLongLong(&ok);
        if (!ok || v < 0 || v > kMaxItemPrice) {
            // 一个格子填错就整体不保存：宁可不生效，也别让一半商品悄悄用了错价。
            QMessageBox::warning(this, "价格不对",
                                 QString("「%1」的售价「%2」不是有效数字。\n"
                                         "请填 0 ~ %3 之间的整数（0 = 免费）。")
                                     .arg(c0->text(), txt).arg(kMaxItemPrice));
            m_table->setCurrentCell(i, ColPrice);
            m_table->editItem(c2);
            return;
        }
        prices.insert(c0->text(), v);

        QComboBox *sale = qobject_cast<QComboBox *>(m_table->cellWidget(i, ColSale));
        allowed.insert(c0->text(), !sale || sale->currentIndex() == 0);   // 0 = 上架
    }

    m_page->applyShopPrices(m_curPlugin, prices, allowed, m_curAppid);

    const int keepRow = m_pluginList->currentRow();
    loadRowsForListRow(keepRow);   // 回显规整后的值（改回原价的会变回黑字、不再存配置）
    m_tip->setText(m_curAppid > 0
                       ? QString("已保存并立即生效：机器人「%1」共 %2 件商品的价。其他机器人不受影响。")
                             .arg(m_botCombo->currentText()).arg(prices.size())
                       : QString("已保存并立即生效（默认，全部机器人），共 %1 件商品的价。").arg(prices.size()));
}
