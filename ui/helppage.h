// helppage.h
// 「帮助」页：一段可编辑、自动保存的引导文本（存 data/help.md）。
// 页面上看到的内容就是这段文本本身，改完立刻生效，不需要重新编译。
#ifndef HELPPAGE_H
#define HELPPAGE_H

#include <QWidget>
#include <QString>

class QPlainTextEdit;
class QLabel;
class QTimer;

class HelpPage : public QWidget
{
    Q_OBJECT
public:
    explicit HelpPage(QWidget *parent = nullptr);

private slots:
    void onSaveClicked();      // 立刻落盘
    void onResetClicked();     // 恢复内置初稿

private:
    void setupUI();
    void loadFromFile();
    void saveToFile();
    void setStatus(const QString &text);

    static QString helpFilePath();        // data/help.md
    static QString defaultHelpText();     // 内置初稿（文件不存在时写入）

    QPlainTextEdit *m_editor = nullptr;
    QLabel *m_status = nullptr;
    QTimer *m_saveTimer = nullptr;        // 输入停顿后自动保存
    bool m_loading = false;               // 载入/填充期间不要触发自动保存
};

#endif // HELPPAGE_H
