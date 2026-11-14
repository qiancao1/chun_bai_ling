/*
 * 纯白铃 - QQ 机器人管理平台
 * 内置功能 → 抽奖 界面实现
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "lotterypage.h"

#include "global.h"              // m_accounts / accinfo / g_appid
#include "lottery.h"
#include "multilinedelegate.h"   // 发放内容 / 发放文本 多行编辑

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// ════════════════════════════════════════════════════════════════════════════
// 白名单编辑窗（独立窗口：主界面只留一个按钮，省地方也能写备注）
// ════════════════════════════════════════════════════════════════════════════
class WhitelistDialog : public QDialog
{
public:
    explicit WhitelistDialog(const QString &text, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle(QStringLiteral("可用群白名单"));
        setMinimumSize(560, 400);

        QVBoxLayout *v = new QVBoxLayout(this);
        v->setSpacing(8);

        QLabel *tip = new QLabel(QStringLiteral(
            "留空 = 所有群都能用。把群 openid 或群名写进来就行，后面可以跟备注。\n"
            "匹配方式是「在这段文字里直接寻找文本」，能找到群 id / 群名就算命中，\n"
            "不按行拆，所以这样写都行：\n"
            "    3A1B2C3D4E5F  我的测试群\n"
            "    3A1B2C3D4E5F\n"
            "    A1B2C3D4E5F6, B2C3D4E5F6A7 两个群挤在一行"));
        tip->setObjectName("setCardHint");
        tip->setWordWrap(true);
        v->addWidget(tip);

        m_edit = new QPlainTextEdit;
        m_edit->setPlainText(text);
        m_edit->setPlaceholderText(
            QStringLiteral("群 openid 或群名，可以带备注，随便写\n留空 = 不限制"));
        v->addWidget(m_edit, 1);

        QHBoxLayout *row = new QHBoxLayout;
        QPushButton *btnClear = new QPushButton(QStringLiteral("清空（= 不限制）"));
        QPushButton *btnCancel = new QPushButton(QStringLiteral("取消"));
        QPushButton *btnOk = new QPushButton(QStringLiteral("确定"));
        btnOk->setDefault(true);
        for (QPushButton *b : {btnClear, btnCancel, btnOk})
            b->setCursor(Qt::PointingHandCursor);
        row->addWidget(btnClear);
        row->addStretch();
        row->addWidget(btnCancel);
        row->addWidget(btnOk);
        v->addLayout(row);

        connect(btnClear,  &QPushButton::clicked, m_edit, &QPlainTextEdit::clear);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(btnOk,     &QPushButton::clicked, this, &QDialog::accept);
    }

    QString text() const { return m_edit->toPlainText(); }

private:
    QPlainTextEdit *m_edit = nullptr;
};

// 抽奖列表的列
enum ListCol { LcName = 0, LcDrawAt, LcStatus, LcPlayers, LcWinners, LcCount };
// 奖品配置的列
enum PrizeCol { PcName = 0, PcCount, PcLevel, PcText, PcColCount };
// 中奖人的列
enum WinnerCol { WcNick = 0, WcOpenid, WcPrize, WcText, WcDelivered, WcCount };

QTableWidgetItem *readOnlyItem(const QString &text)
{
    auto *it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

// 「中奖后发放内容」跟份数对不对得上 —— 返回问题列表（空 = 没问题）。
// 口径（跟 core/lottery.cpp 里 textFor 一致）：
//   · 完全没写          = 没配发放内容（管理员手动发）→ 中奖人领奖时提示「未配置」
//   · 只写一条（无 |||）= 这一档所有人领同一条，合法，不算问题
//   · 条数 < 份数       = 多出来的人没码（不会循环重发）→ 一样提示未配置
//   · 条数 > 份数       = 末尾几条永远用不上
QStringList prizeTextProblems(const QList<LotteryPrize> &prizes)
{
    const QString kSep = QStringLiteral("|||");
    QStringList out;
    for (int i = 0; i < prizes.size(); ++i) {
        const LotteryPrize &p = prizes.at(i);
        const QString who = p.name.trimmed().isEmpty()
            ? QStringLiteral("第 %1 档奖品").arg(i + 1)
            : QStringLiteral("「%1」").arg(p.name.trimmed());
        const int slotCount = qMax(0, p.count);   // 这一档几份（别叫 slots：Qt 的 slots 是空宏）
        const QString body = p.text.trimmed();

        if (body.isEmpty()) {
            out << QStringLiteral("%1 没写发放内容：中奖人领奖会提示「未配置」，需要你手动发").arg(who);
            continue;
        }
        // 只有一条 = 这一档所有人领同一条（合法用法，不提示）：3 份奖写一行
        // 「恭喜你中了一等奖!」→ 3 个人领到的都是这句。
        if (!body.contains(kSep)) continue;

        const int n = body.split(kSep, Qt::SkipEmptyParts).size();
        if (n < slotCount) {
            out << QStringLiteral("%1 写了 %2 条，但这一档有 %3 份：后面 %4 人领奖会提示未配置")
                       .arg(who).arg(n).arg(slotCount).arg(slotCount - n);
        } else if (n > slotCount) {
            out << QStringLiteral("%1 写了 %2 条，但这一档只有 %3 份：末尾 %4 条用不上")
                       .arg(who).arg(n).arg(slotCount).arg(n - slotCount);
        }
    }
    return out;
}

QString problemsToText(const QStringList &problems)
{
    return QStringLiteral("发放内容和人数对不上：\n· ") + problems.join(QStringLiteral("\n· "));
}

} // namespace

LotteryPage::LotteryPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    // 机器人侧（参与 / 自动开奖 / 领取）改完会发 changed，界面跟着刷新
    if (g_lottery) {
        connect(g_lottery, &LotteryMgr::changed, this, [this](int appid) {
            if (m_appid == 0 || appid != int(m_appid)) return;
            m_dirty = false;                 // 外部改动优先，界面上的半成品丢弃
            reloadList();
        });
        m_changedHook = true;
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 界面搭建
// ════════════════════════════════════════════════════════════════════════════

void LotteryPage::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    // 上边留一点，别让表格/按钮直接贴在页签栏下面那条分隔线上
    root->setContentsMargins(0, 8, 0, 4);
    root->setSpacing(8);

    // ---------- 主体：左列表 / 右详情 ----------
    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);

    // 左：抽奖列表（按钮放在列表下面，省顶部一行）
    m_list = new QTableWidget(0, LcCount);
    m_list->setHorizontalHeaderLabels(
        {QStringLiteral("抽奖名"), QStringLiteral("开奖时间"), QStringLiteral("状态"),
         QStringLiteral("参与"), QStringLiteral("中奖")});
    m_list->verticalHeader()->setVisible(false);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setAlternatingRowColors(true);
    m_list->horizontalHeader()->setSectionResizeMode(LcName, QHeaderView::Stretch);
    m_list->horizontalHeader()->setSectionResizeMode(LcDrawAt, QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setSectionResizeMode(LcStatus, QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setSectionResizeMode(LcPlayers, QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setSectionResizeMode(LcWinners, QHeaderView::ResizeToContents);
    m_list->setMinimumWidth(360);

    m_btnNew = new QPushButton(QStringLiteral("新建抽奖"));
    m_btnDelete = new QPushButton(QStringLiteral("删除"));
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"));
    for (QPushButton *b : {m_btnNew, m_btnDelete, m_btnRefresh})
        b->setCursor(Qt::PointingHandCursor);

    // 启用抽奖：每个机器人一份。关着的时候机器人侧完全不认这些指令（连用法都不回），
    // 数据都留着，打开就恢复。默认关 —— 不需要抽奖的用户不会被抢走 # 开头的指令。
    m_chkEnabled = new QCheckBox(QStringLiteral("启用抽奖"));
    m_chkEnabled->setCursor(Qt::PointingHandCursor);
    m_chkEnabled->setToolTip(QStringLiteral(
        "勾上：机器人才响应 #参与抽奖 / #领取抽奖 / #抽奖帮助 等指令。\n"
        "没勾：这些指令完全不匹配、也不回复（抽奖数据保留，随时能再打开）。\n"
        "这个是每个机器人单独记的，切号会跟着变。"));
    connect(m_chkEnabled, &QCheckBox::toggled, this, &LotteryPage::onToggleEnabled);

    // 标题不再单独占一行：塞到按钮行右边当小字（显示当前机器人）
    m_title = new QLabel(QStringLiteral("抽奖管理"));
    m_title->setObjectName("setCardHint");

    QWidget *leftSide = new QWidget;
    QVBoxLayout *lvLeft = new QVBoxLayout(leftSide);
    lvLeft->setContentsMargins(0, 0, 0, 0);
    lvLeft->setSpacing(6);
    lvLeft->addWidget(m_list, 1);
    QHBoxLayout *lBtnRow = new QHBoxLayout;
    lBtnRow->setSpacing(6);
    lBtnRow->addWidget(m_btnNew);
    lBtnRow->addWidget(m_btnDelete);
    lBtnRow->addWidget(m_btnRefresh);
    lBtnRow->addWidget(m_chkEnabled);      // 红色框那个空位：启用开关
    lBtnRow->addStretch();
    lBtnRow->addWidget(m_title);
    lvLeft->addLayout(lBtnRow);
    splitter->addWidget(leftSide);

    // 右：详情（右边留边距，别让按钮顶到窗口边）
    QWidget *right = new QWidget;
    QVBoxLayout *rv = new QVBoxLayout(right);
    rv->setContentsMargins(8, 0, 12, 0);
    rv->setSpacing(8);

    // ---- 基本信息（不带头标题，省高度）----
    QWidget *info = new QWidget;
    // 竖向 Fixed：窗口矮的时候宁可让下面的表格滚动，也别把信息区压扁、标签叠在一起
    info->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QVBoxLayout *iv = new QVBoxLayout(info);
    iv->setContentsMargins(0, 0, 0, 0);
    iv->setSpacing(6);

    // 三个操作按钮不在这里显示：整页最下面一行才有位置，也不挤上面的配置行。
    // 对象先建出来，底部行统一 addWidget。
    m_btnSave  = new QPushButton(QStringLiteral("保存配置"), this);
    m_btnDraw  = new QPushButton(QStringLiteral("立即开奖"), this);
    m_btnClose = new QPushButton(QStringLiteral("停止抽奖"), this);
    m_btnClose->setToolTip(QStringLiteral("停止本场抽奖：这场作废、参与名单清空、不会再开奖"));
    for (QPushButton *b : {m_btnSave, m_btnDraw, m_btnClose}) {
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(0);            // 窗口窄的时候允许压缩，别把别的控件挤出界
        b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    QHBoxLayout *r1 = new QHBoxLayout;
    r1->addWidget(new QLabel(QStringLiteral("抽奖名")));
    m_name = new QLineEdit;
    m_name->setPlaceholderText(QStringLiteral("如：周末送键盘"));
    r1->addWidget(m_name, 2);
    r1->addWidget(new QLabel(QStringLiteral("开奖时间")));
    m_drawAt = new QDateTimeEdit;
    m_drawAt->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_drawAt->setCalendarPopup(true);
    m_drawAt->setMinimumWidth(150);
    r1->addWidget(m_drawAt);

    // 满人开奖：参与人数一凑够就立刻开，不用等到开奖时间
    m_chkFull = new QCheckBox(QStringLiteral("满"));
    m_chkFull->setCursor(Qt::PointingHandCursor);
    m_chkFull->setToolTip(QStringLiteral("勾上后：参与人数凑够就从那一刻直接开奖，不等开奖时间"));
    r1->addWidget(m_chkFull);
    m_spinFull = new QSpinBox;
    m_spinFull->setRange(1, 99999);
    m_spinFull->setValue(1);
    m_spinFull->setFixedWidth(64);
    m_spinFull->setSuffix(QStringLiteral(" 人"));
    m_spinFull->setToolTip(QStringLiteral("参与人数达到这个数就开奖"));
    m_spinFull->hide();                   // 没勾的时候不占地方
    r1->addWidget(m_spinFull);
    QLabel *fullHint = new QLabel(QStringLiteral("开奖"));
    fullHint->setObjectName("setCardHint");
    r1->addWidget(fullHint);
    iv->addLayout(r1);

    // 白名单：点按钮弹独立窗口编辑（文本框收进对话框，主界面不再占高度）
    m_btnWhitelist = new QPushButton(this);
    m_btnWhitelist->setCursor(Qt::PointingHandCursor);
    m_btnWhitelist->setToolTip(QStringLiteral("写群 openid 或群名，后面能跟备注（按寻找文本匹配，不拆行）"));
    connect(m_btnWhitelist, &QPushButton::clicked, this, &LotteryPage::onEditWhitelist);
    refreshWhitelistButton();

    // 中奖人那两个操作仍走右键菜单（表格占满宽度，塞不进按钮）。
    // 按钮对象留着只当「可用状态」载体：不进布局 = 不显示，但 loadRecord 里的
    // setEnabled 逻辑照旧，菜单项据此灰显。
    m_btnMarkDelivered = new QPushButton(QStringLiteral("标记已发放"), this);
    m_btnCopyWinner = new QPushButton(QStringLiteral("复制领奖指令"), this);
    for (QPushButton *b : {m_btnMarkDelivered, m_btnCopyWinner})
        b->hide();

    info->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(info, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &) { showInfoMenu(); });
    rv->addWidget(info);

    // ---- 奖品配置 ----
    QWidget *prizeBox = new QWidget;
    QVBoxLayout *pv = new QVBoxLayout(prizeBox);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(6);

    m_prizeTable = new QTableWidget(0, PcColCount);
    m_prizeTable->setHorizontalHeaderLabels(
        {QStringLiteral("奖品名"), QStringLiteral("人数"), QStringLiteral("等级"),
         QStringLiteral("中奖后发放内容（多条用 ||| 分隔）")});
    m_prizeTable->verticalHeader()->setVisible(false);
    m_prizeTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_prizeTable->horizontalHeader()->setSectionResizeMode(PcName, QHeaderView::Interactive);
    m_prizeTable->horizontalHeader()->setSectionResizeMode(PcCount, QHeaderView::ResizeToContents);
    m_prizeTable->horizontalHeader()->setSectionResizeMode(PcLevel, QHeaderView::Interactive);
    m_prizeTable->horizontalHeader()->setSectionResizeMode(PcText, QHeaderView::Stretch);
    m_prizeTable->setColumnWidth(PcName, 150);
    m_prizeTable->setColumnWidth(PcLevel, 100);
    m_prizeTable->setMinimumHeight(112);
    // 发放内容一格里要能写多条（回车换行、Ctrl+回车确认）
    MultiLineDelegate *ml = new MultiLineDelegate(m_prizeTable);
    m_prizeTable->setItemDelegateForColumn(PcText, ml);
    m_prizeTable->setWordWrap(true);
    pv->addWidget(m_prizeTable);

    QLabel *pHint = new QLabel(QStringLiteral(
        "右键表格添加/删除奖品；人数之和 = 中奖名额。"
        "发放内容一人一条用 ||| 分隔；只写一条 = 这一档所有人领同一条；完全空着 = 未配置（提示手动发）"));
    pHint->setObjectName("setCardHint");
    pHint->setWordWrap(true);
    pv->addWidget(pHint);

    m_prizeTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_prizeTable, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &) { showPrizeMenu(); });
    rv->addWidget(prizeBox, 1);

    // ---- 中奖人 ----
    QWidget *winnerBox = new QWidget;
    QVBoxLayout *wv = new QVBoxLayout(winnerBox);
    wv->setContentsMargins(0, 0, 0, 0);
    wv->setSpacing(6);

    m_winnerTable = new QTableWidget(0, WcCount);
    m_winnerTable->setHorizontalHeaderLabels(
        {QStringLiteral("昵称"), QStringLiteral("用户ID"),
         QStringLiteral("奖品"), QStringLiteral("发放的文本"), QStringLiteral("是否发放")});
    m_winnerTable->verticalHeader()->setVisible(false);
    m_winnerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_winnerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_winnerTable->horizontalHeader()->setSectionResizeMode(WcNick, QHeaderView::Interactive);
    m_winnerTable->horizontalHeader()->setSectionResizeMode(WcOpenid, QHeaderView::Interactive);
    m_winnerTable->horizontalHeader()->setSectionResizeMode(WcPrize, QHeaderView::Interactive);
    m_winnerTable->horizontalHeader()->setSectionResizeMode(WcText, QHeaderView::Stretch);
    m_winnerTable->horizontalHeader()->setSectionResizeMode(WcDelivered, QHeaderView::ResizeToContents);
    // 昵称 / 奖品压窄一点，剩下的宽度全给「发放的文本」（兑换码、领取说明都在那一列）
    m_winnerTable->setColumnWidth(WcNick, 96);
    m_winnerTable->setColumnWidth(WcOpenid, 150);
    m_winnerTable->setColumnWidth(WcPrize, 104);
    // 窗口再窄也不许把列压没：拉不动了宁可出横向滚动条（Stretch 列也受这个下限约束）
    m_winnerTable->horizontalHeader()->setMinimumSectionSize(72);
    // 用户ID 一长串 openid 谁也看不懂 → 不占地方显示，需要时右键「复制用户ID」
    // （列本身留着：标记已发放 / 复制 都靠这一列的 Qt::UserRole 定位人）
    m_winnerTable->setColumnHidden(WcOpenid, true);
    m_winnerTable->setMinimumHeight(112);
    m_winnerTable->setItemDelegateForColumn(WcText, ml);
    m_winnerTable->setToolTip(
        QStringLiteral("右键表格：标记已发放 / 复制领奖指令 / 复制用户ID / 复制发放内容\n"
                       "「发放的文本」是开奖时定下来的，鼠标停住看全文"));
    wv->addWidget(m_winnerTable);

    // 整页最下面一行：左边白名单入口 + 状态，右边三个操作按钮
    QHBoxLayout *wFoot = new QHBoxLayout;
    wFoot->setSpacing(8);
    wFoot->addWidget(m_btnWhitelist);
    m_status = new QLabel;
    m_status->setObjectName("setCardHint");
    m_status->setMinimumWidth(0);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_status->setToolTip(QStringLiteral("右键左侧列表 / 右侧信息区：保存配置 · 立即开奖 · 停止本场抽奖"));
    wFoot->addWidget(m_status);
    wFoot->addStretch();
    wFoot->addWidget(m_btnSave);
    wFoot->addWidget(m_btnDraw);
    wFoot->addWidget(m_btnClose);
    wv->addLayout(wFoot);

    m_winnerTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_winnerTable, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &) { showWinnerMenu(); });
    rv->addWidget(winnerBox, 1);

    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({380, 900});
    root->addWidget(splitter, 1);

    // ---------- 信号 ----------
    connect(m_list, &QTableWidget::itemSelectionChanged, this, &LotteryPage::onListSelectionChanged);
    connect(m_btnNew, &QPushButton::clicked, this, &LotteryPage::onNewLottery);
    connect(m_btnDelete, &QPushButton::clicked, this, &LotteryPage::onDeleteLottery);
    connect(m_btnRefresh, &QPushButton::clicked, this, &LotteryPage::reloadList);
    connect(m_btnSave, &QPushButton::clicked, this, &LotteryPage::onSaveCurrent);
    connect(m_btnDraw, &QPushButton::clicked, this, &LotteryPage::onDrawNow);
    connect(m_btnClose, &QPushButton::clicked, this, &LotteryPage::onCloseCurrent);
    connect(m_name, &QLineEdit::textChanged, this, &LotteryPage::onMarkDirty);
    connect(m_drawAt, &QDateTimeEdit::dateTimeChanged, this, &LotteryPage::onMarkDirty);
    connect(m_chkFull, &QCheckBox::toggled, this, [this](bool on) {
        m_spinFull->setVisible(on);       // 不勾就不占宽度
        onMarkDirty();
    });
    connect(m_spinFull, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &LotteryPage::onMarkDirty);
    connect(m_prizeTable, &QTableWidget::cellChanged, this, &LotteryPage::onMarkDirty);

    // 左侧列表右键：新建 / 删除 / 刷新 + 保存 / 开奖 / 停止（按钮也留在列表下方）
    // 右键哪一行就先选中那一行，避免「操作了上一场」的误操作
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                const QModelIndex idx = m_list->indexAt(pos);
                if (idx.isValid()) m_list->selectRow(idx.row());
                showListMenu();
            });

    clearDetail();
}

// ════════════════════════════════════════════════════════════════════════════
// 切号 / 显示
// ════════════════════════════════════════════════════════════════════════════

void LotteryPage::setAppId(uint32_t appid)
{
    if (m_appid == appid) {
        if (m_list->rowCount() == 0 && !m_currentId.isEmpty()) reloadList();
        return;
    }

    // 旧账号上还没保存的改动先落库，再换数据
    savePendingIfDirty();

    m_appid = appid;
    m_currentId.clear();
    m_current = LotteryRecord();
    clearDetail();

    QString botName;
    const int idx = accinfo(int(appid));
    if (appid != 0 && idx >= 0 && idx < m_accounts.size() && m_accounts[idx])
        botName = m_accounts[idx]->Ai_nickname;

    // 小字提示，只写关键信息（列表下方空间有限）
    if (appid == 0)
        m_title->setText(QStringLiteral("未选择机器人"));
    else if (botName.isEmpty())
        m_title->setText(QStringLiteral("机器人 %1").arg(appid));
    else
        m_title->setText(QStringLiteral("%1（%2）").arg(botName).arg(appid));

    refreshEnabledCheck();          // 启用开关跟着机器人走

    if (!m_changedHook && g_lottery) {
        connect(g_lottery, &LotteryMgr::changed, this, [this](int a) {
            if (m_appid == 0 || a != int(m_appid)) return;
            reloadList();
        });
        m_changedHook = true;
    }

    // 白名单跟着「这一场」走，loadRecord 里填，切号时由 reloadList 重新加载
    reloadList();
}

void LotteryPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (g_appid != 0 && m_appid != g_appid) setAppId(g_appid);
    else if (m_appid != 0 && m_list->rowCount() == 0) reloadList();
}

// ════════════════════════════════════════════════════════════════════════════
// 列表
// ════════════════════════════════════════════════════════════════════════════

void LotteryPage::reloadList()
{
    const QString keep = m_currentId;

    m_loading = true;
    m_list->setRowCount(0);

    if (g_lottery && m_appid != 0) {
        const QList<LotteryRecord> all = g_lottery->list(int(m_appid));
        for (const LotteryRecord &r : all) {
            const int row = m_list->rowCount();
            m_list->insertRow(row);

            QTableWidgetItem *name = readOnlyItem(r.name.isEmpty() ? r.id : r.name);
            name->setData(Qt::UserRole, r.id);
            m_list->setItem(row, LcName, name);

            m_list->setItem(row, LcDrawAt, readOnlyItem(
                r.drawAt > 0 ? QDateTime::fromSecsSinceEpoch(r.drawAt)
                                   .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                             : QStringLiteral("—")));
            m_list->setItem(row, LcStatus, readOnlyItem(r.statusText()));
            m_list->setItem(row, LcPlayers, readOnlyItem(QString::number(r.joinedCount())));
            m_list->setItem(row, LcWinners, readOnlyItem(
                QStringLiteral("%1/%2").arg(r.winners.size()).arg(r.totalSlots())));
        }
    }
    m_loading = false;

    // 选中并加载（保持原来选中的那一场；没了就取第一行）
    QString target = keep;
    if (target.isEmpty() && m_list->rowCount() > 0 && m_list->item(0, LcName))
        target = m_list->item(0, LcName)->data(Qt::UserRole).toString();

    int rowToSelect = -1;
    for (int r = 0; r < m_list->rowCount(); ++r) {
        if (m_list->item(r, LcName) && m_list->item(r, LcName)->data(Qt::UserRole).toString() == target) {
            rowToSelect = r;
            break;
        }
    }
    if (rowToSelect < 0 && m_list->rowCount() > 0) {
        rowToSelect = 0;
        target = m_list->item(0, LcName)->data(Qt::UserRole).toString();
    }

    {
        QSignalBlocker blocker(m_list);
        if (rowToSelect >= 0) m_list->setCurrentCell(rowToSelect, 0);
    }

    if (target.isEmpty()) {
        m_currentId.clear();
        clearDetail();
    } else {
        // 一律重读：参与人数、中奖结果可能刚被机器人侧改过，光看 id 没变就不刷新会显示旧数据
        m_currentId = target;
        LotteryRecord rec;
        if (g_lottery && g_lottery->get(int(m_appid), target, rec)) loadRecord(rec);
        else clearDetail();
    }
}

void LotteryPage::onListSelectionChanged()
{
    if (m_loading) return;
    const int row = m_list->currentRow();
    if (row < 0 || !m_list->item(row, LcName)) return;

    const QString id = m_list->item(row, LcName)->data(Qt::UserRole).toString();
    if (id.isEmpty() || id == m_currentId) return;

    savePendingIfDirty();

    LotteryRecord rec;
    if (g_lottery && g_lottery->get(int(m_appid), id, rec)) {
        loadRecord(rec);
    } else {
        m_currentId.clear();
        clearDetail();
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 详情
// ════════════════════════════════════════════════════════════════════════════

void LotteryPage::loadRecord(const LotteryRecord &rec)
{
    m_loading = true;
    m_current = rec;
    m_currentId = rec.id;

    m_name->setText(rec.name);
    m_drawAt->setDateTime(QDateTime::fromSecsSinceEpoch(
        rec.drawAt > 0 ? rec.drawAt : QDateTime::currentSecsSinceEpoch()));
    m_whitelist = rec.whitelist;              // 只在对话框里编辑，这里只存文本
    refreshWhitelistButton();

    {
        const QSignalBlocker b1(m_chkFull), b2(m_spinFull);
        m_chkFull->setChecked(rec.autoDrawCount > 0);
        m_spinFull->setValue(rec.autoDrawCount > 0 ? rec.autoDrawCount
                                                  : qMax(1, rec.totalSlots()));
        m_spinFull->setVisible(rec.autoDrawCount > 0);
    }

    // 奖品表
    m_prizeTable->setRowCount(0);
    for (const LotteryPrize &p : rec.prizes) {
        const int row = m_prizeTable->rowCount();
        m_prizeTable->insertRow(row);
        m_prizeTable->setItem(row, PcName, new QTableWidgetItem(p.name));
        m_prizeTable->setItem(row, PcCount, new QTableWidgetItem(QString::number(p.count)));
        m_prizeTable->setItem(row, PcLevel, new QTableWidgetItem(p.level));
        m_prizeTable->setItem(row, PcText, new QTableWidgetItem(p.text));
    }

    refreshWinnerTable(rec);

    const bool running = (rec.status == LotteryRunning);
    m_btnSave->setEnabled(true);
    m_btnDraw->setEnabled(running);
    m_btnClose->setEnabled(running);
    m_btnMarkDelivered->setEnabled(!rec.winners.isEmpty());

    // 左下角那行很窄，只放「列表里没有的」关键信息：开奖公布到哪个会话。
    // 状态 / 参与人数 / 中奖人数 左侧列表已经有列，不重复占字。
    m_status->setText(rec.convId.isEmpty()
                          ? QStringLiteral("公布群：待带入")
                          : QStringLiteral("公布群：%1").arg(
                                rec.convId.size() > 14
                                    ? rec.convId.left(12) + QStringLiteral("…")
                                    : rec.convId));
    m_status->setToolTip(QStringLiteral("状态：%1 · 参与 %2 人 · 中奖名额 %3%4\n公布群 = 开奖公告发到哪；"
                                        "不用手填，第一个参与者进来时自动记录")
                             .arg(rec.statusText())
                             .arg(rec.joinedCount())
                             .arg(rec.totalSlots())
                             .arg(rec.status == LotteryDrawn
                                      ? QStringLiteral(" · 实际中奖 %1 人").arg(rec.winners.size())
                                      : QString()));

    m_dirty = false;
    m_loading = false;
}

void LotteryPage::refreshWinnerTable(const LotteryRecord &rec)
{
    m_winnerTable->setRowCount(0);
    for (const LotteryWinner &w : rec.winners) {
        const int row = m_winnerTable->rowCount();
        m_winnerTable->insertRow(row);

        QTableWidgetItem *openid = readOnlyItem(w.openid);
        openid->setData(Qt::UserRole, w.openid);      // 标记已发放按 openid 定位
        // 后台也优先显示安全昵称（平台审核过的），没有才用原始昵称
        m_winnerTable->setItem(row, WcNick, readOnlyItem(
            w.safeNick.trimmed().isEmpty() ? w.nickname : w.safeNick));
        m_winnerTable->setItem(row, WcOpenid, openid);
        m_winnerTable->setItem(row, WcPrize, readOnlyItem(
            w.level.isEmpty() ? w.prizeName : QStringLiteral("%1·%2").arg(w.level, w.prizeName)));
        // 空 = 这一档没配兑换码（或条数不够）→ 中奖人领不到，得手动发；真内容存在 UserRole 里
        QTableWidgetItem *textIt = readOnlyItem(w.text.trimmed().isEmpty()
            ? QStringLiteral("（未配置，需手动发）") : w.text);
        textIt->setData(Qt::UserRole, w.text);
        m_winnerTable->setItem(row, WcText, textIt);
        m_winnerTable->setItem(row, WcDelivered, readOnlyItem(
            w.delivered ? QStringLiteral("已发放") : QStringLiteral("未发放")));
    }
    m_btnMarkDelivered->setEnabled(!rec.winners.isEmpty());
    m_btnCopyWinner->setEnabled(!rec.winners.isEmpty());
}

void LotteryPage::clearDetail()
{
    m_loading = true;
    m_name->clear();
    m_drawAt->setDateTime(QDateTime::currentDateTime());
    m_whitelist.clear();
    refreshWhitelistButton();
    {
        const QSignalBlocker b1(m_chkFull), b2(m_spinFull);
        m_chkFull->setChecked(false);
        m_spinFull->setValue(1);
        m_spinFull->hide();
    }
    m_prizeTable->setRowCount(0);
    m_winnerTable->setRowCount(0);
    m_btnSave->setEnabled(false);
    m_btnDraw->setEnabled(false);
    m_btnClose->setEnabled(false);
    m_btnMarkDelivered->setEnabled(false);
    m_btnCopyWinner->setEnabled(false);
    m_status->setText(m_appid == 0 ? QStringLiteral("未选择机器人")
                                   : QStringLiteral("还没有抽奖，点左下「新建抽奖」"));
    m_status->setToolTip(QString());
    m_dirty = false;
    m_loading = false;
}

bool LotteryPage::collectRecord(LotteryRecord &rec) const
{
    rec = m_current;                 // players / winners / createdAt 原样带回去
    rec.appid  = int(m_appid);
    rec.id     = m_currentId;
    rec.name   = m_name->text().trimmed();
    rec.drawAt = m_drawAt->dateTime().toSecsSinceEpoch();

    // convId / ctype 不用管界面：第一个参与者进来时由机器人侧写进去
    rec.whitelist = m_whitelist;
    rec.autoDrawCount = m_chkFull->isChecked() ? m_spinFull->value() : 0;

    rec.prizes.clear();
    for (int r = 0; r < m_prizeTable->rowCount(); ++r) {
        LotteryPrize p;
        auto cellText = [this, r](int col) {
            QTableWidgetItem *it = m_prizeTable->item(r, col);
            return it ? it->text() : QString();
        };
        p.name  = cellText(PcName).trimmed();
        p.count = cellText(PcCount).trimmed().toInt();
        p.level = cellText(PcLevel).trimmed();
        p.text  = cellText(PcText);
        if (p.name.isEmpty() && p.text.trimmed().isEmpty() && p.count <= 0)
            continue;                // 整行空着，别存成垃圾奖品
        if (p.name.isEmpty()) p.name = QStringLiteral("奖品");
        if (p.count < 0) p.count = 0;
        rec.prizes.append(p);
    }
    return true;
}

void LotteryPage::savePendingIfDirty()
{
    if (!m_dirty || m_currentId.isEmpty() || !g_lottery || m_appid == 0) return;

    LotteryRecord rec;
    collectRecord(rec);
    if (!g_lottery->save(rec)) {
        setStatusText(QStringLiteral("保存失败"), true);
        return;
    }
    m_current = rec;
    m_dirty = false;
}

void LotteryPage::setStatusText(const QString &msg, bool error)
{
    m_status->setText(msg);
    m_status->setStyleSheet(error ? QStringLiteral("color:#C0392B;") : QString());
    // 底部这行窄的时候文字会被压缩，全文挂 tooltip 上兜底
    m_status->setToolTip(msg.isEmpty()
        ? QStringLiteral("右键左侧列表 / 右侧信息区：保存配置 · 立即开奖 · 停止本场抽奖")
        : msg);
}

void LotteryPage::onMarkDirty()
{
    if (m_loading) return;
    m_dirty = true;
}

// ════════════════════════════════════════════════════════════════════════════
// 按钮
// ════════════════════════════════════════════════════════════════════════════

void LotteryPage::onNewLottery()
{
    if (!g_lottery || m_appid == 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("先在标题栏选一个机器人"));
        return;
    }
    savePendingIfDirty();

    LotteryRecord rec;
    rec.appid  = int(m_appid);
    rec.name   = QStringLiteral("新抽奖");
    rec.drawAt = QDateTime::currentSecsSinceEpoch() + 24 * 3600;
    rec.status = LotteryRunning;

    LotteryPrize p;
    p.name  = QStringLiteral("一等奖奖品");
    p.count = 1;
    p.level = QStringLiteral("一等奖");
    p.text  = QStringLiteral("恭喜你中了一等奖！");
    rec.prizes.append(p);

    if (!g_lottery->save(rec)) {
        QMessageBox::warning(this, QStringLiteral("失败"), QStringLiteral("新建抽奖失败，看看日志"));
        return;
    }

    m_currentId = rec.id;
    m_current = rec;
    reloadList();
    setStatusText(QStringLiteral("已新建，改完点「保存配置」；抽奖ID %1").arg(rec.id));
}

void LotteryPage::onDeleteLottery()
{
    if (!g_lottery || m_currentId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("先在左边选一场抽奖"));
        return;
    }
    const QString name = m_name->text().trimmed().isEmpty() ? m_currentId : m_name->text().trimmed();
    if (QMessageBox::question(this, QStringLiteral("删除抽奖"),
            QStringLiteral("确定删除「%1」？参与记录和中奖记录会一起删掉，不能恢复。").arg(name))
        != QMessageBox::Yes)
        return;

    if (!g_lottery->remove(int(m_appid), m_currentId)) {
        QMessageBox::warning(this, QStringLiteral("失败"), QStringLiteral("删除失败，看看日志"));
        return;
    }
    m_currentId.clear();
    m_current = LotteryRecord();
    m_dirty = false;
    clearDetail();
    reloadList();
    setStatusText(QStringLiteral("已删除"));
}

void LotteryPage::onSaveCurrent()
{
    if (!g_lottery || m_currentId.isEmpty()) return;

    LotteryRecord rec;
    collectRecord(rec);
    if (rec.name.isEmpty()) rec.name = QStringLiteral("抽奖 ") + m_currentId;

    // 存之前先对一遍「发放内容」和份数：对不上就提示，让人有机会改（不想改也能继续存）
    const QStringList problems = prizeTextProblems(rec.prizes);
    if (!problems.isEmpty()) {
        if (QMessageBox::question(this, QStringLiteral("发放内容对不上"),
                problemsToText(problems) + QStringLiteral("\n\n仍要保存吗？"))
            != QMessageBox::Yes)
            return;
    }

    if (!g_lottery->save(rec)) {
        setStatusText(QStringLiteral("保存失败"), true);
        return;
    }
    // 保存后从库里重读：参与名单 / 中奖名单以库为准（保存时可能有人刚好参与进来）
    LotteryRecord fresh;
    if (g_lottery->get(int(m_appid), rec.id, fresh)) rec = fresh;
    m_current = rec;
    m_dirty = false;
    reloadList();
    setStatusText(problems.isEmpty()
                      ? QStringLiteral("已保存 · 参与 %1 人 · 名额 %2")
                            .arg(rec.joinedCount()).arg(rec.totalSlots())
                      : QStringLiteral("已保存（发放内容有 %1 处对不上，自己核对下）")
                            .arg(problems.size()),
                  !problems.isEmpty());
}

void LotteryPage::onDrawNow()
{
    if (!g_lottery || m_currentId.isEmpty()) return;

    // 用库里的最新数据判断（界面手上的 m_current 可能是打开这一场时的旧快照）
    LotteryRecord fresh;
    if (g_lottery->get(int(m_appid), m_currentId, fresh) && fresh.joinedCount() <= 0) {
        QMessageBox::information(this, QStringLiteral("还没有人参与"),
            QStringLiteral("这场抽奖还没有人报名，现在开奖不会有中奖人。\n"
                           "等有人参与后再开；不想办了就右键列表「停止本场抽奖」作废。"));
        return;
    }

    if (QMessageBox::question(this, QStringLiteral("立即开奖"),
            QStringLiteral("不等开奖时间，现在就把这场抽奖开掉？开完不能重抽。"))
        != QMessageBox::Yes)
        return;

    savePendingIfDirty();

    // 开奖那一刻就把发放内容快照进中奖记录，之后改配置不生效 → 先确认对得上再开
    LotteryRecord check;
    if (g_lottery->get(int(m_appid), m_currentId, check)) {
        const QStringList problems = prizeTextProblems(check.prizes);
        if (!problems.isEmpty()
            && QMessageBox::question(this, QStringLiteral("发放内容对不上"),
                   problemsToText(problems)
                       + QStringLiteral("\n\n开奖后发放内容就定下来了（之后改配置不生效），"
                                        "确定现在开奖吗？"))
                   != QMessageBox::Yes)
            return;
    }

    QString err;
    if (!g_lottery->draw(int(m_appid), m_currentId, true, &err)) {
        setStatusText(err, true);
        QMessageBox::warning(this, QStringLiteral("开奖失败"), err);
        return;
    }
    reloadList();
    LotteryRecord rec;
    if (g_lottery->get(int(m_appid), m_currentId, rec)) loadRecord(rec);
    setStatusText(QStringLiteral("已开奖 · 中奖 %1 人").arg(rec.winners.size()));
}

void LotteryPage::onCloseCurrent()
{
    if (!g_lottery || m_currentId.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("停止本场抽奖"),
            QStringLiteral("停止后这场抽奖就作废了：参与名单清空、也不会再开奖，确定吗？"))
        != QMessageBox::Yes)
        return;

    QString err;
    if (!g_lottery->closeLottery(int(m_appid), m_currentId, &err)) {
        setStatusText(err, true);
        return;
    }
    LotteryRecord rec;
    if (g_lottery->get(int(m_appid), m_currentId, rec)) loadRecord(rec);
    reloadList();
    setStatusText(QStringLiteral("已停止 · 这场抽奖作废，不再开奖"));
}

void LotteryPage::onAddPrizeRow()
{
    if (m_currentId.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("先新建或选一场抽奖"));
        return;
    }
    const int row = m_prizeTable->rowCount();
    m_prizeTable->insertRow(row);
    m_prizeTable->setItem(row, PcName, new QTableWidgetItem(QStringLiteral("新奖品")));
    m_prizeTable->setItem(row, PcCount, new QTableWidgetItem(QStringLiteral("1")));
    m_prizeTable->setItem(row, PcLevel, new QTableWidgetItem(QStringLiteral("一等奖")));
    m_prizeTable->setItem(row, PcText, new QTableWidgetItem(QString()));
    m_prizeTable->editItem(m_prizeTable->item(row, PcName));
    m_dirty = true;
}

void LotteryPage::onRemovePrizeRow()
{
    const int row = m_prizeTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("先在奖品表格里选一行"));
        return;
    }
    m_prizeTable->removeRow(row);
    m_dirty = true;
}

void LotteryPage::onMarkDelivered()
{
    if (!g_lottery || m_currentId.isEmpty()) return;
    const int row = m_winnerTable->currentRow();
    if (row < 0 || !m_winnerTable->item(row, WcOpenid)) {
        setStatusText(QStringLiteral("先在中奖人表格里选一行"), true);
        return;
    }
    const QString openid = m_winnerTable->item(row, WcOpenid)->data(Qt::UserRole).toString();
    const QString nick   = m_winnerTable->item(row, WcNick)->text();
    if (!g_lottery->markDelivered(int(m_appid), m_currentId, openid)) {
        setStatusText(QStringLiteral("标记失败"), true);
        return;
    }
    LotteryRecord rec;
    if (g_lottery->get(int(m_appid), m_currentId, rec)) loadRecord(rec);
    setStatusText(QStringLiteral("已标记 %1 为已发放")
                      .arg(nick.isEmpty() ? openid : nick));
}

void LotteryPage::onCopyId()
{
    if (m_currentId.isEmpty()) {
        setStatusText(QStringLiteral("还没有选中抽奖"), true);
        return;
    }
    QApplication::clipboard()->setText(m_currentId);
    setStatusText(QStringLiteral("已复制抽奖ID：%1").arg(m_currentId));
}

// 非空行的条数（按钮上显示「N 条」）。只用于显示，匹配时不拆行、直接找文本。
int LotteryPage::whitelistCount(const QString &text)
{
    int n = 0;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        if (raw.trimmed().isEmpty()) continue;
        ++n;
    }
    return n;
}

void LotteryPage::refreshWhitelistButton()
{
    if (!m_btnWhitelist) return;
    const int n = whitelistCount(m_whitelist);
    m_btnWhitelist->setText(n <= 0 ? QStringLiteral("白名单：不限制")
                                   : QStringLiteral("白名单：%1 条").arg(n));
    m_btnWhitelist->setToolTip(
        n <= 0 ? QStringLiteral("当前没有限制，所有群都能参与 / 查看。点这里设置可用群。")
               : QStringLiteral("只有名单里的群能参与 / 查看。点这里编辑（可写群 id + 备注）。"));
}

void LotteryPage::refreshEnabledCheck()
{
    if (!m_chkEnabled) return;
    const bool on = (g_lottery && m_appid != 0) ? g_lottery->isEnabled(int(m_appid)) : false;
    const QSignalBlocker b(m_chkEnabled);        // 程序改勾选别反过来触发写库
    m_chkEnabled->setChecked(on);
    m_chkEnabled->setEnabled(m_appid != 0);
}

void LotteryPage::onToggleEnabled(bool on)
{
    if (m_appid == 0 || !g_lottery) {            // 还没选机器人：勾上也白搭，退回去
        refreshEnabledCheck();
        return;
    }
    if (!g_lottery->setEnabled(int(m_appid), on)) {
        setStatusText(QStringLiteral("保存启用开关失败（抽奖库没打开？）"), true);
        refreshEnabledCheck();
        return;
    }
    setStatusText(on
        ? QStringLiteral("已启用：机器人现在会响应 #参与抽奖 / #领取抽奖 / #抽奖帮助 等指令")
        : QStringLiteral("已关闭：所有抽奖指令都不匹配、不回复（数据都留着，随时能再打开）"));
}

void LotteryPage::onEditWhitelist()
{
    if (m_appid == 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("先在标题栏选一个机器人"));
        return;
    }
    WhitelistDialog dlg(m_whitelist, this);
    if (dlg.exec() != QDialog::Accepted) return;

    m_whitelist = dlg.text();
    refreshWhitelistButton();
    onMarkDirty();
    const int n = whitelistCount(m_whitelist);
    setStatusText(n <= 0 ? QStringLiteral("白名单已清空：所有群都能用")
                         : QStringLiteral("白名单已改：%1 条（记得保存配置）").arg(n));
}

void LotteryPage::onCopyWinnerId()
{
    // 领奖凭证就是抽奖 ID：复制整条指令直接贴给中奖人
    if (m_currentId.isEmpty()) {
        setStatusText(QStringLiteral("还没有选中抽奖"), true);
        return;
    }
    const QString cmd = QStringLiteral("#领取抽奖%1").arg(m_currentId);
    QApplication::clipboard()->setText(cmd);
    setStatusText(QStringLiteral("已复制领奖指令：%1").arg(cmd));
}

// ════════════════════════════════════════════════════════════════════════════
// 右键菜单（按钮行都收进来了，省界面高度）
// ════════════════════════════════════════════════════════════════════════════

void LotteryPage::showListMenu()
{
    QMenu menu(this);
    QAction *aNew = menu.addAction(QStringLiteral("新建抽奖"));
    QAction *aDel = menu.addAction(QStringLiteral("删除本场"));
    QAction *aRef = menu.addAction(QStringLiteral("刷新列表"));
    // 抽奖 ID 界面上不显示了（占地方），要发给群友时从这里复制
    QAction *aCopyId = menu.addAction(QStringLiteral("复制抽奖ID"));
    aDel->setEnabled(!m_currentId.isEmpty());
    aCopyId->setEnabled(!m_currentId.isEmpty());

    // 开奖 / 停止也放这里：右键列表就能操作，比跑到右边空白处右键好找
    menu.addSeparator();
    QAction *aSave = menu.addAction(QStringLiteral("保存配置"));
    QAction *aDraw = menu.addAction(QStringLiteral("立即开奖"));
    QAction *aStop = menu.addAction(QStringLiteral("停止本场抽奖"));
    aSave->setEnabled(m_btnSave->isEnabled());
    aDraw->setEnabled(m_btnDraw->isEnabled());
    aStop->setEnabled(m_btnClose->isEnabled());

    QAction *picked = menu.exec(QCursor::pos());
    if (picked == aNew)       onNewLottery();
    else if (picked == aDel)  onDeleteLottery();
    else if (picked == aRef)  reloadList();
    else if (picked == aCopyId) onCopyId();
    else if (picked == aSave) onSaveCurrent();
    else if (picked == aDraw) onDrawNow();
    else if (picked == aStop) onCloseCurrent();
}

void LotteryPage::showInfoMenu()
{
    QMenu menu(this);
    QAction *aSave  = menu.addAction(QStringLiteral("保存配置"));
    QAction *aDraw  = menu.addAction(QStringLiteral("立即开奖"));
    QAction *aClose = menu.addAction(QStringLiteral("停止本场抽奖"));
    aSave->setEnabled(m_btnSave->isEnabled());
    aDraw->setEnabled(m_btnDraw->isEnabled());
    aClose->setEnabled(m_btnClose->isEnabled());
    QAction *picked = menu.exec(QCursor::pos());
    if (picked == aSave)       onSaveCurrent();
    else if (picked == aDraw)  onDrawNow();
    else if (picked == aClose) onCloseCurrent();
}

void LotteryPage::showPrizeMenu()
{
    QMenu menu(this);
    QAction *aAdd = menu.addAction(QStringLiteral("添加奖品"));
    QAction *aDel = menu.addAction(QStringLiteral("删除选中行"));
    aDel->setEnabled(m_prizeTable->currentRow() >= 0);
    QAction *picked = menu.exec(QCursor::pos());
    if (picked == aAdd)      onAddPrizeRow();
    else if (picked == aDel) onRemovePrizeRow();
}

void LotteryPage::showWinnerMenu()
{
    QMenu menu(this);
    QAction *aMark  = menu.addAction(QStringLiteral("标记已发放"));
    menu.addSeparator();
    QAction *aCopy  = menu.addAction(QStringLiteral("复制领奖指令"));
    QAction *aCopyText = menu.addAction(QStringLiteral("复制发放内容"));
    QAction *aCopyUid = menu.addAction(QStringLiteral("复制用户ID"));
    const int row = m_winnerTable->currentRow();
    const bool hasRow = (row >= 0);
    aMark->setEnabled(hasRow && m_btnMarkDelivered->isEnabled());
    aCopy->setEnabled(!m_currentId.isEmpty());
    aCopyText->setEnabled(hasRow && m_winnerTable->item(row, WcText) != nullptr);
    aCopyUid->setEnabled(hasRow && m_winnerTable->item(row, WcOpenid) != nullptr);
    QAction *picked = menu.exec(QCursor::pos());
    if (picked == aMark)      onMarkDelivered();
    else if (picked == aCopy) onCopyWinnerId();
    else if (picked == aCopyText && hasRow) {
        // 这一格就是「这个人会领到什么」：多段 ||| 时每人一份，复制的就是他那一份
        if (QTableWidgetItem *it = m_winnerTable->item(row, WcText)) {
            const QString txt = it->data(Qt::UserRole).toString();   // 真内容（格子里的「未配置」只是提示）
            QApplication::clipboard()->setText(txt);
            setStatusText(txt.trimmed().isEmpty()
                              ? QStringLiteral("这一行没配发放内容（中奖人领奖会提示未配置）")
                              : QStringLiteral("已复制发放内容（%1 字）").arg(txt.size()),
                          txt.trimmed().isEmpty());
        }
    }
    else if (picked == aCopyUid && hasRow) {
        // 用户ID 列是隐藏的，要复制只能走这里（openid 存在 Qt::UserRole 里）
        if (QTableWidgetItem *it = m_winnerTable->item(row, WcOpenid)) {
            const QString uid = it->data(Qt::UserRole).toString();
            QApplication::clipboard()->setText(uid);
            setStatusText(QStringLiteral("已复制用户ID：%1").arg(uid));
        }
    }
}
