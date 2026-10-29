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
#include <QSpinBox>
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

// ==================== 插件工具（把插件的指令注册成 AI 工具）====================
// 「插件工具」页里勾选的每一条指令 = 一个工具。存 data/plugin_tools.json。
// 调用链：AI 返回 tool_calls → handleAiResponse（aiwidget2.cpp）认领 toolName →
//         AiWidget::runPluginTool → pluginPage->invokeToolCommand（pluginpage.cpp）。
// 返回值的两种来源：
//   ① 同步返回值：Python 插件 return 的文本 / 原生库 onMessagev3 的返回值；
//   ② 发送拦截：JS / 32 位易语言 / 只调发送 API 的插件没有返回值，
//      调插件前先 toolGateBegin 设一道 gate（见 core/global.h）—— 插件往本会话发的消息
//      在 API_ID_SEND_MESSAGES 入口就被扣下，**不真发出去**，正文交给 AI 决定怎么处理。
struct PluginToolItem {
    QString     pluginId;      // 插件 id（空 = 按名字认）
    QString     pluginName;    // 插件名（界面上显示 / 日志用）
    int         matchType = 0; // 注册时的 MatchType 整数值（防同名不同匹配方式串味）
    QString     key;           // 插件注册的**原始**指令
    QString     toolName;      // 给 AI 的函数名（自动生成，稳定不变）
    QString     desc;          // 工具说明（AI 看的）；空 = 按指令名自动生成
    QStringList params;        // 参数说明 p1..pN（空 = 一个可留空的 p1）
    bool        enabled = false; // 旧字段（全局开关）。现在看 bots；读老配置时 true 会迁成 bots={"*"}
    // 启用这条工具的机器人（appid_int 字符串，按机器人各管各的）。
    //   "*" = 所有机器人 —— 旧版全局勾选迁移过来的标记，用户一改动就展开成具体 appid；
    //   空     = 谁都没开。
    QStringList bots;
};

// 插件工具调用器：runToolLoop 只负责「按顺序跑 tool_calls」，具体怎么等结果交给它。
// 目前只有一种实现 —— 阻塞等（runPluginTool：卡住当前线程，最多 m_pluginToolWaitMs = 3 秒）。
// 留成函数对象只是为了让工具循环和「等结果」解耦，不是异步。
// cb 必须**恰好调用一次**，参数是给 AI 看的结果文本。
using PluginToolInvoker = std::function<void(const PluginToolItem &,
                                             const QString &args,
                                             const MessageEvent &ev,
                                             std::function<void(const QString &)> cb)>;

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
    bool bot_admin=false; //机器人是不是管理员 跟着最近一条 ev 更新（flushPendingMessagesTail 里手工拼的 ev 要带上它）
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
    // 异步版 Ai_post(model,msg,timeout)：按模型名解析接口/key，结果一次性回调。
    // 配置错误（模型不存在/未设接口）也走 cb，调用方拿到的语义和同步版返回值一致。
    void Ai_postAsync(const QString &model, const QString &msg, int timeoutMs, AiReplyCb cb);
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

    // 给 WebUI 用：网页上改完 data/model_config.json / data/fujia.json 之后重新读一遍，
    // 免得桌面端还显示旧数据。分别对应「模型配置」和「附加模型」两个标签页。
    void webuiReloadModelConfig();
    void webuiReloadFujia();
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
    PlaceholderLineEdit *editRobotName, *editContext, *editNoReplySeconds, *editNoReplyMinutes, *editDelayReply;
    PlaceholderLineEdit *set_zl, *set_sc,*set_qy,*set_sjhf,*set_递增概率,*set_固定条数;
    QComboBox *comboModel, *comboSetting,*comboPplx,*combo_xiangliang;

    QListWidget *settingListWidget;    // 全局设定列表
    QTextEdit *settingTextEdit;

    QLabel *lblSettingName;
    PlaceholderLineEdit *editSettingName;
    QPushButton *btnAddSetting;        // 添加/保存设定
    QPushButton *btnDeleteSetting;     // 删除设定

    QPushButton *btnSaveRobot;         // 保存机器人按钮（位于首页）

    // ============== 模型配置 (tab 2) ==============
    QTableWidget *modelListTable;      // 左侧：模型名
    QPushButton *modelListAddBtn;      // 左侧：添加新行
    QPushButton *modelListDelBtn;      // 左侧：删除选中
    QPushButton *modelListFetchBtn;    // 左侧：拉取选中接口的模型列表并加入

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
    // 用「接口列表」里选中的接口去请求它的 /models，用户在弹窗里挑一个 →
    // 加入左侧模型列表，并自动勾选（启用）这个接口。
    void onModelFetchFromInterface();
    void fetchModelListStep();          // 依次尝试候选 models 地址（异步，回调已切回 GUI 线程）
    // 上面那条异步链的中间状态（只在 GUI 线程读写）
    struct FetchModelState {
        bool        inFlight = false;
        int         iface    = -1;
        QString     key;
        QStringList urls;
        int         pos      = 0;
        QString     lastErr;
    } m_fetchModel;
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

    // ============== 插件工具 (tab 4)：把插件的指令注册成 AI 工具 ==============
    void loadPluginTools();                    // 读 data/plugin_tools.json
    void savePluginTools() const;              // 写回
    void refreshPluginToolPlugins();           // 重扫 m_pluginList，填左侧插件列表（保留当前选中）
    void rebuildPluginToolTable();             // 按当前选中的插件重建右侧指令表
    void rebuildPluginToolIndex();             // 有 toolName 的项 → m_pluginToolByName
    // 某台机器人是否启用这条插件工具（bots 里带 "*" = 所有机器人，旧配置迁移标记）
    static bool pluginToolEnabledFor(const PluginToolItem &it, int appid);
    QString currentBotName() const;            // 当前 g_appid 对应的机器人名（界面提示用）
    QStringList allBotAppids() const;          // 现有全部机器人的 appid 字符串（展开 "*" 用）
    QString pluginToolHintText() const;        // 「插件工具」页顶部那句提示（含当前机器人）
    void refreshPluginToolChecks();            // 切机器人后只刷勾选状态 + 提示，不重扫插件表
    QJsonObject pluginToolFunctionJson(const PluginToolItem &it) const;  // 单个工具的 schema
    PluginToolItem *findPluginToolItem(const QString &pluginId, int matchType, const QString &key);
    // AI 真正调用一个插件工具：argsJson = tool_call 的 arguments（JSON 文本）。
    // 永远返回一段给 AI 看的文本（失败/超时也是文本，不让整轮对话断掉）。
    // ⚠ **阻塞**：会卡住当前线程等插件把话说完，硬上限 3 秒（m_pluginToolWaitMs）。
    QString runPluginTool(const PluginToolItem &it, const QString &argsJson, const MessageEvent &ev);
    // 前半段：p1..p8 拼参数、按 msgid 里的 seq 补真实发送人。
    static QString ptJoinArgs(const QString &argsJson);
    void ptFillSender(MessageEvent &ev);

    // 插件工具页的槽（指针式 connect 要私有成员函数即可）
    void onPluginToolPluginChanged(int row);
    void onPluginToolItemChanged(QTableWidgetItem *item);
    void onPluginToolWaitChanged(int secs);

    QListWidget *pluginToolPluginList = nullptr;   // 左：插件
    QTableWidget *pluginToolTable = nullptr;       // 右：该插件的指令（带勾选）
    QPushButton *pluginToolRefreshBtn = nullptr;
    QLabel      *pluginToolHintLabel = nullptr;    // 顶部提示（显示当前机器人）
    QSpinBox    *pluginToolWaitSpin = nullptr;     // 没返回值时最多等几秒
    QList<PluginToolItem> m_pluginTools;           // 全部配置项（是否对某台机器人启用看 bots）
    QHash<QString, int>   m_pluginToolByName;      // toolName → m_pluginTools 下标（含未启用的，调用前再校验）
    // ⚠ 上面两张表在 GUI 线程写（勾选/改说明），却在**线程池线程**读（get_tools / handleAiResponse
    //   调工具）→ 所有读写都要持这把锁，否则 QList 扩容时读侧会踩到已释放的内存。
    mutable QMutex m_pluginToolsMutex;
    QString pluginToolCfgPath = "data/plugin_tools.json";
    int     m_pluginToolWaitMs = 3000;   // 阻塞等待上限（界面 1~3 秒，代码里再 clamp 一次）
    // 右侧表当前显示的插件 + 每行对应的规则（只读视图，不把 py::object 暴露到 UI 层）
    struct PluginToolRow { int matchType = 0; QString key; QString view; };
    QList<PluginToolRow> m_pluginToolRows;
    int m_pluginToolCurPlugin = -1;
    // ============== 工具/函数配置 (tab 3) ==============
    QTableWidget *funcListTable;       // 左侧：备注列表
    QPushButton *funcListDelBtn;       // 左侧：删除选中
    QPushButton *funcListAddBtn;       // 左侧：添加行

    QTextEdit *funcCodeEdit;           // 右侧：上半部 Python 代码输入框

    PlaceholderLineEdit *funcNameEdit;           // 函数名
    QPushButton *funcSaveBtn;          // 保存按钮
    QCheckBox *funcInterruptCheck;     // 触发后中断

    PlaceholderLineEdit *param1Edit, *param2Edit, *param3Edit, *param4Edit;
    PlaceholderLineEdit *param5Edit, *param6Edit, *param7Edit, *param8Edit; // 8个参数

    // --- 数据 ---
    QList<FunctionData> functionList;   // 所有函数数据
    int currentRow = -1;                // 当前选中的行索引
    QString configFilePath = "data/functions.json"; // 配置文件路径

    QList<RoleSetting> m_globalSettings;     // 全局设定库
    ToolConfig m_toolConfig;                 // 工具配置

    QHash<QString,QJsonObject> m_fun;


    QJsonObject buildBaseContext(AccountInfo* info, const QString &Gid, const QString& openid, int type, bool bot_admin);
    void flushPendingMessages(const QString& openid,bool send);

    // flushPendingMessages 的后半段：决策结果出来之后（可能是异步回调里）接着走
    void flushPendingMessagesTail(const QString &openid, AccountInfo *info, int model_index,
                                  QJsonObject baseContext,
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
    // 处理一次 AI 响应：追加上下文 + 执行工具调用（同步阻塞，插件工具最多等 3 秒）。
    AiStepResult handleAiResponse(const QJsonObject &obj, const MessageEvent &ev, QJsonObject &sxw);

    // 前半段：解析 choices、追加 assistant 消息、把正文发出去。
    // 返回 false = 已经彻底结束（ret 里填好了失败原因），调用方直接返回即可。
    bool beginAiStep(const QJsonObject &obj, const MessageEvent &ev, QJsonObject &sxw,
                     AiStepResult &ret, QJsonArray &toolCalls, QString &text);
    // 工具循环：按 inv 逐个跑 tool_calls，跑完（或遇到 interrupt）就 done。
    // textIn 是「没调成任何工具时要吐出来的正文」（和旧逻辑一致）。
    void runToolLoop(const QJsonArray &calls, const MessageEvent &ev,
                     std::shared_ptr<QJsonObject> sxw, const QString &textIn,
                     PluginToolInvoker inv, std::function<void(AiStepResult)> done);

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