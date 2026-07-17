#ifndef APPWINDOW_H
#define APPWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QTreeView>          // 改用 QTreeView
#include <QFileSystemModel>   // 新增文件系统模型
#include <QPushButton>
#include <qcheckbox.h>
#include <qjsonarray.h>
#include <qjsonobject.h>
#include <qpainter.h>

#include "sandboxwindow.h"

class AppWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AppWindow(QWidget *parent = nullptr);
    ~AppWindow();
    QString Ai_post(const QString &url, const QString &key, QString &err);
    QString Ai_posts(const QString &model) ;
    void addMessage(const QString &text, bool isUser);
    // 填充模型下拉框
    void setModels();

    QTextEdit *chatTextEdit;   // 替代原 chatListView
    //QListWidget *chatList;

signals:

    void modelChanged(const QString &modelName);    // 模型选择改变时触发

private slots:
    void openFolder();
    void onFileClicked(const QModelIndex &index);  // 参数改成 QModelIndex
    void onSendMessage(const QString &text);

private:
    void Folder();
    void init_system(const QString &text);
    void 内置函数(const QString &Nmae,const QString &remark,const QStringList &params);
    void 内置函数();
    QString tools_fun(const QString &tool_name, const QString &args, const QString &model);
    QJsonArray m_fun;
    QLabel *pathLabel;
    QString m_dir;
    QTreeView *fileTree;          // 换成树形视图
    QFileSystemModel *fileModel;  // 文件系统模型
    CodeEditor *codeEditor;
    QCheckBox *nosh;
    QString lastSelectedFile;
    QJsonObject sxw;
    int model_index=0;
    bool m_run=false;
    bool qxzd=false;
    void clearChat();
    QThread* m_execThread = nullptr;
    QTextEdit *messageInput;
    QPushButton *sendBtn;
    QPushButton *clearBtn;
    QComboBox *modelCombo;   // 新增模型下拉框
};

#endif // APPWINDOW_H