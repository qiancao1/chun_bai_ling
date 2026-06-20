#ifndef BOTRULECONFIGWIDGET_H
#define BOTRULECONFIGWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QSplitter>
#include <QPushButton>
#include <QHash>

struct AccountInfo; // 前置声明

// 机器人规则项（与之前定义类似，但使用不同名称避免冲突）
struct BotRuleItem {
    bool enabled = true;
    QString remark;
    QString jzc;
    QString buttonText;
    int matchType = 0;       // 匹配类型整数
    QString candidateWords;

    QJsonObject toJson() const;
    static BotRuleItem fromJson(const QJsonObject &obj);
};

// 支持拖拽插入式移动的行移动表格
class BotMovableTableWidget : public QTableWidget {
    Q_OBJECT
public:
    explicit BotMovableTableWidget(QWidget *parent = nullptr);

signals:
    void rowsSwapped(int fromRow, int toRow);   // 请求交换行数据

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dropEvent(QDropEvent *event) override;

private:
    int dragStartRow = -1;
};

// 机器人规则配置主控件
class BotRuleConfigWidget : public QWidget {
    Q_OBJECT

public:
    explicit BotRuleConfigWidget(QWidget *parent = nullptr);
    ~BotRuleConfigWidget();
    void 列表行被单击(QListWidgetItem *item);


private slots:

    void onAddRow();
    void onDeleteRow();
    void onCopyRow();
    void onCopyAllRows();
    void onPasteFromClipboard();
    void onMoveRowUp();
    void onMoveRowDown();
    void onSaveToFile();
    void onRowsSwapped(int fromRow, int toRow);
    void onTableDataChanged();

private:
    void setupUI();
    void initTable();
    void loadRulesForRobot(int robotId);                 // 加载指定机器人的规则到表格
    void saveCurrentRulesToMap();                        // 从表格保存到 m_ruleMap
    void addRowFromRuleItem(const BotRuleItem &item);
    BotRuleItem getRuleItemFromRow(int row) const;
    void setRuleItemToRow(int row, const BotRuleItem &item);
    QStringList getTableAsTSV() const;
    void addRowsFromTSV(const QString &tsv);
    void saveAllRulesToFile(const QString &filePath = "data/bot_rules.json");
    void loadAllRulesFromFile(const QString &filePath = "data/bot_rules.json");
    void oninitbot();
    // UI组件
    QSplitter *mainSplitter;

    BotMovableTableWidget *ruleTable;
    QPushButton *addBtn, *deleteBtn, *copyRowBtn, *copyAllBtn, *pasteBtn;
    QPushButton *moveUpBtn, *moveDownBtn, *saveBtn;

    // 数据存储: robotId (appid_int) -> 规则列表
    QHash<int, QList<BotRuleItem>> m_ruleMap;
    int m_currentRobotId = 0;

    // 列索引
    static constexpr int COL_ENABLED = 0;
    static constexpr int COL_REMARK = 0;
    static constexpr int COL_BUTTON_TYPE = 1;
    static constexpr int COL_BUTTON_TEXT = 2;
    static constexpr int COL_MATCH_TYPE = 3;
    static constexpr int COL_CANDIDATE = 4;

signals:
    void needLoadBotRules(int robotId);   // 队列加载信号
};

#endif // BOTRULECONFIGWIDGET_H