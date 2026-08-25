#ifndef KEYWORDPUNISHCONFIGWIDGET_H
#define KEYWORDPUNISHCONFIGWIDGET_H

#include "qqbotclient.h"
#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QComboBox>
#include <QMap>
#include <QJsonObject>
#include <QStringList>

// 前置声明（AhoCorasick 可从原回复模块复用）
class AhoCorasick;

struct KeywordPunishRule {
    bool enabled = true;
    QStringList keywords;
    int punishType = 0;    // 0:撤回, 1:禁言, 2:踢出, 3:撤回+禁言, 4:踢出+撤回
    int param = 0;         // 时长（秒）或其他整数参数
    QString extra;         // 提示文本

    QJsonObject toJson() const;
    static KeywordPunishRule fromJson(const QJsonObject &obj);
};

struct PunishAction {
    int type = -1;
    int param = 0;
    QString extra;
    bool isValid() const { return type >= 0; }
};

class KeywordPunishConfigWidget : public QWidget {
    Q_OBJECT
public:
    explicit KeywordPunishConfigWidget(QWidget *parent = nullptr);
    ~KeywordPunishConfigWidget();

    // 匹配接口：返回惩罚动作
    bool match(const MessageEvent &ev);

private slots:
    void onAddRow();
    void onDeleteRow();
    void onCopyRow();
    void onCopyAllRows();
    void onPasteFromClipboard();
    void onTableDataChanged();
    void onSaveToFile();

private:
    void setupUI();
    void initTable();
    void saveCurrentRulesToMap();
    void setRuleItemToRow(int row, const KeywordPunishRule &rule);
    KeywordPunishRule getRuleItemFromRow(int row) const;
    void addRowFromRuleItem(const KeywordPunishRule &rule);
    QStringList getTableAsTSV() const;
    void addRowsFromTSV(const QString &tsv);
    void saveAllRulesToFile(const QString &filePath = "keyword_punish_rules.json");
    void loadAllRulesFromFile(const QString &filePath = "keyword_punish_rules.json");
    void buildMatcherForRobot(int appid);
    void refreshTable();
    void loadRulesForRobot(int robotId);

    QTableWidget *ruleTable;
    QPushButton *saveBtn, *addBtn, *deleteBtn, *copyRowBtn, *copyAllBtn, *pasteBtn;

    QMap<int, QList<KeywordPunishRule>> rulesMap;

    // 匹配缓存
    struct RuleIndex {
        int punishType;
        int param;
        QString extra;
        QStringList keywords;
    };
    static QMap<int, AhoCorasick> s_acMatchers;
    static QMap<int, QList<RuleIndex>> s_rulesList;
};

#endif // KEYWORDPUNISHCONFIGWIDGET_H