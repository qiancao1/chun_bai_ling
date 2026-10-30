#ifndef RULEEDITDIALOG_H
#define RULEEDITDIALOG_H

#include <QDialog>
#include <QList>
#include <QStringList>

class PluginPage;
class QListWidget;
class QTableWidget;
class QLabel;
class QComboBox;
class QPushButton;

// 「编辑指令」窗口：左边插件列表（点一下把数据加载到右边），
// 右边一行一条：[启用类型] [指令名] [新指令名] [匹配方式] + 底部 [一键测试] [保存修改]。
//
// 顶部还有一个**机器人下拉**：
//   · 「默认（全部机器人）」—— 改的是所有机器人共用的那一套（含指令改名）；
//   · 某个具体机器人     —— 改的是**这个机器人自己**的启用类型（改名仍是全局的，这一档只读）。
// 机器人没单独配过时，表格显示的就是它当前实际继承的默认值，改哪条存哪条 → 从此独立。
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
    void onBotChanged(int comboRow);
    void onFollowDefault();     // 删掉当前机器人的独立配置 → 重新跟随默认
    void onSave();

private:
    void reloadPluginList(int preselectIndex);
    void loadRowsForListRow(int listRow);
    // 表格里**此刻**标成「禁用」的条目（"<匹配方式>\x1f<注册原名>"）—— 直接读下拉，含还没保存的改动。
    // 交给 PluginPage 的测试接口 → 这些指令一律不跑（「单独关掉某条指令的测试」的开关）。
    QStringList disabledRuleKeys() const;

    PluginPage   *m_page       = nullptr;
    QListWidget  *m_pluginList = nullptr;
    QComboBox    *m_botCombo   = nullptr;   // 机器人下拉（第 0 项 = 默认）
    QPushButton  *m_followBtn  = nullptr;   // 「跟随默认」，仅具体机器人且已单独配置时可用
    QTableWidget *m_table      = nullptr;
    QLabel       *m_tip        = nullptr;
    QList<int>    m_indexes;      // 列表第 n 行 → m_pluginList 下标
    QList<int>    m_botAppids;    // 下拉第 n 项 → appid（第 0 项 = 0 = 默认）
    int           m_curPlugin  = -1;
    int           m_curAppid   = 0;    // 0 = 默认（全部机器人）
};

#endif // RULEEDITDIALOG_H
