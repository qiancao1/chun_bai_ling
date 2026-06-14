#ifndef GLOBAL_H
#define GLOBAL_H

#include "AccountPage.h"
#include "BlacklistPage.h"
#include "HomePage.h"
#include "PluginPage.h"
#include "SharedMemoryBridge.h"

#include "cardwidget.h"
#include "chatpage.h"
#include "forbiddenwordpage.h"
#include "keywordmatchconfigwidget.h"
#include "logdb.h"
#include "LogPage.h"
#include <QJsonObject>
#include "node_plugin_manager.h"
#include "node_process.h"
#include "qqbotclient.h"
#include "botdb.h"
#include "lmdbkv.h"
#include "sandboxwindow.h"
#include "set.h"
class Global
{
public:
    Global();
};


struct dblog
{
    int time=0;//除以60存
    int appid=0;
    int type=0;
    QByteArray user;//固定长度16字节
    QByteArray groupId; //固定长度16字节
    QString msg;        // 日志消息内容

};





extern HomePage *homePage;
extern AccountPage *accountPage;
extern LogPage *logPage;
extern PluginPage *pluginPage;
extern ChatPage *chatPage;
extern SandboxWindow *Sandbox;
extern KeywordMatchConfigWidget *keyword;
extern set *setA;
extern QStackedWidget *stackedWidget;
extern ForbiddenWordPage *forbidden;
extern int m_currentBotIndex;
extern QList<LogEntry> g_EventLogs;
extern QList<LogEntry> g_channelLogs;
extern QList<LogEntry> g_privateLogs;
extern QList<LogEntry> g_channel_privateLogs;
extern QList<LogEntry> g_groupLogs;
extern int g_EventLogs_index;
extern int g_channelLogs_index_index;
extern int g_privateLogs_index;
extern int g_channel_privateLogs_index;
extern int g_groupLogs_index;
extern int Color_0;//默认
extern int Color_1;//默认

extern int miaomiao32;
extern int miaomiao;
extern LmdbKV *cache_db;
extern LogDB *g_logdb;
extern RingBuffer<LogEntry> m_logStore[5];
extern QHash<QString, QString> m_blacklist; // 黑名单哈希表
extern SharedMemoryBridge *bridge;
extern QJsonObject g_config;
extern QList<PluginInfo> m_pluginList;
extern QList<std::shared_ptr<AccountInfo>> m_accounts;
extern QHash<int, QQBotClient*> m_botClients;
extern QHash<int, BotDB*> g_botdb;
extern QHash<int, CardWidget*> g_CW;
extern QString g_sandboxuuid;
extern double totalMemMB;
extern qint64 g_totalRuntime;
extern std::string g_keyuuid;
extern char* g_keyuuid2;
extern QString ffmpegdiv;
extern BlacklistPage *Black;
extern int 聊天发送模式;
extern int plugin_n;
void showAutoCloseMessageBox(const QString &title, const QString &text, int timeoutMs = 5000);
void AppendEventLog(const QString &msg,const QColor color=Qt::black);
QString upload(const QString &path);
QString upload(const QByteArray &data);
int mapTypeToTabIndex(int type);
void loadconfig();
void saveConfig();
QString extractBetween(const QString &source, const QString &left, const QString &right);//文本取中间
QString replaceBetweenAll(const QString &original,const QString &left,const QString &right,const QString &replacement,int maxReplacements = -1);
QString replaceFileTag(const QString &content, const QString &format = "[文件]%1(%2)");
QString joinIntListFast(const QList<int>& list, const QString& sep);//整数到文本数组
QString subTextReplace(const QString &source,const QString &find,const QString &replace,int replaceCount = -1,int startPos = 1); //子文本替换
QString normalizeNewlinesToCR(const QString &input);//处理换行符
void botnomsg(int type, const QString &openid, const QString &msgid);
qint64 mergeToId(int appid, int type);
void parseFromId(qint64 id, int &appid, int &type);












class SendMessageTask : public QRunnable {
public:
    SendMessageTask(QQBotClient* client,
                    int msgType,                     // 改为 int
                    const QString& contactId,
                    const QString& text,
                    const QString& msgIdFirst,
                    const QString& msgIdRetry,
                    const QString& nickname,
                    bool mode)              // 参数名 chatPage
        : m_client(client),
        m_msgType(msgType),
        m_contactId(contactId),
        m_text(text),
        m_msgIdFirst(msgIdFirst),
        m_msgIdRetry(msgIdRetry),
        m_nickname(nickname),
        mode(mode)
    {
        setAutoDelete(true);
    }

    void run() override {
        QString sentText = m_text;
        QString nickname = m_nickname;
        QQBotClient* client = m_client;

        bool success = false;
        QString finalDisplayText = sentText;
        QString deleteid,ref;
        bool zh=false;
        for (int attempt = 0; attempt < 2; ++attempt) {
            QString currentMsgId = (attempt == 0) ? m_msgIdFirst : m_msgIdRetry;
            QString txt = "[沙箱|%1ms]";
            QString rawData = client->send_messages(m_msgType,m_contactId,txt, m_text, currentMsgId,zh,mode);

            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(rawData.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                QJsonObject obj = doc.object();
                QString msg = obj["messge"].toString();
                deleteid = obj["id"].toString();
                QJsonObject obj2 =obj["ext_info"].toObject();
                ref = obj2["ref_idx"].toString();
                if(!ref.isEmpty()) ref = "[ref,msg_idx="+ref+"]";
                if (!deleteid.isEmpty() || msg == "消息提交安全审核成功") {
                    success = true;
                    break;
                }

                if(attempt==0 && m_msgType==2)
                {
                    zh=true; //召回
                    continue;
                }
                finalDisplayText = sentText + rawData;
            } else {
                finalDisplayText = sentText + rawData;
            }
            success = false;
            break;
        }

        QMetaObject::invokeMethod(qApp, [sentText, finalDisplayText, success, nickname,deleteid,ref]() {
            if (success) {
                // 发送成功：清空输入框，添加自己的消息
                chatPage->inputEdit->clear();   // 假设 inputEdit 是公有成员
                chatPage->addMessage(Message("", sentText, true,
                                             QDateTime::currentDateTime().toString("hh:mm:ss"),
                                             nickname,ref,deleteid));
            } else {
                // 发送失败：显示错误信息
                Message m{};
                m.isSelf=true;
                m.msg="[发送失败]"+finalDisplayText;
                m.timestamp=QDateTime::currentDateTime().toString("hh:mm:ss");
                m.name="我";
                chatPage->addMessage(m);

            }
        });
    }

private:
    QQBotClient* m_client;
    int m_msgType;                  // 整数类型
    QString m_contactId;
    QString m_text;
    QString m_msgIdFirst;
    QString m_msgIdRetry;
    QString m_nickname;
    bool mode;
};
class JsApiTask : public QRunnable {
public:
    JsApiTask(NodePluginManager* mgr, const QString& uuid, int id,
              const QString& method, const QJsonArray& params,
              QPointer<NodeProcess> proc)
        : m_mgr(mgr), m_uuid(uuid), m_id(id), m_method(method), m_params(params), m_proc(proc)
    {
        setAutoDelete(true);
    }

    void run() override {
        // 在线程池线程中执行耗时的 API 调用
        QString result = m_mgr->processApiRequest(m_uuid, m_method, m_params);

        // 将响应发送回主线程（因为 QProcess::write 必须在主线程）
        QMetaObject::invokeMethod(qApp, [weakProc = m_proc, id = m_id, result]() {
            NodeProcess* proc = weakProc.data();
            if (proc && proc->isRunning()) {  // 此时在主线程，安全
                proc->sendResponse(id, result);
            }
        }, Qt::QueuedConnection);
    }

private:
    NodePluginManager* m_mgr;
    QString m_uuid;
    int m_id;
    QString m_method;
    QJsonArray m_params;
    QPointer<NodeProcess> m_proc;
};
#endif // GLOBAL_H
