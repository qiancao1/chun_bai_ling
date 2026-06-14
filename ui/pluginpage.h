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

struct PythonPluginobj {
    //py::dict globals;
    QList<QString> requires;
    py::object instance;                 // on_message 函数
    py::object onSet;                   // 加载后调用
    py::object onEnable;                 // 启用时调用
    py::object onDisable;                // 禁用时调用
    py::object onUnload;                 // 卸载前调用
};
struct JsPlugin {
    QProcess* process = nullptr;
    QString entryScript;           // main.js 完整路径
    bool isReady = false;          // 是否已就绪（收到 ready 消息）
    QJsonObject pendingRequest;    // 如果请求响应模式需要，可以暂存
};
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


struct DLLPluginobj {
    GetPluginInfoFunc getPluginInfo;
    OnMessageFunc onMessage;
    OnFunc0 onEnable;
    OnFunc0 onDisable;
    OnFunc0 onUnload;
    OnFunc0 onSet;
};
struct PluginInfo {
    QString name; //插件名字
    int type;        // "DLL" 或 "内置"
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
    bool uninstall_Plugin(PluginInfo &info);
    bool Enabled_Plugin(int index);//启用
    bool Reload_Plugin(int index);//重载
    bool disable_Plugin(PluginInfo &info);//禁用
    void savePlugins();
    void loadPlugins();
    void dispatch_message(const QString &text, const MessageEvent &msg);
    void initPluginList(const QList<PluginInfo> &plugins);
    void appendPlugin(const PluginInfo &info);
    void insertPlugin(int index, const PluginInfo &info);
    void removePlugin(int index);
    void updatePlugin(int index, const PluginInfo &newInfo);
    void addPluginItemToUI(int index, const PluginInfo &info);
    void insertPluginItemToUI(int index, const PluginInfo &info);
    void updatePluginItemInUI(int index);
    QString LoadPlugin_js(PluginInfo &info);
    int findPluginIndex(const QString &id) const;
    QString sendData32(int type,PluginInfo &info,const QString &appidlist = QString());
    QString LoadPlugin_DLL32(PluginInfo &info);
    void syncPluginsTo32();

private slots:
    void onPluginSelected(int row);
    void onAccountCheckStateChanged(QListWidgetItem *item);
    void onPluginRowsMoved(const QModelIndex &parent, int start, int end, const QModelIndex &destination, int row);


private:
    void setupUi();
    void updateInfo(const PluginInfo &info);
    void LoadPlugin_DLL();
    void LoadPlugin_Python();
    void LoadPlugin_JS();
    void updateDetailPanel(int index);
    void updateAccountCheckList(int pluginIndex);
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
    QPushButton *pypip;

    QPushButton *loadBtn;
    QPushButton *addPluginBtn;   // 顶部按钮
    QPushButton *addPluginBtn2,*addPluginBtn3;   // 顶部按钮
    QPushButton *uninstallBtn;   // 卸载按钮
    QPushButton *setBtn;
    int currentSelected_index;


};

#endif // PLUGINPAGE_H
