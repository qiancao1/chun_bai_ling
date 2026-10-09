#ifndef PLUGINPAGE_H
#define PLUGINPAGE_H


#include <qprocess.h>
#pragma push_macro("slots")
#undef slots
#include <pybind11/embed.h>
#pragma pop_macro("slots")

#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QMap>
#include <QTextBrowser>
#include <QJsonObject>
#include <QLibrary>
#include <functional>      // sendData32Async 的回调
#include "qqbotclient.h"



class PluginManager;   // 前置声明
class QScrollArea;
class QLayout;


namespace py = pybind11;
const char* myCallback(const char* uuid,int apiId, int appid, const char* _1, const char* _2,
                       const char* _3, const char* _4, const char* _5,
                       const char* _6, const char* _7, const char* _8);
const char* myCallbackA(const char* uuid,int apiId, int appid, const char* _1, const char* _2,
                        const char* _3, const char* _4, const char* _5,
                        const char* _6, const char* _7, const char* _8);
typedef const char* (*UniversalApiCallback)(const char* uuid,int apiId, int appid, const char* _1, const char* _2,
                                            const char* _3, const char* _4, const char* _5,
                                            const char* _6, const char* _7, const char* _8);




typedef const char* (*GetPluginInfoFunc)(char*,UniversalApiCallback);
typedef void (*OnMessageFunc)(const char*);                 // 旧版：1 参
typedef void (*OnMessageFunc2)(const char*,qint64);         // 中间版：2 参
// 新版（**导出符号 onMessagev3**，不看插件信息里的字段）：3 参
//   (json   完整消息帧；正文 = **用户实输原文**
//    fun    框架匹配到的处理函数地址（插件据此分派到自己的 handler）
//    cmd    该指令的「**生效命令词**」= 界面上改过名就是新名、没改就是原名，**绝不为空**)
//   ⚠ 参数怎么取：插件拿 cmd 去切 json 正文里的参数（如正文 "战斗 打谁"、cmd "战斗" → 参数 "打谁"）。
//     **不能拿自己注册的原名去切** —— 改名后原名与生效名长度对不上（"攻击"→"修仙攻击" 就切错）。
//   **返回值 = 想让机器人发的话**（框架代发到本条消息的来源）。NULL / 空串 = 不发，
//   插件也可以照旧自己调 send_message，两种都支持。
//   ⚠ 所有权：返回的字符串由**插件持有**（静态缓冲 / 字面量），框架只读、绝不释放。
//   ⚠ v3 是新入口，签名就从这里定死成返回 const char*；on_message / onMessagev2 仍是 void。
typedef const char* (*OnMessageFunc3)(const char*,qint64,const char*);
typedef void (*OnFunc0)();

// ---- 昵称审核接口（插件可选实现，加进昵称审核窗口的下拉框后生效）----
// 函数名定死，Python 与原生库插件同名：
//   get_review_list(开始位置:int, 数量:int, 状态:int) -> JSON 文本 {"总数量":xx,"data":[{"id":整数,"name":"..","id2":".."}]}
//       状态（第 3 个参数）：1=待审核  2=已审核
//   submit_review(JSON文本) -> 任意文本  入参 {"状态":"同意"/"拒绝","data":[...]}
constexpr const char *kPluginFuncGetReviewList = "get_review_list";
constexpr const char *kPluginFuncSubmitReview  = "submit_review";
typedef const char* (*ReviewFetchFunc)(int, int, int);   // get_review_list(开始位置, 数量, 状态)
typedef const char* (*ReviewSubmitFunc)(const char*);    // submit_review
typedef const char* (*OnFunc1)();    // submit_review
typedef const char* (*OnFunc2)(const char*,const char*);    // submit_review

enum class MatchType {
    Equals,
    StartsWith,
    EndsWith,
    Contains,
    Regex,
    event
};

struct Rule {
    MatchType type;
    QString key;            // 插件注册的原始指令（永不改动）
    QString newKey;         // 用户重命名后的指令（空 = 未重命名）；匹配用「非空的那个」
    bool enabled = true;    // 该条指令是否启用（默认全部启用）
    py::object function;    // 已解析的 Python 可调用对象
    bool wantsCmd = false;  // 该处理函数是否接受第 2 个参数「生效命令词」（加载时按形参个数探测；
                            //   老插件只写 1 个形参 → false，仍按 1 参调用，不报错）
    bool caseSensitive = true;
    QRegularExpression regex;   // 多线程安全，只读使用
};
struct Rule_js {
    MatchType type;
    QString key;            // 插件注册的原始指令
    QString newKey;         // 重命名后的指令（空 = 未重命名）
    bool enabled = true;
    QString fun;    // 插件侧函数名
    bool caseSensitive = true;
    QRegularExpression regex;   // 多线程安全，只读使用
};
struct Rule_Dll {
    MatchType type;
    QString key;            // 插件注册的原始指令
    QString newKey;         // 重命名后的指令（空 = 未重命名）
    bool enabled = true;
    qint64 fun;    // 插件侧函数地址/序号
    bool caseSensitive = true;
    QRegularExpression regex;   // 多线程安全，只读使用
};

// 指令编辑页用的一行（只读视图，不把 py::object / 函数地址暴露给 UI 层）
struct RuleConfigRow {
    int     type    = 0;        // MatchType 的整数值
    QString key;                // 插件注册的原始指令
    QString newKey;             // 用户设置的新指令（空 = 不改名）
    bool    enabled = true;     // 是否启用
};
struct PythonPluginobj {
    //py::dict globals;

    py::object instance;                 // on_message 函数
    py::object onSet;                   // 加载后调用
    py::object onEnable;                 // 启用时调用
    py::object onDisable;                // 禁用时调用
    py::object onUnload;                 // 卸载前调用
    py::object getReviewList;            // 昵称审核：get_review_list(开始位置,数量,状态)
    py::object submitReview;             // 昵称审核：submit_review(JSON文本)
    py::object get_config_list;
    py::object set_config_value;
    bool isV2 = false;      // true = get_plugin_info 返回的 JSON 里 sdk >= 2 → 属「新版」，才允许指令重命名
    QList<Rule> rules;      // 所有规则列表（替代原来的 equals hash）


};
struct JsPlugin {
    QProcess* process = nullptr;
    QString entryScript;           // main.js 完整路径
    bool isReady = false;          // 是否已就绪（收到 ready 消息）
    bool isV2 = false;             // true = get_plugin_info 返回的 JSON 里 sdk >= 2 → 属「新版」，才允许指令重命名
    QJsonObject pendingRequest;    // 如果请求响应模式需要，可以暂存
    QList<Rule_js> rules;      // 所有规则列表（替代原来的 equals hash）
};


struct DLLPluginobj {
    GetPluginInfoFunc getPluginInfo;
    OnFunc1 get_config_list;
    OnFunc2 set_config_value;
    OnMessageFunc onMessage;
    OnMessageFunc2 onMessage2;
    OnMessageFunc3 onMessage3 = nullptr;
    bool isV2 = false;          // 新版（才允许指令重命名）：原生库(1) 看是否导出 onMessagev3；
                                // 32 位/易语言(2) 看插件信息 JSON 里的 sdk >= 2（它没有导出符号可查）
    OnFunc0 onEnable;
    OnFunc0 onDisable;
    OnFunc0 onUnload;
    OnFunc0 onSet;
    ReviewFetchFunc getReviewList = nullptr;    // 昵称审核：get_review_list(开始位置,数量,状态)
    ReviewSubmitFunc submitReview = nullptr;    // 昵称审核：submit_review(JSON文本)
    QList<Rule_Dll> rules;      // 所有规则列表（替代原来的 equals hash）
};
struct PluginInfo {
    QString id;
    QString name; //插件名字

    QString version; //版本

    QString author; //作者
    QString description; // 插件说明
    QString path;    // 路径
    QString icon;
    QLibrary* dllLib = nullptr;
    QString loadedDllPath;
    PythonPluginobj python;
    DLLPluginobj DLL;
    JsPlugin js;
    QString uuid;
    QList<int> appid;
    int type=0;        // "DLL" 或 "内置"
    int version_int=0; //版本
    int SendQuantity=0;
    bool enabled;
};
// 自定义列表小部件

class PluginItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit PluginItemWidget(const PluginInfo &info, QWidget *parent = nullptr);
    void updateInfo(const PluginInfo &info);

private:
    QLabel *iconLabel;
    QLabel *statusIndicator;
    QLabel *nameLabel;
    QLabel *authorLabel;
    QLabel *versionLabel;
    QLabel *typeLabel;      // 插件类型徽章（Python / DLL / DLL32 / JS）
};

class PluginPage : public QWidget {
    Q_OBJECT
public:
    explicit PluginPage(QWidget *parent = nullptr);
    void foruninstall_Plugin();
    QString LoadPlugin(const QString &path,int type,bool enabled,QList<int> &array);
    QString LoadPlugin_DLL(PluginInfo &info);
    QString LoadPlugin_py(PluginInfo &info);
    bool uninstall_Plugin(int index);//卸载
    bool uninstall_Plugin2(int index);
    bool uninstall_Plugin(PluginInfo &info);
    bool Enabled_Plugin(int index);//启用
    bool Enabled_Plugin(PluginInfo &info);
    bool Reload_Plugin(int index);//重载
    bool disable_Plugin(PluginInfo &info);//禁用
    void savePlugins();
    void loadPlugins();
    void dispatch_message(const QString &text, MessageEvent &msg);

    // ==================== 指令编辑（勾选启用 / 重命名）====================
    // 配置按「插件 id」存；id 为空时退化成插件名。给 RuleEditDialog 用的几个只读视图接口。
    QList<int> ruleConfigPluginIndexes() const;         // 有指令的插件下标（按列表顺序）
    QString    ruleConfigPluginLabel(int index) const;  // 列表里显示的文字
    QList<RuleConfigRow> ruleConfigRows(int index) const;
    bool ruleConfigAllowRename(int index) const;        // 只有新版插件才允许改名（原生库看 onMessagev3 符号；32位/Python/JS 看 sdk 字段）
    void applyRuleConfigRows(int index, const QList<RuleConfigRow> &rows);   // 存盘 + 立即对活体生效

    void initPluginList(const QList<PluginInfo> &plugins);
    void appendPlugin(const PluginInfo &info);
    void insertPlugin(int index, const PluginInfo &info);
    void removePlugin(int index);
    void updatePlugin(int index, const PluginInfo &newInfo);
    void addPluginItemToUI(int index, const PluginInfo &info);
    void insertPluginItemToUI(int index, const PluginInfo &info);
    void updatePluginItemInUI(int index);
    void npmJSpk(const QString &dir);
    QString LoadPlugin_js(PluginInfo &info);
    int findPluginIndex(const QString &id) const;

    // ==================== 往 32 位模块（miaomiao32）发命令 ====================
    // 桥接本身只有「写任务槽 → 阻塞等返回值」这一条同步通道（processRequestsA）。
    // 直接在 GUI 线程上等 = 界面「无响应」+ 全 app 的 QTimer / 网络 / AI 一起停摆
    //（32 位模块没在跑时要白等满 5 秒），所以内核改成了**异步**：
    //   写任务槽 → 立刻返回 → 每 100ms 用 QTimer 回头看一次有没有返回值 → 拿到就回调，到点超时。
    // 100ms 这个间隔是 2026-10-09 用户拍板的（对用户无感）。
    QString sendData32(int type,PluginInfo &info,const QString &appidlist = QString());
    QString sendData32(int type, PluginInfo &info, const QString &id, const QString &value);
    // 异步内核：回调在**调用者线程**执行（GUI 线程发的就在 GUI 线程回调）。
    //   timeoutMs <= 0 → 只发不等（返回值没人要的场景，连等待者都不登记、一个定时器都不起）
    // 每条命令都会领一个**请求号**塞进 JSON 的 "reqid"（易语言回吐时原样带回，见
    // SharedMemoryBridge::deliverCommandResult），所以**多条命令可以同时在飞**、互不串包。
    // 只有「桥里登记失败」（请求号用尽）才会立即回调空串。
    void sendData32Async(const QJsonObject &req, int timeoutMs,
                         const std::function<void(const QString &)> &cb);
    // 只发不等：和 sendData32 一样的组帧，但甩完就走（调用点本来也不看返回值）
    void sendData32NoWait(int type, PluginInfo &info, const QString &appidlist);
    QString LoadPlugin_DLL32(PluginInfo &info);
    void syncPluginsTo32();
    QString anzpip(const QString &reqPath);
    void safeCall(const py::object &func);

    void LoadPlugin_Python_pip(const QString &dir);

private slots:
    void onPluginSelected(int row);
    void onAccountCheckStateChanged(QListWidgetItem *item);
    void onPluginRowsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row);
    void stopAsyncioThread();
    void onItemDoubleClicked(QListWidgetItem *item);//列表被双击
    void onNpmFinished(int exitCode, QProcess::ExitStatus status);
    void onNpmOutputReady();
    void onNpmErrorReady();

    void onPipFinished(int exitCode, QProcess::ExitStatus status);
    void onPipOutputReady();
    void onPipErrorReady();

private:
    // 插件加载完成（rules 已解析）后套用已保存的指令配置（启停 / 重命名 / regex 重建）
    void applySavedRuleConfig(PluginInfo &info);

    // 32 位命令的**同步外壳**（给上面那两个 sendData32 用）：
    //   GUI 线程 → 局部事件循环驱动 100ms 轮询（界面照常重绘，其它定时器/网络也照跑）；
    //   ⚠ 等待期间不派发用户输入（按钮点击排队，等这次做完再处理），否则连点两下会撞上
    //     「同一时间只允许一个命令在飞」的桥接约束；
    //   非 GUI 线程（WebUI 的 HTTP 线程 / 机器人线程）没有事件循环可跑 → 保持原来的阻塞等待。
    QString sendData32Wait(const QJsonObject &req, int timeoutMs);

    void doLoadPlugin(const QString &dir); // 从 LoadPlugin_JS 中提取加载逻辑
    void initPython();
    QProcess *m_npmProcess = nullptr;
    QDialog *m_npmDialog = nullptr;
    QTextEdit *m_npmLog = nullptr;

    void doLoadPythonPlugin(const QString &dir);  // 加载插件核心逻辑

    QProcess *m_pipProcess = nullptr;
    QDialog *m_pipDialog = nullptr;
    QTextEdit *m_pipLog = nullptr;


    void setupUi();

    // 配置区视口宽度一变（首次布局 / 拉窗口），折行数就变 → 高度要跟着重算
    bool eventFilter(QObject *obj, QEvent *event) override;

    void updateInfo(const PluginInfo &info);
    void LoadPlugin_DLL();
    void LoadPlugin_Python();

    void LoadPlugin_JS();
    void updateDetailPanel(int index);
    void updateAccountCheckList(int pluginIndex);
    void onMessageReceived(MessageEvent &msg, const PluginInfo &p, std::optional<py::gil_scoped_acquire> &gil) ;
    // ---- 插件配置（get_config_list / set_config_value，四种类型插件都可实现）----
    void rebuildConfigPanel(int index);
    void clearConfigPanel();
    void updateConfigScrollHeight();          // 按可用宽度重算配置区高度（不超过 5 行）
    int  configAvailableWidth() const;        // 配置区可用宽度（兜底 260）
    QString callGetConfigList(int index);
    QString callSetConfigValue(int index, const QString &id, const QString &value);
    void applyPluginConfig(int index, const QString &id, const QString &value);
    QListWidget *pluginListWidget;
    QPushButton *reloadBtn;
    QPushButton *openDirBtn;
    QLabel *detailpathLabel;
    QTextBrowser *detailDescLabel;
    QScrollArea *configScroll = nullptr;      // 插件配置滚动区
    QWidget *configContainer = nullptr;       // 配置项容器
    QLayout *configLayout = nullptr;          // 配置项布局（自适应折行，见 pluginpage.cpp）
    bool m_configHeightLock = false;          // 重算高度时防递归（改高度会再触发视口 resize）
    QListWidget *rightCheckList;
    QPushButton *pypip,*ai_c_j,*ai_b_j;
    QPushButton *plugin_sc;
    QPushButton *loadBtn;
    QPushButton *addPluginBtn;   // 顶部按钮
    QPushButton *addPluginBtn2,*addPluginBtn3;   // 顶部按钮
    QPushButton *uninstallBtn;   // 卸载按钮
    QPushButton *setBtn;
    QPushButton *bj_zl = nullptr;   // 「编辑指令」按钮
    QJsonObject m_ruleCfg;          // 指令配置：{ "<插件id或名>": [ {t,k,n,e} ... ] }
    int currentSelected_index;
    std::thread m_asyncio_thread; // 改成成员变量

    py::object m_asyncio_mod;
    py::object m_run_coro_func;
    py::object m_loop;
};

#endif // PLUGINPAGE_H
