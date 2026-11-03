#ifndef SHOPPRICEDIALOG_H
#define SHOPPRICEDIALOG_H

#include "pluginpage.h"
#include <QDialog>
#include <QList>
#include <qtablewidget.h>

class PluginPage;
class QListWidget;
class QTableWidget;
class QLabel;
class QComboBox;
class QPushButton;

// 「商品定价」窗口：左边是**注册了商品**的插件，右边是这个插件的售价表。上架但默认下架。
//
// 顶部机器人下拉与「编辑指令」同一套口径：
//   · 「默认（全部机器人）」→ 改的是所有机器人共用的基准价（没单独配过的号都跟这一套）
//   · 某个具体机器人     → 只改这个号的价，别的号完全不受影响
// 插件注册的 price 只是**默认值**：把价格改回和上一级一样时会自动删掉这条配置，
// 所以「改回去」= 继续跟随，配置文件里不会留垃圾，也不会越改越乱。
//
// 本类是只读视图 + 一个提交接口，全部走 PluginPage 的 shopPrice* / applyShopPrices；
// 插件内部结构（py::object / 函数地址）一概不碰。
class ShopPriceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ShopPriceDialog(PluginPage *page, int preselectIndex = -1, QWidget *parent = nullptr);

private slots:
    void onPluginRowChanged(int row);
    void onBotChanged(int comboRow);
    void onFollowDefault();     // 删掉当前机器人的独立定价 → 重新跟随默认
    void onSave();

private:
    void reloadPluginList(int preselectIndex);
    void extracted(const ShopPriceRow &r, QTableWidgetItem *&p);
    void loadRowsForListRow(int listRow);

    PluginPage   *m_page       = nullptr;
    QListWidget  *m_pluginList = nullptr;
    QComboBox    *m_botCombo   = nullptr;   // 机器人下拉（第 0 项 = 默认）
    QPushButton  *m_followBtn  = nullptr;   // 「跟随默认」，仅具体机器人且已单独定价时可用
    QTableWidget *m_table      = nullptr;
    QLabel       *m_tip        = nullptr;
    QList<int>    m_indexes;      // 列表第 n 行 → m_pluginList 下标
    QList<int>    m_botAppids;    // 下拉第 n 项 → appid（第 0 项 = 0 = 默认）
    int           m_curPlugin  = -1;
    int           m_curAppid   = 0;    // 0 = 默认（全部机器人）
};

#endif // SHOPPRICEDIALOG_H
