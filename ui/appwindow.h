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
#include <qnetworkreply.h>
#include <qpainter.h>
#include <QComboBox>

#include "sandboxwindow.h"

class AppWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AppWindow(const QString &path=QString(),QWidget *parent = nullptr);
    ~AppWindow();
    QString Ai_post(const QString &url, const QString &key, QString &err);
    QString Ai_posts(const QString &model) ;

    void setModels();

    QTextEdit *chatTextEdit;   // 替代原 chatListView
    //QListWidget *chatList;

signals:

    void modelChanged(const QString &modelName);    // 模型选择改变时触发

private slots:
    void openFolder();
    void onFileClicked(const QModelIndex &index);  // 参数改成 QModelIndex
    void onSendMessage(const QString &text);
    void saveCurrentFile();

private:
    enum class MessageType {
        User,
        AI,
        Tool,
        sk
    };
    void addMessage(const QString &text, MessageType type);
    void addMessage2(const QString &text, MessageType type, const QString &toolname = QString());
    void Folder();
    void init_system(const QString &text);
    void 内置函数(const QString &Nmae,const QString &remark,const QStringList &params);
    void 内置函数();
    void removeimg();
    QJsonObject climg(const QString &text);
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
    QPushButton *sendimg,*sendBtn,*sendBtn2;
    QPushButton *clearBtn;
    QComboBox *modelCombo;   // 新增模型下拉框
    QString currentFilePath;
    QString addmsg;
    QString liu_text;
    struct StreamSession {
        QString model;
        std::function<void(const QString&)> callback;
        QMap<QString, std::function<QString(const QString&, const QString&)>> toolHandlers; // 工具名 -> 处理函数
        QNetworkReply *reply = nullptr;
        QByteArray buffer;
        QString accumulatedReasoning;   // 累积的思考文本
        QString accumulatedContent;     // 累积的正文文本
        QMap<int, QJsonObject> toolCallsMap; // index -> tool call 对象
        QTextCursor uiCursor;           // 流式 UI 占位光标
        QString reasoningPlaceholderId; // 思考占位块的唯一 ID
        QString contentPlaceholderId;   // 正文占位块的唯一 ID
        int interfaceIndex = 0;
        int keyIndex = 0;
        int retryCount = 0;
        int aiReplyStartPos=0;
        QString currentUrl;
        QString currentKey;
        bool finished = false;
        bool userAborted = false;
        bool dyc=false;
        bool toolCallPending = false;

    };
    int m_completion_tokens=0; //补全
    int m_prompt_tokens=0; //提示词
    int m_prompt_tokens_details=0; //命中缓存
    int m_completion_tokens2=0; //补全
    int m_prompt_tokens2=0; //提示词
    int m_prompt_tokens_details2=0; //命中缓存

    int m_completion_tokens3=0; //补全
    int m_prompt_tokens3=0; //提示词
    int m_prompt_tokens_details3=0; //命中缓存
    void scrollToBottom();
    void onScrollChanged(int value);
    bool m_programScroll= false;
    bool zdgd=true;
    bool m_ok=false;
    QString m_err;

    std::unique_ptr<StreamSession> m_stream; // 当前流式会话
    QMap<QString, std::function<QString(const QString&, const QString&)>> m_toolHandlers;
    void tryNextStreamEndpoint(StreamSession *s);
    void startStreamRequest(StreamSession *s, const QString &url, const QString &key);
    void onStreamReadyRead();
    void onStreamFinished();
    void parseSSE(StreamSession *s, const QByteArray &line);

    void startStreamUI(StreamSession *s,const QString &type);
    void appendReasoningChunk(StreamSession *s, const QString &chunk);
    void appendContentChunk(StreamSession *s, const QString &chunk);
    void appendToolCallChunk(StreamSession *s, const QString &info);

    void finishStreamSession(StreamSession *s, bool success);
    void finalizeStreamCard(StreamSession *s);
    void Ai_posts_stream(const QString &model,std::function<void(const QString&)> callback);
    void onSendMessage_stream(const QString &text);
    QString buildMessageHtml(const QString &text, MessageType type);
    QString buildMessageHtml2(const QString &text, MessageType type, const QString &toolname);
};

#endif // APPWINDOW_H