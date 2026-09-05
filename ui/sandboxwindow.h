#ifndef SANDBOXWINDOW_H
#define SANDBOXWINDOW_H

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QProcess>
#include <QThread>
#include <QPushButton>
#include <QListWidget>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextEdit>

#include "placeholderlineedit.h"
#include "placeholdertextedit.h"   // 如果你也有 QTextEdit 的替换

// 全局替换宏
#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit

class PythonHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    PythonHighlighter(QTextDocument *parent = nullptr) : QSyntaxHighlighter(parent)
    {
        HighlightingRule rule;

        QStringList keywords = {
            "and", "as", "assert", "async", "await", "break", "class", "continue",
            "def", "del", "elif", "else", "except", "False", "finally", "for",
            "from", "global", "if", "import", "in", "is", "lambda", "None",
            "nonlocal", "not", "or", "pass", "raise", "return", "True", "try",
            "while", "with", "yield"
        };
        rule.pattern = QRegularExpression("\\b(" + keywords.join("|") + ")\\b");
        rule.format.setForeground(QColor(0xFF7F32));
        rule.format.setFontWeight(QFont::Bold);
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\s*(?=\\()");
        rule.format.setForeground(QColor(0x6A5ACD));
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("(\".*?\"|'.*?')");
        rule.format.setForeground(QColor(0xD2691E));
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("#[^\n]*");
        rule.format.setForeground(QColor(0x8A8A8A));
        rule.format.setFontItalic(true);
        highlightingRules.append(rule);

        rule.pattern = QRegularExpression("\\b[0-9]+\\b");
        rule.format.setForeground(QColor(0xB8860B));
        highlightingRules.append(rule);
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
            QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                QRegularExpressionMatch match = matchIterator.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;
};

// 自定义代码编辑器，支持行号
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);
    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();

protected:
    void resizeEvent(QResizeEvent *event) override;


private slots:
    void updateLineNumberAreaWidth(int newBlockCount = 0);
    void updateLineNumberArea(const QRect &rect, int dy);
    void highlightCurrentLine();

private:
    QWidget *lineNumberArea;
};
class LineNumberArea : public QWidget
{
public:
    LineNumberArea(CodeEditor *editor) : QWidget(editor), codeEditor(editor)
    {
        setContentsMargins(0,0,0,0);
    }

    QSize sizeHint() const override
    {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor *codeEditor;
};
class SandboxWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit SandboxWindow(QWidget *parent = nullptr);
    ~SandboxWindow();
    QTextEdit *outputLog;
    void addChatMessage(const QString &text, bool isUser); // 添加气泡消息
    void appendOutput(const QString &text);
    void clearOutput();

signals:
    void userSentMessage(const QString &message);
    void userSentImage(const QString &imagePath);


public slots:
    void setCode(const QString &code);
    QString getCode() const;
protected:

    bool eventFilter(QObject *obj, QEvent *event) override;
private slots:
    void onSendClicked();

    void onSavePluginClicked();
    void onSaveCodeClicked();
    void onOpenCodeClicked();

private:
    void setupUI();
    void applyStyleSheet();


    void clearChat();
    QListWidget *chatWidget;          // 替代原来的 chatDisplay
    QTextEdit *messageInput;          // 多行输入框（原 QLineEdit）
    QPushButton *clearBtn;            // 清空对话按钮（原 imageBtn）

              // 清空所有气泡
    CodeEditor *codeEditor;

    QTextEdit *chatDisplay;

    QString pyfilepath;

    QThread* m_execThread = nullptr;
    bool m_isRunning = false;
    void* m_pyThreadState = nullptr;   // 实际是 PyThreadState*，用 void* 隐藏类型

    QString m_scriptDir;        // 脚本存放目录
    QProcess *m_process;        // 用于运行 Python 脚本
};

#endif // SANDBOXWINDOW_H