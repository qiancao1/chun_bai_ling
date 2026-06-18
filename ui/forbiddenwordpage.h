// forbiddenwordpage.h
#ifndef FORBIDDENWORDPAGE_H
#define FORBIDDENWORDPAGE_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QVector>
#include <QQueue>
#include <QHash>
#include "placeholderlineedit.h"
#include "placeholdertextedit.h"   // 如果你也有 QTextEdit 的替换

// 全局替换宏
#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit
// ---------- 违禁词专用 Aho‑Corasick 自动机 ----------
class ForbiddenWordAhoCorasick {
public:
    struct Node {
        QHash<QChar, int> next;
        int fail = 0;
        QVector<int> output;
    };
    std::vector<Node> nodes;

    ForbiddenWordAhoCorasick() { nodes.emplace_back(); }

    void insert(const QString &pattern, int patternIndex) {
        int node = 0;
        for (QChar ch : pattern) {
            if (!nodes[node].next.contains(ch)) {
                nodes[node].next[ch] = nodes.size();
                nodes.emplace_back();
            }
            node = nodes[node].next[ch];
        }
        nodes[node].output.append(patternIndex);
    }

    void build() {
        QQueue<int> q;
        for (auto it = nodes[0].next.begin(); it != nodes[0].next.end(); ++it) {
            int child = it.value();
            nodes[child].fail = 0;
            q.enqueue(child);
        }
        while (!q.isEmpty()) {
            int v = q.dequeue();
            for (auto it = nodes[v].next.begin(); it != nodes[v].next.end(); ++it) {
                QChar ch = it.key();
                int u = it.value();
                int f = nodes[v].fail;
                while (f != 0 && !nodes[f].next.contains(ch))
                    f = nodes[f].fail;
                if (nodes[f].next.contains(ch))
                    nodes[u].fail = nodes[f].next[ch];
                else
                    nodes[u].fail = 0;
                nodes[u].output.append(nodes[nodes[u].fail].output);
                q.enqueue(u);
            }
        }
    }
};

// ---------- 违禁词过滤页面 ----------
class ForbiddenWordPage : public QWidget {
    Q_OBJECT
public:
    explicit ForbiddenWordPage(QWidget *parent = nullptr);
    ~ForbiddenWordPage();

    QString filterText(const QString &input) const;   // 过滤文本，违禁词替换为"...."

private slots:
    void onAddRow();
    void onDeleteRow();
    void onSave();           // 导出到用户指定文件
    void onBatchImport();
    void onTableDataChanged();
    void onTestFilter();

private:
    void setupUI();
    void refreshTable();
    void buildAutomaton();                       // 重建AC自动机
    QVector<QPair<int,int>> getMatchIntervals(const QString &text) const;
    void saveToDefaultFile();                    // 自动保存到默认路径
    void loadFromDefaultFile();                  // 启动时加载默认路径数据

    static QString defaultFilePath();

    static constexpr int COLUMN_COUNT = 8;
    QTableWidget *m_tableWidget;
    QPushButton *m_addBtn;
    QPushButton *m_deleteBtn;
    QPushButton *m_saveBtn;
    QPushButton *m_importBtn;
    QTextEdit *m_testInputEdit;
    QLabel *m_testResultLabel;

    QStringList m_forbiddenWords;
    ForbiddenWordAhoCorasick *m_ac;
    bool m_updating;
};

#endif // FORBIDDENWORDPAGE_H