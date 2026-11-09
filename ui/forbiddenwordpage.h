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
#include "placeholdertextedit.h"   // QTextEdit 替身：占位符自绘 + 强制纯文本粘贴


#define QTextEdit PlaceholderTextEdit

// ---------- 违禁词词库文件的编码 ----------
// 统一按 UTF-8 存取。老版本桌面端用的是 QTextStream 默认编码（Windows 上是系统
// ANSI 码页，中文机器 = GBK），写出来的文件 WebUI 按 UTF-8 读就是乱码。
// 这里读取时先按 UTF-8 解，出现替换字符（U+FFFD）再退回本地码页 —— 老文件能正常
// 显示，下一次保存就自动转成 UTF-8 了。
inline QString decodeForbiddenWordsText(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return QString();

    QString text = QString::fromUtf8(bytes);
    if (text.contains(QChar(0xFFFD))) {
        const QString local = QString::fromLocal8Bit(bytes);
        if (!local.contains(QChar(0xFFFD)))
            text = local;   // GBK 老文件
    }
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);  // 去掉可能存在的 UTF-8 BOM
    return text;
}

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

    // 从默认路径重新读词库并重建 AC 自动机。
    // 给 WebUI 用：网页上改完 data/forbidden_words.txt 后调用，立刻生效。
    void webuiReload() { loadFromDefaultFile(); }

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