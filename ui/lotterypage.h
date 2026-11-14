/*
 * 纯白铃 - QQ 机器人管理平台
 * 内置功能 → 抽奖 界面
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

// 「内置 → 抽奖」页：抽奖列表 + 奖品配置 + 中奖人，全部按 appid 隔离。
// 数据与抽取逻辑在 core/lottery.h，这里只做界面（切号靠 setAppId，跟素材页一个套路）。
//
// 交互：改动只落在控件上，点「保存配置」才写库；切走（换选中的抽奖 / 换机器人 / 换页）
// 时如果有未保存改动，自动先存一次 —— 管理员改一半去点别的抽奖，回来发现改动没了很恼人。

#ifndef LOTTERYPAGE_H
#define LOTTERYPAGE_H

#include <QWidget>
#include <QList>
#include <QString>

#include "lottery.h"

class QTableWidget;
class QLineEdit;
class QLabel;
class QDateTimeEdit;
class QPushButton;
class QPlainTextEdit;
class QCheckBox;
class QSpinBox;
class QShowEvent;

class LotteryPage : public QWidget
{
    Q_OBJECT
public:
    explicit LotteryPage(QWidget *parent = nullptr);

    // 切换机器人（主窗口 applyConfigAppId 里调）
    void setAppId(uint32_t appid);

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void reloadList();
    void onListSelectionChanged();
    void onNewLottery();
    void onDeleteLottery();
    void onSaveCurrent();
    void onDrawNow();
    void onCloseCurrent();
    void onAddPrizeRow();
    void onRemovePrizeRow();
    void onMarkDelivered();
    void onCopyId();
    void onCopyWinnerId();
    void onMarkDirty();
    void onEditWhitelist();
    void onToggleEnabled(bool on);               // 启用抽奖（关掉后指令全不匹配）

private:
    void buildUi();
    void loadRecord(const LotteryRecord &rec);
    void clearDetail();
    bool collectRecord(LotteryRecord &rec) const;
    void savePendingIfDirty();
    void setStatusText(const QString &msg, bool error = false);
    void refreshWinnerTable(const LotteryRecord &rec);
    void refreshWhitelistButton();               // 白名单按钮上的「N 个群 / 不限制」
    void refreshEnabledCheck();                  // 按当前机器人同步「启用抽奖」勾选（不触发写库）
    static int whitelistCount(const QString &text);
    // 右键菜单（按钮行都收进菜单了）
    void showListMenu();
    void showInfoMenu();
    void showPrizeMenu();
    void showWinnerMenu();

    uint32_t m_appid = 0;
    QString  m_currentId;      // 当前正在编辑的抽奖 id（空 = 没选）
    LotteryRecord m_current;   // 当前记录的完整内容（players/winners 界面不改，原样带回去）
    bool m_loading = false;    // 程序化填表期间为 true，屏蔽 cellChanged → dirty
    bool m_dirty = false;
    bool m_changedHook = false;

    QLabel         *m_title = nullptr;
    QTableWidget   *m_list = nullptr;
    QPushButton    *m_btnNew = nullptr;
    QPushButton    *m_btnDelete = nullptr;
    QPushButton    *m_btnRefresh = nullptr;
    QCheckBox      *m_chkEnabled = nullptr;      // 启用抽奖（每个机器人一份，存 lottery_meta）

    // 白名单不内嵌输入框了：点按钮弹独立窗口编辑，这里只存文本
    QString         m_whitelist;
    QPushButton    *m_btnWhitelist = nullptr;

    QLineEdit      *m_name = nullptr;
    QDateTimeEdit  *m_drawAt = nullptr;
    QCheckBox      *m_chkFull = nullptr;        // 满人开奖（勾上才显示人数）
    QSpinBox       *m_spinFull = nullptr;
    QPushButton    *m_btnSave = nullptr;
    QPushButton    *m_btnDraw = nullptr;
    QPushButton    *m_btnClose = nullptr;

    QTableWidget   *m_prizeTable = nullptr;

    QTableWidget   *m_winnerTable = nullptr;
    QPushButton    *m_btnMarkDelivered = nullptr;
    QPushButton    *m_btnCopyWinner = nullptr;

    QLabel         *m_status = nullptr;
};

#endif // LOTTERYPAGE_H
