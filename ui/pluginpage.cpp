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

// ======================== 插件类型徽章（列表卡片 / 详情用） ========================
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

// 「指令」列表弹窗里每条规则的显示文字：改过名 / 被停用都直接标出来
template <typename RuleT>
static QString ruleDisplayText(const RuleT &rule)
{
    QString s = rule.key;
    if (!rule.newKey.isEmpty()) s += " → " + rule.newKey;
    if (!rule.enabled)          s += "（停用）";
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

PluginPage::PluginPage(QWidget *parent) : QWidget(parent)
{
    // 指令配置（勾选启用 / 重命名）要在插件开始载入之前就拿到，载入时才能套用
    m_ruleCfg = g_config.value("plugin_rules").toObject();

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
    addPluginBtn2 = new QPushButton("添加-Python");
    addPluginBtn3 = new QPushButton("添加-JS");


    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addWidget(addPluginBtn);
    btnRow->addWidget(addPluginBtn2);
    btnRow->addWidget(addPluginBtn3);
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
    // 自适应折行布局：input 独占一行，checkbox / button 能并排就并排
    configLayout = createConfigLayout(configContainer);
    configLayout->setContentsMargins(0, 0, 0, 0);

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
        updatePluginItemInUI(currentSelected_index);
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
    connect(addPluginBtn2, &QPushButton::clicked, [this](){ LoadPlugin_Python(); });
    connect(addPluginBtn3, &QPushButton::clicked, [this](){ LoadPlugin_JS(); });


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


    connect(pypip, &QPushButton::clicked, this, [this]() {
        // 1. 选择 requirements.txt 文件
        QString defaultDir = QCoreApplication::applicationDirPath() + "/plugin";
        QString reqPath = QFileDialog::getOpenFileName(
            this,
            "选择 requirements.txt 文件",
            defaultDir,
            "文本文件 (*.txt);;所有文件 (*)"
            );
        if (reqPath.isEmpty()) {
            return;
        }
        QString err = anzpip(reqPath);
        if (!err.isEmpty()) {
            QMessageBox::critical(this, "错误", err);
        }
    });

    connect(tjzl, &QPushButton::clicked, this, [this]() {
        QString text;
        text.reserve(4096);

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
        ScrollTextDialog::show(this, "插件注册指令", text);
    });

    connect(ai_c_j, &QPushButton::clicked,this, [this]() {
        auto *w = new AppWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
    });

    // 编辑指令：打开独立页面，左侧插件列表 + 右侧 [启用][指令名][新指令名][匹配方式]
    connect(bj_zl, &QPushButton::clicked, this, [this]() {

        auto *dlg = new RuleEditDialog(this, 0, this);
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
QString PluginPage::anzpip(const QString &reqPath)
{
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
                         "\"%1\" -m pip install -r \"%2\" Pillow -i https://pypi.tuna.tsinghua.edu.cn/simple --trusted-host pypi.tuna.tsinghua.edu.cn"
                         ).arg(pythonExe, reqPath);

    // 构建完整的 cmd /k 命令，/k 会保持窗口打开
    QString fullCmd = QString(
                          "echo 欢迎使用插件依赖安装工具 & echo 提示： "
                          "& echo   - \"Requirement already satisfied\" 表示库已存在，无需重复下载 "
                          "& echo   - \"Successfully installed\" 表示新库安装成功 "
                          "& echo. & %1 & echo. & echo 安装完成，请检查上述输出，然后关闭此窗口."
                          ).arg(pipCmd);

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

void plug_tji() {
    plugin_n=2;

    for (int i = 0; i < m_pluginList.size(); ++i) {

        if (m_pluginList[i].type == 3 && m_pluginList[i].js.rules.size()==0) plugin_n++;


    }

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

// 插件输出正文里的 [注册原指令](...) → [生效指令](...)（改名后的按钮兜底重定向）
//
// 背景：插件注册的指令被用户改名后，插件内部仍按**注册原名**输出按钮，例如 [ping]()。
// 点它发出去的是原名，而框架此时只按新名匹配（pong）→ 按钮点不动 / 提示指令不存在。
// 这里按「**该插件自己的规则表**」把正文改写成新名：
//   · [ping]()            目标为空 → 改方括号里的文字（点击就是发这段文字）
//   · [点我](ping)        目标就是指令 → 改圆括号里的目标
//   · [ping](https://..) / [x](其他) → 原样不动
// 只取「该 uuid 的插件」+「newKey 非空且 != key（真正改过名）」的规则，所以：
//   · 没改过名的插件 / 关键词匹配（uuid 不在插件表里）→ 空映射 → 原文一字不动（零开销）
//   · 别的插件的同名指令不受影响
QString redirectPluginCmdsMarkdown(const QString &input, const QString &uuid)
{
    if (input.isEmpty() || uuid.isEmpty()) return input;
    if (!input.contains(QLatin1Char('[')) || !input.contains(QLatin1Char('(')))
        return input;                       // 快速排除：正文里根本没有 markdown 链接

    // 1) 收集「注册原名 → 生效新名」
    QHash<QString, QString> renames;
    for (const PluginInfo &p : std::as_const(m_pluginList)) {
        if (p.uuid != uuid) continue;
        auto collect = [&renames](const auto &rules) {
            for (const auto &r : rules) {
                if (r.newKey.isEmpty() || r.newKey == r.key) continue;
                renames.insert(r.key, r.newKey);
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

bool matchRule(const Rule &rule, const MessageEvent &ev) {
    if (!rule.enabled) return false;          // 被停用的指令不参与匹配
    const QString key = ruleMatchKey(rule.key, rule.newKey);
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

        return rule.regex.match(msg).hasMatch();  // const 操作，线程安全
    }
    case MatchType::event:

        return ev.msgType == key;
    }
    return false;
}
bool matchRule2(const Rule_Dll &rule, const MessageEvent &ev) {
    if (!rule.enabled) return false;
    const QString key = ruleMatchKey(rule.key, rule.newKey);
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

        return rule.regex.match(msg).hasMatch();  // const 操作，线程安全
    }
    case MatchType::event:

        return ev.msgType == key;
    }
    return false;
}
bool matchRule3(const Rule_js &rule, const MessageEvent &ev) {
    if (!rule.enabled) return false;
    const QString key = ruleMatchKey(rule.key, rule.newKey);
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

        for (const Rule &rule : std::as_const(p.python.rules)) {
            if (matchRule(rule, msg)) {
                if (!gil) gil.emplace();   // 第一次命中才获取，循环内复用

                // 正文一个字不动（用户实输就在 msg.msg 里）。这条指令的「生效命令词」
                // （改名=新名、没改=原名）作为**第 2 个参数**交给插件，让它去切正文取参数 ——
                // 与原生库 v3 的参数含义一致（那边是 (json, fun, cmd)）。
                // 兼容：老插件只写 1 个形参（def on_x(msg)）→ rule.wantsCmd=false，仍按 1 参调用。
                py::object ret;
                if (rule.wantsCmd) {
                    const QString effCmd = ruleMatchKey(rule.key, rule.newKey);
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


void PluginPage::dispatch_message(const QString &text, MessageEvent &msg)
{

    QByteArray utf8 = text.toUtf8();
    int _32=0;
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
            if(p.DLL.rules.size()>0){
                for (const Rule_Dll &rule : std::as_const(p.DLL.rules)) {
                    if (matchRule2(rule, msg)) {
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
                        one["cmd"]   = ruleMatchKey(rule.key, rule.newKey);
                        one["text"]  = msg.msg;     // 用户实输原文（冗余，正文里也有；插件可自行忽略）
                        plugins32.append(one);
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
            for (const Rule_js &rule : std::as_const(p.js.rules)) {
                if (!matchRule3(rule, msg)) continue;
                hit = true;
                funs.append(rule.fun);
                cmds.append(ruleMatchKey(rule.key, rule.newKey));   // 改名=新名、没改=原名
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
            if (p.DLL.onMessage3) {
                for (const Rule_Dll &rule : std::as_const(p.DLL.rules)) {
                    if (!matchRule2(rule, msg)) continue;

                    // 缓冲区显式存活到调用结束，别写成临时对象的 constData()
                    const QByteArray effCmd = ruleMatchKey(rule.key, rule.newKey).toUtf8(); // 生效命令词（改名=新名）
                    // 返回值 = 插件想让机器人发的话：非空即代发（插件也可以照旧自己调 API 发，两种都行）。
                    // ⚠ 内存所有权：返回的字符串由**插件持有**（静态缓冲 / 字面量），框架只读、绝不释放。
                    const char *ret = p.DLL.onMessage3(utf8.constData(), rule.fun, effCmd.constData());
                    if (ret && *ret) {
                        const QString s = QString::fromUtf8(ret);
                        if (dllReply.isEmpty()) dllReply = s;
                        else dllReply += "\n---\n" + s;
                    }
                }
            } else if (p.DLL.onMessage2) {
                for (const Rule_Dll &rule : std::as_const(p.DLL.rules)) {
                    if (!matchRule2(rule, msg)) continue;

                    p.DLL.onMessage2(utf8.constData(), rule.fun);
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
    if(msg.at_you && msg.subType==0)
        botnomsg(msg.appid,msg.type,msg.groupId,msg.msgId,_32);

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
static const int kConfigRowGap    = 10;   // 「行与行」的间距（垂直）——比项间距大一点，输入框/按钮上下不显挤
static const int kConfigConfirmWidth = 56;  // input 行右侧「确认」按钮宽度

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
    layout->setRowGap(kConfigRowGap);   // 行间距 > 项间距：输入框和下面那排按钮不至于贴在一起
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
        qWarning() << "get_config_list 返回的内容不是 JSON 数组:" << json.left(200);
        configScroll->setVisible(false);
        return;
    }

    int rows = 0;
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
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
        rowLayout->setSpacing(8);

        QLabel *lb = new QLabel(desc, row);
        lb->setStyleSheet("font-size: 12px; color: #222222; background: transparent;");
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
            if (!info.loadedDllPath.isEmpty() && QFile::exists(info.loadedDllPath)) {
                QFile::remove(info.loadedDllPath);
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
        if (info.DLL.onUnload) info.DLL.onUnload();
        if (info.dllLib) {
            info.dllLib->unload();

            delete info.dllLib;
            info.dllLib = nullptr;
        }
        info.DLL.getReviewList = nullptr;
        info.DLL.submitReview = nullptr;
        info.DLL.get_config_list = nullptr;
        info.DLL.set_config_value = nullptr;
        if (!info.loadedDllPath.isEmpty() && QFile::exists(info.loadedDllPath)) {
            QFile::remove(info.loadedDllPath);
            info.loadedDllPath.clear();
        }

    } else if (info.type == 2) {
        bool ok = ( sendData32(4, info) == "true");

        if (!info.loadedDllPath.isEmpty() && QFile::exists(info.loadedDllPath)) {
            QFile::remove(info.loadedDllPath);
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

    int index = findPluginIndex(normPath);
    if(index!=-1) return normPath + "\n插件已经 载入请勿重复载入";
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
    plug_tji();
    return QString();
}

void PluginPage::LoadPlugin_DLL() //按钮
{
    #ifdef Q_OS_WIN
        QString filter = tr("动态链接库 (*.dll)");
    #else
        QString filter = tr("动态链接库 (*.so)");
    #endif

    QString path = QFileDialog::getOpenFileName(this, tr("选择插件"), "", filter);
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
void PluginPage::onPipOutputReady()
{
    if (!m_pipProcess || !m_pipLog) return;
    QString output = QString::fromLocal8Bit(m_pipProcess->readAllStandardOutput());
    m_pipLog->append(output);
    m_pipLog->moveCursor(QTextCursor::End);
}

void PluginPage::onPipErrorReady()
{
    if (!m_pipProcess || !m_pipLog) return;
    QString error = QString::fromLocal8Bit(m_pipProcess->readAllStandardError());
    m_pipLog->append("<font color='red'>" + error + "</font>");
    m_pipLog->moveCursor(QTextCursor::End);
}
void PluginPage::onPipFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_pipDialog) return;

    bool success = (status == QProcess::NormalExit && exitCode == 0);
    QString resultMsg = success ? "pip install 成功完成！" :
                            QString("pip install 失败 (退出码: %1)").arg(exitCode);
    m_pipLog->append("<font color='blue'>" + resultMsg + "</font>");
    QString absDir = m_pipProcess->workingDirectory();
    QString relDir = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(absDir);
    doLoadPythonPlugin(relDir);
    if (success) {
        // 成功：延迟 10 秒自动关掉日志窗，留时间让用户看完安装输出。
        // 用 QPointer 兜住"用户在这 10 秒里自己先把窗口关了"的情况 ——
        // dialog 是 WA_DeleteOnClose 的裸指针成员，关掉之后裸指针就成了野指针。
        m_pipLog->append("\n安装已完成，窗口将在 10 秒后自动关闭...");
        QPointer<QDialog> alive(m_pipDialog);   // 兜住"用户在这 10 秒里提前自己关掉"
        QTimer::singleShot(10000, this, [this, alive]() {
            if (alive) alive->close();          // WA_DeleteOnClose → 自动 delete
            m_pipDialog = nullptr;              // 不管用户有没有提前关，都清掉成员，
            m_pipLog = nullptr;                 // 否则关过的窗口会在成员里留下野指针
        });
    } else {
        // 安装失败，提示用户，也可选择不加载
        QMessageBox::warning(this, "安装依赖失败",
                             "pip install 失败，请检查网络或手动安装依赖。\n"
                             "您可以手动执行：pip install -r requirements.txt");
        // 失败后若想继续加载（依赖可能已存在），可调用 doLoadPythonPlugin，但一般不推荐
        m_pipLog->append("\n安装失败，窗口不会自动关闭，请手动关闭...");
    }
}
void PluginPage::LoadPlugin_Python_pip(const QString &dir)
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

    // ==================== 创建日志对话框 ====================
    m_pipDialog = new QDialog(this);
    m_pipDialog->setWindowTitle("正在安装 Python 依赖 (pip install)");
    m_pipDialog->resize(600, 400);
    m_pipDialog->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(m_pipDialog);
    m_pipLog = new QTextEdit(m_pipDialog);
    m_pipLog->setReadOnly(true);
    m_pipLog->setFontFamily("Consolas");
    layout->addWidget(m_pipLog);

    QPushButton *closeBtn = new QPushButton("关闭", m_pipDialog);
    connect(closeBtn, &QPushButton::clicked, m_pipDialog, &QDialog::close);
    layout->addWidget(closeBtn);
    m_pipDialog->show();

    // ==================== 启动 pip 进程 ====================
    m_pipProcess = new QProcess(this);
    m_pipProcess->setWorkingDirectory(dir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    m_pipProcess->setProcessEnvironment(env);

    connect(m_pipProcess, &QProcess::readyReadStandardOutput,
            this, &PluginPage::onPipOutputReady);
    connect(m_pipProcess, &QProcess::readyReadStandardError,
            this, &PluginPage::onPipErrorReady);
    connect(m_pipProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PluginPage::onPipFinished);

    // ==================== pip install 参数（平台自适应） ====================
    QStringList args;
    args << "-m" << "pip" << "install" << "-r" << "requirements.txt"

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

    m_pipProcess->start(pythonExe, args);

    if (!m_pipProcess->waitForStarted(3000)) {
        m_pipDialog->close();
        QMessageBox::warning(this, "错误", "无法启动 pip 进程，请检查 Python 环境。");
        QString relDir = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(dir);
        doLoadPythonPlugin(relDir);
    }
}
void PluginPage::LoadPlugin_Python()
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择 Python 插件文件夹");
    if (dir.isEmpty()) return;
    LoadPlugin_Python_pip(dir);

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

// npm 输出实时追加到日志文本框
void PluginPage::onNpmOutputReady() {
    if (!m_npmProcess || !m_npmLog) return;
    QString output = QString::fromLocal8Bit(m_npmProcess->readAllStandardOutput());
    m_npmLog->append(output);
    // 自动滚动到底部
    m_npmLog->moveCursor(QTextCursor::End);
}

void PluginPage::onNpmErrorReady() {
    if (!m_npmProcess || !m_npmLog) return;
    QString error = QString::fromLocal8Bit(m_npmProcess->readAllStandardError());
    m_npmLog->append("<font color='red'>" + error + "</font>");
    m_npmLog->moveCursor(QTextCursor::End);
}


// npm 进程结束槽
void PluginPage::onNpmFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_npmDialog) return;

    QString resultMsg;
    bool success = false;
    if (status == QProcess::NormalExit && exitCode == 0) {
        resultMsg = "npm install 成功完成！";
        success = true;
    } else {
        resultMsg = QString("npm install 失败 (退出码: %1)").arg(exitCode);
        success = false;
    }
    m_npmLog->append("<font color='blue'>" + resultMsg + "</font>");

    if (success) {
        //m_npmDialog->close();  // 或 accept()
        QString absDir = m_npmProcess->workingDirectory();
        QString relDir = QDir(QCoreApplication::applicationDirPath()).relativeFilePath(absDir);

        doLoadPlugin(relDir);

        // 成功：延迟 10 秒自动关掉日志窗（和 pip 那个一致）。
        m_npmLog->append("\n安装已完成，窗口将在 10 秒后自动关闭...");
        QPointer<QDialog> alive(m_npmDialog);   // 兜住"用户在这 10 秒里提前自己关掉"
        QTimer::singleShot(10000, this, [this, alive]() {
            if (alive) alive->close();          // WA_DeleteOnClose → 自动 delete
            m_npmDialog = nullptr;              // 不管用户有没有提前关，都清掉成员，
            m_npmLog = nullptr;                 // 否则关过的窗口会在成员里留下野指针
        });
    } else {
        QMessageBox::warning(this, "安装依赖失败", "npm install 失败，请检查网络或手动安装依赖。");
        m_npmLog->append("\n安装失败，窗口不会自动关闭，请手动关闭...");
    }
}
// 主函数
void PluginPage::LoadPlugin_JS() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择 JS 插件文件夹");
    if (dir.isEmpty()) return;


    npmJSpk(dir);


}
void PluginPage::npmJSpk(const QString &dir){
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

    m_npmDialog = new QDialog(this);
    m_npmDialog->setWindowTitle("正在安装依赖 (npm install)");
    m_npmDialog->resize(600, 400);
    m_npmDialog->setAttribute(Qt::WA_DeleteOnClose); // 关闭时自动删除

    QVBoxLayout *layout = new QVBoxLayout(m_npmDialog);
    m_npmLog = new QTextEdit(m_npmDialog);
    m_npmLog->setReadOnly(true);
    m_npmLog->setFontFamily("Consolas");
    layout->addWidget(m_npmLog);

    m_npmDialog->show();

    // 创建进程
    m_npmProcess = new QProcess(this);
    m_npmProcess->setWorkingDirectory(dir);

    connect(m_npmProcess, &QProcess::readyReadStandardOutput, this, &PluginPage::onNpmOutputReady);
    connect(m_npmProcess, &QProcess::readyReadStandardError, this, &PluginPage::onNpmErrorReady);
    connect(m_npmProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PluginPage::onNpmFinished);
    m_npmProcess->start(npmPath, QStringList() << "install");

    if (!m_npmProcess->waitForStarted(3000)) {
        m_npmDialog->close();
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
    info.loadedDllPath = tmpDir.filePath(newFileName);


    if (!QFile::copy(srcAbsPath, info.loadedDllPath)) return QString("复制 DLL 到临时目录失败: %1 -> %2").arg(info.path, info.loadedDllPath);
    QLibrary* lib = new QLibrary(info.loadedDllPath);
    if (!lib->load()) {
        QString errorMsg = "加载 DLL|SO 失败:: " + lib->errorString();
        delete lib;                          // 释放 QLibrary 对象
        QFile::remove(info.loadedDllPath);   // 删除临时文件
        info.loadedDllPath.clear();          // 清除路径（可选）
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
    if (!info.DLL.getPluginInfo)
        return info.path + "\n get_plugin_info 函数不存在";
    if (!info.DLL.onMessage && !info.DLL.onMessage2 && !info.DLL.onMessage3)
        return info.path + "\n on_message / onMessagev2 / onMessagev3 一个都没有";
    info.DLL.rules.clear();
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

                    // 构造 Rule 对象
                    Rule rule;
                    rule.type = matchType;
                    rule.key = keyStr;
                    rule.function = funcObj;
                    rule.wantsCmd = pyFuncWantsCmd(funcObj);   // 2 个形参就传「生效命令词」，1 个不传
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

                            // 构造 Rule 对象
                            Rule rule;
                            rule.type = matchType;
                            rule.key = key;
                            rule.function = funcObj;
                            rule.wantsCmd = pyFuncWantsCmd(funcObj);   // 2 个形参就传「生效命令词」，1 个不传
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

// ==================== 指令配置（勾选启用 / 重命名）存取 ====================
// 配置以「插件 id」为键，id 为空时退化成插件名，存放在 g_config["plugin_rules"]：
//   { "<插件id或名>": [ {"t":匹配方式, "k":注册原指令, "n":新指令, "e":是否启用} ... ] }
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
            r.enabled = o.value("e").toBool(true);
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
    const QJsonArray arr = m_ruleCfg.value(key).toArray();
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

QList<RuleConfigRow> PluginPage::ruleConfigRows(int index) const
{
    QList<RuleConfigRow> out;
    if (index < 0 || index >= m_pluginList.size()) return out;
    const PluginInfo &p = m_pluginList[index];
    auto push = [&out](const auto &rules) {
        for (const auto &r : rules)
            out.append(RuleConfigRow{ static_cast<int>(r.type), r.key, r.newKey, r.enabled });
    };
    if (p.type == 0)                       push(p.python.rules);
    else if (p.type == 1 || p.type == 2)   push(p.DLL.rules);
    else if (p.type == 3)                  push(p.js.rules);
    return out;
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

void PluginPage::applyRuleConfigRows(int index, const QList<RuleConfigRow> &rows)
{
    if (index < 0 || index >= m_pluginList.size()) return;
    PluginInfo &p = m_pluginList[index];
    const QString key = ruleCfgKeyOf(p);
    if (key.isEmpty()) return;

    // 1) 整表覆盖（UI 就是全量提交）
    QJsonArray arr;
    for (const RuleConfigRow &row : rows) {
        QJsonObject o;
        o["t"] = row.type;
        o["k"] = row.key;
        o["n"] = row.newKey;
        o["e"] = row.enabled;
        arr.append(o);
    }
    m_ruleCfg[key] = arr;

    // 2) 立即对活体生效，不必重载插件
    applySavedRuleConfig(p);

    // 3) 落盘
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
    applySavedRuleConfig(info);   // 套用「编辑指令」里存下的启停 / 重命名
    return QString();
}
