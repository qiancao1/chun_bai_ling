/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GLOBAL_H
#define GLOBAL_H

#include "AccountPage.h"
#include "AiWidget.h"
#include "BlacklistPage.h"
#include "HomePage.h"
#include "PluginPage.h"
#include "ScheduleConfigWidget.h"
#include "ScreenshotSyncClient.h"
#include "SharedMemoryBridge.h"
#include <QMessageBox>
#include "cardwidget.h"
#include "chatpage.h"
#include "forbiddenwordpage.h"
#include "htmltoimagewidget.h"
#include "keywordmatchconfigwidget.h"
#include "logdb.h"
#include "LogPage.h"
#include <QJsonObject>
#include <qapplication.h>
#include <qjsondocument.h>

#include "node_plugin_manager.h"
#include "node_process.h"
#include "qqbotclient.h"
#include "botdb.h"
#include "lmdbkv.h"
#include <QPointer>
#include "sandboxwindow.h"
#include "set.h"
#include "websocketserver.h"
#include "PluginMarketWindow.h"
class Global
{
public:
    Global();
};
enum BitMask {
    BIT_Ainiren = 1 << 0,
    BIT_ruqun = 1 << 1,
    BIT_tuiqun = 1 << 2,
    BIT_AI_BAI = 1 << 3,
    BIT_SHUA_P = 1<<4,
    BIT_AUTO_JOJI_KG = 1<<5,
    BIT_AUTO_JOJI = 1<<6,
    BIT_ADMIN= 1<<7
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

struct cos_data{
    bool e=false;
    int errcs=0;
    QString secretId;
    QString secretKey;
    QString host;      // 完整域名
    QString baseUrl;   // https://bucket.cos.ap-guangzhou.myqcloud.com
};
struct cnb_data{

    QString repo;
    QString key;
    int qcjs=0;
    int errcs=0;
    bool e=false;
};



extern QString g_admin;
extern QString g_ip;
extern QListWidget *robotListWidget;
extern AiWidget *ai_ui;
extern HtmlToImageWidget *htmltoimg;
extern ScreenshotSyncClient *ScreenA;
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
extern QString g_neiw;
extern int g_EventLogs_index;
extern int g_channelLogs_index_index;
extern int g_privateLogs_index;
extern int g_channel_privateLogs_index;
extern int g_groupLogs_index;
extern int g_appid;
extern int Color_0;//默认
extern int Color_1;//默认
extern ScheduleConfigWidget *schedule;
extern int miaomiao32;
extern int miaomiao;
extern LmdbKV *cache_db;
extern LmdbKV *aidb;
extern LmdbKV *dsdb;
extern LmdbKV *accdb;
extern quint16 g_ws_port;
extern QString ws_token;
extern cos_data g_cos;
extern cnb_data g_cnb;
extern std::array<std::unique_ptr<LogDB>, 5> g_logdb;
extern WebSocketServer *ws_server;
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
extern int 定时检查变量;
extern bool 框架退出;
extern int plugin_n;
extern int plugin_n2;
extern QList<PluginInfo2> m_allPlugins;
extern MessageEvent *g_cqev;

void showAutoCloseMessageBox(const QString &title, const QString &text, int timeoutMs = 5000);
void AppendEventLog(const QString &msg, int color=Color_0);
QString upload(const QString &path);
QString upload(const QByteArray &data);
int mapTypeToTabIndex(int type);
void loadconfig();
void saveConfig();
QString extractBetween(const QString &source, const QString &left, const QString &right);//文本取中间
QStringList takeAllTextMiddle(const QString &original, const QString &leftMarker,const QString &rightMarker,bool includeSides);//文本取中间 批量
QString replaceBetweenAll(const QString &original,const QString &left,const QString &right,const QString &replacement,int maxReplacements = -1);
QString replaceFileTag(const QString &content, const QString &format = "[文件]%1(%2)");
QString joinIntListFast(const QList<int>& list, const QString& sep);//整数到文本数组
QString subTextReplace(const QString &source,const QString &find,const QString &replace,int replaceCount = -1,int startPos = 1); //子文本替换
QString normalizeNewlinesToCR(const QString &input);//处理换行符
void botnomsg(int appid,int type, const QString &openid, const QString &msgid);
qint64 mergeToId(int appid, int type);
void parseFromId(qint64 id, int &appid, int &type);
void doWork(int totalDelay);//延迟 ms
bool downloadFile(const QString &url, const QString &savePath, QString &errorMsg);
QByteArray R_file(const QString &path);
bool W_file(const QString &path,const QByteArray &data);
int accinfo(int appid);
void processPendingEvents();
QString python_code(const QString &py_code);
QString python_code(const QString &py_code,const MessageEvent &msg);


class SendMessageTask : public QRunnable {
public:
    SendMessageTask(QQBotClient* client,
                    int msgType,                     // 改为 int
                    const QString& contactId,
                    const QString& text,
                    const QString& msgIdFirst,

                    const QString& pname,
                    bool mode)              // 参数名 chatPage
        : m_client(client),
        m_msgType(msgType),
        m_contactId(contactId),
        m_text(text),
        m_msgIdFirst(msgIdFirst),
        m_pname(pname),
        mode(mode)
    {
        setAutoDelete(true);
    }
    void run() override {

        QQBotClient* client = m_client;
        bool success = false;
        QString deleteid,ref;
        bool zh=false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if(attempt==2 && m_msgType!=2) continue;
            QString currentMsgId = (attempt == 0) ? m_msgIdFirst : "";
            QString rawData = client->send_messages(m_msgType,m_contactId,m_pname, m_text, currentMsgId,zh,mode,聊天发送模式);
            if(rawData.isEmpty()) break;
            if (rawData.contains("ROBOT") || rawData.contains("消息提交安全审核成功")) { //检查发送成功 或主动推送
                success = true;
                break;
            }
            if(attempt==1 && m_msgType==2)
            {
                zh = true; //召回
            }
            continue;
        }
        QMetaObject::invokeMethod(qApp, [ success]() {
            if (success) {
                chatPage->inputEdit->clear();   // 假设 inputEdit 是公有成员
            }
        });
    }

private:
    QQBotClient* m_client;
    int m_msgType;                  // 整数类型
    QString m_contactId;
    QString m_text;
    QString m_msgIdFirst;
    QString m_pname;
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



enum CharCategory { CatUnknown, CatChinese, CatLetter, CatDigit, CatSymbol };

static CharCategory getCategory(const QChar &ch) {
    if (ch.isDigit()) return CatDigit;
    if (ch.isLetter() && ch.unicode() < 128) return CatLetter;
    if (ch.script() == QChar::Script_Han) return CatChinese;
    return CatSymbol;
}

template<typename... Args>
int extractParams(const QString &text, const QString &cmd, int filter, Args&... args) {
    // 1. 检查指令
    if (!text.startsWith(cmd)) return -1;

    // 2. 解析所有参数到 QList（自动按类型分割）
    QList<QString> params;
    int start = cmd.length();
    if (start < text.length()) {
        int paramStart = start, paramLen = 0;
        CharCategory currentType = CatUnknown;
        for (int i = start; i < text.length(); ++i) {
            QChar ch = text[i];
            CharCategory cat = getCategory(ch);
            CharCategory mergedCat = cat;
            if (mergedCat == CatLetter || mergedCat == CatDigit)
                mergedCat = CatLetter;   // 统一视为 CatLetter（或定义 CatAlnum）
            if (mergedCat != currentType) {
                if (paramLen > 0)
                    params.append(text.mid(paramStart, paramLen));
                paramStart = i;
                paramLen = 0;
                currentType = mergedCat;  // 存储合并后的类型
            }
            bool skip = false;
            switch (cat) {
            case CatChinese: skip = (filter & 4); break;
            case CatLetter:  skip = (filter & 1); break;
            case CatDigit:   skip = (filter & 2); break;
            case CatSymbol:  skip = !(filter & 8); break;
            default: break;
            }
            if (!skip) ++paramLen;
        }
        if (paramLen > 0)
            params.append(text.mid(paramStart, paramLen));
    }

    // 3. 将参数分配给变量（最后一个变量收尾）
    constexpr int varCount = sizeof...(Args);
    if (varCount == 0) return params.size();

    // 辅助递归函数：idx 为当前变量索引，varCount 为总变量数
    auto assignRecursive = [&](auto&& self, int idx, int varCnt, const QList<QString> &p, auto& first, auto&... rest) -> void {
        if (idx == varCnt - 1) {
            // 最后一个变量：将 p 从 idx 起的所有元素用空格连接
            QStringList remaining;
            for (int i = idx; i < p.size(); ++i)
                remaining << p[i];
            first = remaining.join(" ");
            return; // 没有后续变量了
        } else {

            if (idx < p.size())
                first = p[idx];

            if constexpr (sizeof...(rest) > 0) {
                self(self, idx + 1, varCnt, p, rest...);
            }
        }
    };

    // 启动递归
    assignRecursive(assignRecursive, 0, varCount, params, args...);

    return params.size(); // 返回实际提取的参数个数
}

#endif // GLOBAL_H
