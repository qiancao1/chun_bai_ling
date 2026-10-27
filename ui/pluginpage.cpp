#include "pluginpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QPixmap>
#include <QHeaderView>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QPointer>
#include <QTimer>
#include <QApplication>
#include <QEventLoop>       // 32 位命令的同步外壳：GUI 线程上用局部事件循环跑 100ms 轮询
#include <QElapsedTimer>
#include <QThread>
#include <qlibrary.h>
#include "appwindow.h"

#include "pluginmarketwindow.h"
#include "plugininstaller.h"  // PluginInstaller::pluginsRootDir()：文件对话框默认定位到 <程序目录>/plugins
#include "ruleeditdialog.h"   // 「编辑指令」独立页面
#include "global.h"
#include "node_plugin_manager.h"
#include "scrolltextdialog.h"   // 可滚动的只读文本弹窗（内容多的提示用它替代 QMessageBox）

#include <QListWidget>
#include <QScrollArea>
#include <QCheckBox>
#include <QLineEdit>
#include <QLayout>
#include <QEvent>
#include <QPlainTextEdit>    // pip/npm 安装日志区（纯文本；QTextEdit 在本工程被 #define 成 PlaceholderTextEdit）
#include <QTextCharFormat>   // 日志按行上色（普通 / stderr / 结果）
#include <QColor>
#include <QFont>
#include <memory>            // std::shared_ptr：pip/npm 每次安装各持一份独立状态
#include <functional>
#include <algorithm>         // std::stable_sort：配置区里把 input 项排到前面，聚成一整块
#include <limits>            // 商店总价溢出保护（numeric_limits<qint64>::max）
#include <QVector>

#ifdef Q_OS_WIN
// 插件 DLL 依赖同目录其它 DLL 时，加载期间要把该目录临时塞进 DLL 搜索路径
// —— SetDllDirectoryW / GetDllDirectoryW（用法见 LoadPlugin_DLL）。
// 用 Qt 自带的 qt_windows.h：它内部已经替我们定义好 NOMINMAX / WIN32_LEAN_AND_MEAN，
// 不会把 min/max 那批宏带进来污染本文件后面的 Qt / STL 代码。
#  include <qt_windows.h>
#endif

// ======================== 插件类型徽章（列表卡片 / 详情用） ========================
// 文件对话框的起始目录（定义在文件下方）：py引用库 的「导入txt」也要用它，而那段代码
// 在 setupUi() 里、比定义位置靠前，所以这里先声明一下。
static QString pluginDialogStartDir();

static QString pluginTypeName(int type)
{
    switch (type) {
    case 0: return "Python";
    case 1: return "DLL";
    case 2: return "DLL32";
    case 3: return "JS";
    default: return "未知";
    }
}

static QString pluginTypeColor(int type)
{
    switch (type) {
    case 0: return "#2EE89F";
    case 1: return "#FFA500";
    case 2: return "#FF6347";
    case 3: return "#97CEEB";
    default: return "#CCCCCC";
    }
}

// 配置区布局的工厂：ConfigFlowLayout 定义在下面的「配置区」段里（本文件后半部分），
// setupUi 在同一文件更靠前的位置，没法直接 new，所以走一个前置声明的工厂函数。
static QLayout *createConfigLayout(QWidget *parent);

// 「指令」列表弹窗里每条规则的显示文字：改过名 / 停用 / 限定权限都直接标出来
template <typename RuleT>
static QString ruleDisplayText(const RuleT &rule)
{
    QString s = rule.key;
    if (!rule.newKey.isEmpty()) s += " → " + rule.newKey;
    if (rule.role <= kRuleRoleOff)       s += "（停用）";
    else if (rule.role > kRuleRoleAll)   s += QString("（限%1）").arg(ruleRoleName(rule.role));
    return s;
}

static void safeCall(const py::object &func) {
    if (func.is_none()) return;
    if (!py::isinstance<py::function>(func) && !PyCallable_Check(func.ptr())) return;
    try {
        py::gil_scoped_acquire gil;
        func();
    } catch (...) {}
}
PluginItemWidget::PluginItemWidget(const PluginInfo &info, QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(48);
    // ⚠ 不能写 setStyleSheet("background: transparent;")：不带选择器的声明块在 Qt 里等价于
    // `* { ... }`，会**向下传播给所有后代**，把后代从全局样式表继承到的样式（比如 QPushButton
    // 的 #FFF0DE 底色）覆盖掉。要限定到控件自身，必须带 #objectName。
    setObjectName("qcPluginItem");
    setStyleSheet("#qcPluginItem { background: transparent; }");

    QHBoxLayout *hLayout = new QHBoxLayout(this);
    hLayout->setContentsMargins(8, 4, 8, 4);

    // 图标
    iconLabel = new QLabel;
    iconLabel->setFixedSize(36, 36);
    iconLabel->setScaledContents(true);
    iconLabel->setObjectName("icon_AAA");
    // ⚠ 全局样式表里有 `QFrame { background: #FFFFFF; }`，而 **QLabel 是 QFrame 的子类**
    // → 不显式声明的话每个 QLabel 都会自带一块白底（在浅色卡片上很明显）。
    // 所以这里的 QLabel 一律自带 `background: transparent;`（自身规则优先于全局的 QFrame）。
    iconLabel->setStyleSheet("border: 1px solid #89b4fa; border-radius: 2px; background: transparent;");
    QPixmap pix(info.icon);
    if (!pix.isNull()) iconLabel->setPixmap(pix);
    else iconLabel->clear();
    QVBoxLayout *vLayout = new QVBoxLayout;
    vLayout->setSpacing(2);
    QHBoxLayout *line1 = new QHBoxLayout;
    statusIndicator = new QLabel;
    statusIndicator->setFixedSize(12, 12);
    if (info.enabled)
        statusIndicator->setStyleSheet("background: #a6e3a1; border-radius: 6px;");
    else
        statusIndicator->setStyleSheet("background: #f38ba8; border-radius: 6px;");
    line1->addWidget(statusIndicator);
    nameLabel = new QLabel(info.name);
    nameLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #111111; background: transparent;");
    line1->addWidget(nameLabel);
    // 类型徽章紧跟插件名
    typeLabel = new QLabel(pluginTypeName(info.type));
    typeLabel->setStyleSheet(QString("background: %1; color: #111111; border-radius: 3px;"
                                     " padding: 0px 6px; font-size: 11px; font-weight: bold;")
                                 .arg(pluginTypeColor(info.type)));
    line1->addStretch();
    line1->addWidget(typeLabel, 0, Qt::AlignVCenter);

    vLayout->addLayout(line1);

    // 第二行：作者 | 版本
    QHBoxLayout *line2 = new QHBoxLayout;
    line2->setSpacing(6);
    authorLabel = new QLabel(info.author.isEmpty() ? "未知作者" : info.author);
    authorLabel->setStyleSheet("font-size: 12px; color: #111111; background: transparent;");
    versionLabel = new QLabel("v" + info.version);
    versionLabel->setStyleSheet("font-size: 12px; color: #89b4fa; font-weight: bold; background: transparent;");
    line2->addWidget(authorLabel);
    line2->addStretch();
    line2->addWidget(versionLabel);
    vLayout->addLayout(line2);

    hLayout->addWidget(iconLabel);
    hLayout->addLayout(vLayout, 1);
}



void PluginItemWidget::updateInfo(const PluginInfo &info) {
    // 更新图标
    QPixmap pix(info.icon);
    if (!pix.isNull())
        iconLabel->setPixmap(pix);
    else
        iconLabel->clear();
    // 更新名称
    nameLabel->setText(info.name);
    // 更新类型徽章
    typeLabel->setText(pluginTypeName(info.type));
    typeLabel->setStyleSheet(QString("background: %1; color: #111111; border-radius: 3px;"
                                     " padding: 0px 6px; font-size: 11px; font-weight: bold;")
                                 .arg(pluginTypeColor(info.type)));
    // 更新作者
    authorLabel->setText(info.author.isEmpty() ? "未知作者" : info.author);
    // 更新版本
    versionLabel->setText("v" + info.version);
    // 更新状态指示灯
    if (info.enabled)
        statusIndicator->setStyleSheet("background: #a6e3a1; border-radius: 6px;");
    else
        statusIndicator->setStyleSheet("background: #f38ba8; border-radius: 6px;");
}

// 指令配置的结构迁移（2026-10-20 起「按机器人区分」）：
//   老：{ "<插件key>": [ 行... ] }
//   新：{ "<插件key>": { "rows": [ 行... ], "bots": { "<appid>": { 指令 → {t,r} } } } }
// 载入时统一包一层（里面一行都不动），存盘就按新结构写。
static QJsonObject migrateRuleCfg(const QJsonObject &old)
{
    QJsonObject out;
    for (auto it = old.constBegin(); it != old.constEnd(); ++it) {
        QJsonObject w;
        if (it.value().isArray()) w["rows"] = it.value().toArray();   // 老格式
        else                      w = it.value().toObject();          // 新格式
        out.insert(it.key(), w);
    }
    return out;
}

PluginPage::PluginPage(QWidget *parent) : QWidget(parent)
{
    // 指令配置（启用类型 / 重命名 / 每个机器人的权限覆盖）要在插件开始载入之前就拿到，
    // 载入时才能套用「默认」那一档；按机器人的覆盖在匹配时现查 m_botRuleRole。
    m_ruleCfg = migrateRuleCfg(g_config.value("plugin_rules").toObject());
    rebuildBotRuleRoles();

    setupUi();
    initPython();

    QTimer::singleShot(0, this, &PluginPage::loadPlugins);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &PluginPage::stopAsyncioThread);

}
// 初始化异步引擎（严格按你的要求：后台线程必须持锁跑 run_forever）
void PluginPage::initPython() {
    // 主线程先拿锁绑定线程状态
    py::gil_scoped_acquire gil;

    m_asyncio_mod = py::module_::import("asyncio");
    m_run_coro_func = m_asyncio_mod.attr("run_coroutine_threadsafe");

    // 使用 promise/future 安全传递 loop 给主线程
    std::promise<py::object> loop_promise;
    std::future<py::object> loop_future = loop_promise.get_future();

    // 启动专属后台线程
    m_asyncio_thread = std::thread([this, loop_promise = std::move(loop_promise)]() mutable {
        // 【必须持有 GIL，否则在 3.14t 下直接崩】
        py::gil_scoped_acquire acquire;

        py::object local_loop = m_asyncio_mod.attr("new_event_loop")();
        m_asyncio_mod.attr("set_event_loop")(local_loop);

        // 把 loop 传递给外面
        loop_promise.set_value(local_loop);

        // 死循环（一直持有 GIL，驱动事件调度）
        local_loop.attr("run_forever")();
    });

    // 主线程阻塞等待，直到后台线程把 loop 建好
    m_loop = loop_future.get();
}


// 核心调用函数（解决 “coroutine never awaited” 警告！）
void PluginPage::safeCall(const py::object &func) {
    if (func.is_none()) return;
    if (!py::isinstance<py::function>(func) && !PyCallable_Check(func.ptr())) return;

    try {
        // 3.14t 下，每次调用 Python API 前必须绑定当前线程状态
        py::gil_scoped_acquire gil;

        // 判断是不是 async 函数
        if (m_asyncio_mod.attr("iscoroutinefunction")(func).cast<bool>()) {
            // 是异步函数：执行拿到协程对象（瞬间返回，不执行内部逻辑）
            py::object coro = func();

            // 扔给后台执行（防止警告，让它在后台真正被 await）
            if (!coro.is_none()) {
                m_run_coro_func(coro, m_loop);
            }
        } else {
            // 普通同步函数：直接执行
            func();
        }
    } catch (const py::error_already_set& e) {
        qWarning() << "safeCall 执行异常:" << e.what();
        PyErr_Clear();
    } catch (...) {
        qWarning() << "safeCall 未知异常";
    }
}


void PluginPage::stopAsyncioThread() {

    if (m_loop.is_none()) return;


    py::gil_scoped_acquire gil;
    try {

        py::object stop_func = m_loop.attr("stop");
        m_loop.attr("call_soon_threadsafe")(stop_func);
    } catch (const py::error_already_set& e) {
        qWarning() << "停止循环时发生 Python 异常:" << e.what();
    }

    if (m_asyncio_thread.joinable()) {
        m_asyncio_thread.join();
    }


   // m_loop = py::object();
    //m_asyncio_mod = py::object();
    //m_run_coro_func = py::object();

    qDebug() << "异步引擎线程已安全退出";
}



// 源文件中实现
void PluginPage::onItemDoubleClicked(QListWidgetItem *item)
{
    if (item) {

        QString name = item->data(Qt::UserRole).toString();
        int index=findPluginIndex(name);
        if (index!=-1) {
            currentSelected_index = index;
            Enabled_Plugin(currentSelected_index);
            updatePluginItemInUI(currentSelected_index);
            savePlugins();
        }

    }
}
void PluginPage::setupUi()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);


    QVBoxLayout *leftLayout = new QVBoxLayout;
    QLabel *listTitle = new QLabel("插件列表(双击启用)");
    listTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #222222; margin: 4px;");

    pluginListWidget = new QListWidget;
    pluginListWidget->setFixedWidth(260);
    pluginListWidget->setSpacing(2);
    pluginListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pluginListWidget->setObjectName("pluginList");
    pluginListWidget->setDragEnabled(true);                 // 允许拖动列表项
    pluginListWidget->setAcceptDrops(true);                // 允许接收拖放
    pluginListWidget->setDragDropMode(QAbstractItemView::InternalMove); // 内部移动模式（不复制，只移动）
    pluginListWidget->setDefaultDropAction(Qt::MoveAction); // 确保移动动作
    // 假设在类的构造函数或初始化函数中
    connect(pluginListWidget, &QListWidget::itemDoubleClicked, this, &PluginPage::onItemDoubleClicked);
    addPluginBtn  = new QPushButton("添加-DLL");
    addScriptBtn  = new QPushButton("添加-脚本");     // Python / JS 合并：选目录后按 main.py / main.js 自动分流
    addScriptBtn->setToolTip("选择插件文件夹（内含 main.py 或 main.js）");

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addWidget(addPluginBtn);
    btnRow->addWidget(addScriptBtn);
    leftLayout->addWidget(listTitle);
    leftLayout->addWidget(pluginListWidget);
    leftLayout->addLayout(btnRow);
    leftLayout->setContentsMargins(5,5,5,5);

    // ========== 中间：账号列表（原 rightCheckList） ==========
    QWidget *middleWidget = new QWidget;
    QVBoxLayout *middleLayout = new QVBoxLayout(middleWidget);
    middleLayout->setContentsMargins(5, 5, 5, 5);
    QLabel *middleTitle = new QLabel("账号列表");
    middleTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #222222; margin: 4px;");
    middleLayout->addWidget(middleTitle);

    rightCheckList = new QListWidget;
    rightCheckList->setFixedWidth(240);
    rightCheckList->setSelectionMode(QAbstractItemView::NoSelection);
    rightCheckList->setStyleSheet("border: 1px solid #cccccc; border-radius: 4px;");
    rightCheckList->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    middleLayout->addWidget(rightCheckList);

    QHBoxLayout *iconNameLayout3 = new QHBoxLayout;
    plugin_sc = new QPushButton("插件市场");
    pypip = new QPushButton("py引用库");
    QPushButton *tjzl = new QPushButton("指令");
    pypip->setFixedWidth(90);
    iconNameLayout3->addWidget(pypip);
    iconNameLayout3->addWidget(plugin_sc);
    iconNameLayout3->addWidget(tjzl);
    middleLayout->addLayout(iconNameLayout3);
    //middleLayout->addStretch(); // 让列表顶部分布，下方留白

    // ========== 右侧：插件详情 ==========
    // 顶部那一块（「插件详情」标题 / 图标 / 插件名 / 编辑·AI 按钮 / 类型·版本·作者）已按要求移除，
    // 面板只保留「路径 + 说明 + 插件配置 + 操作按钮」；「编辑当前插件 / AI生成插件」下移到操作按钮上一行。
    QGroupBox *detailGroup = new QGroupBox;
    detailGroup->setStyleSheet(
        "QGroupBox { padding-top: 0px; margin-top: 0px; border: 1px solid #cccccc; border-radius: 4px; }"
        );
    QVBoxLayout *rightMainLayout = new QVBoxLayout(detailGroup);
    rightMainLayout->setSpacing(8);
    rightMainLayout->setContentsMargins(10, 10, 10, 10);

    // ---- 路径 ----
    QHBoxLayout *pathLayout = new QHBoxLayout;
    pathLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *pathTagLabel = new QLabel("路径：");
    pathTagLabel->setStyleSheet("font-size: 12px; color: #222222;");
    detailpathLabel = new QLabel;
    detailpathLabel->setStyleSheet("font-size: 12px; color: #666666;");
    detailpathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailpathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    pathLayout->addWidget(pathTagLabel);
    pathLayout->addWidget(detailpathLabel, 1);
    rightMainLayout->addLayout(pathLayout);

    // ---- 描述框 ----
    detailDescLabel = new QTextBrowser;
    detailDescLabel->setOpenExternalLinks(true);
    detailDescLabel->setMinimumHeight(80);
    detailDescLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    detailDescLabel->setStyleSheet(
        "background: #ffffff; "
        "border: 1px solid #cccccc; "
        "border-radius: 4px; "
        "padding: 8px;"
        );
    rightMainLayout->addWidget(detailDescLabel, 1);

    // ---- 插件配置（get_config_list / set_config_value，四种类型插件都可实现）----
    configContainer = new QWidget;
    // 同上：必须带 #objectName 限定，否则 `* { background: transparent; }` 会让配置区里
    // 所有 QPushButton 丢掉全局底色（看起来就是「白按钮」）。
    configContainer->setObjectName("qcConfigContainer");
    configContainer->setStyleSheet("#qcConfigContainer { background: transparent; }");
    configContainer->setContentsMargins(0, 0, 0, 0);

    // 自适应折行布局：input 独占一行，checkbox / button 能并排就并排
    configLayout = createConfigLayout(configContainer);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setSpacing(3);
    configScroll = new QScrollArea;
    configScroll->setWidget(configContainer);
    configScroll->setWidgetResizable(true);
    configScroll->setFrameShape(QFrame::NoFrame);
    configScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    configScroll->setStyleSheet("QScrollArea { background: transparent; border: none; }"
                                "QScrollArea > QWidget > QWidget { background: transparent; }");
    configScroll->viewport()->installEventFilter(this);   // 视口宽度变了 → 重算折行高度
    configScroll->setVisible(false);
    rightMainLayout->addWidget(configScroll);

    // ---- 编辑 / AI 生成（操作按钮的上一行）----
    QHBoxLayout *editBtnLayout = new QHBoxLayout;
    editBtnLayout->setSpacing(6);
    ai_b_j = new QPushButton("编辑当前插件");

    ai_c_j = new QPushButton("AI生成插件");

    bj_zl = new QPushButton("编辑指令");   // 勾选启用 / 重命名插件注册的指令

    editBtnLayout->addWidget(ai_b_j);
    editBtnLayout->addWidget(ai_c_j);
    editBtnLayout->addWidget(bj_zl);

    rightMainLayout->addLayout(editBtnLayout);

    // ---- 操作按钮 ----
    QHBoxLayout *btnLayout = new QHBoxLayout;

    loadBtn = new QPushButton("启用");
    reloadBtn = new QPushButton("重载");
    openDirBtn = new QPushButton("目录");
    uninstallBtn = new QPushButton("卸载");
    setBtn = new QPushButton("设置");
    // 详情面板变窄了，5 个按钮再写死 60px 会被挤掉（原来「目录」就是被截掉的），
    // 改成给最小宽度后平分这一行
    const int btnMinWidth = 46;
    loadBtn->setMinimumWidth(btnMinWidth);
    reloadBtn->setMinimumWidth(btnMinWidth);
    openDirBtn->setMinimumWidth(btnMinWidth);
    uninstallBtn->setMinimumWidth(btnMinWidth);
    setBtn->setMinimumWidth(btnMinWidth);
    btnLayout->setSpacing(4);


    btnLayout->addWidget(loadBtn);
    btnLayout->addWidget(reloadBtn);
    btnLayout->addWidget(uninstallBtn);
    btnLayout->addWidget(setBtn);
    btnLayout->addWidget(openDirBtn);
    rightMainLayout->addLayout(btnLayout);

    // ========== 使用 QSplitter 实现可调节的三列布局 ==========
    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    // 左侧容器
    QWidget *leftWidget = new QWidget;
    leftWidget->setLayout(leftLayout);
    // 中间容器（rightCheckList 独立）
    // 右侧容器（detailGroup）
    splitter->addWidget(leftWidget);
    splitter->addWidget(middleWidget);
    splitter->addWidget(detailGroup);
    // 设置初始比例（左:中:右 = 1:1:3）
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 3);

    mainLayout->addWidget(splitter);

    // ========== 信号连接（完全不变） ==========
    connect(uninstallBtn, &QPushButton::clicked, [this](){
        uninstall_Plugin(currentSelected_index);
        savePlugins();

    });
    connect(plugin_sc, &QPushButton::clicked, [this](){
        PluginMarketWindow *win = new PluginMarketWindow();
        win->setWindowFlags(Qt::Dialog); // 确保是普通对话框

        win->show();  // 或 win->exec() 模态
    });
    connect(reloadBtn, &QPushButton::clicked, [this](){

        Reload_Plugin(currentSelected_index);

    });

    connect(openDirBtn, &QPushButton::clicked, [this]() {
        if (currentSelected_index < 0 || currentSelected_index >= m_pluginList.size()) return;
        QString fullPath = QDir(QApplication::applicationDirPath()).absoluteFilePath(m_pluginList[currentSelected_index].path);
        QFileInfo info(fullPath);
        if (m_pluginList[currentSelected_index].type == 0) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fullPath));
        } else {
            QString dirPath = info.absolutePath();
            if (!dirPath.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
            }
        }
    });

    connect(loadBtn, &QPushButton::clicked, this, [this]() {
        Enabled_Plugin(currentSelected_index);
        updatePluginItemInUI(currentSelected_index);
        savePlugins();
    });
    connect(pluginListWidget->model(), &QAbstractItemModel::rowsMoved,
            this, &PluginPage::onPluginRowsMoved);
    connect(rightCheckList, &QListWidget::itemChanged, this, &PluginPage::onAccountCheckStateChanged);
    connect(pluginListWidget, &QListWidget::currentRowChanged, this, &PluginPage::onPluginSelected);
    connect(addPluginBtn, &QPushButton::clicked, [this](){ LoadPlugin_DLL(); });
    connect(addScriptBtn, &QPushButton::clicked, [this](){ LoadPlugin_Script(); });


    connect(setBtn, &QPushButton::clicked, [this](){

        if(currentSelected_index<0 || currentSelected_index>=m_pluginList.size()) return;
        if(m_pluginList[currentSelected_index].type==0) {
            safeCall(m_pluginList[currentSelected_index].python.onSet);
        } else if(m_pluginList[currentSelected_index].type==1) {
            if(m_pluginList[currentSelected_index].DLL.onSet) m_pluginList[currentSelected_index].DLL.onSet();
        } else if(m_pluginList[currentSelected_index].type==2) {
            QString res = sendData32(9,m_pluginList[currentSelected_index]);
            QString text ="打开" + m_pluginList[currentSelected_index].name + "设置 结果：" + res + "\n如果返回失败代表 你可能有顶级窗口独占线程 请关闭那个窗口才能打开新窗口";
            AppendEventLog(text);
            if(res=="true") return;
            QMessageBox::warning(this,"",text);
        }
    });


    // py引用库：不再要求先准备好 requirements.txt —— **直接填文本**装库。
    // 输入框里一行一个（也可空格分隔），支持 `#` 行注释；「导入txt」只把文件内容读进输入框，
    // 最终统一由「开始安装」走 anzpip()（命令里 `-r "<路径>"` 改成直接跟包名，其余一行没动）。
    connect(pypip, &QPushButton::clicked, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("安装 Python 引用库");
        dlg.resize(480, 360);

        QVBoxLayout *dlgLayout = new QVBoxLayout(&dlg);
        QLabel *dlgTip = new QLabel("请输入要安装的库：一行一个（也可用空格分隔），例如\n"
                                    "requests\n"
                                    "numpy==1.26.4\n"
                                    "（以 # 开头的行会被忽略）", &dlg);
        dlgTip->setStyleSheet("font-size: 12px; color: #444444;");
        dlgLayout->addWidget(dlgTip);

        QPlainTextEdit *input = new QPlainTextEdit(&dlg);
        {   // 等宽字体：包名 / 版本号好认一点
            QFont mono("Consolas");
            mono.setStyleHint(QFont::Monospace);
            input->document()->setDefaultFont(mono);
        }
        input->setPlaceholderText("requests\nnumpy==1.26.4");
        dlgLayout->addWidget(input, 1);

        QHBoxLayout *dlgBtnRow = new QHBoxLayout;
        QPushButton *importTxtBtn = new QPushButton("导入txt");
        QPushButton *installBtn   = new QPushButton("开始安装");
        QPushButton *cancelBtn    = new QPushButton("取消");
        dlgBtnRow->addWidget(importTxtBtn);
        dlgBtnRow->addStretch(1);
        dlgBtnRow->addWidget(installBtn);
        dlgBtnRow->addWidget(cancelBtn);
        dlgLayout->addLayout(dlgBtnRow);

        // 导入txt：只把文件内容读进输入框（**追加**，不覆盖已输入的内容，方便多次导入 / 手工补充）
        connect(importTxtBtn, &QPushButton::clicked, &dlg, [&dlg, input]() {
            QString reqPath = QFileDialog::getOpenFileName(
                &dlg,
                "选择 requirements.txt 文件",
                pluginDialogStartDir(),
                "文本文件 (*.txt);;所有文件 (*)"
                );
            if (reqPath.isEmpty()) return;

            QFile f(reqPath);
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QMessageBox::warning(&dlg, "错误", "无法读取文件：" + reqPath);
                return;
            }
            const QString txt = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
            if (txt.isEmpty()) {
                QMessageBox::information(&dlg, "提示", "文件内容为空");
                return;
            }
            const QString old = input->toPlainText().trimmed();
            input->setPlainText(old.isEmpty() ? txt : old + "\n" + txt);
            input->moveCursor(QTextCursor::End);   // 光标跟到末尾，方便接着改
        });

        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        connect(installBtn, &QPushButton::clicked, &dlg, [this, &dlg, input]() {
            const QString pkgs = input->toPlainText();
            if (pkgs.trimmed().isEmpty()) {
                QMessageBox::information(&dlg, "提示", "请先输入要安装的库名，或点「导入txt」选择文件");
                return;
            }
            dlg.accept();                    // 先收掉输入窗，pip 在独立终端窗口里跑
            QString err = anzpip(pkgs);
            if (!err.isEmpty()) {
                QMessageBox::critical(this, "错误", err);
            }
        });

        dlg.exec();
    });

    connect(tjzl, &QPushButton::clicked, this, [this]() {
        QString text;
        text.reserve(4096);
        int zls=0;//统计指令数量
        for (const auto &plugin : std::as_const(m_pluginList)) {
            text.append(QStringLiteral("插件: %1\n").arg(plugin.name));
            if (plugin.type == 0)  // 仅处理已启用的插件
            {

                if (plugin.python.rules.isEmpty()) {
                    text.append("  未注册任何指令\n");
                } else {
                    // 1. 按类型分组
                    QMap<MatchType, QStringList> groups;
                    for (const auto &rule : plugin.python.rules) {
                        groups[rule.type].append(ruleDisplayText(rule));
                        zls++;
                    }
                    // 2. 记录类型首次出现的顺序（保持注册顺序）
                    QList<MatchType> typeOrder;
                    for (const auto &rule : plugin.python.rules) {
                        if (!typeOrder.contains(rule.type))
                            typeOrder.append(rule.type);
                    }
                    // 3. 按顺序输出每组
                    for (auto type : typeOrder) {
                        const auto &keys = groups[type];
                        if (keys.isEmpty()) continue;

                        QString typeStr;
                        switch (type) {
                        case MatchType::Equals:     typeStr = "等于"; break;
                        case MatchType::StartsWith: typeStr = "开头"; break;
                        case MatchType::EndsWith:   typeStr = "结尾"; break;
                        case MatchType::Contains:   typeStr = "包含"; break;
                        case MatchType::Regex:      typeStr = "正则"; break;
                        case MatchType::event:      typeStr = "事件"; break;
                        }
                        text.append(QStringLiteral("  %1: %2\n").arg(typeStr, keys.join("，")));
                    }
                }
                text.append("\n");  // 插件间空行
            }else if(plugin.type == 1 || plugin.type==2)
            {
                if (plugin.DLL.rules.isEmpty()) {
                    text.append("  未注册任何指令\n");
                } else {
                    QList<MatchType> typeOrder;
                    QMap<MatchType, QStringList> groups;
                    for (const auto &rule : plugin.DLL.rules) {
                        groups[rule.type].append(ruleDisplayText(rule));
                        zls++;
                        if (!typeOrder.contains(rule.type))
                            typeOrder.append(rule.type);
                    }


                    for (auto type : typeOrder) {
                        const auto &keys = groups[type];
                        if (keys.isEmpty()) continue;

                        QString typeStr;
                        switch (type) {
                        case MatchType::Equals:     typeStr = "等于"; break;
                        case MatchType::StartsWith: typeStr = "开头"; break;
                        case MatchType::EndsWith:   typeStr = "结尾"; break;
                        case MatchType::Contains:   typeStr = "包含"; break;
                        case MatchType::Regex:      typeStr = "正则"; break;
                        case MatchType::event:      typeStr = "事件"; break;
                        }
                        text.append(QStringLiteral("  %1: %2\n").arg(typeStr, keys.join("，")));
                    }
                }
                text.append("\n");  // 插件间空行
            }else if(plugin.type == 3)
            {
                if (plugin.js.rules.isEmpty()) {
                    text.append("  未注册任何指令\n");
                } else {
                    // 1. 按类型分组
                    QMap<MatchType, QStringList> groups;
                    for (const auto &rule : plugin.js.rules) {
                        groups[rule.type].append(ruleDisplayText(rule));
                        zls++;
                    }
                    // 2. 记录类型首次出现的顺序（保持注册顺序）
                    QList<MatchType> typeOrder;
                    for (const auto &rule : plugin.js.rules) {
                        if (!typeOrder.contains(rule.type))
                            typeOrder.append(rule.type);
                    }
                    // 3. 按顺序输出每组
                    for (auto type : typeOrder) {
                        const auto &keys = groups[type];
                        if (keys.isEmpty()) continue;

                        QString typeStr;
                        switch (type) {
                        case MatchType::Equals:     typeStr = "等于"; break;
                        case MatchType::StartsWith: typeStr = "开头"; break;
                        case MatchType::EndsWith:   typeStr = "结尾"; break;
                        case MatchType::Contains:   typeStr = "包含"; break;
                        case MatchType::Regex:      typeStr = "正则"; break;
                        case MatchType::event:      typeStr = "事件"; break;
                        }
                        text.append(QStringLiteral("  %1: %2\n").arg(typeStr, keys.join("，")));
                    }
                }
                text.append("\n");  // 插件间空行
            }
        }
        // 内容多时 QMessageBox 会被屏幕挤住、显示不全也不能滚动 —— 改用可滚动的文本窗口
        ScrollTextDialog::show(this, "插件注册指令",QString("%1\n\n注册的指令数量：%2").arg(text).arg(zls));
    });

    connect(ai_c_j, &QPushButton::clicked,this, [this]() {
        auto *w = new AppWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });

    // 编辑指令：打开独立页面，左侧插件列表 + 右侧 [启用][指令名][新指令名][匹配方式]
    connect(bj_zl, &QPushButton::clicked, this, [this]() {
        int i = currentSelected_index;
        if(i<0) i=0;
        if(i>=m_pluginList.size()) i=0;
        auto *dlg = new RuleEditDialog(this, i, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    connect(ai_b_j, &QPushButton::clicked,this, [this]() {

        if(currentSelected_index<0 || currentSelected_index>=m_pluginList.size())
        {
            QMessageBox::warning(this,"","请选择一个插件");
            return;
        }
        int type =m_pluginList[currentSelected_index].type;
        if(type!=0 && type!=3)
        {
            QMessageBox::warning(this,"","仅限Python JS类型插件可以直接编辑");
            return;
        }
        auto *w = new AppWindow(m_pluginList[currentSelected_index].path);
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });
}
// 安装 Python 引用库。
// 入参是**用户直接输入的文本**（一行一个 / 空格分隔、支持 `#` 注释），不再是 requirements.txt 的路径 ——
// py引用库 的「导入txt」只是把文件内容读进输入框，最后统一走这里，所以这里按文本解析一遍。
QString PluginPage::anzpip(const QString &pkgsText)
{
    // ---- 把自由文本拆成 pip 认的包规格列表 ----
    // 逐行：先砍掉 `#` 起的行内注释（`requests  # 网络库`），再把回车 / 制表符拉平成空格，最后按空格切词。
    // 顺带去重 —— 重复的包名 pip 会重复解析，纯属白等。
    QStringList pkgs;
    const QStringList lines = pkgsText.split('\n');
    for (const QString &rawLine : lines) {
        QString line = rawLine;
        const int hash = line.indexOf('#');
        if (hash >= 0) line.truncate(hash);
        line.replace('\r', ' ').replace('\t', ' ');
        const QStringList toks = line.split(' ', Qt::SkipEmptyParts);
        for (const QString &t : toks) {
            if (!pkgs.contains(t)) pkgs.append(t);
        }
    }
    if (pkgs.isEmpty()) {
        return "没有解析到任何库名，请输入例如：requests";
    }
    const QString pkgArgs = pkgs.join(' ');

    QString pythonExe = QCoreApplication::applicationDirPath() + "/python3.14t.exe";


    QProcess checkPip;
    checkPip.start(pythonExe, QStringList() << "-c" << "import pip");
    if (!checkPip.waitForFinished(3000) || checkPip.exitCode() != 0) {
        QMessageBox::information(this, "提示", "pip 未就绪，正在尝试修复...");
        QProcess fixPip;
        fixPip.start(pythonExe, QStringList() << "-m" << "ensurepip" << "--upgrade");
        if (!fixPip.waitForFinished(10000) || fixPip.exitCode() != 0) {
            return "修复 pip 失败，请手动检查环境。";
        }
        QMessageBox::information(this, "提示", "pip 修复成功。");
    }
    // 构造要执行的 pip 安装命令（注意转义内部引号）
    QString pipCmd = QString(
                         "\"%1\" -m pip install %2 Pillow -i https://pypi.tuna.tsinghua.edu.cn/simple --trusted-host pypi.tuna.tsinghua.edu.cn"
                         ).arg(pythonExe, pkgArgs);

    // 构建完整的 cmd /k 命令，/k 会保持窗口打开
    QString fullCmd = QString(
                          "echo 欢迎使用插件依赖安装工具 & echo 正在安装: %2 "
                          "& echo 提示： "
                          "& echo   - \"Requirement already satisfied\" 表示库已存在，无需重复下载 "
                          "& echo   - \"Successfully installed\" 表示新库安装成功 "
                          "& echo. & %1 & echo. & echo 安装完成，请检查上述输出，然后关闭此窗口."
                          ).arg(pipCmd, pkgArgs);

    // 直接启动 cmd.exe，带 /k 参数，让它在新的终端窗口中执行
    QStringList args;
    args << "/k" << fullCmd;
    return QProcess::startDetached("cmd", args) ? "" : "无法启动终端窗口，请检查系统环境！";
}
void PluginPage::onPluginRowsMoved(const QModelIndex &parent, int start, int end,
                                   const QModelIndex &destination, int row)
{
    // 只处理同一列表内的移动
    if (parent != destination) return;

    int from = start;
    int to = row > start ? row - (end - start + 1) : row;
    if (from == to) return;

    // 利用 std::rotate 原地重排整个区间，避免逐元素拷贝
    // 原理：将 [from, to] 区间旋转到目标位置
    auto &list = m_pluginList;
    if (to < from) {
        // 上移：将 [to, from-1] 后移，把 [from, end] 插入到 to 位置
        std::rotate(list.begin() + to, list.begin() + from, list.begin() + end + 1);
    } else {
        // 下移：将 [from, end] 移动到 to 之后
        std::rotate(list.begin() + from, list.begin() + end + 1, list.begin() + to + 1);
    }

    // 刷新 UI 列表（保持与 m_pluginList 顺序一致）
    for (int i = 0; i < list.size(); ++i) {
        updatePluginItemInUI(i);   // 假设该方法根据索引刷新列表项
    }

    // 保存顺序（该操作可能涉及序列化，若有 Python 对象需小心）
    savePlugins();

    // 保持选中新位置
    int newCurrent = (to >= 0 && to < list.size()) ? to : from;
    pluginListWidget->setCurrentRow(newCurrent);
}

void PluginPage::updateAccountCheckList(int pluginIndex)
{
    if (!rightCheckList) return;
    if (pluginIndex < 0 || pluginIndex >= m_pluginList.size()) {
        rightCheckList->clear();
        return;
    }
    const PluginInfo &info = m_pluginList[pluginIndex];
    rightCheckList->blockSignals(true);
    rightCheckList->clear();
    for (const auto &acc : std::as_const(m_accounts)) {
        QListWidgetItem *item = new QListWidgetItem(acc->nickname);
        item->setData(Qt::UserRole, acc->appid_int);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        bool checked = info.appid.contains(acc->appid_int);
        item->setCheckState(checked ? Qt::Unchecked : Qt::Checked );
        rightCheckList->addItem(item);
    }
    rightCheckList->blockSignals(false);
}

// 当右侧列表的勾选状态改变时，更新当前插件的 appid 列表
void PluginPage::onAccountCheckStateChanged(QListWidgetItem *item)
{
    if (!item) return;
    int row = rightCheckList->row(item);
    if (row < 0 || row >= m_accounts.size()) return;
    int appid = item->data(Qt::UserRole).toInt();
    bool isChecked = (item->checkState() == Qt::Checked); // true=打勾(启用)
    int pluginIdx = currentSelected_index; // 当前选中的插件索引
    if (pluginIdx < 0 || pluginIdx >= m_pluginList.size()) return;
    PluginInfo &info = m_pluginList[pluginIdx];
    if (isChecked) {
        info.appid.removeAll(appid);
    } else {
        if (!info.appid.contains(appid))
            info.appid.append(appid);
    }
    // 只是通知 32 位侧刷新账号列表，返回值没人看 → 只发不等（原来是每次都白等最多 5 秒）
    sendData32NoWait(11, info, joinIntListFast(info.appid, ","));
    savePlugins();
}
//================================================================================================================================================
void PluginPage::initPluginList(const QList<PluginInfo> &plugins) {
    py::gil_scoped_acquire gil;
    m_pluginList = plugins;
    pluginListWidget->clear();
    for (int i = 0; i < m_pluginList.size(); ++i) {
        addPluginItemToUI(i, m_pluginList[i]);
    }
}
void PluginPage::appendPlugin(const PluginInfo &info) {


    try {
        m_pluginList.append(info);
        int index = m_pluginList.size() - 1;  // 新插入元素的索引
        QMetaObject::invokeMethod(qApp, [this, index]() {

            py::gil_scoped_acquire gilMain;
            try {

                const PluginInfo& infoRef = m_pluginList.at(index);
                addPluginItemToUI(index, infoRef);
            } catch (const py::error_already_set& e) {
                qWarning() << "Python error in addPluginItemToUI:" << e.what();
                PyErr_Clear();
            } catch (const std::exception& e) {
                qWarning() << "C++ exception in addPluginItemToUI:" << e.what();
            } catch (...) {
                qWarning() << "Unknown exception in addPluginItemToUI";
            }
        }, Qt::QueuedConnection);

    } catch (const py::error_already_set& e) {
        qWarning() << "Python error in appendPlugin (data append):" << e.what();
        PyErr_Clear();
    } catch (const std::exception& e) {
        qWarning() << "C++ exception in appendPlugin (data append):" << e.what();
    } catch (...) {
        qWarning() << "Unknown exception in appendPlugin (data append)";
    }
}
// 在指定位置插入
void PluginPage::insertPlugin(int index, const PluginInfo &info) {
    py::gil_scoped_acquire gil;
    m_pluginList.insert(index, info);

    insertPluginItemToUI(index, info);
}

void PluginPage::removePlugin(int index) {
    if (index < 0 || index >= m_pluginList.size()) return;
    py::gil_scoped_acquire gil;
    m_pluginList.removeAt(index);
    delete pluginListWidget->takeItem(index);  // 同时删除 UI 条目
}
void PluginPage::updatePlugin(int index, const PluginInfo &newInfo) {
    if (index < 0 || index >= m_pluginList.size()) return;
    py::gil_scoped_acquire gil;
    m_pluginList[index] = newInfo;
    updatePluginItemInUI(index);
}
// 在末尾添加一个 UI 条目
void PluginPage::addPluginItemToUI(int index, const PluginInfo &info) {
    QListWidgetItem *item = new QListWidgetItem;
    if(info.type==0)
        item->setData(Qt::UserRole, info.path);  // 唯一标识
    else
        item->setData(Qt::UserRole, info.uuid);  // 唯一标识
    item->setSizeHint(QSize(0, 48));
    PluginItemWidget *widget = new PluginItemWidget(info);
    pluginListWidget->addItem(item);
    pluginListWidget->setItemWidget(item, widget);
}

// 在指定位置插入 UI 条目
void PluginPage::insertPluginItemToUI(int index, const PluginInfo &info) {
    QListWidgetItem *item = new QListWidgetItem;
    if(info.type==0)
        item->setData(Qt::UserRole, info.path);  // 唯一标识
    else
        item->setData(Qt::UserRole, info.uuid);  // 唯一标识
    item->setSizeHint(QSize(0, 66));
    PluginItemWidget *widget = new PluginItemWidget(info);
    pluginListWidget->insertItem(index, item);
    pluginListWidget->setItemWidget(item, widget);
}

// 更新指定位置的 Widget 内容（复用 Widget，避免重建）
void PluginPage::updatePluginItemInUI(int index) {
    QListWidgetItem *item = pluginListWidget->item(index);
    if (!item) return;
    PluginItemWidget *widget = qobject_cast<PluginItemWidget*>(
        pluginListWidget->itemWidget(item));
    if (widget) {
        widget->updateInfo(m_pluginList[index]);  // 需要在 PluginItemWidget 中实现此方法
    }
    if(currentSelected_index==index)
        updateDetailPanel(index);
}
int PluginPage::findPluginIndex(const QString &id) const {
    for (int i = 0; i < m_pluginList.size(); ++i) {

        if (m_pluginList[i].path == id)
            return i;
        if (m_pluginList[i].uuid == id)
            return i;
    }
    return -1;
}

// 按「市场口径」找已载入插件：优先比插件自报的 id，id 为空或没命中时退回插件名。
// 插件市场判断「是否已安装」用的就是 `marketInfo.id == pluginInfo.id`，这里保持一致。
// ⚠ 两趟分开扫：先全表比 id、再全表比名称。在同一趟里混着比的话，
//   若某个"名称相同但 id 不同"的插件排在前，会盖过后面真正 id 命中的那个 → 卸载错对象。
int PluginPage::findPluginIndexByIdentity(const QString &id, const QString &name) const {
    if (!id.isEmpty()) {
        for (int i = 0; i < m_pluginList.size(); ++i)
            if (m_pluginList[i].id == id) return i;
    }
    if (!name.isEmpty()) {
        for (int i = 0; i < m_pluginList.size(); ++i)
            if (m_pluginList[i].name == name) return i;
    }
    return -1;
}


QString python_code(const QString &py_code,const MessageEvent &msg)
{
    py::gil_scoped_acquire gil;
    try {
        py::module_ qiancao = py::module_::import("qiancao_sdk");
        py::object api = qiancao.attr("QQApi")(g_keyuuid);

        py::dict exec_globals = py::dict(py::module_::import("qq_api").attr("__dict__"));
        exec_globals["__builtins__"] = py::module_::import("builtins");
        exec_globals["msg"] = py::cast(msg);
        exec_globals["api"] = api;               // 注入 api 对象

        // 4. 执行用户代码
        py::exec(py_code.toStdString(), exec_globals);

        // 5. 读取返回值
        QString ret;
        if (exec_globals.contains("__result__"))
            ret = QString::fromStdString(py::str(exec_globals["__result__"]));

        return ret;
    } catch (const py::error_already_set &e) {
        AppendEventLog("[Python] Execute code error: " + QString::fromUtf8(e.what()) ,0xff);
    } catch (const std::exception &e) {
        AppendEventLog("[Python] Execute code error: " + QString::fromUtf8(e.what()) ,0xff);
    }
    return QString();
}

// ==================== 指令启用 / 重命名 的公共小工具 ====================
// 命中的实际匹配键：配了重命名就用 newKey，否则用插件注册的 key
static inline QString ruleMatchKey(const QString &key, const QString &newKey)
{
    return newKey.isEmpty() ? key : newKey;
}

// ==================== 「启用类型」：指令对谁能生效 ====================
// 取值：-1 停用 / 0 全部人（默认） / 1 超管 / 2 机器人管理 / 3 群主 / 4 群管理 / 5 代管。
// 数值越小权限越高 → 设置 N 表示 member_role <= N 的人都能用
//   （如设置 5 → 1超管/2机器人管理/3群主/4群管理/5代管 全放行，6群成员不行；
//     设置 1 → 只有超管）。
// ev.member_role 的取值见 qqbotclient.h：1超管 2机器人管理 3群主 4群管理 5代管 6群成员，
// **0 = 身份未识别**（私聊 / 频道消息不解析成员身份）→ 只在「全部人可用」时放行；
// 但超管 / 机器人管理是全局表命中（在私聊里也会被置成 1/2），所以他们在私聊里照样能用受限指令。
bool ruleRoleMatched(int role, int memberRole)
{
    if (role <= kRuleRoleOff) return false;          // 停用
    if (role == kRuleRoleAll) return true;           // 全部人可用
    return memberRole >= 1 && memberRole <= role;    // 限定权限：只放行 <= 设定等级的人
}

QString ruleRoleName(int role)
{
    switch (role) {
    case kRuleRoleOff: return "禁用";
    case 1:            return "超管";
    case 2:            return "机器人管理";
    case 3:            return "群主";
    case 4:            return "群管理";
    case 5:            return "代管";
    case kRuleRoleAll:
    default:           return "全部人";
    }
}

// 下拉框 item 顺序：第 0 项「禁用」→ role = -1，第 i 项 → role = i - 1
QStringList ruleRoleComboItems()
{
    return QStringList() << "禁用" << "全部人" << "超管" << "机器人管理"
                         << "群主" << "群管理" << "代管";
}

int ruleRoleFromComboIndex(int i) { return i - 1; }
int ruleRoleToComboIndex(int role) { return qBound(0, role + 1, ruleRoleComboItems().size() - 1); }

// ==================== 插件自带的默认触发权限 ====================
// 规则 JSON（get_plugin_info 里 equals/contains/… 的每一项）支持**可选**字段 "role"：
//     {"key":"重启","fun":140713319494384,"role":1}
// 取值与上面「启用类型」完全一致：-1 停用 / 0 全部人 / 1 超管 / 2 机器人管理 /
//   3 群主 / 4 群管理 / 5 代管；语义也一样（写 N = 等级 <= N 的人都能触发）。
// 没写（老插件）→ kRuleRoleAll，与改动前逐字节同行为。
// 越界值（< -1 或 > 5）按「全部人」处理，免得插件写错一个数字把指令永久锁死。
// ⚠ 优先级：**插件的默认值 < 用户在「编辑指令」里的设置**（applySavedRuleConfig 在后面套用，用户赢）。
static int ruleRoleFromJson(const QJsonObject &o)
{
    if (!o.contains("role")) return kRuleRoleAll;
    const int r = o.value("role").toInt(kRuleRoleAll);
    if (r < kRuleRoleOff || r > 5) return kRuleRoleAll;
    return r;
}

// pname 直接当「发起方 uuid」用：展示标签一律以 '[' 开头（"[插件名|%1ms]" / "[关键词匹配|%1ms]" …），
// 插件侧传的是**裸 uuid**，不带 '[' —— 只看首字符就能区分，不用任何分隔符。
QString senderUuidFromPname(const QString &pname)
{
    return pname.startsWith(QLatin1Char('[')) ? QString() : pname;
}

// 裸 uuid → 展示标签 "[插件名|%1ms]"（addmsglog 还原日志前缀用）。
// 「不在插件表」正常到不了：myCallback 里 uuid 不在插件表就早退了，pluginpage 传的就是表里的 p.uuid。
QString pluginPnameText(const QString &uuid)
{
    for (const PluginInfo &p : std::as_const(m_pluginList)) {
        if (p.uuid != uuid) continue;
        return "[" + p.name + "|%1ms]";
    }
    return QString();
}

QString redirectPluginCmdsMarkdown(const QString &input, const QString &uuid, int appid)
{
    if (input.isEmpty() || uuid.isEmpty()) return input;
    if (!input.contains(QLatin1Char('[')) || !input.contains(QLatin1Char('(')))
        return input;                       // 快速排除：正文里根本没有 markdown 链接

    // 1) 收集「注册原名 → 生效新名」
    //    改名可以按机器人分开配 → 这里按 appid 取该机器人实际生效的那个（没配过就是默认档）。
    QHash<QString, QString> renames;
    for (const PluginInfo &p : std::as_const(m_pluginList)) {
        if (p.uuid != uuid) continue;
        const QString &cfgKey = p.id.isEmpty() ? p.name : p.id;
        auto collect = [&renames, &cfgKey, appid](const auto &rules) {
            for (const auto &r : rules) {
                QString newKey = r.newKey;
                // appid <= 0 / 该机器人没配过 → 原样返回，等价于只看默认档
                if (appid > 0 && pluginPage)
                    newKey = pluginPage->effectiveNewKey(appid, cfgKey,
                                                         static_cast<int>(r.type), r.key, newKey);
                if (newKey.isEmpty() || newKey == r.key) continue;
                renames.insert(r.key, newKey);
            }
        };
        if (p.type == 0)                     collect(p.python.rules);
        else if (p.type == 1 || p.type == 2) collect(p.DLL.rules);
        else if (p.type == 3)                collect(p.js.rules);
        break;                              // uuid 唯一，找到就停
    }
    if (renames.isEmpty()) return input;

    // 2) 逐条 [显示](目标) 改写（与 convertMarkdownLinksToXml 同一套正则，排除 ![图片]）
    static const QRegularExpression re(R"((?<!!)\[([^\]]*?)\]\(([^\)]*?)\))");
    QString output;
    int lastIndex = 0;
    auto it = re.globalMatch(input);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int start = m.capturedStart();
        output += input.mid(lastIndex, start - lastIndex);

        const QString showText = m.captured(1);
        const QString target   = m.captured(2);
        if (!target.isEmpty()) {
            const auto hit = renames.constFind(target);       // 目标就是指令 → 换目标
            output += (hit != renames.constEnd())
                        ? QStringLiteral("[%1](%2)").arg(showText, hit.value())
                        : m.captured(0);
        } else {
            const auto hit = renames.constFind(showText);     // 目标为空 → 换方括号里的文字
            output += (hit != renames.constEnd())
                        ? QStringLiteral("[%1]()").arg(hit.value())
                        : m.captured(0);
        }
        lastIndex = m.capturedEnd();
    }
    output += input.mid(lastIndex);
    return output;
}

// 探测 Python 处理函数是否接受第 2 个参数「生效命令词」（形参 >= 2 个，或带 *args）。
// 用于「老插件只写 1 个形参（def on_x(msg)）→ 仍按 1 参调用」的兼容，避免多传实参
// 触发 TypeError 被吞、插件静默失效。探测失败一律按 1 参（旧行为，最安全）。
static bool pyFuncWantsCmd(const py::object &fn)
{
    try {
        py::object inspect = py::module_::import("inspect");
        py::object params  = inspect.attr("signature")(fn).attr("parameters");
        py::object vals    = params.attr("values")();   // 参数对象序列（不是键）
        py::object P       = inspect.attr("Parameter");
        py::object kPosOnly = P.attr("POSITIONAL_ONLY");
        py::object kPosOrKw = P.attr("POSITIONAL_OR_KEYWORD");
        py::object kVarPos  = P.attr("VAR_POSITIONAL");
        int pos = 0;
        for (auto v : vals) {
            py::object kind = v.attr("kind");
            if (kind.equal(kVarPos)) return true;                       // *args：多传也收
            if (kind.equal(kPosOnly) || kind.equal(kPosOrKw)) pos++;
        }
        return pos >= 2;
    } catch (...) {
        return false;
    }
}

// ⚠ 这里**不做任何正文替换**（2026-10-09 定稿）。
//   改名后的「用户实输」原样留在正文里，框架只额外把这条指令的「**生效命令词**」
//   （= 界面上改过名就是新名、没改就是原名，**绝不为空**）用**参数 / 数据**带下去：
//   四条落点：原生库 → onMessagev3 第 3 个参数；32 位 → 帧里的 plugins[].cmd；
//   Python → 处理函数第 2 个参数；JS → on_message 第 3 个参数（funs ↔ cmds 一一对应）。
//   没配过改名的插件走的是与改动前逐字节相同的路径（不拷贝、不重编码）。

// role   = 这条指令在**当前这个机器人**下的启用类型（按机器人区分，见 botRuleOverrideOf）。
// newKey = 同理的「生效新指令名」（空 = 不改名）。两者没单独配过的机器人拿到的就是规则自带的值，
//          与改动前完全等价。
// ⚠ newKey 只影响**匹配用的键**；Regex 规则平时直接用预编译好的 rule.regex（rule.newKey 那个），
//   只有真的按机器人改过名时才临时编译一次（正则 + 按机器人改名是极冷门的组合，不值得为它建缓存）。
bool matchRule(const Rule &rule, const MessageEvent &ev, int role, const QString &newKey) {
    if (!ruleRoleMatched(role, ev.member_role)) return false;   // 停用 / 权限不够
    const QString key = ruleMatchKey(rule.key, newKey);
    QString msg = ev.msg;
    switch (rule.type) {
    case MatchType::Equals:
        return rule.caseSensitive ? (msg == key)
                                  : (msg.compare(key, Qt::CaseInsensitive) == 0);
    case MatchType::StartsWith:
        return rule.caseSensitive ? msg.startsWith(key)
                                  : msg.startsWith(key, Qt::CaseInsensitive);
    case MatchType::EndsWith:
        return rule.caseSensitive ? msg.endsWith(key)
                                  : msg.endsWith(key, Qt::CaseInsensitive);
    case MatchType::Contains:
        return rule.caseSensitive ? msg.contains(key)
                                  : msg.contains(key, Qt::CaseInsensitive);
    case MatchType::Regex: {
        // 按机器人改过名 → 用它的新名现编正则；否则用插件载入时预编译的那个（零开销）
        if (newKey != rule.newKey) {
            QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
            if (!rule.caseSensitive) opt |= QRegularExpression::CaseInsensitiveOption;
            return QRegularExpression(key, opt).match(msg).hasMatch();
        }
        return rule.regex.match(msg).hasMatch();  // const 操作，线程安全
    }
    case MatchType::event:

        return ev.msgType == key;
    }
    return false;
}
bool matchRule2(const Rule_Dll &rule, const MessageEvent &ev, int role, const QString &newKey) {
    if (!ruleRoleMatched(role, ev.member_role)) return false;   // 停用 / 权限不够
    const QString key = ruleMatchKey(rule.key, newKey);
    QString msg = ev.msg;
    switch (rule.type) {
    case MatchType::Equals:
        return rule.caseSensitive ? (msg == key)
                                  : (msg.compare(key, Qt::CaseInsensitive) == 0);
    case MatchType::StartsWith:
        return rule.caseSensitive ? msg.startsWith(key)
                                  : msg.startsWith(key, Qt::CaseInsensitive);
    case MatchType::EndsWith:
        return rule.caseSensitive ? msg.endsWith(key)
                                  : msg.endsWith(key, Qt::CaseInsensitive);
    case MatchType::Contains:
        return rule.caseSensitive ? msg.contains(key)
                                  : msg.contains(key, Qt::CaseInsensitive);
    case MatchType::Regex: {
        // 按机器人改过名 → 用它的新名现编正则；否则用插件载入时预编译的那个（零开销）
        if (newKey != rule.newKey) {
            QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
            if (!rule.caseSensitive) opt |= QRegularExpression::CaseInsensitiveOption;
            return QRegularExpression(key, opt).match(msg).hasMatch();
        }
        return rule.regex.match(msg).hasMatch();  // const 操作，线程安全
    }
    case MatchType::event:

        return ev.msgType == key;
    }
    return false;
}
bool matchRule3(const Rule_js &rule, const MessageEvent &ev, int role, const QString &newKey) {
    if (!ruleRoleMatched(role, ev.member_role)) return false;   // 停用 / 权限不够
    const QString key = ruleMatchKey(rule.key, newKey);
    QString msg = ev.msg;
    switch (rule.type) {
    case MatchType::Equals:
        return rule.caseSensitive ? (msg == key)
                                  : (msg.compare(key, Qt::CaseInsensitive) == 0);
    case MatchType::StartsWith:
        return rule.caseSensitive ? msg.startsWith(key)
                                  : msg.startsWith(key, Qt::CaseInsensitive);
    case MatchType::EndsWith:
        return rule.caseSensitive ? msg.endsWith(key)
                                  : msg.endsWith(key, Qt::CaseInsensitive);
    case MatchType::Contains:
        return rule.caseSensitive ? msg.contains(key)
                                  : msg.contains(key, Qt::CaseInsensitive);
    case MatchType::Regex: {
        // 按机器人改过名 → 用它的新名现编正则；否则用插件载入时预编译的那个（零开销）
        if (newKey != rule.newKey) {
            QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
            if (!rule.caseSensitive) opt |= QRegularExpression::CaseInsensitiveOption;
            return QRegularExpression(key, opt).match(msg).hasMatch();
        }
        return rule.regex.match(msg).hasMatch();  // const 操作，线程安全
    }
    case MatchType::event:

        return ev.msgType == key;
    }
    return false;
}

void PluginPage::onMessageReceived(MessageEvent &msg,const PluginInfo &p,std::optional<py::gil_scoped_acquire> &gil) {

    try {
        // 3.14t 下必须持锁，保持原有的 acquire
        QString reply;
        auto process_ret = [&](py::object ret) {
            msg.op=true;
            if (!ret.is_none() && !m_asyncio_mod.is_none() &&
                m_asyncio_mod.attr("iscoroutine")(ret).cast<bool>()) {
                if (!m_run_coro_func.is_none() && !m_loop.is_none()) {
                    m_run_coro_func(ret, m_loop); // 非阻塞，毫秒级返回，不会卡 C++ 线程！
                }

                return;
            }

            // 【关键修复 2】只有非协程的同步返回值，才拼接到 reply
            if (!ret.is_none() && py::isinstance<py::str>(ret)) {
                QString str = QString::fromStdString(py::str(ret).cast<std::string>());
                if (reply.isEmpty()) reply = str;
                else reply += "\n---\n" + str;
            }
        };

        // 该插件在配置表里的键（id 优先，空则名）——按机器人查启用类型要用它。
        // 用**引用**而不是拷贝：dispatch 是每条消息都走的热路径。
        const QString &cfgKey = p.id.isEmpty() ? p.name : p.id;

        for (const Rule &rule : std::as_const(p.python.rules)) {
            int role = rule.role;
            QString newKey = rule.newKey;      // 空 = 不改名
            botRuleOverrideOf(msg.appid, cfgKey, static_cast<int>(rule.type),
                              rule.key, role, newKey);
            if (matchRule(rule, msg, role, newKey)) {
                if (!gil) gil.emplace();   // 第一次命中才获取，循环内复用

                // 正文一个字不动（用户实输就在 msg.msg 里）。这条指令的「生效命令词」
                // （改名=新名、没改=原名）作为**第 2 个参数**交给插件，让它去切正文取参数 ——
                // 与原生库 v3 的参数含义一致（那边是 (json, fun, cmd)）。
                // 兼容：老插件只写 1 个形参（def on_x(msg)）→ rule.wantsCmd=false，仍按 1 参调用。
                py::object ret;
                if (rule.wantsCmd) {
                    const QString effCmd = ruleMatchKey(rule.key, newKey);
                    ret = rule.function(msg, effCmd.toStdString());   // async 时返回协程对象
                } else {
                    ret = rule.function(msg);                         // 老插件：1 参，同旧路径
                }
                process_ret(ret);

            }
        }

        // 只有拼出了同步的 reply 才通过 C++ 发送，异步的交给后台自己跑就行了
        if (!reply.isEmpty()) {
            QQBotClient *client = m_botClients[msg.appid];
            if (client) {
                // pname 直接传发起方 uuid（不带 '['）：发送管线入口据此把正文里 [注册原名]()
                // 重定向成改名后的指令；addmsglog 再按 uuid 还原成 "[插件名|%1ms]" 显示。
                client->send_msgAsync(msg.type, msg.groupId, p.uuid, reply, msg.msgId);

            }

            return;
        }


    } catch (const std::exception &e) {
        AppendEventLog("[Python] " + p.name + " 错误: " + e.what(), 0xff);
    } catch (...) {
        AppendEventLog("[Python] " + p.name + " 未知错误", 0xff);
    }

}


// ==================== 铃铛商店（框架级指令）====================
// 分工：**框架**管「列目录 / 查余额 / 扣费 / 回话」，**插件**管「这单到底给不给、给什么」。
// 插件在 get_plugin_info 里注册 "shop" 数组（格式见 pluginpage.h 的 ShopItem_Dll 注释），
// 用户在群里发两条指令即可：
//   「<货币名>商店」                      → 列出所有启用插件在售的商品（全局统一序号）
//   「兑换物品 <序号> <数量> <其他内容…>」 → 调该商品注册的函数，成功才扣费
// 货币名来自全局 g_currency_name（默认「铃铛」），整套系统里只在这一处拼字符串。
// 计价口径：**积分**。看一次广告 = g_points_per_ad 积分（默认 1000）→ 商品价按 1000 的倍数定。

// 货币名（全局可改；空值兜底成「铃铛」，免得拼出「商店」这种半截指令）
static QString shopCurrencyName()
{
    return g_currency_name.isEmpty() ? QStringLiteral("铃铛") : g_currency_name;
}
// 全角空格也要按分隔符处理（手机输入法常打全角）
static inline bool isShopSpace(QChar c) { return c.isSpace() || c == QChar(0x3000); }

// 插件返回值 = **只有成败**（非 0 = 成功），不返回任何文本：
//   · 原生库：int（C ABI），0 = 失败不扣费 / 非 0 = 成功扣费
//   · Python：True / False（None 也算失败）
// 想给用户看什么（发了什么货、为什么买不了）由插件自己调发送 API 发；
// 框架只补一条账单回执（兑换成功：xx × n，花费 … ，余额 …）。
// 这样框架永远不持有插件给的指针 —— 谁分配谁释放的问题直接消失。

bool PluginPage::handleShopCommand(const QString &frame, MessageEvent &msg)
{
    const QString body = msg.msg.trimmed();
    if (body.isEmpty()) return false;

    const QString cur     = shopCurrencyName();
    const QString listCmd = cur + QStringLiteral("商店");

    // ---- 1) 是商店指令吗？不是就直接放手（连商品表都不拼） ----
    QString buyWord;      // 兑换命令词（后面必须跟空白/结尾）
    const bool isList = (body == listCmd) || (body == QStringLiteral("铃铛商店"));
    if (!isList) {
        if (body.startsWith(QStringLiteral("兑换物品")))            buyWord = QStringLiteral("兑换物品");
        else if (!cur.isEmpty() && body.startsWith(cur + QStringLiteral("兑换"))) buyWord = cur + QStringLiteral("兑换");
        if (buyWord.isEmpty()) return false;
        // 「兑换物品abc」不算（命令词与参数之间必须有分隔）
        if (body.size() > buyWord.size() && !isShopSpace(body.at(buyWord.size()))) return false;
    }

    // ---- 2) 现拼在售商品表（数量很少，每次现拼最省心，也不会留下陈旧指针） ----
    struct ShopOffer {
        int     pluginIndex = -1;
        int     itemIndex   = -1;
        bool    isPython    = false;
        bool    buyable     = true;   // false = 只上架展示，点了回「暂时不支持购买」（32 位易语言 / JS）
        QString name;          // 商品备注
        QString pluginName;    // 插件名（列表里标来源）
        QString pluginUuid;    // 回消息的 pname（日志按它还原成 [插件名|%1ms]）
        qint64  price = 0;
        qint64  fun   = 0;     // 原生库：函数地址（有 onShop 导出时只是一个 id，框架原样回传）
    };
    QList<ShopOffer> offers;
    for (int pi = 0; pi < m_pluginList.size(); ++pi) {
        const PluginInfo &p = m_pluginList.at(pi);
        if (!p.enabled) continue;
        if (p.appid.contains(msg.appid)) continue;      // 该机器人下这个插件是关的
        if (p.type == 0) {                              // Python
            for (int ii = 0; ii < p.python.shop.size(); ++ii) {
                const ShopItem_py &g = p.python.shop.at(ii);
                ShopOffer o;
                o.pluginIndex = pi; o.itemIndex = ii; o.isPython = true;
                o.name = g.key; o.pluginName = p.name; o.pluginUuid = p.uuid;
                o.price = g.price;
                offers.append(o);
            }
        } else if (p.type == 1) {                       // 原生库（有 onShop 走 id，没有就把 fun 当函数地址）
            for (const ShopItem_Dll &g : std::as_const(p.DLL.shop)) {
                if (!p.DLL.onShop && !g.fun) continue;  // 没 onShop 又没地址 → 调下去必崩，直接不上架
                ShopOffer o;
                o.pluginIndex = pi;
                o.name = g.key; o.pluginName = p.name; o.pluginUuid = p.uuid;
                o.price = g.price; o.fun = g.fun;
                offers.append(o);
            }
        } else if (p.type == 2) {                       // 32 位易语言：**上架但买不了**
            for (const ShopItem_Dll &g : std::as_const(p.DLL.shop)) {
                ShopOffer o;
                o.pluginIndex = pi;
                o.buyable = false;
                o.name = g.key; o.pluginName = p.name; o.pluginUuid = p.uuid;
                o.price = g.price; o.fun = g.fun;
                offers.append(o);
            }
        } else if (p.type == 3) {                       // JS：**上架但买不了**
            for (const ShopItem_js &g : std::as_const(p.js.shop)) {
                ShopOffer o;
                o.pluginIndex = pi;
                o.buyable = false;
                o.name = g.key; o.pluginName = p.name; o.pluginUuid = p.uuid;
                o.price = g.price;
                offers.append(o);
            }
        }
        // 32 位（2）/ JS（3）：**上架展示，但 buyable = false** ——
        //   列表里能看见（也标了「暂不可购买」），真要兑换时框架直接回一句「暂时不支持购买」，
        //   既不扣费也不调插件（这两端调用的桥接还没打通）。以后放开只删掉那些 buyable = false。
    }

    // 统一出口：发一条 + 记「已处理」（置 op 后 AI 不会再兜一条重复的）
    // pname 与框架其它指令同一口径：传 uuid → 发送管线还原成 "[插件名|%1ms]"；
    // 商店自己发的回话传 "[<货币名>商店|%1ms]"（⚠ %1ms 是留给发送管线填耗时的，不能走 arg()）。
    const QString shopLogTag = QStringLiteral("[") + cur + QStringLiteral("商店|%1ms]");
    auto sendReply = [&](const QString &text, const QString &pname) {
        if (text.isEmpty()) return;
        QQBotClient *client = m_botClients.value(msg.appid, nullptr);
        if (!client) return;
        QString pn = pname;      // send_msgAsync 要非 const 引用
        QString tx = text;
        client->send_msgAsync(msg.type, msg.groupId, pn, tx, msg.msgId);
        msg.op = true;
    };

    // ---- 3) 列表 ----
    if (isList) {
        // 1 次广告 = 多少积分（全局可调）；列表里直接翻成「几次广告」，用户对价格有直觉
        const qint64 perAd = (g_points_per_ad > 0) ? g_points_per_ad : 1000;
        QStringList lines;
        lines << QStringLiteral("【%1商店】").arg(cur);
        if (offers.isEmpty()) {
            lines << QStringLiteral("暂时没有上架的商品");
        } else {
            for (int i = 0; i < offers.size(); ++i) {
                const ShopOffer &o = offers.at(i);
                QString priceText;
                if (o.price <= 0) {
                    priceText = QStringLiteral("免费");
                } else if (o.price % perAd == 0) {
                    priceText = QStringLiteral("%1 %2（约 %3 次广告）")
                                    .arg(o.price).arg(cur).arg(o.price / perAd);
                } else {
                    priceText = QStringLiteral("%1 %2").arg(o.price).arg(cur);
                }
                // 来源插件后面标一句「暂不可购买」，免得用户点了才发现买不了
                const QString srcText = o.buyable
                        ? o.pluginName
                        : o.pluginName + QStringLiteral("，暂不可购买");
                lines << QStringLiteral("%1. %2 —— %3（%4）")
                         .arg(i + 1).arg(o.name, priceText, srcText);
            }
            lines << QString();
            lines << QStringLiteral("发送：兑换物品 序号 数量");
            lines << QStringLiteral("例：兑换物品 1 1");
        }
        // 顺带把余额报一下（不然用户没法判断买不买得起；读一次库，可忽略）
        if (BotDB *db = g_botdb.value(msg.appid, nullptr)) {
            const qint64 myPoints = db->getPoints(QByteArray::fromHex(msg.user.toUtf8()));
            lines << QStringLiteral("你的余额：%1 %2").arg(myPoints).arg(cur);
        }
        lines << QStringLiteral("（看 1 次广告 +%1 %2）").arg(perAd).arg(cur);
        sendReply(lines.join('\n'), shopLogTag);
        return true;
    }

    // ---- 4) 兑换：切「序号 / 数量 / 其余原文」 ----
    int p = buyWord.size();
    while (p < body.size() && isShopSpace(body.at(p))) ++p;
    int idxEnd = p;
    while (idxEnd < body.size() && !isShopSpace(body.at(idxEnd))) ++idxEnd;
    const QString idxStr = body.mid(p, idxEnd - p);

    bool idxOk = false;
    const int seq = idxStr.toInt(&idxOk);
    if (!idxOk || seq <= 0 || seq > offers.size()) {
        sendReply(QStringLiteral("没有这个商品，发送「%1」查看在售列表").arg(listCmd), shopLogTag);
        return true;
    }

    int q = idxEnd;                                  // 数量（省略 = 1 件）
    while (q < body.size() && isShopSpace(body.at(q))) ++q;
    int cntEnd = q;
    while (cntEnd < body.size() && !isShopSpace(body.at(cntEnd))) ++cntEnd;
    qint64 count = 1;
    if (cntEnd > q) {
        bool cOk = false;
        const qint64 c = body.mid(q, cntEnd - q).toLongLong(&cOk);
        if (cOk && c > 0) count = c;
    }
    if (count > 1000000) count = 1000000;            // 防呆：别让一次买上百万份把总价撑爆

    // cmd = 「序号」这个参数**之后**的原文（含数量与其后的内容），与 onMessagev3 第 3 个参数同义：
    // 插件拿它去切自己要的参数即可，正文原文也在 json 帧里（frame）。
    const QString cmd = body.mid(idxEnd).trimmed();

    const ShopOffer &offer = offers.at(seq - 1);

    // ---- 4.5) 这一端还没打通购买（32 位易语言 / JS）：能看见、但买不了 ----
    //   **不扣费、不调插件**，只回一句「暂时不支持购买」，别让用户白等也别让钱扣了退不回来。
    if (!offer.buyable) {
        sendReply(QStringLiteral("「%1」暂时不支持购买（%2 还没适配）")
                      .arg(offer.name, offer.pluginName),
                  shopLogTag);
        return true;
    }

    PluginInfo &plug = m_pluginList[offer.pluginIndex];

    // 总价（单价 × 数量，带溢出保护）
    qint64 cost = 0;
    if (offer.price > 0) {
        const qint64 maxCost = (std::numeric_limits<qint64>::max)();
        cost = (count > maxCost / offer.price) ? maxCost : offer.price * count;
    }

    // ---- 5) 先扣费（原子），插件说失败再退回 ----
    // ⚠ 顺序不能反：先给货再扣费的话，并发下会出现「货发了、钱没扣到」。
    //   先扣后调，最坏是「钱扣了、插件说没有」→ 下面原路退回去，用户不吃亏。
    //   （不用额外的「先查一次余额」预检：spendPoints 失败时会带出当前余额，省一次读库）
    BotDB *db = g_botdb.value(msg.appid, nullptr);
    const QByteArray openidBin = QByteArray::fromHex(msg.user.toUtf8());
    qint64 left = 0;
    if (cost > 0) {
        if (!db || !db->spendPoints(openidBin, cost, &left)) {   // 失败时 left = 当前余额
            sendReply(QStringLiteral("余额不足：需要 %1 %2，你当前有 %3 %2")
                          .arg(cost).arg(cur).arg(left),
                      offer.pluginUuid);
            return true;
        }
    } else if (db) {
        left = db->getPoints(openidBin);
    }

    // ---- 6) 调插件：参数与 onMessagev3 完全一致（json 帧 / fun / cmd），**只看返回值真假** ----
    //   有 onShop 导出 → 走 onShop（fun 当 id 原样回传，Go 版 SDK 靠这个）；
    //   没有 → 把 fun 当函数地址直接调（C/C++/Rust/C# 的常规写法，不用多写一个导出）。
    //   ⚠ 名字这种要在调用前先抄出来：插件回调里有可能把自己卸载掉，m_pluginList 一变 plug 就悬空了。
    const QString plugName = plug.name;
    bool ok = false;
    if (offer.isPython) {
        py::gil_scoped_acquire gil;
        try {
            const ShopItem_py &goods = plug.python.shop.at(offer.itemIndex);
            py::object fn = goods.function;              // 持 GIL 拷一份，回调期间被卸载也不怕
            const bool wantsCmd = goods.wantsCmd;
            py::object ret = wantsCmd ? fn(msg, cmd.toStdString()) : fn(msg);
            if (ret.is_none()) {
                ok = false;
            } else if (py::isinstance<py::bool_>(ret) || py::isinstance<py::int_>(ret)) {
                ok = (PyObject_IsTrue(ret.ptr()) == 1);
            } else {
                // 返回值只能是 True/False —— 文本一律不认（框架不接字符串，谁释放的问题也就不存在）
                AppendEventLog("[商店] " + plugName + " 商品函数应返回 True/False，实际返回 "
                                   + QString::fromStdString(py::str(py::type::of(ret)).cast<std::string>()),
                               0xff);
            }
        } catch (const py::error_already_set &e) {
            AppendEventLog("[商店] " + plugName + " 商品函数异常: " + QString::fromUtf8(e.what()), 0xff);
        } catch (...) {
            AppendEventLog("[商店] " + plugName + " 商品函数未知异常", 0xff);
        }
    } else {
        const QByteArray frameUtf8 = frame.toUtf8();      // 显式存活到调用结束
        const QByteArray cmdUtf8   = cmd.toUtf8();
        ShopFunc fn = plug.DLL.onShop;
        if (!fn) fn = reinterpret_cast<ShopFunc>(static_cast<quintptr>(offer.fun));
        if (fn) ok = (fn(frameUtf8.constData(), offer.fun, cmdUtf8.constData()) != 0);
    }

    if (!ok) {
        // 插件不给货 → 把钱原路退回（addPoints 也是原子的；失败只可能是库挂了，记一笔）
        if (cost > 0 && db) {
            bool backOk = false;
            left = db->addPoints(openidBin, cost, &backOk);
            if (!backOk)
                AppendEventLog("[商店] " + plugName + " 退款失败：" + offer.name, 0xff);
        }
        sendReply((cost > 0)
                      ? QStringLiteral("兑换失败：%1（已退回 %2 %3）").arg(offer.name).arg(cost).arg(cur)
                      : QStringLiteral("兑换失败：%1").arg(offer.name),
                  offer.pluginUuid);
        return true;
    }

    // ---- 7) 成功：只补一条账单回执（要发什么货、说什么话，插件自己发） ----
    QString replyText;
    if (cost <= 0) {
        replyText = QStringLiteral("兑换成功：%1 × %2（免费），余额 %3 %4")
                        .arg(offer.name).arg(count).arg(left).arg(cur);
    } else {
        replyText = QStringLiteral("兑换成功：%1 × %2，花费 %3 %4，余额 %5 %4")
                        .arg(offer.name).arg(count).arg(cost).arg(cur).arg(left);
    }

    sendReply(replyText, offer.pluginUuid);
    return true;
}

int PluginPage::dispatch_message(const QString &text, MessageEvent &msg)
{

    QByteArray utf8 = text.toUtf8();
    int _32=0;

    // 铃铛商店是**框架级**指令（不属于任何插件的规则）：
    // 「<货币名>商店」/「兑换物品 序号 数量 …」命中就直接回话并吃掉这条消息 ——
    // 不再往下发给插件 / 32 位 / JS，也不会被 AI 再兜一条（内部已置 msg.op）。
    // ⚠ 这里传的是**原始帧** text：与插件 onMessagev3 第 1 个参数同一份，插件拿它取正文/群号。
    if (handleShopCommand(text, msg)) {

        return 254;
    }


    // 每条消息最多获取一次：命中第一个 Python 插件时 emplace，本条消息的所有 Python 插件共用。
    std::optional<py::gil_scoped_acquire> gil;
    QJsonArray jsTargets;    // 本轮命中的 JS 插件（uuid + 命中规则名），遍历结束后合成**一帧**投给 node 宿主
    // 32 位易语言侧：把本轮要投的插件收集起来，最后随帧一起投一次。
    //   · 注册式    —— 只列**命中规则**的那些，附「函数指针 + 命中原指令 + 用户实输」
    //   · 非注册式  —— **每条消息都列**，只给 uuid（没有函数指针可给，插件自己判断）
    // 正文一律不动；易语言侧按 plugins[] 逐条分派即可，不必再自己匹配一遍。
    // ⚠ 旧版 32 位插件（插件信息里没带 sdk）也照样收到 plugins[]，只是它看不懂、继续按 d.content
    //   自己匹配 —— 正因如此**旧版不允许在界面上改名**（见 ruleConfigAllowRename）。
    QJsonArray plugins32;
    for (const auto & p:std::as_const(m_pluginList)) {
        if (!p.enabled) continue;
        if(p.appid.contains(msg.appid)) continue; //这个插件禁用

        if (p.type == 0){
            onMessageReceived(msg,p,gil);
            continue;
        }
        else if(p.type == 2) //收集命中的 32 位插件，遍历结束后随帧一起投递
        {
            const QString &cfgKey = p.id.isEmpty() ? p.name : p.id;
            if(p.DLL.rules.size()>0){
                for (const Rule_Dll &rule : std::as_const(p.DLL.rules)) {
                    int role = rule.role;
                    QString newKey = rule.newKey;
                    botRuleOverrideOf(msg.appid, cfgKey, static_cast<int>(rule.type),
                                      rule.key, role, newKey);
                    if (matchRule2(rule, msg, role, newKey)) {
                        _32++;
                        // 命中即登记一条：插件标识 + 函数指针 + **生效命令词**（+ 一份用户实输兜底）。
                        // fun 是**32 位进程里的地址**（≤ 0x7FFFFFFF，double 能精确表示）；
                        // 再给一份 fun_s 十进制字符串，方便按字符串读的解析器。
                        QJsonObject one;
                        one["uuid"]  = p.uuid;
                        one["fun"]   = double(rule.fun);
                        one["fun_s"] = QString::number(rule.fun);
                        // cmd = **生效命令词**（界面上改过名就是新名、没改就是原名，绝不为空）：
                        // 易语言拿它去切帧正文里的参数；别用「注册原名」的长度切（改名后长度对不上）。
                        one["cmd"]   = ruleMatchKey(rule.key, newKey);
                        one["text"]  = msg.msg;     // 用户实输原文（冗余，正文里也有；插件可自行忽略）
                        plugins32.append(one);
                        break;
                    }
                }
            }else{
                _32++;
                // 非注册式（老式）32 位插件：没有规则表，**每条消息都要投给它**，由它自己匹配。
                // 没有函数指针可给 → 只登记 uuid，fun / fun_s / cmd / text 一律不写。
                // ⚠ 易语言侧按**本地插件的「注册式」标志**分流，别靠这些字段是否存在来判。
                QJsonObject one;
                one["uuid"] = p.uuid;
                plugins32.append(one);
            }

            continue;
        }else if (p.type == 3) {

            // 规则表为空 = 每条消息都喂给它（保持原语义）；
            // 否则把**所有命中**的规则名（fun）收集起来一起下发 —— 框架既然已经匹配过了，
            // node 侧直接按名字执行即可，不用再匹配一遍（也顺带让 regex 规则可用）。
            bool hit = p.js.rules.isEmpty();
            QJsonArray funs;    // 命中的规则函数名（顺序与 cmds 一一对应）
            QJsonArray cmds;    // 对应的「生效命令词」；用户实输就在 data 的正文里，不用另传
            const QString &cfgKey = p.id.isEmpty() ? p.name : p.id;
            for (const Rule_js &rule : std::as_const(p.js.rules)) {
                int role = rule.role;
                QString newKey = rule.newKey;
                botRuleOverrideOf(msg.appid, cfgKey, static_cast<int>(rule.type),
                                  rule.key, role, newKey);
                if (!matchRule3(rule, msg, role, newKey)) continue;
                hit = true;
                funs.append(rule.fun);
                cmds.append(ruleMatchKey(rule.key, newKey));   // 改名=新名、没改=原名
                break;
            }
            if (hit) {
                QJsonObject item;
                item["uuid"] = p.uuid;
                item["funs"] = funs;
                item["cmds"] = cmds;    // 宿主原样透传，插件按 funs[i] ↔ cmds[i] 对应取
                jsTargets.append(item);
            }
            continue;
        }

        try {
            // 命中一条规则就调一次；正文一律原样（不做替换），额外信息全走参数。
            //   新版(导出 onMessagev3) → onMessagev3(json, fun, **生效命令词**)：
            //     生效命令词 = 界面上改过名就是新名、没改就是原名（绝不为空）。插件拿它去切
            //     json 正文里的参数 —— 不能拿自己注册的原名切，改名后长度对不上。
            //     **返回值 = 想让机器人发的话**（NULL/空串 = 不发）
            //   中间版                → onMessagev2(json, fun)
            //   旧版                  → on_message(json)（不看规则，每条都喂）
            // 三者按优先级取其一，插件导出哪个就走哪条，互不干扰。
            // v3 返回值攒在这里（命中多条用分隔行拼起来，与 Python 插件 return 文本的做法一致）
            QString dllReply;
            const QString &cfgKey = p.id.isEmpty() ? p.name : p.id;
            if (p.DLL.onMessage3) {
                for (const Rule_Dll &rule : std::as_const(p.DLL.rules)) {
                    int role = rule.role;
                    QString newKey = rule.newKey;
                    botRuleOverrideOf(msg.appid, cfgKey, static_cast<int>(rule.type),
                                      rule.key, role, newKey);
                    if (!matchRule2(rule, msg, role, newKey)) continue;

                    // 缓冲区显式存活到调用结束，别写成临时对象的 constData()
                    const QByteArray effCmd = ruleMatchKey(rule.key, newKey).toUtf8(); // 生效命令词（改名=新名）
                    // 返回值 = 插件想让机器人发的话：非空即代发（插件也可以照旧自己调 API 发，两种都行）。
                    // ⚠ 内存所有权：返回的字符串由**插件持有**（静态缓冲 / 字面量），框架只读、绝不释放。
                    const char *ret = p.DLL.onMessage3(utf8.constData(), rule.fun, effCmd.constData());
                    if (ret && *ret) {
                        const QString s = QString::fromUtf8(ret);
                        if (dllReply.isEmpty()) dllReply = s;
                        else dllReply += "\n---\n" + s;
                    }
                    break;
                }
            } else if (p.DLL.onMessage2) {
                for (const Rule_Dll &rule : std::as_const(p.DLL.rules)) {
                    int role = rule.role;
                    QString newKey = rule.newKey;
                    botRuleOverrideOf(msg.appid, cfgKey, static_cast<int>(rule.type),
                                      rule.key, role, newKey);
                    if (!matchRule2(rule, msg, role, newKey)) continue;

                    p.DLL.onMessage2(utf8.constData(), rule.fun);
                    break;
                }
            }

            if (p.DLL.onMessage) {
                p.DLL.onMessage(utf8.data());
            }

            // v3 返回值 → 框架代发（与 Python 插件 return 文本走的是同一条发送路径）。
            // 顺手置 op：插件已经答过了，别再让 AI 兜底回一条重复的。
            if (!dllReply.isEmpty()) {
                msg.op = true;
                QQBotClient *client = m_botClients.value(msg.appid);
                if (client) {
                    // pname 直接传发起方 uuid（不带 '['）：发送管线入口据此把正文里 [注册原名]()
                    // 重定向成改名后的指令；addmsglog 再按 uuid 还原成 "[插件名|%1ms]" 显示。
                    client->send_msgAsync(msg.type, msg.groupId, p.uuid, dllReply, msg.msgId);
                }
            }
        } catch (const std::exception &e) {
            AppendEventLog("[DLL] " + p.name + " on_message: " + e.what() ,0xff);
        } catch (...) {
            AppendEventLog("[DLL] " + p.name + " on_message: unknown exception" ,0xff);
        }
    }

    // Python 插件的 GIL 到这儿还掉：后面的 JS 投递 / 32 位桥接写块 / botnomsg 都是纯 C++，
    // 可能阻塞。自由线程版要求线程在长时间不碰解释器时 detach，否则 GC 的 stop-the-world
    // 会一直等这个线程。
    gil.reset();

    // JS 插件：把本轮命中的插件（uuid + 各自命中的规则名）合成**一帧**投给 node 宿主，
    // 宿主按 uuid 逐个分发给对应 Worker，并把 funs 交给插件**直接执行**。
    // 好处：① `text`（消息原文）只序列化/跨进程传一次；② 匹配只在框架侧做一次，node 侧零匹配。
    if (!jsTargets.isEmpty())
        NodePluginManager::instance().postEventBatchAsync(jsTargets, "on_message", text, QString());

#ifdef _WIN32
    if(_32!=0 && bridge) {
        // 把「命中的插件 + 函数指针 + 指令」并进原始帧顶层的 plugins[] 一起投过去（仍只投一次）。
        // ⚠ 正文一个字不动；易语言侧按 plugins[i] 直接调函数，不必再自己匹配一遍。
        //   万一帧解析不出来（理论上不会）就退回原始帧，保持旧行为。
        QJsonObject doc = QJsonDocument::fromJson(utf8.constData()).object();
        if (!doc.isEmpty()) {
            doc["plugins"] = plugins32;
            const QByteArray frame32 = QJsonDocument(doc).toJson(QJsonDocument::Compact);
            bridge->writeResponseToBlock(2, frame32.constData());
        } else {
            bridge->writeResponseToBlock(2, utf8.constData());
        }
    }

#endif

   return (_32==0)+jsTargets.isEmpty();
}


//选中列表
void PluginPage::onPluginSelected(int row)
{
    if (row < 0 || row >= pluginListWidget->count()) {
        currentSelected_index=-1;
        return;
    }
    QListWidgetItem *item = pluginListWidget->item(row);
    QString name = item->data(Qt::UserRole).toString();
    int index=findPluginIndex(name);
    if (index!=-1) {
        currentSelected_index = index;
        updateDetailPanel(index);
        updateAccountCheckList(index);
    }
}

QString getShortPath(const QString& path, int maxLen = 64) {
    if (path.length() <= maxLen) {
        return path;
    }
    int startPos = path.length() - maxLen;
    int sepPos = -1;
    for (int i = startPos; i < path.length(); ++i) {
        if (path[i] == '/' || path[i] == '\\') {
            sepPos = i;
            break;
        }
    }
    QString shortPath;
    if (sepPos != -1) {
        shortPath = path.mid(sepPos + 1);
    } else {

        shortPath = path.right(maxLen);
    }
    return QString("...") + shortPath;
}
// ==================== 配置区尺寸常量 ====================
static const int kConfigMaxRows   = 5;    // 高度按 5 行封顶（超出滚动）
static const int kConfigRowHeight = 28;   // 单行高度
static const int kConfigSpacing   = 6;    // 同一行里「项与项」的间距（水平）
static const int kConfigRowGap    = 6;    // 「行与行」的间距（垂直）——10 会显得空，跟项间距一致比较整齐
static const int kConfigConfirmWidth = 56;  // input 行右侧「确认」按钮宽度
static const int kConfigLabelMaxWidth = 120; // input 行标签列的统一宽度上限（超长的裁掉，别把输入框挤没）

// ==================== 配置区自适应折行布局 ====================
// 用 QVBoxLayout 的话 JSON 里连着好几个 checkbox / button 会一人占一行，太占地方。
// 这里的规则：
//   · 「整行项」= widget 动态属性 `qc_config_full_row` 为 true（input 那种要拉满宽度的），独占一行；
//   · 「紧凑项」= 其余（checkbox / button），从左到右并排，放不下自动换行。
// 必须实现 heightForWidth：QScrollArea(widgetResizable) 会用它决定容器高度，否则折行后高度不对。
class ConfigFlowLayout : public QLayout
{
public:
    explicit ConfigFlowLayout(QWidget *parent = nullptr)
        : QLayout(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(kConfigSpacing);
    }
    // 换行时的垂直间距，默认跟项间距（spacing）一样；设了就按设的来。
    void setRowGap(int g) { m_rowGap = g; invalidate(); }
    int  rowGap() const   { return m_rowGap >= 0 ? m_rowGap : spacing(); }
    ~ConfigFlowLayout() override
    {
        while (QLayoutItem *item = takeAt(0)) delete item;
    }

    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        if (index < 0 || index >= m_items.size()) return nullptr;
        return m_items.takeAt(index);
    }
    Qt::Orientations expandingDirections() const override { return Qt::Orientations(); }
    bool hasHeightForWidth() const override { return true; }
    QSize sizeHint() const override
    {
        if (m_items.isEmpty()) return QSize(0, 0);
        // ⚠ 必须用「最近一次真实布局的宽度」算高度。写死 240 的话，容器在更宽的视口里
        // 会被报一个虚高的高度（240 宽要 3 行、300 宽只要 2 行），QScrollArea 就以为
        // 内容超出了视口 → 冒出一条其实没内容可滚的滚动条。
        const int w = m_lastWidth > 0 ? m_lastWidth : 240;
        return QSize(w, heightForWidth(w));
    }
    QSize minimumSize() const override
    {
        // 宽度方向不能回报「最宽那个 item」：那会把容器的最小宽度顶大，视口被撑破后
        // 折行失效（内容被裁）。这里只保留高度方向的最小值。
        int h = 0;
        for (QLayoutItem *item : m_items)
            h = qMax(h, itemSize(item).height());
        return QSize(0, h);
    }
    int heightForWidth(int width) const override
    {
        return doLayout(QRect(0, 0, width, 0), false).height();
    }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        const QMargins mg = contentsMargins();
        m_lastWidth = qMax(0, rect.width() - mg.left() - mg.right());
        doLayout(rect, true);
    }

private:
    // ⚠ 刚 new 出来、还没被 Qt 显示过的控件，QWidgetItem 会把它当「空 item」：
    //    sizeHint()/minimumSize() 直接返回 (0,0)，setGeometry() 也会被跳过。
    //    Qt 是在 addWidget 之后**排队**调 _q_showIfNotHidden 才把控件显示出来的，
    //    所以「建完控件立刻量高度」在同一个事件循环回合里必然量到 0 ——
    //    高度就停在切插件前那个插件的旧值上，表现是切到多配置插件后只露出第一行、
    //    后面的行被容器裁掉且不出滚动条。这里在 item 报空时退回控件自身的 sizeHint
    //    （内部会 ensurePolished，值是准的）。
    static QSize itemSize(QLayoutItem *item)
    {
        QSize s = item->sizeHint();
        if (s.height() <= 0) {
            if (QWidget *w = item->widget()) s = s.expandedTo(w->sizeHint());
        }
        return s;
    }
    // 空 item 的 setGeometry 会被 Qt 丢掉，直接给控件本身设，保证这一遍就能摆好
    static void placeItem(QLayoutItem *item, const QRect &r)
    {
        item->setGeometry(r);
        if (QWidget *w = item->widget()) {
            if (w->geometry() != r) w->setGeometry(r);
        }
    }

    static bool isFullRowItem(QLayoutItem *item)
    {
        QWidget *w = item->widget();
        return w && w->property("qc_config_full_row").toBool();
    }

    QSize doLayout(const QRect &rect, bool apply) const
    {
        const QMargins mg = contentsMargins();
        const int left   = rect.left() + mg.left();
        const int avail  = qMax(0, rect.width() - mg.left() - mg.right());
        const int gap    = spacing();   // 同一行里「项与项」
        const int vGap   = rowGap();    // 「行与行」

        int  y          = rect.top() + mg.top();
        int  x          = left;
        int  lineH      = 0;      // 当前行里最高的那个
        bool hasContent = false;  // 已经放过东西 → 换行时先补 vGap
        int  bottom     = y;

        for (QLayoutItem *item : m_items) {
            const QSize hint = itemSize(item);   // 刚建的控件 item->sizeHint() 会是 0，见上

            if (isFullRowItem(item)) {
                if (hasContent) y += (lineH > 0 ? lineH + vGap : vGap);   // 收掉上一行
                x = left;
                lineH = 0;
                if (apply) placeItem(item, QRect(x, y, avail, hint.height()));
                y += hint.height();
                bottom = qMax(bottom, y);
                hasContent = true;
                continue;
            }

            // 紧凑项要换行的两种情况：
            //   · 上一项是「整行项」（此时 lineH == 0）：上面那个分支只把 y 推到了行底、**没留间距**，
            //     这里必须补 vGap，否则 input 行和紧跟的按钮会贴在一起（几乎 0 间距）。
            //   · 本行已排了东西、再放一个就超宽。
            if (hasContent && (lineH == 0 || x + hint.width() > left + avail)) {
                y += (lineH > 0 ? lineH + vGap : vGap);
                x = left;
                lineH = 0;
            }
            if (apply) placeItem(item, QRect(x, y, hint.width(), hint.height()));
            x += hint.width() + gap;
            lineH = qMax(lineH, hint.height());
            bottom = qMax(bottom, y + hint.height());
            hasContent = true;
        }

        return QSize(rect.width(), bottom - rect.top() + mg.bottom());
    }

    QList<QLayoutItem *> m_items;
    int m_lastWidth = 0;    // 最近一次真实布局可用的宽度（sizeHint 用它算折行高度）
    int m_rowGap    = -1;   // 行间距；< 0 表示沿用 spacing()
};

static QLayout *createConfigLayout(QWidget *parent)
{
    ConfigFlowLayout *layout = new ConfigFlowLayout(parent);
    layout->setRowGap(kConfigRowGap);   // 行间距单独一个口子，和同行里的「项间距」分开调
    return layout;
}

// ==================== 插件配置区（get_config_list / set_config_value）====================
// 四种类型的插件都可以实现这两个函数（是否实现由插件自己决定，没实现就不显示配置区）：
//   type 0 Python    -> 进程内直接调 py 对象
//   type 1 x64 DLL   -> 进程内直接调导出函数
//   type 2 32 位 DLL -> 共享内存桥发给 miaomiao32.exe（sendData32 12/13）
//   type 3 JS        -> 请 node 宿主转发给对应 Worker（NodePluginManager::callXxx，跨进程阻塞等待）
//   get_config_list()            -> JSON 文本：
//        [{"desc":"配置说明","type":"input|checkbox|button","id":"cookie","default":"xxx"}]
//   set_config_value(id, value)  -> 文本；空串 = 成功，非空 = 失败原因
//        input    -> value 是输入框内容（回车才写回）
//        checkbox -> value 是 "1" / "0"
//        button   -> value 是空串
// 配置项本身不设数量上限，但区域高度按 5 行封顶，多出来的滚动查看。

// 读配置项列表：拿不到（没实现 / 返回空 / 不是 JSON 数组）就返回空串
QString PluginPage::callGetConfigList(int index)
{
    if (index < 0 || index >= m_pluginList.size()) return QString();
    PluginInfo &info = m_pluginList[index];

    if (info.type == 1) {
        if (!info.DLL.get_config_list) return QString();
        const char *ret = info.DLL.get_config_list();
        return ret ? QString::fromUtf8(ret) : QString();
    }
    if (info.type == 2) {

        return sendData32(12,info);
    }
    if (info.type == 3) {
        // JS 插件在 node 宿主的 Worker 线程里，跨进程请求（内部超时 3 秒）
        return NodePluginManager::instance().callGetConfigList(info.uuid);
    }
    if (info.type == 0) {
        try {
            py::gil_scoped_acquire gil;
            py::object fn = info.python.get_config_list;   // 拷贝 / 调用必须在持 GIL 时进行
            if (!fn || !PyCallable_Check(fn.ptr())) return QString();
            py::object ret = fn();
            if (ret.is_none()) return QString();
            return QString::fromStdString(py::str(ret).cast<std::string>());
        } catch (const py::error_already_set &e) {
            qWarning() << "get_config_list 执行异常:" << e.what();
            PyErr_Clear();
            return QString();
        } catch (...) {
            return QString();
        }
    }

    return QString();   // 其它类型 / 未实现 get_config_list
}

// 写配置项：返回空串表示成功，非空是插件给的失败原因
QString PluginPage::callSetConfigValue(int index, const QString &id, const QString &value)
{
    if (index < 0 || index >= m_pluginList.size()) return QStringLiteral("插件索引无效");
    PluginInfo &info = m_pluginList[index];

    if (info.type == 1) {
        if (!info.DLL.set_config_value) return QStringLiteral("插件未实现 set_config_value");
        const QByteArray idUtf8 = id.toUtf8();
        const QByteArray valUtf8 = value.toUtf8();
        const char *ret = info.DLL.set_config_value(idUtf8.constData(), valUtf8.constData());
        return ret ? QString::fromUtf8(ret) : QString();
    }
    if (info.type == 2) {

        return sendData32(13,info,id,value);
    }
    if (info.type == 3) {
        // 返回空串 = 成功；超时 / 未实现会给出可读原因（框架会弹窗提示）
        return NodePluginManager::instance().callSetConfigValue(info.uuid, id, value);
    }
    if (info.type == 0) {
        try {
            py::gil_scoped_acquire gil;
            py::object fn = info.python.set_config_value;
            if (!fn || !PyCallable_Check(fn.ptr())) return QStringLiteral("插件未实现 set_config_value");
            py::object ret = fn(id.toStdString(), value.toStdString());
            if (ret.is_none()) return QString();
            return QString::fromStdString(py::str(ret).cast<std::string>());
        } catch (const py::error_already_set &e) {
            const QString msg = QString::fromUtf8(e.what());
            qWarning() << "set_config_value 执行异常:" << msg;
            PyErr_Clear();
            return QStringLiteral("set_config_value 异常: ") + msg;
        } catch (...) {
            return QStringLiteral("set_config_value 未知异常");
        }
    }

    return QStringLiteral("该类型插件不支持配置");
}

// 统一入口：写回 + 失败提示（成功时插件返回空串，什么都不做）
void PluginPage::applyPluginConfig(int index, const QString &id, const QString &value)
{
    if (index < 0 || index >= m_pluginList.size()) return;
    // 控件是 rebuildConfigPanel 生成时绑定的 index，切走插件后旧控件可能还在事件队列里，
    // 不是当前面板的插件就直接丢弃，免得把值写到别的插件身上
    if (index != currentSelected_index) return;
    const QString err = callSetConfigValue(index, id, value);
    if (err.isEmpty()) return;

    AppendEventLog(QString("[插件配置] %1 项 %2 保存失败：%3")
                       .arg(m_pluginList[index].name, id, err), 0xff);
    showAutoCloseMessageBox("配置保存失败", err);
}

void PluginPage::clearConfigPanel()
{
    if (!configLayout) return;
    while (QLayoutItem *item = configLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            // 只是从布局里摘掉不会让它消失，得先隐藏再排队删除，
            // 否则重建时旧控件会留在原位置和新控件叠在一起
            w->hide();
            w->deleteLater();
        }
        delete item;
    }
}

// 按当前选中的插件重建配置区
void PluginPage::rebuildConfigPanel(int index)
{
    clearConfigPanel();
    if (!configScroll) return;

    if (index < 0 || index >= m_pluginList.size()) {
        configScroll->setVisible(false);
        return;
    }

    const QString json = callGetConfigList(index);
    if (json.trimmed().isEmpty()) {
        configScroll->setVisible(false);
        return;
    }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
        QString err = QString("插件：%1 get_config_list 返回的内容不是 JSON 数组:%2").arg(m_pluginList[index].name,json.left(200))  ;
        qWarning() << err;
        AppendEventLog(err);
        configScroll->setVisible(false);
        return;
    }

    int rows = 0;
    const QJsonArray arr = doc.array();

    // 先过一遍：把「整行项」（input / 未知类型）全部排到前面，紧凑项（checkbox / button）排后面。
    // 这样 JSON 里两种类型混着写时，所有输入框会聚成**一整块**，配合下面的标签列定宽才是一张整齐的表；
    // 否则输入框被复选框打散成几截，对齐了也看着散。同类内部保持 JSON 原有顺序（stable_sort）。
    QVector<QJsonObject> items;
    items.reserve(arr.size());
    for (const QJsonValue &v : arr) items.append(v.toObject());
    std::stable_sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
        auto compact = [](const QJsonObject &o) {
            const QString t = o["type"].toString().trimmed().toLower();
            return t == "checkbox" || t == "button";
        };
        return !compact(a) && compact(b);
    });

    QList<QLabel *> rowLabels;   // 整行项的标签，构建完后统一宽度 → 输入框左边缘才会在一条竖线上

    for (const QJsonObject &obj : items) {
        const QString id = obj["id"].toString();
        if (id.isEmpty()) continue;                       // 没有 id 就没法写回，直接跳过
        const QString desc = obj["desc"].toString(id);
        const QString itemType = obj["type"].toString().trimmed().toLower();
        const QString def = obj["default"].toString();

        if (itemType == "checkbox") {
            // 紧凑项：直接丢进 ConfigFlowLayout，和别的 compact 项并排 / 自动换行
            QCheckBox *box = new QCheckBox(desc, configContainer);
            // 先 setChecked 再 connect，避免初始化时白写一次
            box->setChecked(def == "1" || def.compare("true", Qt::CaseInsensitive) == 0);
            box->setStyleSheet("font-size: 12px; color: #222222;");
            box->setFixedHeight(kConfigRowHeight);
            configLayout->addWidget(box);
            connect(box, &QCheckBox::toggled, this, [this, index, id](bool checked) {
                applyPluginConfig(index, id, checked ? QStringLiteral("1") : QStringLiteral("0"));
            });
            ++rows;
            continue;
        }

        if (itemType == "button") {
            QPushButton *btn = new QPushButton(desc, configContainer);
            btn->setFixedHeight(kConfigRowHeight);
            configLayout->addWidget(btn);
            connect(btn, &QPushButton::clicked, this, [this, index, id]() {
                applyPluginConfig(index, id, QString());   // 按钮不带值，传空串
            });
            ++rows;
            continue;
        }

        // 其余一律按 input 处理：整行项，独占一行
        QWidget *row = new QWidget(configContainer);
        // 同上：无选择器写法会把这一行里的「确认」按钮底色一起抹掉
        row->setObjectName("qcConfigRow");
        row->setStyleSheet("#qcConfigRow { background: transparent; }");
        row->setProperty("qc_config_full_row", true);      // 告诉 ConfigFlowLayout 这行要拉满
        QHBoxLayout *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(3);

        QLabel *lb = new QLabel(desc, row);
        lb->setStyleSheet("font-size: 12px; color: #222222; background: transparent;");
        rowLabels.append(lb);
        // ⚠ 本文件里 `#define QLineEdit PlaceholderLineEdit` 是**活跃**的（appwindow.h 间接引入），
        // PlaceholderLineEdit 只有 (QWidget*) 一个构造函数，不能像 QLineEdit 那样一次传文本
        QLineEdit *edit = new QLineEdit(row);
        edit->setText(def);
        edit->setPlaceholderText("输入后点确认保存");
        edit->setFixedHeight(kConfigRowHeight);
        QPushButton *okBtn = new QPushButton("确认", row);
        okBtn->setFixedHeight(kConfigRowHeight);
        okBtn->setFixedWidth(kConfigConfirmWidth);    // 固定宽，多行 input 的按钮能对齐
        rowLayout->addWidget(lb);
        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(okBtn);
        connect(okBtn, &QPushButton::clicked, this, [this, index, id, edit]() {
            applyPluginConfig(index, id, edit->text());
        });
        // 回车一样提交，两种操作都能用
        connect(edit, &QLineEdit::returnPressed, this, [this, index, id, edit]() {
            applyPluginConfig(index, id, edit->text());
        });

        configLayout->addWidget(row);
        ++rows;
    }

    // ★ 框对齐：所有整行项的标签统一成同一个宽度（取最宽的那个），这样它们右侧的输入框
    //   左边缘就落在同一条竖线上，「确认」按钮本来就是定宽，也自然对齐。
    //   （标签是各自自适应的 widget，宽度天然不一样 —— 不对齐不是因为"没加组件"，而是缺这一列定宽。
    //     多行的这个宽度只在本次构建里算，不改 JSON。）
    if (!rowLabels.isEmpty()) {
        int labelW = 0;
        for (QLabel *lb : rowLabels) {
            lb->ensurePolished();          // 先让 QSS 的 font-size 生效，量出来的宽度才准
            labelW = qMax(labelW, lb->sizeHint().width());
        }
        labelW = qMin(labelW, kConfigLabelMaxWidth);   // 封顶，免得个别超长标签把输入框挤没
        // labelW <= 0 说明没量到（字体还没定型等）→ 宁可不设，也别 setFixedWidth(0) 把标签裁没
        if (labelW > 0) {
            for (QLabel *lb : rowLabels) {
                lb->setFixedWidth(labelW);
                lb->setToolTip(lb->text());    // 万一被裁，悬停还能看全
            }
        }
    }

    if (rows == 0) {
        configScroll->setVisible(false);
        return;
    }

    configScroll->setVisible(true);
    updateConfigScrollHeight();   // 按可用宽度折行后算高度，最多 5 行，多了滚动
    // 控件要等下一个事件循环回合才被 Qt 真正显示出来（addWidget 是排队 show 的），
    // 那时尺寸才算彻底定型，再量一次最稳 —— 切插件时高度就靠这一下纠正
    QTimer::singleShot(0, this, [this]() {
        if (configScroll && configScroll->isVisible()) updateConfigScrollHeight();
    });
}

// 配置区可用宽度。首次构建时滚动区还没完成布局（width 为 0），逐级退回兜底值
int PluginPage::configAvailableWidth() const
{
    int w = configScroll ? configScroll->viewport()->width() : 0;
    // 描述框和配置区在同一个布局列里、宽度一致，比容器自身更可靠
    if (w < 80 && detailDescLabel) w = detailDescLabel->width();
    if (w < 80 && configContainer) w = configContainer->width();
    if (w < 80 && configScroll)    w = configScroll->width();
    if (w < 80 && configScroll && configScroll->parentWidget())
        w = configScroll->parentWidget()->width() - 20;    // 父布局左右各 10
    if (w < 80)                    w = 260;
    return w;
}

// 用当前可用宽度问布局要高度（ConfigFlowLayout::heightForWidth 会按折行结果算）。
// 容器高度也一起钉死：容器的 sizeHint 一旦比视口高，QScrollArea 就会冒出多余的滚动条。
void PluginPage::updateConfigScrollHeight()
{
    if (!configScroll || !configLayout || !configContainer) return;
    if (m_configHeightLock) return;      // 下面会改高度 → 视口跟着 resize，别递归回来
    int contentH = configLayout->heightForWidth(configAvailableWidth());
    // 量不到（理论上不该发生）也不要留着上一次插件的高度 —— 那正是「切插件后只露出第一行」的样子
    if (contentH <= 0) contentH = kConfigRowHeight;
    // 封顶高度要按「行间距」算（不是项间距），否则 5 行时会差出 4*(10-6)=16px，
    // 那点差值就足以让最后一行被压出滚动条。
    const int maxH = kConfigMaxRows * kConfigRowHeight + (kConfigMaxRows - 1) * kConfigRowGap;

    m_configHeightLock = true;
    configContainer->setFixedHeight(contentH);                  // 内容的真实高度
    configScroll->setFixedHeight(qBound(1, contentH, maxH));    // 可视高度：超过上限才滚动
    m_configHeightLock = false;
}

// 视口宽度一变（首次布局 / 拉窗口 / 拖 splitter）折行数就变，高度得跟着重算
bool PluginPage::eventFilter(QObject *obj, QEvent *event)
{
    if (configScroll && obj == configScroll->viewport()
        && event->type() == QEvent::Resize && configScroll->isVisible()) {
        updateConfigScrollHeight();
    }
    return QWidget::eventFilter(obj, event);
}

//更新右边面板
void PluginPage::updateDetailPanel(int index)
{
    if (index < 0 || index >= m_pluginList.size()) {
        detailpathLabel->clear();
        detailDescLabel->clear();
        clearConfigPanel();
        if (configScroll) configScroll->setVisible(false);
        return;
    }
    detailpathLabel->setText(getShortPath(m_pluginList[index].path,32));
    QString mdText = m_pluginList[index].description.isEmpty() ? "暂无说明" : m_pluginList[index].description;
    mdText.replace("\n", "  \n");               // 你之前加的换行处理
    mdText.replace("\n#", "\n# ");              // 换行后的#补空格
    mdText.replace("\r#", "\n# ");              // 换行后的#补空格
    if (mdText.startsWith("#") && mdText.length() > 1 && mdText[1] != ' ')
        mdText.insert(1, ' ');                  // 字符串开头的#补空格
    detailDescLabel->setMarkdown(mdText);

    // 配置项随插件切换一起刷新（没实现 get_config_list 的插件这里会自动隐藏）
    rebuildConfigPanel(index);

    if (m_pluginList[index].enabled) {
        loadBtn->setText("禁用");
        loadBtn->setStyleSheet(
            "QPushButton { background: #e74c3c; color: white; border-radius: 4px; padding: 4px 4px; }"
            "QPushButton:hover { background: #c0392b; }"
            );
    } else {
        loadBtn->setText("启用");
        loadBtn->setStyleSheet(
            "QPushButton { background: #42a5f5; color: white; border-radius: 4px; padding: 4px 4px; }"
            "QPushButton:hover { background: #1e88e5; }"
            );
    }
}



bool PluginPage::disable_Plugin(PluginInfo &info)
{
    if (info.type == 0) {
        safeCall(info.python.onDisable);
    } else if (info.type == 1) {
        if (info.DLL.onDisable) info.DLL.onDisable();
    } else if (info.type == 2) {
        if (sendData32(3, info) != "true") return false;
    } else if (info.type == 3) {
        if (!NodePluginManager::instance().disablePlugin(info.uuid)) return false;
    }

    info.enabled = false;
    return true;
}

bool PluginPage::Reload_Plugin(int index) //32ok
{
    if (index<=-1 || index>m_pluginList.length()) return false;
    AppendEventLog("[重载插件]"+m_pluginList[index].name);
    bool enabled = m_pluginList[index].enabled;
    if (m_pluginList[index].enabled) disable_Plugin(m_pluginList[index]);//调禁用

    uninstall_Plugin(m_pluginList[index]);//里面会重置enabled 变量
    m_pluginList[index].enabled = enabled;
    QString err;
    py::gil_scoped_acquire gil;
    if (m_pluginList[index].type==0)
    {
        err = LoadPlugin_py(m_pluginList[index]);
    }else if(m_pluginList[index].type==1) {
        err = LoadPlugin_DLL(m_pluginList[index]);
    }else if(m_pluginList[index].type==2){
        err = LoadPlugin_DLL32(m_pluginList[index]);
    }else  if (m_pluginList[index].type == 3) {


        QString err = LoadPlugin_js(m_pluginList[index]);
        if (err.isEmpty()) {
            if (m_pluginList[index].enabled) {
                NodePluginManager::instance().enablePlugin(m_pluginList[index].uuid);
            }
            updatePluginItemInUI(index);
            return true;
        }
    }else{
        return false;
    }
    if(err.isEmpty())
    {
        if(m_pluginList[index].id.isEmpty())
        {
            m_pluginList[index].id = m_pluginList[index].name;
        }
        updatePluginItemInUI(index);
        showAutoCloseMessageBox("成功","[重载插件]"+m_pluginList[index].name+" 成功 本信息只显示1秒",1000);
        return true;
    }

    AppendEventLog("[重载插件]"+m_pluginList[index].name+" 失败 错误信息:"+err ,0xff);
    showAutoCloseMessageBox("错误","[重载插件]"+m_pluginList[index].name+" 失败 错误信息:"+ err);
    removePlugin(index);


    return false;
}

bool PluginPage::Enabled_Plugin(int index)
{
    if (index < 0 || index >= m_pluginList.size()) return false;

    PluginInfo &info = m_pluginList[index];

    // 如果已经是启用状态，则调用禁用逻辑（与原来一致）
    if (info.enabled) {
        return disable_Plugin(info);
    }

    // 根据类型调用对应的启用函数
    if (info.type == 0) {
        safeCall(info.python.onEnable);
    } else if (info.type == 1) {
        if (info.DLL.onEnable) info.DLL.onEnable();
    } else if (info.type == 2) {
        if (sendData32(2, info) != "true") return false;
    } else if (info.type == 3) {
        if (!NodePluginManager::instance().enablePlugin(info.uuid)) return false;
    } else {
        return false;
    }

    info.enabled = true;
    return true;
}
bool PluginPage::Enabled_Plugin(PluginInfo &info)
{

    if (info.type == 0) {
        safeCall(info.python.onEnable);
    } else if (info.type == 1) {
        if (info.DLL.onEnable) info.DLL.onEnable();
    } else if (info.type == 2) {
        if (sendData32(2, info) != "true") return false;
    } else if (info.type == 3) {
        if (!NodePluginManager::instance().enablePlugin(info.uuid)) return false;
    } else {
        return false;
    }
    info.enabled = true;
    return true;
}

// 卸载一个原生库插件时，清掉「这次加载复制出来的临时文件」。
//   Windows：共享的 p_tmp/<名>_<时间戳>.dll → 只删这一个文件；
//   Linux/macOS：每个插件专属的 p_tmp/<名>_<时间戳>/ → 删掉整个子目录。
static void removeLoadedPluginFiles(const QString &loadedDllPath)
{
    if (loadedDllPath.isEmpty()) return;

    // Linux/macOS：每次加载有自己的子目录 p_tmp/<名>_<时间戳>/ → 连目录一起删，
    //             否则 p_tmp 里会攒一堆只装着 .so 的空目录。
    // Windows：父目录是**共享的** p_tmp → 只能删那一个文件，绝不能递归（那里面还有别的插件的库）。
    // 这里用「父目录名是不是 p_tmp」判断，而不是平台宏，两个平台的代码因此在任何平台都能被编到
    // —— Windows 上的 cl /Zs 语法自检也就能覆盖到 Linux 那条路。
    const QFileInfo fi(loadedDllPath);
    const QString subDir = fi.absolutePath();
    if (!subDir.isEmpty() && QDir(subDir).dirName() != QLatin1String("p_tmp")) {
        QDir(subDir).removeRecursively();
        return;
    }

    if (QFile::exists(loadedDllPath)) QFile::remove(loadedDllPath);
}

void PluginPage::foruninstall_Plugin()
{
    for (int i = 0; i < m_pluginList.size(); ++i) {
        PluginInfo &info = m_pluginList[i];

        // 32 位插件：退出时只要把「禁用 + 卸载」两条甩过去就行（返回值没人看）。
        // 走同步版的话每个插件最高 2×5 秒（禁用 3 + 卸载 4）全卡在退出流程里，
        // 而且退出期间跑嵌套事件循环不是好主意 —— 所以这里只发不等，然后自己清临时 DLL。
        if (info.type == 2) {
            sendData32NoWait(3, info, QString());
            sendData32NoWait(4, info, QString());
            if (!info.loadedDllPath.isEmpty()) {
                removeLoadedPluginFiles(info.loadedDllPath);
                info.loadedDllPath.clear();
            }
            info.enabled = false;
            continue;
        }

        uninstall_Plugin(info);
    }

    // JS 插件现在共用一个常驻 node 宿主进程：上面对每个插件的 unloadPlugin 只是结束了
    // 它的 Worker，宿主本身还在。退出前必须显式关掉，否则会留一个孤儿 node 进程。
    NodePluginManager::instance().shutdown();
}

bool PluginPage::uninstall_Plugin(PluginInfo &info)
{
    if (info.enabled) {
        disable_Plugin(info);
    }

    if (info.type == 0) {
        safeCall(info.python.onUnload);
        py::gil_scoped_acquire gil;

        info.python.rules.clear();
        info.python.shop.clear();     // 商品里有 py::object（函数引用），卸载时要一起松手
        info.python.instance = py::object();
        info.python.onSet = py::object();
        info.python.onEnable = py::object();
        info.python.onDisable = py::object();
        info.python.onUnload = py::object();
        info.python.getReviewList = py::object();
        info.python.submitReview = py::object();
        info.python.get_config_list = py::object();
        info.python.set_config_value = py::object();

        try {
            py::exec(R"(
import sys, os, gc

# 清理函数（用于热重载）
def clean_plugin(plugin_path):
    # 转为绝对路径，并确保以分隔符结尾
    abs_path = os.path.abspath(plugin_path)
    if not abs_path.endswith(os.sep):
        abs_path += os.sep
    sys.path = [p for p in sys.path if os.path.abspath(p) != abs_path.rstrip(os.sep)]
    to_remove = []
    for mod_name, mod in list(sys.modules.items()):
        if hasattr(mod, '__file__') and mod.__file__:
            file_path = os.path.abspath(mod.__file__)
            if file_path.endswith(('.pyc', '.pyo')):
                file_path = file_path[:-1]
            if file_path.startswith(abs_path):
                to_remove.append(mod_name)

    for name in to_remove:
        del sys.modules[name]

    gc.collect()
    return to_remove

)");
        // 执行清理函数并获取结果

            py::object clean_func = py::module_::import("__main__").attr("clean_plugin");
            py::object result = clean_func(info.path.toStdString());
            qDebug() << "清理完成，删除了" << py::len(result) << "个模块";
        } catch (const py::error_already_set& e) {
            qWarning() << "清理插件模块异常:" << e.what();
        }

    } else if (info.type == 1) {
        if (info.DLL.onUnload) info.DLL.onUnload(); //GO 的插件不能被卸载 但是可以减少引用
        if (info.dllLib) {
            info.dllLib->unload();
            delete info.dllLib;
            info.dllLib = nullptr;
        }
        info.DLL.getReviewList = nullptr;
        info.DLL.submitReview = nullptr;
        info.DLL.get_config_list = nullptr;
        info.DLL.set_config_value = nullptr;
        if (!info.loadedDllPath.isEmpty()) {
            removeLoadedPluginFiles(info.loadedDllPath);
            info.loadedDllPath.clear();
        }
    } else if (info.type == 2) {
        bool ok = (sendData32(4, info) == "true");
        if (!info.loadedDllPath.isEmpty()) {
            removeLoadedPluginFiles(info.loadedDllPath);
            info.loadedDllPath.clear();
        }
        return ok;
    } else if (info.type == 3) {
        return NodePluginManager::instance().unloadPlugin(info.uuid);
    }

    return true;
}

bool PluginPage::uninstall_Plugin(int index)
{

    if (index<=-1 || index>m_pluginList.length()) return false;
    if (QMessageBox::question(this, "确认卸载",QString("确定要卸载插件 '%1' 吗？此操作不可恢复。").arg(m_pluginList[index].name))!= QMessageBox::Yes)return false;
    AppendEventLog("[卸载插件]"+m_pluginList[index].name);
    if(!uninstall_Plugin(m_pluginList[index]))
    {
        showAutoCloseMessageBox("卸载失败","32位加载器没有响应");

        return false;
    }



    removePlugin(index);
    onPluginSelected(currentSelected_index);
    if(currentSelected_index==-1){
        detailpathLabel->clear();
        detailDescLabel->clear();
        clearConfigPanel();
        if (configScroll) configScroll->setVisible(false);
    }
    return true;
}


bool PluginPage::uninstall_Plugin2(int index)
{
    if (index<=-1 || index>m_pluginList.length()) return false;
    AppendEventLog("[卸载插件]"+m_pluginList[index].name);
    if(!uninstall_Plugin(m_pluginList[index]))
    {
        showAutoCloseMessageBox("卸载失败","32位加载器没有响应");
        return false;
    }


    removePlugin(index);
    onPluginSelected(currentSelected_index);
    detailpathLabel->clear();
    detailDescLabel->clear();
    clearConfigPanel();
    if (configScroll) configScroll->setVisible(false);
    return true;
}
QString PluginPage::LoadPlugin(const QString &path,int type,bool enabled,QList<int> &array)  //运行时调用
{
    // Python 插件的 path 必须以分隔符结尾：LoadPlugin_py 用 info.path + "main.py" 拼入口文件，
    // 少一个分隔符就会拼成 "plugins/xxxmain.py"，直接报「main.py 文件不存在」。
    // 这里统一规范化，聊天指令 / WebUI / 市场安装器三个入口就不必各自记得补。
    // （JS 不用管：LoadPlugin_js 走 QDir::absoluteFilePath，cleanPath 会自己归一化）
    QString normPath = path;
    if (type == 0 && !normPath.endsWith('/') && !normPath.endsWith('\\'))
        normPath += '/';

    // 同一路径已经载入过 —— 不当作错误，改走**热重载**。
    // 触发场景：插件市场「重新安装」（下完覆盖再加载）、用户重复选同一目录、WebUI / 聊天指令重复加载。
    // ⚠ 原来这里 return「插件已经 载入请勿重复载入」，市场重装时会被上层当成加载失败，
    //   弹「已解压，但加载失败」—— 文件明明已经换成新版，插件却没生效。
    //   findPluginIndex 是按 path **精确匹配**的，能命中就说明旧记录和这次的新路径指向同一个位置，
    //   所以交给 Reload_Plugin(index)（它用自己的那份记录：先 uninstall_Plugin 清 Python 模块缓存 /
    //   释放原生库临时副本，再按类型重新加载）是安全的。
    int index = findPluginIndex(normPath);
    if (index != -1) {
        if (Reload_Plugin(index)) return QString();
        return normPath + "\n插件热重载失败（旧插件已被卸载，可再次尝试载入）";
    }
    py::gil_scoped_acquire gil;
    PluginInfo info;
    info.path=normPath;
    info.type = type;
    info.enabled = enabled;

    info.appid = std::move(array);
    QString err;
    if (type==0)
    {
        err = LoadPlugin_py(info);
    }else if(type==1) {
        err = LoadPlugin_DLL(info);
        if (err.contains("加载 DLL|SO 失败:"))
        {
            err = LoadPlugin_DLL32(info);
            info.type=2;
        }

    }else if(type==2){
        err = LoadPlugin_DLL32(info);
        info.type=2;
    }else if(type==3){
        err = LoadPlugin_js(info);
        info.type=3;
    }else{
        return QString();
    }
    if(!err.isEmpty()) return err;
    if(info.id.isEmpty())
    {
        info.id = info.name;
    }

    try {
        if(info.enabled){
            Enabled_Plugin(info);
        }
    } catch (const py::error_already_set& e) {
        qWarning() << "Python error in appendPlugin (data append):" << e.what();
        PyErr_Clear();
    } catch (const std::exception& e) {
        qWarning() << "C++ exception in appendPlugin (data append):" << e.what();
    } catch (...) {
        qWarning() << "Unknown exception in appendPlugin (data append)";
    }
    appendPlugin(info);

    return QString();
}

// 文件对话框的起始目录：<程序目录>/plugins。
// ⚠ 目录不存在时 QFileDialog 会**忽略**这个起始路径（回退到上次目录 / 当前目录），
//   所以这里先 mkpath 出来。
static QString pluginDialogStartDir()
{
    const QString d = PluginInstaller::pluginsRootDir();
    QDir().mkpath(d);
    return d;
}

void PluginPage::LoadPlugin_DLL() //按钮
{
    #ifdef Q_OS_WIN
        QString filter = tr("动态链接库 (*.dll)");
    #else
        QString filter = tr("动态链接库 (*.so)");
    #endif

    QString path = QFileDialog::getOpenFileName(this, tr("选择插件"),
                                                pluginDialogStartDir(), filter);
    if (path.isEmpty()) return;


    QString appDir = QCoreApplication::applicationDirPath();

    QString normalizedPath = QDir::fromNativeSeparators(path);
    QString normalizedAppDir = QDir::fromNativeSeparators(appDir) + "/";
    if (normalizedPath.startsWith(normalizedAppDir)) {
        path = normalizedPath.mid(normalizedAppDir.length());
    } else {

        path = normalizedPath;
    }

    QList<int> empty{};
    QString err = LoadPlugin(path, 1, false, empty);
    if (!err.isEmpty()) {
        AppendEventLog("[载入插件] " + path + " 错误信息：" + err, 0xff);
        QMessageBox::about(this, "载入插件", "[载入插件] " + path + " 错误信息：" + err);
        return;
    }
    AppendEventLog("[载入插件] " + path);
    savePlugins();
}
void PluginPage::doLoadPythonPlugin(const QString &dir)
{
    QString pluginDir = dir;
    pluginDir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()) + "/");
    pluginDir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()) + "\\");

    if (!pluginDir.endsWith('/') && !pluginDir.endsWith('\\'))
        pluginDir += "/";

    QList<int> empty{};
    QString err = LoadPlugin(pluginDir, 0, false, empty);
    if (!err.isEmpty()) {
        AppendEventLog("[载入插件] " + pluginDir + " 错误信息：" + err, 0xff);
        QMessageBox::warning(this, "错误", err);
        return;
    }
    savePlugins();
    AppendEventLog("[载入插件] " + pluginDir);
}
// ======================== pip / npm 安装日志：真正的实时输出 ========================
// 子进程（pip / node）的输出是一块块回来的，这里统一做三件事：
//   1. 清洗：pip 的进度条是用 '\r' 原地刷新同一行的，不归一化日志窗里会全是半行；
//   2. 入库：以**纯文本**插进 QPlainTextEdit（QTextEdit 会把 pip 输出里的
//      `requests<4,>=2` 这种当 HTML 标签吃掉），并滚到最新一行；
//   3. 外发：可选回调，把同一份输出推给 WebUI 控制台（每次安装各自一份 InstallCtx::onLog）。
static QString normalizeInstallOutput(const QString &raw)
{
    if (raw.isEmpty()) return raw;
    QString s = raw;
    s.replace("\r\n", "\n");
    s.replace('\r', '\n');
    return s;
}

// 子进程的字节流一律按 UTF-8 解：pip 由下面 PYTHONIOENCODING / PYTHONUTF8 强制成 UTF-8，
// node 本来就是 UTF-8（原来按 fromLocal8Bit 解，npm 的中文 / emoji 会乱码）。
static QString decodeInstallOutput(const QByteArray &raw)
{
    return QString::fromUtf8(raw);
}

static void appendInstallLog(QPlainTextEdit *log, const QString &text, const QColor &color)
{
    if (!log || text.isEmpty()) return;
    QTextCharFormat fmt;
    fmt.setForeground(color);
    QTextCursor cur(log->document());    // ⚠ QTextCursor 只吃 QTextDocument*，不是 QPlainTextEdit*
    cur.movePosition(QTextCursor::End);
    cur.insertText(text, fmt);
    log->setTextCursor(cur);
    log->ensureCursorVisible();          // 光标跟到末尾 → 这个日志区是真·滚动的
}

// 每块日志落地后立刻把窗口刷一次：GUI 线程同时还跑着机器人 / 插件，
// 只 update() 有可能被排到安装结束才真正重绘（现象就是「装完才一次性冒出来」）。
// ExcludeUserInputEvents：不在这里派发用户点击，免得装到一半连点按钮。
static void refreshInstallLogUi()
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

// ==================== 每次安装各持一份状态 ====================
// ⚠ 以前 pip / npm 的状态是成员单指针（m_pipDialog / m_pipLog / m_pipProcess），
//   于是「同一时刻只能有一次安装」成了隐含硬约束，第二次调用直接覆盖句柄：
//     · 旧进程 finished 时拿 `m_pipProcess->workingDirectory()` 去加载**另一个插件**；
//     · 旧进程那个 10 秒 QTimer 还会把 m_pipDialog 置空 → 新弹窗从此收不到任何日志；
//     · 两路输出可能 append 进同一个日志区，串台。
//   桌面（单线程 + 弹窗挡着主界面）看不出问题，但 WebUI 走的是**全局唯一**的
//   pluginPage（core/global.h: extern PluginPage *pluginPage），WebSocket 回包也在 GUI 线程
//   → 完全能在桌面弹窗还没关时插进来第二次安装，一并发就炸。
//   现在每次安装各自 new 一套 dialog / log / process，用 shared_ptr<InstallCtx> 贯穿所有回调，
//   互不干扰，真正可以重复创建。
struct InstallCtx {
    QPointer<QProcess>       proc;   // 这次安装的 pip / npm 进程
    QPointer<QPlainTextEdit> log;    // 这次安装的日志区（弹窗 WA_DeleteOnClose，用户点 X 会连它一起删）
    PluginPage::InstallLogFn onLog;  // 这次安装的外发回调（可空）
};

// 把管道里剩下的输出读干净并落盘 / 外发。
// ⚠ finished 与 readyRead 之间有竞态：进程退出前最后一块输出可能还在管道里，
//   结束回调必须再收一次，否则最后几行（往往是 Successfully installed …）会丢。
static void drainInstallOutput(const InstallCtx &ctx)
{
    if (!ctx.proc) return;

    const QString out = normalizeInstallOutput(decodeInstallOutput(ctx.proc->readAllStandardOutput()));
    if (!out.isEmpty()) {
        appendInstallLog(ctx.log, out, QColor(0x22, 0x22, 0x22));
        if (ctx.onLog) ctx.onLog(out, false, false);
    }

    const QString err = normalizeInstallOutput(decodeInstallOutput(ctx.proc->readAllStandardError()));
    if (!err.isEmpty()) {
        appendInstallLog(ctx.log, err, QColor(0xcc, 0x33, 0x33));
        if (ctx.onLog) ctx.onLog(err, true, false);
    }

    refreshInstallLogUi();
}
void PluginPage::LoadPlugin_Python_pip(const QString &dir, InstallLogFn onLog)
{
    if (!QFile::exists(dir + "/main.py")) {
        showAutoCloseMessageBox("错误", "所选文件夹中缺少 main.py");
        return;
    }

    QFileInfo reqFile(dir + "/requirements.txt");
    if (!reqFile.exists()) {
        QString relDir = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(dir);
        doLoadPythonPlugin(relDir);
        return;
    }

    // ==================== 获取 Python 可执行文件路径 ====================
    QString pythonExe;
#ifdef _WIN32
    pythonExe = QCoreApplication::applicationDirPath() + "/python3.14t.exe";
#else
    // 优先使用程序目录下的 python3.14t
    QString bundled = QCoreApplication::applicationDirPath() + "/python3.14t";
    if (QFile::exists(bundled) && QFileInfo(bundled).isExecutable()) {
        pythonExe = bundled;
    } else {
        pythonExe = QStandardPaths::findExecutable("python3.14t");
        if (pythonExe.isEmpty()) {
            QFileInfo info("/usr/local/bin/python3.14t");
            if (info.exists() && info.isExecutable()) pythonExe = info.absoluteFilePath();
        }
    }
#endif

    if (pythonExe.isEmpty() || !QFile::exists(pythonExe)) {
        QMessageBox::warning(this, "错误", "未找到 Python 解释器");
        return;
    }

    // ==================== 检查 pip ====================
    QProcess checkPip;
    checkPip.start(pythonExe, QStringList() << "-c" << "import pip");
    if (!checkPip.waitForFinished(3000) || checkPip.exitCode() != 0) {
        QMessageBox::information(this, "提示", "pip 未就绪，正在尝试修复...");
        QProcess fixPip;
        fixPip.start(pythonExe, QStringList() << "-m" << "ensurepip" << "--upgrade");
        if (!fixPip.waitForFinished(10000) || fixPip.exitCode() != 0) {
            QMessageBox::information(this, "提示", "修复 pip 失败，请手动检查环境。");
            return;
        }
        QMessageBox::information(this, "提示", "pip 修复成功。");
    }

    // ==================== 安装目标目录（平台自适应） ====================
    QString sitePackages;
#ifdef _WIN32
    sitePackages = QCoreApplication::applicationDirPath() + "/Lib/site-packages";
#else
    sitePackages = QCoreApplication::applicationDirPath() + "/lib/python3.14/site-packages";
#endif
    QDir().mkpath(sitePackages);  // 确保目录存在

    // ==================== 创建日志对话框（这次安装独享一份） ====================
    auto ctx = std::make_shared<InstallCtx>();
    ctx->onLog = onLog;                     // 外部（WebUI 控制台）要的实时回调

    // parent 仍是 this（多开和挂 parent 不冲突：同时存在几个子窗口完全正常），
    // 这样弹窗居中于主窗口、主窗口关掉时也一起收掉
    QDialog *dlgRaw = new QDialog(this);
    dlgRaw->setWindowTitle("正在安装 Python 依赖 (pip install)");
    dlgRaw->resize(640, 420);
    dlgRaw->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<QDialog> dlg(dlgRaw);          // 10 秒后自动关窗时用它兜住「用户提前点了 X」

    QVBoxLayout *layout = new QVBoxLayout(dlgRaw);
    layout->addWidget(new QLabel("安装日志（实时输出）：", dlgRaw));
    // 纯文本 + 可滚动：pip 输出里的 `<4,>=2` 不会被当成 HTML 标签吃掉，几千行也不卡
    ctx->log = new QPlainTextEdit(dlgRaw);
    ctx->log->setReadOnly(true);
    {
        QFont mono("Consolas");
        mono.setStyleHint(QFont::Monospace);
        ctx->log->document()->setDefaultFont(mono);
    }
    layout->addWidget(ctx->log.data(), 1);

    QPushButton *closeBtn = new QPushButton("关闭", dlgRaw);
    connect(closeBtn, &QPushButton::clicked, dlgRaw, &QDialog::close);
    layout->addWidget(closeBtn);
    dlgRaw->show();

    // ==================== 启动 pip 进程 ====================
    ctx->proc = new QProcess(this);
    ctx->proc->setWorkingDirectory(dir);

    // 继承系统环境变量（代理等），另外强制子进程「无缓冲 + UTF-8」：
    // pip 的 stdout/stderr 接在管道上（isatty()=false）默认是**块缓冲**的，
    // 关掉它日志才会一行行实时冒出来（否则表现就是「装完才一次性刷出来」）。
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    ctx->proc->setProcessEnvironment(env);

    // 三个回调都只认**自己那份 ctx** —— 同时装多个也互不串台
    connect(ctx->proc, &QProcess::readyReadStandardOutput, this, [ctx]() {
        if (!ctx->proc) return;
        const QString out = normalizeInstallOutput(decodeInstallOutput(ctx->proc->readAllStandardOutput()));
        if (out.isEmpty()) return;
        appendInstallLog(ctx->log, out, QColor(0x22, 0x22, 0x22));
        if (ctx->onLog) ctx->onLog(out, false, false);
        refreshInstallLogUi();
    });
    connect(ctx->proc, &QProcess::readyReadStandardError, this, [ctx]() {
        if (!ctx->proc) return;
        const QString err = normalizeInstallOutput(decodeInstallOutput(ctx->proc->readAllStandardError()));
        if (err.isEmpty()) return;
        appendInstallLog(ctx->log, err, QColor(0xcc, 0x33, 0x33));
        if (ctx->onLog) ctx->onLog(err, true, false);
        refreshInstallLogUi();
    });
    connect(ctx->proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, ctx, dlg](int exitCode, QProcess::ExitStatus status) {
        // ⚠ 目录必须从**这次安装自己的进程**上取。以前从成员取，多开时会拿着别人的
        //   目录去 doLoadPythonPlugin —— 装完加载的是另一个插件。
        const QString absDir = ctx->proc ? ctx->proc->workingDirectory() : QString();

        drainInstallOutput(*ctx);               // 先把管道里剩下的输出收干净，再报结果

        const bool success = (status == QProcess::NormalExit && exitCode == 0);
        const QString resultMsg = success ? QStringLiteral("pip install 成功完成！")
                                          : QString("pip install 失败 (退出码: %1)").arg(exitCode);
        appendInstallLog(ctx->log, resultMsg, success ? QColor(0x15, 0x65, 0xc0) : QColor(0xcc, 0x33, 0x33));
        if (ctx->onLog) ctx->onLog(resultMsg, !success, true);   // isFinal：WebUI 收到就可以关弹窗了
        ctx->onLog = PluginPage::InstallLogFn();                 // 装完了，回调不再需要

        // 这次安装的进程用完就扔，重复安装不会越攒越多
        if (ctx->proc) ctx->proc->deleteLater();
        ctx->proc = nullptr;

        const QString relDir = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(absDir);
        doLoadPythonPlugin(relDir);

        if (success) {
            // 成功：延迟 10 秒自动关掉日志窗，留时间让用户看完安装输出。
            // dlg 是 QPointer —— 用户在这 10 秒里提前点 X 也不会变成野指针。
            appendInstallLog(ctx->log, "\n安装已完成，窗口将在 10 秒后自动关闭...", QColor(0x66, 0x66, 0x66));
            QTimer::singleShot(10000, this, [dlg]() {
                if (dlg) dlg->close();          // WA_DeleteOnClose → 自动 delete
            });
        } else {
            QMessageBox::warning(this, "安装依赖失败",
                                 "pip install 失败，请检查网络或手动安装依赖。\n"
                                 "您可以手动执行：pip install -r requirements.txt");
            appendInstallLog(ctx->log, "\n安装失败，窗口不会自动关闭，请手动关闭...", QColor(0x66, 0x66, 0x66));
        }
    });

    // ==================== pip install 参数（平台自适应） ====================
    QStringList args;
    // ⚠ "-u" 是 **python 自己**的无缓冲开关，必须排在 "-m" 之前
    //   （放后面会被 pip 当成 --upgrade 解析）
    args << "-u" << "-m" << "pip" << "install" << "-r" << "requirements.txt"

         // 主源：电信镜像
         << "-i" << "https://mirrors.ctyun.cn/pypi/simple/"

         // 备用源
         << "--extra-index-url" << "https://mirrors.bfsu.edu.cn/pypi/simple/"
         << "--extra-index-url" << "https://pypi.tuna.tsinghua.edu.cn/simple"
         << "--extra-index-url" << "https://pypi.mirrors.ustc.edu.cn/simple/"
         << "--extra-index-url" << "https://repo.huaweicloud.com/repository/pypi/simple/"
         << "--extra-index-url" << "https://mirrors.cloud.tencent.com/pypi/simple"
         << "--extra-index-url" << "https://pypi.doubanio.com/simple/"
         << "--extra-index-url" << "https://pypi.org/simple"

         // 信任所有源
         << "--trusted-host" << "mirrors.ctyun.cn"
         << "--trusted-host" << "mirrors.bfsu.edu.cn"
         << "--trusted-host" << "pypi.tuna.tsinghua.edu.cn"
         << "--trusted-host" << "pypi.mirrors.ustc.edu.cn"
         << "--trusted-host" << "repo.huaweicloud.com"
         << "--trusted-host" << "mirrors.cloud.tencent.com"
         << "--trusted-host" << "pypi.doubanio.com"
         << "--trusted-host" << "pypi.org";

    ctx->proc->start(pythonExe, args);

    if (!ctx->proc->waitForStarted(3000)) {
        if (dlg) dlg->close();
        // 没起来就不会有 finished 回调，这里顺手把进程也收掉
        ctx->proc->deleteLater();
        ctx->proc = nullptr;
        QMessageBox::warning(this, "错误", "无法启动 pip 进程，请检查 Python 环境。");
        QString relDir = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(dir);
        doLoadPythonPlugin(relDir);
    }
}
// 「添加-脚本」：Python / JS 共用一个按钮。
// 选完文件夹**先判 main.py，再判 main.js**（Python 优先），命中哪个就走哪条补依赖的入口：
//   main.py → LoadPlugin_Python_pip()（pip install -r requirements.txt）
//   main.js → npmJSpk()             （npm install）
// 两个都没有 → 直接提示，不再往下走（以前分开两个按钮时是各自报「缺少 main.py / main.js」）。
void PluginPage::LoadPlugin_Script()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, "选择插件文件夹（含 main.py 或 main.js）", pluginDialogStartDir());
    if (dir.isEmpty()) return;

    if (QFile::exists(dir + "/main.py")) {
        LoadPlugin_Python_pip(dir);
    } else if (QFile::exists(dir + "/main.js")) {
        npmJSpk(dir);
    } else {
        QMessageBox::warning(this, "错误",
                             "所选文件夹里既没有 main.py 也没有 main.js，不是有效的插件目录。");
    }
}


// 提取加载逻辑为独立函数
void PluginPage::doLoadPlugin(const QString &dir) {
    // 这里的代码是从原 LoadPlugin_JS 中拷贝出来的加载部分
    QString pluginDir = dir;
    // 调整路径（原有逻辑）
    pluginDir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()) + "/");
    pluginDir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()) + "\\");

    QList<int> empty{};
    QString err = LoadPlugin(pluginDir, 3, false, empty);
    if (!err.isEmpty()) {
        AppendEventLog("加载JS插件失败: " + err, 0xff);
        QMessageBox::warning(this, "错误", err);
        return;
    }
    savePlugins();
    AppendEventLog("加载JS插件: " + pluginDir);
}

// npm 那套输出处理已经并进 pip 的 InstallCtx / drainInstallOutput（同一份逻辑，npmJSpk 里内联回调）。
// 入口只有一个：PluginPage::LoadPlugin_Script()（选目录 → main.py / main.js 分流）。
void PluginPage::npmJSpk(const QString &dir, InstallLogFn onLog){
    if (!QFile::exists(dir + "/main.js")) {
        QMessageBox::warning(this, "错误", "所选文件夹中缺少 main.js");
        return;
    }
    // 检查 package.json
    QFileInfo packageJson(dir + "/package.json");
    if (!packageJson.exists()) {
        // 没有依赖，直接加载
        doLoadPlugin(dir);
        return;
    }
    QString npmPath = QStandardPaths::findExecutable("npm");
    if (npmPath.isEmpty()) {
        // 如果找不到，尝试 npm.cmd (Windows)
#ifdef Q_OS_WIN
        npmPath = QStandardPaths::findExecutable("npm.cmd");
#endif
    }
    if (npmPath.isEmpty()) {
        QMessageBox::warning(this, "错误", "未找到 npm，请确保 Node.js 已安装并配置 PATH。 如果你从来没安装node.js 请打开下崽器安装");
        // 注意：这里 dialog 还没创建，不能 close()，否则是空指针解引用直接崩

        doLoadPlugin(dir);
        return;
    }

    // ==================== 创建日志对话框（这次安装独享一份，见上方 InstallCtx 注释） ====================
    auto ctx = std::make_shared<InstallCtx>();
    ctx->onLog = onLog;                     // 外部（WebUI 控制台）要的实时回调

    QDialog *dlgRaw = new QDialog(this);    // parent 仍是 this（多开无冲突，见上方注释）
    dlgRaw->setWindowTitle("正在安装依赖 (npm install)");
    dlgRaw->resize(640, 420);
    dlgRaw->setAttribute(Qt::WA_DeleteOnClose); // 关闭时自动删除
    QPointer<QDialog> dlg(dlgRaw);          // 10 秒后自动关窗时用它兜住「用户提前点了 X」

    QVBoxLayout *layout = new QVBoxLayout(dlgRaw);
    layout->addWidget(new QLabel("安装日志（实时输出）：", dlgRaw));
    // 纯文本 + 可滚动（和 pip 那个日志窗一致）
    ctx->log = new QPlainTextEdit(dlgRaw);
    ctx->log->setReadOnly(true);
    {
        QFont mono("Consolas");
        mono.setStyleHint(QFont::Monospace);
        ctx->log->document()->setDefaultFont(mono);
    }
    layout->addWidget(ctx->log.data(), 1);

    dlgRaw->show();

    // ==================== 创建进程 ====================
    ctx->proc = new QProcess(this);
    ctx->proc->setWorkingDirectory(dir);
    {
        // NO_COLOR：npm 在 FORCE_COLOR 之类的环境变量下会吐 ANSI 转义码，
        // 在纯文本日志里就是一堆 `←[32m`。
        QProcessEnvironment npmEnv = QProcessEnvironment::systemEnvironment();
        npmEnv.insert("NO_COLOR", "1");
        ctx->proc->setProcessEnvironment(npmEnv);
    }

    // 三个回调都只认**自己那份 ctx** —— 同时装多个也互不串台
    connect(ctx->proc, &QProcess::readyReadStandardOutput, this, [ctx]() {
        if (!ctx->proc) return;
        const QString out = normalizeInstallOutput(decodeInstallOutput(ctx->proc->readAllStandardOutput()));
        if (out.isEmpty()) return;
        appendInstallLog(ctx->log, out, QColor(0x22, 0x22, 0x22));
        if (ctx->onLog) ctx->onLog(out, false, false);
        refreshInstallLogUi();
    });
    connect(ctx->proc, &QProcess::readyReadStandardError, this, [ctx]() {
        if (!ctx->proc) return;
        const QString err = normalizeInstallOutput(decodeInstallOutput(ctx->proc->readAllStandardError()));
        if (err.isEmpty()) return;
        appendInstallLog(ctx->log, err, QColor(0xcc, 0x33, 0x33));
        if (ctx->onLog) ctx->onLog(err, true, false);
        refreshInstallLogUi();
    });
    connect(ctx->proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, ctx, dlg](int exitCode, QProcess::ExitStatus status) {
        // ⚠ 目录必须从**这次安装自己的进程**上取（多开时成员早被别人覆盖了）
        const QString absDir = ctx->proc ? ctx->proc->workingDirectory() : QString();

        drainInstallOutput(*ctx);               // 先把管道里剩下的输出收干净，再报结果

        const bool success = (status == QProcess::NormalExit && exitCode == 0);
        const QString resultMsg = success ? QStringLiteral("npm install 成功完成！")
                                          : QString("npm install 失败 (退出码: %1)").arg(exitCode);
        appendInstallLog(ctx->log, resultMsg, success ? QColor(0x15, 0x65, 0xc0) : QColor(0xcc, 0x33, 0x33));
        if (ctx->onLog) ctx->onLog(resultMsg, !success, true);   // isFinal：WebUI 收到就可以关弹窗了
        ctx->onLog = PluginPage::InstallLogFn();                 // 装完了，回调不再需要

        // 这次安装的进程用完就扔，重复安装不会越攒越多
        if (ctx->proc) ctx->proc->deleteLater();
        ctx->proc = nullptr;

        if (success) {
            const QString relDir = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(absDir);
            doLoadPlugin(relDir);

            // 成功：延迟 10 秒自动关掉日志窗（和 pip 那个一致）。
            // dlg 是 QPointer —— 用户在这 10 秒里提前点 X 也不会变成野指针。
            appendInstallLog(ctx->log, "\n安装已完成，窗口将在 10 秒后自动关闭...", QColor(0x66, 0x66, 0x66));
            QTimer::singleShot(10000, this, [dlg]() {
                if (dlg) dlg->close();          // WA_DeleteOnClose → 自动 delete
            });
        } else {
            QMessageBox::warning(this, "安装依赖失败", "npm install 失败，请检查网络或手动安装依赖。");
            appendInstallLog(ctx->log, "\n安装失败，窗口不会自动关闭，请手动关闭...", QColor(0x66, 0x66, 0x66));
        }
    });

    ctx->proc->start(npmPath, QStringList() << "install");

    if (!ctx->proc->waitForStarted(3000)) {
        if (dlg) dlg->close();
        // 没起来就不会有 finished 回调，这里顺手把进程也收掉
        ctx->proc->deleteLater();
        ctx->proc = nullptr;
        doLoadPlugin(dir);
        return;
    }
}


QString PluginPage::LoadPlugin_DLL(PluginInfo &info)
{
    // 1. 确保临时目录存在
    QDir tmpDir("p_tmp");
    if (!tmpDir.exists()) {
        if (!tmpDir.mkpath(".")) return "无法创建临时目录 p_tmp";
    }
    QFileInfo originalFile(info.path);
    if (!originalFile.exists() || !originalFile.isFile())
        return QString("DLL 文件不存在: %1").arg(info.path);
    QString srcAbsPath = originalFile.absoluteFilePath();
    QString baseName = originalFile.completeBaseName();
    QString timestamp = QString::number(QDateTime::currentSecsSinceEpoch());

    #ifdef Q_OS_WIN
        QString ext = ".dll";
    #elif Q_OS_MAC
        QString ext = ".dylib";
    #else
        QString ext = ".so";
    #endif

    QString newFileName = baseName + "_" + timestamp + ext;

    // Windows 和 Linux/macOS 的「依赖怎么找」机制完全不同，所以临时库的摆放也不同：
    //   · Windows：主 DLL 单独复制到**共享的** p_tmp/ 就行 —— 依赖的 DLL 由下面那段
    //              SetDllDirectory 从「插件原目录」里找。
    //   · Linux/macOS：依赖不靠"搜索路径"，而是靠**链接期写进 .so 的 RUNPATH/RPATH** 解析，
    //              其中 $ORIGIN = 这个库**自己所在的目录**（macOS 叫 @loader_path）。
    //              只把主库复制到 p_tmp/，它的 $ORIGIN 就跟着变成 p_tmp/ ——
    //              插件作者明明按规范写好了 $ORIGIN，反倒被这步复制搞失效，依赖照样找不到。
    //              所以给**每次加载**开一个独立子目录，把主库和它同目录下所有 .so* 一起放进去：
    //              $ORIGIN 依然指着这一堆库 → 依赖命中；每个插件一个子目录 → 同名依赖不串台。
    // ⚠ 只覆盖「依赖和主库平级」这种常见情况；依赖放在子目录（libs/ 之类）仍需插件自己写 RUNPATH。
    // 下面用**运行时分支**而不是把它整段 #ifdef 掉：这样两个平台的代码在任一平台都能被编译到，
    // Windows 上的 cl /Zs 语法自检也就能覆盖到 Linux 那条路。
#ifdef Q_OS_WIN
    const bool perPluginSubDir = false;
#else
    const bool perPluginSubDir = true;
#endif

    QString targetDir = tmpDir.absolutePath();
    if (perPluginSubDir) {
        targetDir = tmpDir.absoluteFilePath(baseName + "_" + timestamp);
        if (!QDir().mkpath(targetDir)) return QString("无法创建临时目录: %1").arg(targetDir);
    }
    info.loadedDllPath = QDir(targetDir).filePath(newFileName);

    if (perPluginSubDir) {
        const QDir srcDir(originalFile.absolutePath());
        const QFileInfoList depList = srcDir.entryInfoList(
            QStringList() << "*.so" << "*.so.*" << "*.dylib",
            QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &dep : qAsConst(depList)) {
            if (dep.absoluteFilePath() == srcAbsPath) continue;   // 主库走下面那次改名复制
            QFile::copy(dep.absoluteFilePath(), QDir(targetDir).filePath(dep.fileName()));
        }
    }

    if (!QFile::copy(srcAbsPath, info.loadedDllPath)) return QString("复制 DLL 到临时目录失败: %1 -> %2").arg(info.path, info.loadedDllPath);

    // ==================== 让插件 DLL 能找到「它自己依赖的 DLL」 ====================
    // 插件经常自带第三方依赖（helper.dll / 运行库 / 同作者拆出来的公共库…），
    // 就和插件 DLL 放在同一个文件夹里。
    // 但 LoadLibrary 的默认搜索顺序是：
    //     exe 目录 → 系统目录 → Windows 目录 → 当前工作目录 → PATH
    // **不包含「被加载 DLL 自己所在的目录」**；而且上面刚把主 DLL 复制到了 p_tmp/，
    // 依赖就更无从找起 —— 表现就是报「找不到指定的模块」(错误码 126)，
    // 明明 DLL 就在那儿、插件看起来也没问题。
    // 解决：加载期间把它**原目录**临时加进搜索路径（SetDllDirectory 会把该目录
    // 插到「当前工作目录」之前），加载完立刻还原，进程里其它加载行为不受影响。
#ifdef Q_OS_WIN
    const std::wstring depDirW =
        QDir::toNativeSeparators(originalFile.absolutePath()).toStdWString();
    wchar_t prevDllDir[MAX_PATH] = { 0 };
    const DWORD prevDllDirLen = ::GetDllDirectoryW(MAX_PATH, prevDllDir);   // 0 = 原本没设过
    if (!depDirW.empty())
        ::SetDllDirectoryW(depDirW.c_str());
#else
    // Linux / macOS：.so / .dylib 的依赖搜索靠 RUNPATH/RPATH（`$ORIGIN`）+ LD_LIBRARY_PATH +
    // ldconfig，**运行时改不了**（LD_LIBRARY_PATH 只在进程启动时读一次）。
    // 所以 Linux 插件要在**链接时**写 -Wl,-rpath,'$ORIGIN'，依赖才会从插件目录里找。
#endif

    QLibrary* lib = new QLibrary(info.loadedDllPath);
    const bool dllLoaded = lib->load();

#ifdef Q_OS_WIN
    // 还原。注意这是 API 自身的设计、不是本代码的副作用：只要调用过一次
    // SetDllDirectory，进程的 DLL 搜索路径里就不再包含「当前工作目录」
    // —— 这是更安全的行为，对本程序没有影响。
    // ⚠ 例外：若插件用了 delay-load（延迟加载）的依赖，首次真正调用该依赖时
    //   目录早已还原 → 仍然找不到。这种插件得自己显式 LoadLibrary。
    ::SetDllDirectoryW(prevDllDirLen ? prevDllDir : nullptr);
#endif

    if (!dllLoaded) {
        QString errorMsg = "加载 DLL|SO 失败:: " + lib->errorString();
#ifdef Q_OS_WIN
        // 126 = ERROR_MOD_NOT_FOUND。主 DLL 刚刚复制成功，说明它自己是存在的 ——
        // 加载不上基本就是「它依赖的某个 DLL 不在搜索路径里」。
        const QString es = lib->errorString();
        if (es.contains(QStringLiteral("找不到指定的模块")) || es.contains(QStringLiteral("126"))) {
            errorMsg += "\n\n提示：这个插件可能依赖其它 DLL。请把它依赖的 DLL 一起放进插件目录：\n"
                        + QDir::toNativeSeparators(originalFile.absolutePath());
        }
#endif
        delete lib;                          // 释放 QLibrary 对象
        removeLoadedPluginFiles(info.loadedDllPath);   // 清掉这次复制出来的临时文件（Win：单文件 / Linux：整个子目录）
        info.loadedDllPath.clear();                    // 清除路径（可选）
        return errorMsg;
    }
    info.dllLib = lib;
    if(info.uuid=="") //绑定了ui
    {
        QUuid uuid = QUuid::createUuid();
        info.uuid=uuid.toString(QUuid::WithoutBraces);
    }
    OnMessageFunc set_plugin_path = (OnMessageFunc)lib->resolve("set_plugin_path");
    info.DLL.get_config_list = (OnFunc1)lib->resolve("get_config_list");
    info.DLL.set_config_value = (OnFunc2)lib->resolve("set_config_value");

    // 入口统一是 get_plugin_info（新旧都叫这个名）。
    // ⚠「是不是新版」**只看导出符号**：有 onMessagev3（4 参入口）= 新版，没有 = 旧版。
    //   **不读** get_plugin_info 返回 JSON 里的 sdk 字段 —— 动态库的导出符号本身就是事实，
    //   少一个字段少一处对不上（C / C++ / Go / Rust 全都一样，跟入口名无关）。
    //   Python(0) / JS(3) 没有「入口函数」这个概念（规则直接指向任意 handler），那两类继续用 sdk 字段。
    //   只有新版才允许在「编辑指令」里重命名 —— 老插件收到改名后的输入会自己按原指令匹配、匹配不上。
    info.DLL.getPluginInfo = (GetPluginInfoFunc)lib->resolve("get_plugin_info");
    info.DLL.onMessage  = (OnMessageFunc)lib->resolve("on_message");      // 旧版 1 参
    info.DLL.onMessage2 = (OnMessageFunc2)lib->resolve("onMessagev2");    // 中间版 2 参
    info.DLL.onMessage3 = (OnMessageFunc3)lib->resolve("onMessagev3");    // 新版 4 参
    info.DLL.isV2 = (info.DLL.onMessage3 != nullptr);   // 有 v3 入口 = 新版（才允许指令改名）
    info.DLL.onEnable = (OnFunc0)lib->resolve("on_enable");
    info.DLL.onDisable = (OnFunc0)lib->resolve("on_disable");
    info.DLL.onUnload = (OnFunc0)lib->resolve("on_unload");
    info.DLL.onSet = (OnFunc0)lib->resolve("on_set");
    // 昵称审核接口：加载时就把地址取出来（插件不实现则为 nullptr，不报错）
    info.DLL.getReviewList = (ReviewFetchFunc)lib->resolve(kPluginFuncGetReviewList);
    info.DLL.submitReview  = (ReviewSubmitFunc)lib->resolve(kPluginFuncSubmitReview);
    // 铃铛商店：可选导出 onShop(json, fun, cmd) → 非 0 = 成功。
    //   有它 → fun 当 id 原样回传（Go 版 SDK 拿不到函数地址，全靠它）；没有 → fun 当函数地址直接调。
    info.DLL.onShop = (ShopFunc)lib->resolve("onShop");
    if (!info.DLL.getPluginInfo)
        return info.path + "\n get_plugin_info 函数不存在";
    if (!info.DLL.onMessage && !info.DLL.onMessage2 && !info.DLL.onMessage3)
        return info.path + "\n on_message / onMessagev2 / onMessagev3 一个都没有";
    info.DLL.rules.clear();
    info.DLL.shop.clear();     // 商品也要清：热重载时旧商品不能残留（否则列表里会重复/指向旧函数地址）
    QByteArray uuidBytes = info.uuid.toUtf8();
    uuidBytes.append('\0');
    // 假设 info_str 是 DLL 返回的 JSON 字符串

    if (set_plugin_path) {
        const QString fullPath = info.path;

        const int pos1 = fullPath.lastIndexOf('/');
        const int pos2 = fullPath.lastIndexOf('\\');
        const int pos = qMax(pos1, pos2);

        QString dir;
        if (pos >= 0) {
            dir = fullPath.left(pos + 1); // 包含最后一个 / 或 \n
        } else {
            dir = fullPath; // 没有分隔符时按你的需求处理
        }

        // 统一分隔符为 /，并保证末尾一定是 /
        dir.replace('\\', '/');
        if (!dir.endsWith('/')) {
            dir += '/';
        }

        const std::string dirStd = dir.toStdString();
        set_plugin_path(dirStd.c_str());
    }

    const char* info_str = info.DLL.getPluginInfo(uuidBytes.data(), myCallback);
    if (info_str && *info_str) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray(info_str), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("name")) info.name = obj["name"].toString();
            if (obj.contains("version")) info.version = obj["version"].toString();
            if (obj.contains("author")) info.author = obj["author"].toString();
            if (obj.contains("description")) info.description = obj["description"].toString();
            if (obj.contains("icon")) info.icon = obj["icon"].toString();
            if (obj.contains("id")) info.id = obj["id"].toString();
            if (obj.contains("version2")) info.version_int = obj["version2"].toInt();
            // ⚠ 这里**不再**读 "sdk" 判断新旧 —— 原生库的新旧看导出符号 onMessagev3（见上面 resolve 处）。

            auto parseRuleList = [&](const QString &typeKey, MatchType matchType) {

                QJsonArray ruleList = obj[typeKey].toArray();
                for (const auto & value : std::as_const(ruleList)) {
                    QJsonObject obj2 = value.toObject();
                    QString key = obj2["key"].toString();
                    qint64 fun= obj2["fun"].toDouble();
                    bool caseSensitive = obj2["case_sensitive"].toBool();
                    Rule_Dll rule;
                    rule.type = matchType;
                    rule.key = key;
                    rule.fun = fun;
                    rule.role = ruleRoleFromJson(obj2);   // 插件声明的默认触发权限（没写 = 全部人）
                    rule.caseSensitive = caseSensitive;
                    if (matchType == MatchType::Regex) {
                        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
                        if (!caseSensitive) {
                            options |= QRegularExpression::CaseInsensitiveOption;
                        }
                        rule.regex = QRegularExpression(key, options);
                        if (!rule.regex.isValid()) {
                            qWarning() << "正则表达式无效:" << key << rule.regex.errorString();
                        }
                    } else {
                        rule.regex = QRegularExpression(); // 显式置空
                    }
                    info.DLL.rules.append(rule);
                }

            };
            parseRuleList("equals", MatchType::Equals);
            parseRuleList("startswith", MatchType::StartsWith);
            parseRuleList("endswith", MatchType::EndsWith);
            parseRuleList("contains", MatchType::Contains);
            parseRuleList("regex", MatchType::Regex);
            parseRuleList("event", MatchType::event);

            // ---- 铃铛商店：可选的 "shop" 数组 ----
            // 与规则同一个 fun 口径（8 字节整数 / 函数地址）。没写就当作这插件不出售任何商品。
            info.DLL.shop.clear();
            const QJsonArray shopList = obj["shop"].toArray();
            for (const auto &v : shopList) {
                const QJsonObject item = v.toObject();
                ShopItem_Dll goods;
                goods.key   = item["key"].toString();
                goods.fun   = (qint64)item["fun"].toDouble();
                goods.price = (qint64)item["price"].toDouble();
                // 有 onShop 导出时 fun 只是插件自己的 id（可以为 0）；没有时才必须是真函数地址
                if (goods.key.isEmpty() || (goods.fun <= 0 && !info.DLL.onShop)) {
                    qWarning() << "shop 商品缺少 key / fun，跳过:" << goods.key;
                    continue;
                }
                if (goods.price < 0) goods.price = 0;
                info.DLL.shop.append(goods);
            }
        } else {
            uninstall_Plugin(info);
            return info.path + " get_plugin_info 返回的内容非json 或不是标准json";
        }
    }
    if(info.name.isEmpty()) return info.path + "get_plugin_info 函数中未正确 返回插件名字";

    applySavedRuleConfig(info);   // 套用「编辑指令」里存下的启停 / 重命名
    return QString();
}

// ==================== 往 32 位模块发命令（异步内核 + 同步外壳）====================
// 桥接只提供「写任务槽 → 阻塞等返回值」这一条同步通道（processRequestsA 会卡住调用线程）。
// 卡在 GUI 线程上 = 界面「无响应」，而且全 app 的 QTimer / 网络 / AI 一起停摆 ——
// 32 位模块没在跑的时候，点一下「启用」要白等满 5 秒。
// 所以内核改成异步：写任务槽后立刻返回，用 QTimer 每 100ms 回头看一次有没有返回值，
// 拿到就回调、到点还没等到就超时。外面再套一层同步外壳给原来的调用点用（签名没动）。
// ⚠ 100ms 是 2026-10-09 用户拍板的轮询间隔（对用户无感）。
// ⚠⚠ **多路**（2026-10-09 二版）：每条命令先领一个请求号，把它塞进 JSON 的 "reqid"，
//    易语言回吐时原样带回（_post4 第 3 个参数），桥按号精确路由 —— 于是**多条命令可以同时在飞**。
//    （一版是「同一时间只允许一条在飞」，第二条直接失败；根因是回执不带任何标识。）
static constexpr int kData32PollMs    = 100;    // 轮询间隔
static constexpr int kData32TimeoutMs = 5000;   // 单条命令的总超时（沿用旧值）

void PluginPage::sendData32Async(const QJsonObject &req, int timeoutMs,
                                 const std::function<void(const QString &)> &cb)
{
#ifdef _WIN32
    // bridge 只在 纯白铃32.exe 存在时才创建（main.cpp），没装 32 位模块时直接失败返回
    if (!bridge) { if (cb) cb(QString()); return; }

    // 只发不等：调用点根本不看返回值（比如同步账号列表 / 退出时的一串通知），
    // 连等待者都不登记、一个定时器都不起。
    // ⚠ 仍然带一个哨兵号：它的回执（若易语言也回了）永远匹配不上任何一路，
    //   从而不会被误认成「正在等的那条命令」的结果。
    if (timeoutMs <= 0) {
        QJsonObject r = req;
        r["reqid"] = SharedMemoryBridge::NO_WAIT_REQ_ID;
        bridge->writeResponseToBlock(1, QJsonDocument(r).toJson(QJsonDocument::Compact).constData());
        return;
    }

    // 领号 + 登记等待者（一步完成，原子的）
    const int reqId = bridge->prepareCommandWait();
    if (reqId <= 0) {
        qWarning() << "共享内存: 请求号分配失败，本次 32 位命令放弃";
        if (cb) cb(QString());
        return;
    }

    QJsonObject r = req;
    r["reqid"] = reqId;                        // 易语言回吐时原样带回 → 桥按它路由
    const QByteArray data = QJsonDocument(r).toJson(QJsonDocument::Compact);

    // ⚠ 已经登记了等待者再写任务槽：万一对方回得比 100ms 的轮询还快，结果也已经记在
    //   这一路里，下一跳就能取到，不会丢
    if (!bridge->writeResponseToBlock(1, data.constData())) {
        bridge->endCommandWait(reqId);
        if (cb) cb(QStringLiteral("发送命令失败（共享内存繁忙）"));
        return;
    }

    auto timer   = new QTimer(this);           // 挂在 this 上：页面析构时跟着停
    auto elapsed = std::make_shared<QElapsedTimer>();
    timer->setSingleShot(true);
    elapsed->start();

    connect(timer, &QTimer::timeout, this,
            [this, timer, elapsed, cb, timeoutMs, reqId]() {
        QString out;
        if (bridge && bridge->pollCommandResult(reqId, out)) {
            bridge->endCommandWait(reqId);
            timer->deleteLater();
            if (cb) cb(out);                   // ⚠ 空串也是**合法返回值**（很多命令本来就没输出）
            return;
        }
        if (elapsed->elapsed() >= timeoutMs) { // 超时：注销等待者（迟到的结果会被丢弃，防串包）
            if (bridge) bridge->endCommandWait(reqId);
            timer->deleteLater();
            qWarning() << "共享内存: 等待 32 位命令返回超时 (reqid =" << reqId << ")";
            if (cb) cb(QString());
            return;
        }
        timer->start(kData32PollMs);           // 还没有 → 再等 100ms
    });
    timer->start(kData32PollMs);
#else
    Q_UNUSED(req); Q_UNUSED(timeoutMs); Q_UNUSED(cb);
#endif
}

// 同步外壳：GUI 线程用局部事件循环驱动上面的轮询。
// 等待期间**排除两类事件**，其它照常：
//   · ExcludeUserInputEvents    —— 点击排队，等这次做完再处理。同步壳通常是「点按钮 → 发命令 →
//     等结果」这条链上的，等待期间再派发用户输入会在同一个调用栈里递归进 UI 回调。
//     （多路改造后已经不存在「撞上唯一等待者」的问题，但重入这条仍然要挡。）
//   · ExcludeSocketNotifiers    —— 不去读新到的 socket 数据。WebUI 的收发就在 GUI 线程上
//     （websocketserver 没 moveToThread），不排除的话等待期间会嵌套着处理别人的 WS 请求。
// 剩下重绘、QTimer、跨线程排队调用都正常跑 —— 所以界面不再「无响应」，心跳 / 消息轮询也不停。
// 非 GUI 线程（机器人线程等）没有事件循环可跑 → 保持原来的阻塞等待。
QString PluginPage::sendData32Wait(const QJsonObject &req, int timeoutMs)
{
#ifdef _WIN32
    if (!bridge) return QString();

    if (QThread::currentThread() == qApp->thread()) {
        struct WaitCtx { QEventLoop loop; QString ret; bool got = false; };
        auto ctx = std::make_shared<WaitCtx>();          // 堆上放，回调万一晚到也不会引用到已退栈的局部变量
        sendData32Async(req, timeoutMs, [ctx](const QString &r) {
            ctx->ret = r;
            ctx->got = true;
            ctx->loop.quit();
        });
        if (!ctx->got)                                   // 同步就失败（没桥 / 抢不到等待者）时别再 exec
            ctx->loop.exec(QEventLoop::ExcludeUserInputEvents | QEventLoop::ExcludeSocketNotifiers);
        return ctx->got ? ctx->ret : QString();
    }

    // 非 GUI 线程（机器人线程）直接阻塞等：和异步版一样先领号，回执靠号路由
    const int reqId = bridge->prepareCommandWait();
    if (reqId <= 0) return QString();

    QJsonObject r = req;
    r["reqid"] = reqId;
    const QByteArray data = QJsonDocument(r).toJson(QJsonDocument::Compact);
    if (!bridge->writeResponseToBlock(1, data.constData())) {
        bridge->endCommandWait(reqId);
        return QStringLiteral("发送命令失败（共享内存繁忙）");
    }
    return bridge->processRequestsA(reqId, timeoutMs);   // 内部会把这一路注销掉
#else
    Q_UNUSED(req); Q_UNUSED(timeoutMs);
    return QString();
#endif
}

QString PluginPage::sendData32(int type,PluginInfo &info,const QString &appidlist)
{
    QJsonObject reqJson;
    reqJson["type"] = type;                       // 加载插件
    reqJson["path"] = info.path;      // 路径
    reqJson["uuid"] = info.uuid;              // 插件唯一标识（可能为空，由易语言处理）
    reqJson["e"]    = info.enabled;           // 是否启用（bool 型，易语言取逻辑值）
    reqJson["appid"]=appidlist;
    return sendData32Wait(reqJson, kData32TimeoutMs);
}
QString PluginPage::sendData32(int type, PluginInfo &info , const QString &id, const QString &value)
{
    QJsonObject reqJson;
    reqJson["type"] = type;                       // 加载插件

    reqJson["uuid"] = info.uuid;              // 插件唯一标识（可能为空，由易语言处理）
    reqJson["id"] = id;
    reqJson["value"] = value;
    return sendData32Wait(reqJson, kData32TimeoutMs);
}

// 只发不等：调用点不看返回值，那就别白等 5 秒（例：账号勾选时同步账号列表）
void PluginPage::sendData32NoWait(int type, PluginInfo &info, const QString &appidlist)
{
    QJsonObject reqJson;
    reqJson["type"] = type;
    reqJson["path"] = info.path;
    reqJson["uuid"] = info.uuid;
    reqJson["e"]    = info.enabled;
    reqJson["appid"]=appidlist;
    sendData32Async(reqJson, 0, nullptr);
}
QString PluginPage::LoadPlugin_DLL32(PluginInfo &info)
{
    // 1. 确保临时目录存在
    QDir tmpDir("p_tmp");
    if (!tmpDir.exists()) {
        if (!tmpDir.mkpath("."))
            return "无法创建临时目录 p_tmp";
    }
    QFileInfo originalFile(info.path);
    if (!originalFile.exists() || !originalFile.isFile())
        return QString("DLL 文件不存在: %1").arg(info.path);

    QString baseName = originalFile.completeBaseName();
    QString timestamp = QString::number(QDateTime::currentSecsSinceEpoch());
    #ifdef Q_OS_WIN
        QString ext = ".dll";
    #elif Q_OS_MAC
        QString ext = ".dylib";
    #else
        QString ext = ".so";
    #endif
    QString newFileName = baseName + "_" + timestamp + ext;
    info.loadedDllPath = tmpDir.filePath(newFileName);

    if (!QFile::copy(info.path, info.loadedDllPath))
        return QString("复制 DLL 到临时目录失败: %1 -> %2")
            .arg(info.path, info.loadedDllPath);
    if(info.uuid=="")
    {
        QUuid uuid = QUuid::createUuid();
        info.uuid=uuid.toString(QUuid::WithoutBraces);
    }
    QString result = sendData32(1,info);

    if (result.isEmpty())
        return "加载DLL 等待响应超时或返回空";

    // 6. 解析返回的 JSON
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return result;
    QJsonObject obj = doc.object();
    if (obj.contains("error"))
        return obj["error"].toString();

    info.name        = obj["name"].toString();
    info.version     = obj["version"].toString();
    info.author      = obj["author"].toString();
    info.description = obj["description"].toString();
    info.icon        = obj["icon"].toString();
    info.id = obj["id"].toString();
    info.version_int = obj["version2"].toInt();
    info.type=2;
    // 「是不是新版」：32 位（易语言）没有导出符号可查（它压根不是被 LoadLibrary 加载的，
    //   只有固定入口 on_message + 共享内存帧协议），所以只能看**插件信息 JSON 里的 sdk >= 2**。
    //   · 带 sdk（新版）→ 帧里会带上 plugins[].cmd（生效命令词）/text（用户实输），改名才安全
    //   · 没这个字段（老插件）→ 自己拿 d.content 按原指令匹配，改名后它匹配不上 → 静默失效
    info.DLL.isV2 = (obj["sdk"].toInt() >= 2);
    info.DLL.rules.clear();
    auto parseRuleList = [&](const QString &typeKey, MatchType matchType) {

        QJsonArray ruleList = obj[typeKey].toArray();
        for (const auto & value : std::as_const(ruleList)) {
            QJsonObject obj2 = value.toObject();
            QString key = obj2["key"].toString();
            qint64 fun= obj2["fun"].toDouble();
            bool caseSensitive = obj2["case_sensitive"].toBool();
            Rule_Dll rule;
            rule.type = matchType;
            rule.key = key;
            rule.fun = fun;
            rule.role = ruleRoleFromJson(obj2);   // 插件声明的默认触发权限（没写 = 全部人）
            rule.caseSensitive = caseSensitive;
            if (matchType == MatchType::Regex) {
                QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
                if (!caseSensitive) {
                    options |= QRegularExpression::CaseInsensitiveOption;
                }
                rule.regex = QRegularExpression(key, options);
                if (!rule.regex.isValid()) {
                    qWarning() << "正则表达式无效:" << key << rule.regex.errorString();
                }
            } else {
                rule.regex = QRegularExpression(); // 显式置空
            }
            info.DLL.rules.append(rule);
        }

    };
    parseRuleList("equals", MatchType::Equals);
    parseRuleList("startswith", MatchType::StartsWith);
    parseRuleList("endswith", MatchType::EndsWith);
    parseRuleList("contains", MatchType::Contains);
    parseRuleList("regex", MatchType::Regex);
    parseRuleList("event", MatchType::event);
    // ---- 铃铛商店：可选的 "shop" 数组。**32 位/易语言：上架展示，但暂不可购买** ----
    //   照同一口径读进来（key / fun=32 位进程里的函数地址 / price），兑换时框架回「暂时不支持购买」，
    //   不扣费也不调插件（桥接还没打通）。插件作者现在就可以先写，放开时注册格式不用改。
    info.DLL.shop.clear();
    const QJsonArray shopList32 = obj["shop"].toArray();
    for (const auto &v : shopList32) {
        const QJsonObject item = v.toObject();
        ShopItem_Dll goods;
        goods.key   = item["key"].toString();
        goods.fun   = (qint64)item["fun"].toDouble();   // 32 位地址 ≤ 0x7FFFFFFF，double 精确
        goods.price = (qint64)item["price"].toDouble();
        if (goods.key.isEmpty() || goods.fun <= 0) {
            qWarning() << "shop 商品缺少 key / fun，跳过:" << goods.key;
            continue;
        }
        if (goods.price < 0) goods.price = 0;
        info.DLL.shop.append(goods);
    }
    applySavedRuleConfig(info);   // 套用「编辑指令」里存下的启停 / 重命名
    return QString();   // 成功
}

void PluginPage::syncPluginsTo32()
{
    #ifdef _WIN32
    if (!bridge) return;
    QJsonObject cmd;
    cmd["type"] = 10;

    QJsonArray pluginArray;
    for (const auto& p : std::as_const(m_pluginList)) {
        if(p.type!=2) continue;
        QJsonObject plug;

        plug["path"]    = p.loadedDllPath;
        plug["Enable"]  = p.enabled;   // 注意键名首字母大写
        plug["uuid"]    = p.uuid;
        plug["appid"] =joinIntListFast(p.appid,",");
        pluginArray.append(plug);
    }
    cmd["plugin"] = pluginArray;

    // 返回值只是一句「结果说明」，丢进事件日志就行 —— 原来是阻塞等最多 5 秒，
    // 而这个函数是**心跳里每 8 拍就调一次**（mainwindow 的心跳 lambda），
    // 32 位模块没在跑的时候 GUI 线程会周期性卡住。改成异步：发出去，回来了再补日志。
    sendData32Async(cmd, kData32TimeoutMs, [this](const QString &ret) {
        if (ret.isEmpty()) return;
        AppendEventLog(ret);
    });
    #endif
}

QString PluginPage::LoadPlugin_py(PluginInfo &info)
{
    bool isReload = !info.python.rules.isEmpty();
    if (isReload) {
        qDebug() << "热重载插件:" << info.path << "，执行清理旧缓存";

        // 1. 调用旧插件的 on_unload
        if (info.python.onUnload && !info.python.onUnload.is_none()) {
            try {
                info.python.onUnload();
            } catch (const py::error_already_set& e) {
                qWarning() << "on_unload 执行异常:" << e.what();
            }
        }

        // 2. 清空 C++ 侧持有的所有 Python 对象引用

        info.python.rules.clear();
        info.python.shop.clear();     // 铃铛商店：旧商品一并清掉（热重载不能残留在售列表里）
        info.python.instance = py::object();
        info.python.onSet = py::object();
        info.python.onEnable = py::object();
        info.python.onDisable = py::object();
        info.python.onUnload = py::object();
        info.python.getReviewList = py::object();
        info.python.submitReview = py::object();
        info.python.get_config_list = py::object();
        info.python.set_config_value = py::object();

        // 3. 清理 sys.path 中该插件目录（如果还残留），并从 sys.modules 删除该插件所有模块
        py::exec(R"(
import sys, os, gc

# 清理函数（用于热重载）
def clean_plugin(plugin_path):
    # 转为绝对路径，并确保以分隔符结尾
    abs_path = os.path.abspath(plugin_path)
    if not abs_path.endswith(os.sep):
        abs_path += os.sep

    # 从 sys.path 中移除该插件目录（如果存在）
    sys.path = [p for p in sys.path if os.path.abspath(p) != abs_path.rstrip(os.sep)]

    # 收集所有属于该插件的模块（基于 __file__ 路径）
    to_remove = []
    for mod_name, mod in list(sys.modules.items()):
        if hasattr(mod, '__file__') and mod.__file__:
            file_path = os.path.abspath(mod.__file__)
            if file_path.endswith(('.pyc', '.pyo')):
                file_path = file_path[:-1]
            if file_path.startswith(abs_path):
                to_remove.append(mod_name)

    print("=== 热重载删除模块 ===")
    for name in to_remove:
        print("  -", name)

    for name in to_remove:
        del sys.modules[name]

    gc.collect()
    return to_remove

)");
        // 执行清理函数并获取结果
        try {
            py::object clean_func = py::module_::import("__main__").attr("clean_plugin");
            py::object result = clean_func(info.path.toStdString());
            qDebug() << "清理完成，删除了" << py::len(result) << "个模块";
        } catch (const py::error_already_set& e) {
            qWarning() << "清理插件模块异常:" << e.what();
        }
    }

    // 4. 加载插件（使用包结构）
    QString mainPy = info.path + "main.py";
    if (!QFile::exists(mainPy)) {
        return info.path + "main.py 文件不存在";
    }

    try {
        QString moduleName = info.path;
        moduleName.replace('/', '.').replace('\\', '.');
        if (moduleName.endsWith('.')) moduleName.chop(1);
        QString fullModuleName = moduleName + ".main";  // 例如 "plugin.漂流瓶.main"
        py::exec(R"(
# 1. 注册表容器
_plugin_commands = {
    "equals": [],
    "startswith": [],
    "endswith": [],
    "contains": [],
    "regex": [],
    "event": []
}

# 2. 内部注册器
def _register_rule(match_type):
    def decorator(key, case_sensitive=True):
        def wrapper(func):
            _plugin_commands[match_type].append({
                "key": key,
                "fun": func.__name__,
                "case_sensitive": case_sensitive
            })
            return func
        return wrapper
    return decorator

# 3. 开放给开发者使用的简洁装饰器
equals = _register_rule("equals")
startswith = _register_rule("startswith")
endswith = _register_rule("endswith")
contains = _register_rule("contains")
regex = _register_rule("regex")
event = _register_rule("event")
        )");

        py::module_ plugin_module = py::module_::import(fullModuleName.toUtf8().constData());
        py::dict plugin_globals = plugin_module.attr("__dict__");

        // 5. 提取 on_message（入口函数）
        if (plugin_globals.contains("on_message")) {
            info.python.instance = plugin_globals["on_message"];
        }

        // 6. 提取生命周期回调
        auto getCb = [&](const char *name) -> py::object {
            if (plugin_globals.contains(name)) {
                py::object obj = plugin_globals[name];
                return (py::isinstance<py::function>(obj) || PyCallable_Check(obj.ptr())) ? obj : py::object();
            }
            return py::object();
        };
        if (info.uuid.isEmpty()) {
            QUuid uuid = QUuid::createUuid();
            info.uuid = uuid.toString(QUuid::WithoutBraces);
        }
        info.python.onSet = getCb("on_set");
        info.python.onEnable = getCb("on_enable");
        info.python.onDisable = getCb("on_disable");
        info.python.onUnload = getCb("on_unload");

        info.python.get_config_list = getCb("get_config_list");
        info.python.set_config_value = getCb("set_config_value");

        // 昵称审核接口：加载时就取出函数对象（插件不实现则为空对象，不报错）
        info.python.getReviewList = getCb(kPluginFuncGetReviewList);
        info.python.submitReview  = getCb(kPluginFuncSubmitReview);
        // 7. 解析 _plugin_commands（规则注册）
        info.python.rules.clear();
        info.python.shop.clear();     // 铃铛商店：解析前先清空（重载/重复载入都不会叠加）
        py::object set_plugin_path = getCb("set_plugin_path");
        if (set_plugin_path) {
            const std::string dirStd = info.path.toStdString();
            set_plugin_path(dirStd.c_str());
        }


        if (plugin_globals.contains("_plugin_commands") && py::isinstance<py::dict>(plugin_globals["_plugin_commands"])) {
            py::dict commands = plugin_globals["_plugin_commands"].cast<py::dict>();

            auto processList = [&](const char* key, MatchType matchType) {
                if (!commands.contains(key) || !py::isinstance<py::list>(commands[key])) return;

                py::list list = commands[key].cast<py::list>();
                for (auto item : list) {
                    py::dict cmd = item.cast<py::dict>();
                    QString keyStr = QString::fromStdString(cmd["key"].cast<std::string>());
                    QString funName = QString::fromStdString(cmd["fun"].cast<std::string>());

                    py::object funcObj = plugin_globals[py::str(funName.toStdString())];

                   if (!funcObj){
                        qWarning() << "指令/事件函数" << funName << "不存在或不可调用，跳过";
                        continue;
                    }

                    bool caseSensitive = true;
                    if (cmd.contains("case_sensitive")) {
                        caseSensitive = cmd["case_sensitive"].cast<bool>();
                    }

                    // 触发权限：可选字段 "role"（0 全部人 / 1 后台管理 / 2 机器人管理 /
                    // 3 群主 / 4 群管理 / 5 代管）；没写 = 全部人，越界同样退回全部人
                    int role = kRuleRoleAll;
                    if (cmd.contains("role") && !cmd["role"].is_none()) {
                        try {
                            const int r = cmd["role"].cast<int>();
                            if (r >= kRuleRoleOff && r <= 5) role = r;
                        } catch (...) { role = kRuleRoleAll; }
                    }

                    // 构造 Rule 对象
                    Rule rule;
                    rule.type = matchType;
                    rule.key = keyStr;
                    rule.function = funcObj;
                    rule.wantsCmd = pyFuncWantsCmd(funcObj);   // 2 个形参就传「生效命令词」，1 个不传
                    rule.role = role;
                    rule.caseSensitive = caseSensitive;

                    if (matchType == MatchType::Regex) {
                        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
                        if (!caseSensitive) {
                            options |= QRegularExpression::CaseInsensitiveOption;
                        }
                        rule.regex = QRegularExpression(keyStr, options);
                        if (!rule.regex.isValid()) {
                            qWarning() << "正则表达式无效:" << keyStr << rule.regex.errorString();
                        }
                    } else {
                        rule.regex = QRegularExpression(); // 显式置空
                    }

                    info.python.rules.append(rule);
                }
            };

            processList("equals", MatchType::Equals);
            processList("startswith", MatchType::StartsWith);
            processList("endswith", MatchType::EndsWith);
            processList("contains", MatchType::Contains);
            processList("regex", MatchType::Regex);
            processList("event", MatchType::event);
        }

        // 8. 获取插件信息（get_plugin_info）
        if (plugin_globals.contains("get_plugin_info")) {

            try {
                py::dict dict = plugin_globals["get_plugin_info"](py::str(info.uuid.toStdString()));
                if (dict.is_none()) {
                    return QString("执行 %1/main.py 中 get_plugin_info 函数异常：返回空").arg(info.path);
                }
                auto readString = [&](const char* key, QString& target) {
                    if (dict.contains(key) && !dict[key].is_none()) {
                        target = QString::fromStdString(dict[key].cast<std::string>());
                    }
                };
                readString("name", info.name);
                readString("version", info.version);
                readString("author", info.author);
                readString("description", info.description);
                readString("icon", info.icon);
                readString("id", info.id);

                if (dict.contains("version2") && !dict["version2"].is_none()) {
                    info.version_int = dict["version2"].cast<int>();
                }

                // 新版标志：插件信息字典里带 sdk >= 2（老插件没这个键 → isV2 = false，不可改名）
                info.python.isV2 = false;
                if (dict.contains("sdk") && !dict["sdk"].is_none()) {
                    try { info.python.isV2 = (dict["sdk"].cast<long long>() >= 2); }
                    catch (const std::exception &) { info.python.isV2 = false; }
                }

                // 解析规则列表（与原来一致）
                auto parseRuleList = [&](const QString &typeKey, MatchType matchType) {
                    if (dict.contains(py::str(typeKey.toStdString())) && py::isinstance<py::list>(dict[py::str(typeKey.toStdString())])) {
                        py::list ruleList = dict[py::str(typeKey.toStdString())].cast<py::list>();
                        for (py::handle item : ruleList) {
                            if (!py::isinstance<py::dict>(item)) {
                                qWarning() << typeKey << "规则项不是字典，跳过";
                                continue;
                            }
                            py::dict ruleDict = item.cast<py::dict>();

                            QString key;
                            if (ruleDict.contains("key") && !ruleDict["key"].is_none()) {
                                key = QString::fromStdString(ruleDict["key"].cast<std::string>());
                            } else {
                                qWarning() << typeKey << "规则缺少 key，跳过";
                                continue;
                            }

                            QString funName;
                            if (ruleDict.contains("fun") && !ruleDict["fun"].is_none()) {
                                funName = QString::fromStdString(ruleDict["fun"].cast<std::string>());
                            } else {
                                qWarning() << typeKey << "规则缺少 fun，跳过";
                                continue;
                            }

                            py::object funcObj;

                            if (plugin_globals.contains(py::str(funName.toStdString()))) {
                                py::object obj = plugin_globals[py::str(funName.toStdString())];
                                if (py::isinstance<py::function>(obj) || PyCallable_Check(obj.ptr())) {
                                    funcObj = obj;
                                }
                            }



                            if (!funcObj) {
                                qWarning() << "函数" << funName << "不存在或不可调用，跳过该规则";
                                continue;
                            }

                            bool caseSensitive = true;
                            if (ruleDict.contains("case_sensitive") && !ruleDict["case_sensitive"].is_none()) {
                                caseSensitive = ruleDict["case_sensitive"].cast<bool>();
                            }

                            // 触发权限：可选字段 "role"（0 全部人 / 1 后台管理 / 2 机器人管理 /
                            // 3 群主 / 4 群管理 / 5 代管）；没写 = 全部人，越界同样退回全部人
                            int role = kRuleRoleAll;
                            if (ruleDict.contains("role") && !ruleDict["role"].is_none()) {
                                try {
                                    const int r = ruleDict["role"].cast<int>();
                                    if (r >= kRuleRoleOff && r <= 5) role = r;
                                } catch (...) { role = kRuleRoleAll; }
                            }

                            // 构造 Rule 对象
                            Rule rule;
                            rule.type = matchType;
                            rule.key = key;
                            rule.function = funcObj;
                            rule.wantsCmd = pyFuncWantsCmd(funcObj);   // 2 个形参就传「生效命令词」，1 个不传
                            rule.role = role;
                            rule.caseSensitive = caseSensitive;

                            // 仅当类型是 Regex 时初始化正则表达式
                            if (matchType == MatchType::Regex) {
                                QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
                                if (!caseSensitive) {
                                    options |= QRegularExpression::CaseInsensitiveOption;
                                }
                                rule.regex = QRegularExpression(key, options);
                                if (!rule.regex.isValid()) {
                                    qWarning() << "正则表达式无效:" << key << rule.regex.errorString();
                                }
                            } else {
                                // 其他类型保持默认构造（无效），不会使用
                                rule.regex = QRegularExpression();
                            }

                            info.python.rules.append(rule);
                        }
                    }
                };

                // 调用方式不变
                parseRuleList("equals", MatchType::Equals);
                parseRuleList("startswith", MatchType::StartsWith);
                parseRuleList("endswith", MatchType::EndsWith);
                parseRuleList("contains", MatchType::Contains);
                parseRuleList("regex", MatchType::Regex);
                parseRuleList("event", MatchType::event);

                // ---- 铃铛商店：可选的 "shop" 列表 ----
                // 每项 {"key":"商品备注","fun":"main.py 里的函数名","price":500}：
                // fun 是**函数名字符串**（Python 没有函数指针），加载时就从插件模块里解析成可调用对象。
                info.python.shop.clear();
                if (dict.contains("shop") && py::isinstance<py::list>(dict["shop"])) {
                    py::list shopList = dict["shop"].cast<py::list>();
                    for (py::handle item : shopList) {
                        if (!py::isinstance<py::dict>(item)) continue;
                        py::dict goodsDict = item.cast<py::dict>();

                        ShopItem_py goods;
                        if (goodsDict.contains("key") && !goodsDict["key"].is_none())
                            goods.key = QString::fromStdString(goodsDict["key"].cast<std::string>());
                        QString funName;
                        if (goodsDict.contains("fun") && !goodsDict["fun"].is_none())
                            funName = QString::fromStdString(goodsDict["fun"].cast<std::string>());
                        if (goodsDict.contains("price") && !goodsDict["price"].is_none()) {
                            try { goods.price = goodsDict["price"].cast<qint64>(); } catch (...) { goods.price = 0; }
                        }
                        if (goods.price < 0) goods.price = 0;

                        if (goods.key.isEmpty() || funName.isEmpty()) {
                            qWarning() << "shop 商品缺少 key / fun，跳过:" << goods.key;
                            continue;
                        }
                        py::object funcObj;
                        if (plugin_globals.contains(py::str(funName.toStdString()))) {
                            py::object obj = plugin_globals[py::str(funName.toStdString())];
                            if (py::isinstance<py::function>(obj) || PyCallable_Check(obj.ptr()))
                                funcObj = obj;
                        }
                        if (!funcObj) {
                            qWarning() << "shop 商品函数" << funName << "不存在或不可调用，跳过";
                            continue;
                        }
                        goods.function = funcObj;
                        goods.wantsCmd = pyFuncWantsCmd(funcObj);   // 2 个形参 → 传 (消息, cmd)
                        info.python.shop.append(goods);
                    }
                }

            } catch (const py::error_already_set &e) {
                return QString("执行 %1/main.py 中 get_plugin_info 函数异常：%2").arg(info.path, e.what());
            }
        }

        if (info.name.isEmpty()) {
            return info.path + "/main.py 中 get_plugin_info 函数未返回插件名称";
        }

        applySavedRuleConfig(info);   // 套用「编辑指令」里存下的启停 / 重命名
        return QString();

    } catch (const py::error_already_set &e) {
        return QString("%1 错误: %2").arg(info.path, e.what());
    }
}

// ==================== 指令配置（启用类型 / 重命名 / 按机器人）存取 ====================
// 配置以「插件 id」为键，id 为空时退化成插件名，存放在 g_config["plugin_rules"]：
//   { "<插件id或名>": {
//         "rows": [ {"t":匹配方式,"k":注册原指令,"n":新指令,"r":启用类型(-1/0..5)} ... ],  // 默认（全部机器人）
//         "bots": { "<appid>": { "<注册原指令>": {"t":匹配方式,"r":启用类型,"n":新指令} } }  // 每个机器人的覆盖
//     } }
// 分层语义：
//   · "rows"（默认档）**烧进规则对象**（rule.role / rule.newKey），插件载入时套用；
//   · "bots" 覆盖**启用类型 + 新指令名**，匹配时按 ev.appid 现查（botRuleOverrideOf），
//     没配过的机器人直接用规则自带值 → 与改动前逐字节相同、热路径零额外开销。
//     ⚠ "bots" 里的 "n" **可以不存在**（= 跟随默认档的改名）；存在但为空串 = 这个号不改名。
//       这样「默认把 ping 改成 乒乓」时，某个号仍能用原名 ping。
// ⚠ 老配置（值是**数组**）在 PluginPage 构造时由 migrateRuleCfg() 包成 { "rows": [...] }，行为不变。
// ⚠ 行里的 "r" 是 2026-10-15 起的新字段（原先只有 "e":是否启用 bool）。老配置读的时候按
//   "e" 折算：true → 0（全部人可用）、false → -1（停用）；写的时候 **两个都写**（"r" 给新版读，
//   "e" 给降级回去的老版本读）。
// ⚠ 这里只存「差异」：没配过的插件根本不会出现在这张表里。
static inline QString ruleCfgKeyOf(const PluginInfo &info)
{
    return info.id.isEmpty() ? info.name : info.id;
}

// 把配置行套到规则的活体上（按 (匹配方式, 注册原指令) 定位，三套 Rule 结构字段名一致 → 一个模板搞定）
// allowRename = false（旧版插件）时**忽略配置里的新指令名**，防止历史脏值把它改成匹配不上的名字。
template <typename RuleT>
static void applyRowsToRules(QList<RuleT> &rules, const QJsonArray &arr, bool allowRename = true)
{
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const int     t = o.value("t").toInt(-1);
        const QString k = o.value("k").toString();
        if (k.isEmpty()) continue;
        for (RuleT &r : rules) {
            if (static_cast<int>(r.type) != t || r.key != k) continue;
            r.newKey  = allowRename ? o.value("n").toString() : QString();
            // 启用类型：优先读新字段 "r"；老配置只有 "e"(bool) → true=0 全部人 / false=-1 停用
            r.role = o.contains("r") ? o.value("r").toInt(kRuleRoleAll)
                                     : (o.value("e").toBool(true) ? kRuleRoleAll : kRuleRoleOff);
            if (r.type == MatchType::Regex) {
                // 重命名后正则必须按新值重建，否则匹配的还是插件注册时那个老正则
                QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
                if (!r.caseSensitive) opt |= QRegularExpression::CaseInsensitiveOption;
                r.regex = QRegularExpression(r.newKey.isEmpty() ? r.key : r.newKey, opt);
            }
            break;
        }
    }
}

void PluginPage::applySavedRuleConfig(PluginInfo &info)
{
    const QString key = ruleCfgKeyOf(info);
    if (key.isEmpty()) return;
    // 只套「默认」那一档（"rows"）。按机器人的 "bots" 不烧进规则对象 —— 匹配时现查。
    const QJsonArray arr = m_ruleCfg.value(key).toObject().value("rows").toArray();
    if (arr.isEmpty()) return;

    // 旧版插件不允许改名 → 配置里残留的新指令名一律不生效（启用/停用照常套用）。
    // 原生库看 onMessagev3 符号、32 位看 sdk，两者都落在 info.DLL.isV2 上。
    const bool allowRename = (info.type == 2) ? info.DLL.isV2 : true;

    switch (info.type) {
    case 0:          applyRowsToRules(info.python.rules, arr); break;
    case 1: case 2:  applyRowsToRules(info.DLL.rules,    arr, allowRename); break;
    case 3:          applyRowsToRules(info.js.rules,     arr); break;
    default: break;
    }
}

// ---- 「按机器人」的覆盖：热路径缓存 ----
// 配置里 "bots" 是 JSON，匹配每条消息都去翻 QJsonObject 太贵，所以解析成
//   m_botRuleRole: appid → 插件key → 注册原指令 → {匹配方式, 启用类型, 是否覆盖改名, 新指令名}
// 只在「载入配置 / 保存配置」时重建一次（都是用户动作，频率极低）。
void PluginPage::rebuildBotRuleRoles()
{
    QWriteLocker locker(&m_botRoleLock);   // dispatch 在工作线程上读，这里整表重建
    m_botRuleRole.clear();
    for (auto it = m_ruleCfg.constBegin(); it != m_ruleCfg.constEnd(); ++it) {
        const QString pluginKey = it.key();
        const QJsonObject bots = it.value().toObject().value("bots").toObject();
        for (auto b = bots.constBegin(); b != bots.constEnd(); ++b) {
            bool ok = false;
            const int appid = b.key().toInt(&ok);
            if (!ok || appid <= 0) continue;          // 0 = 默认档，不参与覆盖
            auto &dst = m_botRuleRole[appid][pluginKey];
            const QJsonObject ruleObjs = b.value().toObject();
            for (auto r = ruleObjs.constBegin(); r != ruleObjs.constEnd(); ++r) {
                const QJsonObject ro = r.value().toObject();
                BotRuleOverride ov;
                ov.type      = ro.value("t").toInt(0);
                ov.role      = ro.value("r").toInt(kRuleRoleAll);
                ov.hasNewKey = ro.contains("n");       // 有 "n" 才是覆盖（空串 = 这个号不改名）
                ov.newKey    = ro.value("n").toString();
                dst.insert(r.key(), ov);
            }
        }
    }
}

// 某机器人下某条指令的「启用类型 + 新指令名」。没单独配过 → 原样返回 baseRole / baseNewKey。
// ⚠ 每条消息的每个插件每条规则都会走这里，所以：
//   · appid <= 0 / 缓存空 → 直接返回，零分配；
//   · 查表用**规则原键**（不拼字符串，避免热路径上 QString 分配）。
void PluginPage::botRuleOverrideOf(int appid, const QString &pluginKey, int type,
                                   const QString &key, int &role, QString &newKey) const
{
    if (appid <= 0 || m_botRuleRole.isEmpty()) return;   // 无锁快路径（绝大多数消息走这里）
    QReadLocker locker(&m_botRoleLock);
    const auto b = m_botRuleRole.constFind(appid);
    if (b == m_botRuleRole.constEnd()) return;      // 这个机器人完全没配过
    const auto p = b->constFind(pluginKey);
    if (p == b->constEnd()) return;
    const auto r = p->constFind(key);
    if (r == p->constEnd() || r->type != type) return;   // 同名不同匹配方式不串味
    role = r->role;
    if (r->hasNewKey) newKey = r->newKey;

}

QString PluginPage::effectiveNewKey(int appid, const QString &pluginKey, int type,
                                    const QString &key, const QString &baseNewKey) const
{
    QString newKey = baseNewKey;
    int role = kRuleRoleAll;          // 这里不关心启用类型，只为复用同一次查表
    botRuleOverrideOf(appid, pluginKey, type, key, role, newKey);
    return newKey;
}

// ---- 给 RuleEditDialog 用的只读视图 / 提交接口 ----
QList<int> PluginPage::ruleConfigPluginIndexes() const
{
    QList<int> out;
    for (int i = 0; i < m_pluginList.size(); ++i) {
        const PluginInfo &p = m_pluginList[i];
        const bool has = (p.type == 0 && !p.python.rules.isEmpty())
                      || ((p.type == 1 || p.type == 2) && !p.DLL.rules.isEmpty())
                      || (p.type == 3 && !p.js.rules.isEmpty());
        if (has) out.append(i);
    }
    return out;
}

QString PluginPage::ruleConfigPluginLabel(int index) const
{
    if (index < 0 || index >= m_pluginList.size()) return QString();
    const PluginInfo &p = m_pluginList[index];
    return QString("%1  [%2]").arg(p.name, pluginTypeName(p.type));
}

QList<RuleConfigRow> PluginPage::ruleConfigRows(int index, int appid) const
{
    QList<RuleConfigRow> out;
    if (index < 0 || index >= m_pluginList.size()) return out;
    const PluginInfo &p = m_pluginList[index];
    const QString &cfgKey = p.id.isEmpty() ? p.name : p.id;
    // appid > 0 时把该机器人的覆盖叠上去（启用类型 + 新指令名）；
    // 没配过 → 拿到的就是默认值（= 它现在实际跑的样子）
    auto push = [this, &out, &cfgKey, appid](const auto &rules) {
        for (const auto &r : rules) {
            RuleConfigRow row{ static_cast<int>(r.type), r.key, r.newKey, r.role };
            if (appid > 0) {
                int role = r.role;
                QString newKey = r.newKey;
                botRuleOverrideOf(appid, cfgKey, row.type, row.key, role, newKey);
                row.role   = role;
                row.newKey = newKey;
            }
            out.append(row);
        }
    };
    if (p.type == 0)                       push(p.python.rules);
    else if (p.type == 1 || p.type == 2)   push(p.DLL.rules);
    else if (p.type == 3)                  push(p.js.rules);
    return out;
}

// 机器人下拉：第 0 项固定是「默认（全部机器人）」、appid = 0，后面每个账号一条。
QStringList PluginPage::ruleConfigBotLabels() const
{
    QStringList out;
    out << "默认（全部机器人）";
    for (const auto &acc : std::as_const(m_accounts)) {
        if (!acc) continue;
        out << (acc->nickname.isEmpty() ? QString::number(acc->appid_int) : acc->nickname);
    }
    return out;
}

QList<int> PluginPage::ruleConfigBotAppids() const
{
    QList<int> out;
    out << 0;
    for (const auto &acc : std::as_const(m_accounts)) {
        if (!acc) continue;
        out << acc->appid_int;
    }
    return out;
}

bool PluginPage::hasBotRuleConfig(int index, int appid) const
{
    if (appid <= 0 || index < 0 || index >= m_pluginList.size()) return false;
    const PluginInfo &p = m_pluginList[index];
    const QString &cfgKey = p.id.isEmpty() ? p.name : p.id;
    QReadLocker locker(&m_botRoleLock);
    const auto b = m_botRuleRole.constFind(appid);
    if (b == m_botRuleRole.constEnd()) return false;
    return b->contains(cfgKey);
}

// 只有「新版插件」才允许改名。新版标志按插件类型各看各的：
//   · 原生 DLL / SO (1)：**导出符号** onMessagev3 存在即新版（不看入口名，也不看 JSON 里的 sdk）
//   · 32 位/易语言 (2)：它不是 LoadLibrary 加载的，没有导出符号可查 → 看**插件信息 JSON 里的 sdk >= 2**
//   · Python (0)       ：main.py 的 get_plugin_info 返回的 dict 里 "sdk":2
//   · JS (3)           ：main.js 的 get_plugin_info 返回的对象里 sdk:2
//     （Python / JS 没有「入口函数」可看：前者规则直接指向任意 handler，后者固定导出 on_message）
//   老插件 → 只能勾选启用/停用、改不了名（改了名它内部仍按原指令匹配 → 静默失效）。
bool PluginPage::ruleConfigAllowRename(int index) const
{
    if (index < 0 || index >= m_pluginList.size()) return false;
    const PluginInfo &p = m_pluginList[index];
    if (p.type == 0) return p.python.isV2;
    if (p.type == 1) return p.DLL.isV2;
    if (p.type == 2) return p.DLL.isV2;
    if (p.type == 3) return p.js.isV2;
    return true;
}

void PluginPage::applyRuleConfigRows(int index, const QList<RuleConfigRow> &rows, int appid)
{
    if (index < 0 || index >= m_pluginList.size()) return;
    PluginInfo &p = m_pluginList[index];
    const QString key = ruleCfgKeyOf(p);
    if (key.isEmpty()) return;

    QJsonObject cfg = m_ruleCfg.value(key).toObject();

    if (appid <= 0) {
        // ---- 默认档（全部机器人）：整表覆盖 ----
        QJsonArray arr;
        for (const RuleConfigRow &row : rows) {
            QJsonObject o;
            o["t"] = row.type;
            o["k"] = row.key;
            o["n"] = row.newKey;
            o["r"] = row.role;                      // 启用类型（-1 停用 / 0 全部人 / 1..5 限定权限）
            o["e"] = (row.role > kRuleRoleOff);      // 兼容老版本：非停用即「启用」
            arr.append(o);
        }
        cfg["rows"] = arr;
        m_ruleCfg[key] = cfg;

        rebuildBotRuleRoles();
        applySavedRuleConfig(p);   // 默认档是烧进规则对象的 → 立刻对活体生效，不必重载插件
    } else {
        // ---- 单个机器人：存**启用类型 + 新指令名** ----
        // 键 = 插件注册的原指令；值 = { t:匹配方式, r:启用类型, n:新指令名 }
        // 改名按「相对默认的差异」存：跟默认档一样就**不写 "n"**（继续跟随默认，默认以后改了它也跟着变）；
        // 不一样才写 —— 写空串表示「这个号不改名」（默认把 ping 改成乒乓，这个号仍用 ping）。
        QHash<QString, QString> defaultNewKey;
        const QJsonArray defRows = cfg.value("rows").toArray();
        for (const QJsonValue &v : defRows) {
            const QJsonObject o = v.toObject();
            defaultNewKey.insert(o.value("k").toString(), o.value("n").toString());
        }
        const bool allowRename = ruleConfigAllowRename(index);
        QJsonObject one;
        for (const RuleConfigRow &row : rows) {
            QJsonObject o;
            o["t"] = row.type;
            o["r"] = row.role;
            if (allowRename) {
                const QString nk = (row.newKey == row.key) ? QString() : row.newKey;
                if (nk != defaultNewKey.value(row.key)) o["n"] = nk;
            }
            one[row.key] = o;
        }
        QJsonObject bots = cfg.value("bots").toObject();
        bots[QString::number(appid)] = one;
        cfg["bots"] = bots;
        m_ruleCfg[key] = cfg;

        rebuildBotRuleRoles();     // 覆盖是匹配时现查的 → 只需刷新缓存，不用碰规则对象
    }

    savePlugins();   // 落盘
}

// 删掉某机器人的覆盖 → 该机器人重新跟随「默认」。
void PluginPage::clearBotRuleConfig(int index, int appid)
{
    if (appid <= 0 || index < 0 || index >= m_pluginList.size()) return;
    const QString key = ruleCfgKeyOf(m_pluginList[index]);
    if (key.isEmpty()) return;

    QJsonObject cfg = m_ruleCfg.value(key).toObject();
    QJsonObject bots = cfg.value("bots").toObject();
    const QString akey = QString::number(appid);
    if (!bots.contains(akey)) return;

    bots.remove(akey);
    cfg["bots"] = bots;
    m_ruleCfg[key] = cfg;

    rebuildBotRuleRoles();
    savePlugins();
}

void PluginPage::savePlugins() {
    QJsonArray arr;
     for (int i = 0; i < m_pluginList.size(); ++i) {
        QJsonObject obj;
        obj["path"] = m_pluginList[i].path;
        obj["enabled"] =  m_pluginList[i].enabled;
        obj["type"] =  m_pluginList[i].type;
        QJsonArray array;
        for (int i2 = 0; i2 < m_pluginList[i].appid.size(); ++i2) {
            array.append(m_pluginList[i].appid[i2]);
        }
        obj["appid"] = array;
        arr.append(obj);
    }

    g_config["plugins"] = arr;
    g_config["plugin_rules"] = m_ruleCfg;   // 指令的启用 / 重命名配置（按插件 id 存）
    saveConfig();

}

void PluginPage::loadPlugins() {

    const QJsonArray arr = g_config["plugins"].toArray();

    for (const QJsonValue &val : arr) {
        QJsonObject obj = val.toObject();
        QString path = obj["path"].toString();
        if(path.isEmpty()) continue;
        bool enabled = obj["enabled"].toBool(false);
        int type = obj["type"].toInt(0);
        QList<int> array;
        const QJsonArray appidArr = obj["appid"].toArray();
        for (const QJsonValue &v : appidArr) {
            array.append(v.toInt());
        }
        if (type == 0) {
            if (!QDir(path).exists()) continue;

        } else {
            if (!QFile::exists(path)) continue;
        }
        QString err =LoadPlugin(path,type,enabled,array);
        if(!err.isEmpty())
        {
            AppendEventLog("[载入插件] 错误："+err ,0xff);
        }else{
            AppendEventLog("[载入插件] 成功："+path,0x35E496);
        }
    }
}

QString PluginPage::LoadPlugin_js(PluginInfo& info) {
    QString fullPath = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(info.path);
    if (!QDir(fullPath).exists()) return "目录不存在";
    if (!QFile::exists(fullPath + "/main.js")) return "缺少 main.js";

    if (info.uuid.isEmpty()) {
        info.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonObject metadata = NodePluginManager::instance().loadPlugin(fullPath, info.uuid);
    if (metadata.contains("error")) {
        return metadata["error"].toString();
    }

    info.name = metadata["name"].toString();
    info.version = metadata["version"].toString();
    info.author = metadata["author"].toString();
    info.description = metadata["description"].toString();
    info.icon = metadata["icon"].toString();
    info.id = metadata["id"].toString();
    info.version_int = metadata["version2"].toInt();
    info.type = 3;
    // 新版标志：插件元数据 JSON 里带 sdk >= 2（老插件没这个字段 → isV2 = false，不可改名）
    info.js.isV2 = (metadata["sdk"].toInt() >= 2);
    info.js.rules.clear();
    info.js.shop.clear();          // 铃铛商店：上架展示但暂不可购买（重载不叠加）
    auto parseRuleList = [&](const QString &typeKey, MatchType matchType) {

        QJsonArray ruleList = metadata[typeKey].toArray();
        for (const auto & value : std::as_const(ruleList)) {
            QJsonObject obj2 = value.toObject();
            QString key = obj2["key"].toString();
            QString funName= obj2["fun"].toString();
            bool caseSensitive = obj2["case_sensitive"].toBool();
            Rule_js rule;
            rule.type = matchType;
            rule.key = key;
            rule.fun = funName;
            rule.role = ruleRoleFromJson(obj2);   // 插件声明的默认触发权限（没写 = 全部人）
            rule.caseSensitive = caseSensitive;
            if (matchType == MatchType::Regex) {
                QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
                if (!caseSensitive) {
                    options |= QRegularExpression::CaseInsensitiveOption;
                }
                rule.regex = QRegularExpression(key, options);
                if (!rule.regex.isValid()) {
                    qWarning() << "正则表达式无效:" << key << rule.regex.errorString();
                }
            } else {
                rule.regex = QRegularExpression(); // 显式置空
            }
            info.js.rules.append(rule);
        }

    };
    parseRuleList("equals", MatchType::Equals);
    parseRuleList("startswith", MatchType::StartsWith);
    parseRuleList("endswith", MatchType::EndsWith);
    parseRuleList("contains", MatchType::Contains);
    parseRuleList("regex", MatchType::Regex);
    parseRuleList("event", MatchType::event);
    // ---- 铃铛商店：可选的 "shop" 数组。**JS：上架展示，但暂不可购买** ----
    //   fun = main.js 里的函数名（字符串），与 JS 规则同一口径；兑换时框架回「暂时不支持购买」，
    //   不扣费也不调插件（桥接还没打通）。放开时只要在 handleShopCommand 里去掉 buyable = false。
    info.js.shop.clear();
    const QJsonArray shopListJs = metadata["shop"].toArray();
    for (const auto &v : shopListJs) {
        const QJsonObject item = v.toObject();
        ShopItem_js goods;
        goods.key   = item["key"].toString();
        goods.fun   = item["fun"].toString();
        goods.price = (qint64)item["price"].toDouble();
        if (goods.key.isEmpty() || goods.fun.isEmpty()) {
            qWarning() << "shop 商品缺少 key / fun，跳过:" << goods.key;
            continue;
        }
        if (goods.price < 0) goods.price = 0;
        info.js.shop.append(goods);
    }
    applySavedRuleConfig(info);   // 套用「编辑指令」里存下的启停 / 重命名
    return QString();
}
