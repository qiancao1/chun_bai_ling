#ifndef SANDBOXWINDOW_H
#define SANDBOXWINDOW_H

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QProcess>
#include <QThread>
#include <QPushButton>
#include <QListWidget>
#include "placeholderlineedit.h"
#include "placeholdertextedit.h"   // 如果你也有 QTextEdit 的替换

// 全局替换宏
#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit
class LineNumberArea;

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