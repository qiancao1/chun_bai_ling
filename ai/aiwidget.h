#ifndef AIWIDGET_H
#define AIWIDGET_H

#include <QWidget>
#include <QList>
#include <qcheckbox.h>
#include <qlabel.h>
#include <qlistwidget.h>
#include <qpushbutton.h>
#include <qtablewidget.h>
#include "accountinfo.h"
#include "aifujia.h"
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <functional>
#include <memory>
#include <QMutex>
#include <QSharedPointer>
#include <atomic>

#include "qqbotclient.h"
#include "placeholderlineedit.h"
#include "placeholdertextedit.h"
#include "vectormemory.h"

//#define QLineEdit PlaceholderLineEdit
//#define QTextEdit PlaceholderTextEdit




struct KeyData {
    bool enabled=false;
    QString key;
    int usageCount = 0;
    QString lastUsed;
};

struct InterfaceData {
    int key_index=0;
    QString remark;
    QString url;
    QList<KeyData> keys;
};


struct FunctionData {

    QString remark;               // 备注文本（显示在第一列）
    QString funcName;             // 函数名
    QString code;                 // Python 代码
    QStringList params;           // 8 个参数，索引 0~7
    bool interrupt = false;       // 中断复选框
};
struct PendingMessage {
    QString text;
    QStringList imagePaths; // 本地缓存图片路径（绝对路径）
};
struct Ai_Fun {

    QString p1,p2,p3,p4,p5,p6,p7,p8;

};

// AI 回复回调：异步请求完成时调用（在线程池线程执行，不是主线程）
using AiReplyCb = std::function<void(const QString&)>;
using AiRawCb   = std::function<void(const QByteArray&)>;


struct SessionContext {
    QTimer* timer = nullptr;
    QList<PendingMessage> pendingMessages; // 等待合并的消息
    bool isProcessing = false;
    int appid=0;
    int type=0;
    int sjs=0;
    int ts=0;
    int dslx=0; //0常规对话 1定时N秒 1定时N分钟
    int duihts=0;
    int cflx=0;//触发类型 1艾特 2其他
    // 用 QSharedPointer：AI 请求在线程池线程跑，回调里还会接着用 memory，
    // 裸指针会被主线程的清理(clearSessionResources)提前 delete → use-after-free。
    // QSharedPointer 的引用计数是原子的，跨线程拷贝安全，持有副本即可保证对象活着。
    QSharedPointer<VectorMemory> memory;
    QString groupId;
    QString msgId;
    QString openid;

    AccountInfo* accountInfo = nullptr;
};
class AiWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AiWidget(QWidget *parent = nullptr);
    ~AiWidget();
    QString Ai_post(AccountInfo *info, const MessageEvent &ev);

    QString Ai_post(const QString &model, const QString &msg, int type);
    QString Ai_posts(const MessageEvent &ev, int model_index, QJsonObject &sxw, int timeoutMs);
    QString Ai_post(const MessageEvent &ev, const QString &url, const QString &key, QJsonObject &sxw, QString &err, int timeoutMs);
    QByteArray Ai_post3(const QString &url, const QString &key, QJsonObject &sxw, int timeoutMs);

    // ---- 异步回调版（请求发出后立即返回，不占用调用线程）----
    // cb 一定会被调用一次；失败时传回空串（同步版是返回空串让 Ai_posts 换下一个 key）
    void Ai_postsAsync(const MessageEvent &ev, int model_index, const QJsonObject &sxw, int timeoutMs, AiReplyCb cb);
    void Ai_postAsync(const MessageEvent &ev, const QString &url, const QString &key,
                      const QJsonObject &sxw, int timeoutMs, AiReplyCb cb);
    void Ai_post3Async(const QString &url, const QString &key, const QJsonObject &sxw,
                       int timeoutMs, AiRawCb cb);

    QJsonArray get_tools(const AccountInfo *info);
    QString Ai_qx(AccountInfo *info, const MessageEvent &ev);
    void list_c(); // 切换机器人
    // 注意：m_sessions 会被线程池线程访问（flushPendingMessages / ...Tail），
    // 主线程（onNewMessage / onCleanupTimer / onAsyncReply / 析构 / aisxw 界面）也在读写。
    // 任何一次访问都必须持 m_sessionsMutex，否则 QMap 和 SessionContext 里的
    // QString(COW) / 裸指针会被并发写坏 —— 表现为后面 delete ctx.timer / stop() 莫名其妙崩。
    QMap<QString, SessionContext> m_sessions;   // 以 openid 为键
    mutable QRecursiveMutex m_sessionsMutex;

    // 线程安全地改写某个会话的 isProcessing（可在任意线程调用）
    void setSessionProcessing(const QString &openid, bool processing);

    // 析构中：此时可能还有 AI 回调在线程池里飞，入口先判断它，别再去碰已销毁的成员
    std::atomic_bool m_shuttingDown{false};
    QList<ModelData> modelList;
    QList<InterfaceData> globalInterfaces;
    void trimToolResponses(QJsonObject &context, int maxToolMessages, int truncateLimit);
    void trimContextByMessageCount(QJsonObject &context, int maxMessages);
    QString trimContextByMessageCount2(QJsonObject &context, int maxMessages);

public slots:
    void onNewMessage(AccountInfo* info, const MessageEvent &ev, bool send, bool notime);


signals:
    void newMessageArrived(AccountInfo* info,const MessageEvent &ev, bool send,bool notime);
    void asyncReplyReceived(const QString &openid, const QString &reply,
                            const QJsonObject &updatedContext,       // mutableContext（含 AI 回复）
                            int oldMsgCount);
    void modelListUpdated(); // 仅仅作为一个“通知”

private slots:

    void onCleanupTimer();
    void onAsyncReply(const QString &openid, const QString &reply,
                      const QJsonObject &updatedContext,    // baseContextCopy
                      int oldMsgCount);
    // --- 机器人列表相关 ---


    void on_btnSaveRobot_clicked();                  // 保存当前机器人信息（新增/更新）

    // --- 全局设定相关 ---
    void on_settingListWidget_currentRowChanged(int currentRow);
    void on_btnAddSetting_clicked();
    void on_btnDeleteSetting_clicked();



private:
    void setupUi();                          // 纯代码构建全部 UI
    void loadFromFile();                     // 加载 roles.json
    void saveToFile1() const;                 // 保存到 roles.json
    void addtoui(const std::shared_ptr<AccountInfo> acc);

    void refreshSettingList();               // 刷新右侧全局设定列表
    void refreshSettingCombo();              // 刷新“设定”下拉框
    void onFuncListItemChanged(QTableWidgetItem *item);
    void loadFromFile2();
    void onFuncListAdd();
    void onFuncListDelete() ;
    void saveCurrentRowData();
    void onFuncListCurrentCellChanged(int currentRow, int currentCol,int previousRow, int previousCol);
    void clearRightPanel();
    void onFuncSave();
    void saveToFile();





    // 1. 把文字变成向量（调用嵌入模型）
    QVector<double> getEmbedding(const QString &text, const QString &url2, const QString &model, const QString &key);



    QString generateHash(const QString &url);

    QString downloadImage(const QString &url, const QString &hash);
    PendingMessage parseImageTagsAndDownload(const QString &msg);

    void appendPendingMessageToContext(QJsonObject &context, const PendingMessage &pm);

    void trimContextImages(QJsonObject &context, int maxImageMessages = 3);
    void convertContextImagesToBase64(QJsonObject &context);


    // --- UI 控件指针 ---
    QTabWidget *tabWidget;
    Aifujia *ai_fujia;
    QCheckBox *feibaimd,*chkGroupChat, *chkGroupPersonal, *chkPrivateChat;
    QCheckBox *chkChannel, *chkAtTrigger, *chkChannelPersonal, *chkImageRec,*chkniren,*向量数据库,*juece;//决策

    QLabel *lblRobotName, *lblModel, *lblSetting, *lblContext;
    QLabel *lblNoReplySeconds, *lblNoReplyMinutes, *lblDelayReply,*lblPplx;
    QLineEdit *editRobotName, *editContext, *editNoReplySeconds, *editNoReplyMinutes, *editDelayReply;
    QLineEdit *set_zl, *set_sc,*set_qy,*set_sjhf,*set_递增概率,*set_固定条数;
    QComboBox *comboModel, *comboSetting,*comboPplx,*combo_xiangliang;

    QListWidget *settingListWidget;    // 全局设定列表
    QTextEdit *settingTextEdit;

    QLabel *lblSettingName;
    QLineEdit *editSettingName;
    QPushButton *btnAddSetting;        // 添加/保存设定
    QPushButton *btnDeleteSetting;     // 删除设定

    QPushButton *btnSaveRobot;         // 保存机器人按钮（位于首页）

    // ============== 模型配置 (tab 2) ==============
    QTableWidget *modelListTable;      // 左侧：模型名
    QPushButton *modelListAddBtn;      // 左侧：添加新行
    QPushButton *modelListDelBtn;      // 左侧：删除选中

    QTableWidget *interfaceTable;      // 中间：备注、接口
    QPushButton *interfaceAddBtn;      // 中间：添加新行
    QPushButton *interfaceDelBtn;      // 中间：删除选中

    QTableWidget *keyTable;            // 右侧：key、使用次数、最后
    QPushButton *keyAddBtn;            // 右侧：添加新行
    QPushButton *keyDelBtn;            // 右侧：删除选中



    int currentModelRow = -1;
    int currentInterfaceRow = -1; // 当前选中的全局接口索引
    QString configFilePath2 = "data/model_config.json";

    void onModelAdd();
    void onModelDelete();
    void onModelCurrentCellChanged(int currentRow, int currentCol,int previousRow, int previousCol);
    void refreshInterfaceTableForModel(int modelIndex);
    void onInterfaceAdd();
    void onInterfaceDelete();
    void onInterfaceCurrentCellChanged(int currentRow, int currentCol,int previousRow, int previousCol);
    void loadKeysForInterface(int modelIndex, int interfaceIndex);
    void onInterfaceItemChanged(QTableWidgetItem *item);
    void onKeyAdd();
    void onKeyDelete();
    void saveToFile2();
    void loadFromFile3();
    void loadKeysForInterface(int interfaceIndex) ;
    void onInterfaceTableCellChanged(int row, int column);
    void onKeyTableCellChanged(int row, int column);
    void onmodelListTableCellChanged(int row, int column) ;
    void 刷新模型();
    void 内置函数();
    void 内置函数(const QString &Nmae,const QString &remark,const QStringList &params);
    // ============== 工具/函数配置 (tab 3) ==============
    QTableWidget *funcListTable;       // 左侧：备注列表
    QPushButton *funcListDelBtn;       // 左侧：删除选中
    QPushButton *funcListAddBtn;       // 左侧：添加行

    QTextEdit *funcCodeEdit;           // 右侧：上半部 Python 代码输入框

    QLineEdit *funcNameEdit;           // 函数名
    QPushButton *funcSaveBtn;          // 保存按钮
    QCheckBox *funcInterruptCheck;     // 触发后中断

    QLineEdit *param1Edit, *param2Edit, *param3Edit, *param4Edit;
    QLineEdit *param5Edit, *param6Edit, *param7Edit, *param8Edit; // 8个参数

    // --- 数据 ---
    QList<FunctionData> functionList;   // 所有函数数据
    int currentRow = -1;                // 当前选中的行索引
    QString configFilePath = "data/functions.json"; // 配置文件路径

    QList<RoleSetting> m_globalSettings;     // 全局设定库
    ToolConfig m_toolConfig;                 // 工具配置

    QHash<QString,QJsonObject> m_fun;


    QJsonObject buildBaseContext(AccountInfo* info, const QString &Gid, const QString& openid, int type);
    void flushPendingMessages(const QString& openid,bool send);

    // flushPendingMessages 的后半段：决策结果出来之后（可能是异步回调里）接着走
    void flushPendingMessagesTail(const QString &openid, AccountInfo *info, int model_index,
                                  QJsonObject baseContext, int oldMsgCount,
                                  bool juecejg, const QString &fh,
                                  const QList<PendingMessage> &pendings, const MessageEvent &ev);

    // 一次 AI 响应的解析结果
    //  Ok            → obj 可用，继续判断 choices / 工具调用
    //  TokenOverflow → 上下文超 token，已删一条历史，调用方应重试
    //  Failed        → 彻底失败（原因已写进 err），换下一个 key
    enum class AiParseResult { Ok, TokenOverflow, Failed };
    AiParseResult parseAiResponse(const QByteArray &response, const QString &key,
                                  QJsonObject &sxw, QString &err, QJsonObject &obj);

    // 一次 AI 响应的处理（追加上下文 + 执行工具调用）。同步/异步两条链路共用这段。
    struct AiStepResult {
        bool finished = false;  // true=本轮结束；false=带着新上下文再发一轮
        bool failed   = false;  // finished 且 failed=失败（err 为原因）
        QString err;
        QString text;           // finished 且 !failed=最终回复
    };
    AiStepResult handleAiResponse(const QJsonObject &obj, const MessageEvent &ev, QJsonObject &sxw);

    // 异步核心：单次接口上的「重试 + 多轮工具调用」状态机
    void Ai_postAsyncCore(const MessageEvent &ev, const QString &url, const QString &key,
                          std::shared_ptr<QJsonObject> sxw, std::shared_ptr<QString> err,
                          int timeoutMs, AiReplyCb cb);
    // 同 Ai_postsAsync，但上下文用 shared_ptr 共享：回调里能拿到请求过程中新增的消息
    void Ai_postsAsyncCore(const MessageEvent &ev, int model_index,
                           std::shared_ptr<QJsonObject> sxw, int timeoutMs, AiReplyCb cb);


    QTimer* m_cleanupTimer = nullptr;
    void startHourlyCleanupTimer();
    void clearSessionResources(SessionContext &ctx);   // 调用方需已持有 m_sessionsMutex
    void clearAllSessions();
};

#endif // AIWIDGET_H