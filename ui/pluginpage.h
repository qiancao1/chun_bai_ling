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
#include <QLibrary>
#include "qqbotclient.h"



class PluginManager;   // 前置声明


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
typedef void (*OnMessageFunc)(const char*);
typedef void (*OnFunc0)();


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
    QString key;            // 匹配值
    py::object function;    // 已解析的 Python 可调用对象
    bool caseSensitive = true;
    QRegularExpression regex;   // 多线程安全，只读使用
};

struct PythonPluginobj {
    //py::dict globals;

    py::object instance;                 // on_message 函数
    py::object onSet;                   // 加载后调用
    py::object onEnable;                 // 启用时调用
    py::object onDisable;                // 禁用时调用
    py::object onUnload;                 // 卸载前调用
    QList<Rule> rules;      // 所有规则列表（替代原来的 equals hash）


};
struct JsPlugin {
    QProcess* process = nullptr;
    QString entryScript;           // main.js 完整路径
    bool isReady = false;          // 是否已就绪（收到 ready 消息）
    QJsonObject pendingRequest;    // 如果请求响应模式需要，可以暂存
};


struct DLLPluginobj {
    GetPluginInfoFunc getPluginInfo;
    OnMessageFunc onMessage;
    OnFunc0 onEnable;
    OnFunc0 onDisable;
    OnFunc0 onUnload;
    OnFunc0 onSet;
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
    QString sendData32(int type,PluginInfo &info,const QString &appidlist = QString());
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

    void updateInfo(const PluginInfo &info);
    void LoadPlugin_DLL();
    void LoadPlugin_Python();

    void LoadPlugin_JS();
    void updateDetailPanel(int index);
    void updateAccountCheckList(int pluginIndex);
    void onMessageReceived(MessageEvent &msg, int i);
    QListWidget *pluginListWidget;
    QPushButton *reloadBtn;
    QPushButton *openDirBtn;
    QLabel *detailIconLabel;
    QLabel *detailNameLabel;
    QLabel *detailTypeLabel;
    QLabel *detailVersionLabel;
    QLabel *detailAuthorLabel;
    QLabel *detailpathLabel;
    QTextBrowser *detailDescLabel;
    QLabel *detailStatusLabel;
    QListWidget *rightCheckList;
    QPushButton *pypip,*ai_c_j,*ai_b_j;
    QPushButton *plugin_sc;
    QPushButton *loadBtn;
    QPushButton *addPluginBtn;   // 顶部按钮
    QPushButton *addPluginBtn2,*addPluginBtn3;   // 顶部按钮
    QPushButton *uninstallBtn;   // 卸载按钮
    QPushButton *setBtn;
    int currentSelected_index;
    std::thread m_asyncio_thread; // 改成成员变量

    py::object m_asyncio_mod;
    py::object m_run_coro_func;
    py::object m_loop;
};

#endif // PLUGINPAGE_H
