#ifndef RULEEDITDIALOG_H
#define RULEEDITDIALOG_H

#include <QDialog>
#include <QList>

class PluginPage;
class QListWidget;
class QTableWidget;
class QLabel;

// 「编辑指令」窗口：左边插件列表（点一下把数据加载到右边），
// 右边一行一条：[启用] [指令名] [新指令名] [匹配方式] + 底部 [保存修改]。
//
// 只读视图 + 一个提交接口，全部走 PluginPage 的 ruleConfig* / applyRuleConfigRows，
// 本类不直接碰 py::object / 函数地址，也不用 QLineEdit（避开工程里 QTextEdit/QLineEdit 的宏）
class RuleEditDialog : public QDialog
{
    Q_OBJECT
public:
    explicit RuleEditDialog(PluginPage *page, int preselectIndex = -1, QWidget *parent = nullptr);

private slots:
    void onPluginRowChanged(int row);
    void onSave();

private:
    void reloadPluginList(int preselectIndex);
    void loadRowsForListRow(int listRow);

    PluginPage   *m_page       = nullptr;
    QListWidget  *m_pluginList = nullptr;
    QTableWidget *m_table      = nullptr;
    QLabel       *m_tip        = nullptr;
    QList<int>    m_indexes;      // 列表第 n 行 → m_pluginList 下标
    int           m_curPlugin  = -1;
};

#endif // RULEEDITDIALOG_H
