#ifndef TEXTREPLACECONFIGWIDGET_H
#define TEXTREPLACECONFIGWIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QListWidget>
#include <QSplitter>
#include <QPushButton>
#include <QHash>

struct AccountInfo; // 前置声明

// 文本替换规则结构体
struct TextReplaceRule {
    bool enabled = true;
    QString remark;
    QString replaceRule;   // 格式 "左边||右边"
    QString appendText;    // 无条件添加文本
    QString forbiddenWords; // 禁止词，多个用|||分割

    QJsonObject toJson() const;
    static TextReplaceRule fromJson(const QJsonObject &obj);
};

// 支持拖拽信号的行移动表格
class RowMovableTableWidget : public QTableWidget {
    Q_OBJECT
public:
    explicit RowMovableTableWidget(QWidget *parent = nullptr);

signals:
    void rowsSwapped(int fromRow, int toRow);   // 请求交换行数据

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dropEvent(QDropEvent *event) override;

private:
    int dragStartRow = -1;
};

// 主配置控件
class TextReplaceConfigWidget : public QWidget {
    Q_OBJECT

public:
    explicit TextReplaceConfigWidget(QWidget *parent = nullptr);
    ~TextReplaceConfigWidget();
    void oninitbot();
    void 列表行被单击(QListWidgetItem *item);   // 切换机器人


signals:
    void needLoadRules(int appid);
private slots:

    void onAddRow();
    void onDeleteRow();
    void onCopyRow();
    void onCopyAllRows();
    void onPasteFromClipboard();
    void onMoveRowUp();
    void onMoveRowDown();
    void onSaveToFile();
    void onRowsSwapped(int fromRow, int toRow);   // 拖拽交换
    void onTableDataChanged();                    // 表格编辑后同步

private:
    void setupUI();
    void initTable();
    void loadRulesForRobot(int appid);            // 加载指定机器人的规则到表格
    void saveCurrentRulesToMap();                 // 从表格保存到rulesMap
    void addRowFromRuleItem(const TextReplaceRule &item);
    TextReplaceRule getRuleItemFromRow(int row) const;
    void setRuleItemToRow(int row, const TextReplaceRule &item);
    QStringList getTableAsTSV() const;
    void addRowsFromTSV(const QString &tsv);
    void saveAllRulesToFile(const QString &filePath = "data/text_replace_rules.json");
    void loadAllRulesFromFile(const QString &filePath = "data/text_replace_rules.json");

    // UI组件
    QSplitter *mainSplitter;

    RowMovableTableWidget *ruleTable;
    QPushButton *addBtn, *deleteBtn, *copyRowBtn, *copyAllBtn, *pasteBtn;
    QPushButton *moveUpBtn, *moveDownBtn, *saveBtn;

    // 数据
    QHash<int, QList<TextReplaceRule>> rulesMap;   // appid_int -> 规则列表
    int currentAppId = 0;

    // 列索引
    static constexpr int COL_ENABLED = 0;
    static constexpr int COL_REMARK = 0;
    static constexpr int COL_REPLACE_RULE = 1;
    static constexpr int COL_APPEND_TEXT = 2;
    static constexpr int COL_FORBIDDEN = 3;
};

#endif // TEXTREPLACECONFIGWIDGET_H