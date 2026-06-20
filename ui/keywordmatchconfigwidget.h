#ifndef KEYWORDMATCHCONFIGWIDGET_H
#define KEYWORDMATCHCONFIGWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QSplitter>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include <QStringList>

// 前置声明
class AhoCorasick;

// 关键词匹配规则结构体
struct KeywordMatchRule {
    bool enabled = true;
    QStringList keywords;       // 多个关键词，存储时用 ||| 连接
    int matchType = 0;          // 0=精确 1=指令头(首个为头,其余必须在剩余部分出现) 2=包含
    QString replyContent;
    QStringList forbiddenWords; // 禁止词列表，任一出现则失败

    QJsonObject toJson() const;
    static KeywordMatchRule fromJson(const QJsonObject &obj);
};

// 可拖拽行移动的表格
class MovableKeywordTable : public QTableWidget {
    Q_OBJECT
public:
    explicit MovableKeywordTable(QWidget *parent = nullptr);
protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void dropEvent(QDropEvent *event) override;
signals:
    void rowsSwapped(int fromRow, int toRow);
private:
    int dragStartRow = -1;
};

class KeywordMatchConfigWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordMatchConfigWidget(QWidget *parent = nullptr);
    ~KeywordMatchConfigWidget();

    // 对外提供的静态匹配接口
    static QString match(int appid, const QString &msg);

    // 重新构建某个机器人的匹配器（规则改变后调用）
    void buildMatcherForRobot(int appid);
    void 列表行被单击(QListWidgetItem *item);
    // 将当前机器人规则应用到全局 m_accounts（可选）
    void oninitbot();

signals:
    void needLoadRules(int robotId);

private slots:


    void onAddRow();
    void onDeleteRow();
    void onCopyRow();
    void onCopyAllRows();
    void onPasteFromClipboard();
    void onMoveRowUp();
    void onMoveRowDown();
    void onRowsSwapped(int fromRow, int toRow);
    void onTableDataChanged();
    void onSaveToFile();
    void loadRulesForRobot(int robotId);

private:
    void setupUI();
    void initTable();
    void saveCurrentRulesToMap();
    void setRuleItemToRow(int row, const KeywordMatchRule &rule);
    KeywordMatchRule getRuleItemFromRow(int row) const;
    void addRowFromRuleItem(const KeywordMatchRule &rule);
    QStringList getTableAsTSV() const;
    void addRowsFromTSV(const QString &tsv);
    void saveAllRulesToFile(const QString &filePath = "data/keyword_match_rules.json");
    void loadAllRulesFromFile(const QString &filePath = "data/keyword_match_rules.json");

    enum Column {
        COL_ENABLED = 0,
        COL_KEYWORDS=0,
        COL_MATCH_TYPE=1,
        COL_REPLY=2,
        COL_FORBIDDEN=3,
        COL_COUNT=4
    };

    // UI 组件
    QSplitter *mainSplitter = nullptr;

    MovableKeywordTable *ruleTable = nullptr;
    QPushButton *saveBtn = nullptr;
    QPushButton *addBtn = nullptr;
    QPushButton *deleteBtn = nullptr;
    QPushButton *copyRowBtn = nullptr;
    QPushButton *copyAllBtn = nullptr;
    QPushButton *pasteBtn = nullptr;
    QPushButton *moveUpBtn = nullptr;
    QPushButton *moveDownBtn = nullptr;

    int currentRobotId = 0;
    QMap<int, QList<KeywordMatchRule>> rulesMap;   // 原始规则存储

    // 高性能匹配相关（静态成员，所有实例共享）
    struct RuleIndex {
        int ruleIdx = -1;
        bool isHeaderMode = false;
        bool isExactMode = false;
        QStringList keywords;
        QSet<QString> keywordSet;
        QSet<QString> forbiddenSet;
        QString reply;
    };
    static QMap<int, AhoCorasick> s_acMatchers;
    static QMap<int, QList<RuleIndex>> s_rulesList;
    static QMap<int, QHash<QString, int>> s_exactMaps;
    static QMap<int, bool> s_matcherBuilt;
};

// ==================== Aho‑Corasick 自动机 ====================
struct ACNode {
    QMap<QChar, int> next;
    int fail = 0;
    QList<int> output;
    ACNode() = default;
};

class AhoCorasick {
public:
    void insert(const QString &pattern, int patternIndex);
    void build();
    QSet<int> scan(const QString &text) const;
private:
    std::vector<ACNode> nodes{1};
};

#endif // KEYWORDMATCHCONFIGWIDGET_H