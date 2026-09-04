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
#include <QApplication>
#include <qlibrary.h>
#include "appwindow.h"

#include "pluginmarketwindow.h"
#include "global.h"
#include "node_plugin_manager.h"

#include <QListWidget>

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
    setStyleSheet("background: transparent;");

    QHBoxLayout *hLayout = new QHBoxLayout(this);
    hLayout->setContentsMargins(8, 4, 8, 4);

    // 图标
    iconLabel = new QLabel;
    iconLabel->setFixedSize(36, 36);
    iconLabel->setScaledContents(true);
    iconLabel->setObjectName("icon_AAA");
    iconLabel->setStyleSheet("border: 1px solid #89b4fa; border-radius: 2px;");
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
    nameLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #111111;");
    line1->addWidget(nameLabel);
    line1->addStretch();
    vLayout->addLayout(line1);

    // 第二行：作者 | 版本
    QHBoxLayout *line2 = new QHBoxLayout;
    authorLabel = new QLabel(info.author.isEmpty() ? "未知作者" : info.author);
    authorLabel->setStyleSheet("font-size: 12px; color: #111111;");
    versionLabel = new QLabel("v" + info.version);
    versionLabel->setStyleSheet("font-size: 12px; color: #89b4fa; font-weight: bold;");
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

    // ========== 右侧：插件详情（不含账号列表） ==========
    QGroupBox *detailGroup = new QGroupBox("插件详情");
    detailGroup->setStyleSheet(
        "QGroupBox { padding-top: 0px; margin-top: 0px; border: 1px solid #cccccc; border-radius: 4px; }"
        "QGroupBox::title { subcontrol-position: top left; padding: 0px; margin: 0px; top: 10px; }"
        );
    QVBoxLayout *rightMainLayout = new QVBoxLayout(detailGroup);
    rightMainLayout->setSpacing(8);
    rightMainLayout->setContentsMargins(10, 30, 10, 10);

    // ---- 图标 + 插件名（水平布局） ----
    QHBoxLayout *iconNameLayout = new QHBoxLayout;
    detailIconLabel = new QLabel;
    detailIconLabel->setFixedSize(64, 64);
    detailIconLabel->setScaledContents(true);
    detailIconLabel->setStyleSheet("border: 1px solid #222222; border-radius: 0px;");
    iconNameLayout->addWidget(detailIconLabel);
    QFormLayout *formLayout2 = new QFormLayout;


    detailNameLabel = new QLabel;
    detailNameLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #222222;");
    formLayout2->addWidget(detailNameLabel);
    QHBoxLayout *iconNameLayout2 = new QHBoxLayout;
    iconNameLayout2->setSpacing(3);
    ai_b_j = new QPushButton("编辑当前插件");
    ai_b_j->setFixedWidth(120);
    ai_c_j= new QPushButton("Ai生成插件");
    ai_c_j->setFixedWidth(100);

    iconNameLayout2->addWidget(ai_b_j);
    iconNameLayout2->addWidget(ai_c_j);
    formLayout2->addItem(iconNameLayout2);
    iconNameLayout->addLayout(formLayout2);

    rightMainLayout->addLayout(iconNameLayout);

    // ---- 表单信息（类型、版本、作者、路径） ----
    QFormLayout *formLayout = new QFormLayout;
    formLayout->setSpacing(10);
    formLayout->setContentsMargins(0, 0, 0, 0);
    detailTypeLabel = new QLabel;
    detailVersionLabel = new QLabel;
    detailAuthorLabel = new QLabel;
    detailpathLabel = new QLabel;
    formLayout->addRow("类型：", detailTypeLabel);
    formLayout->addRow("版本：", detailVersionLabel);
    formLayout->addRow("作者：", detailAuthorLabel);
    formLayout->addRow("路径：", detailpathLabel);
    rightMainLayout->addLayout(formLayout);

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

    // ---- 操作按钮 ----
    QHBoxLayout *btnLayout = new QHBoxLayout;

    loadBtn = new QPushButton("启用");
    reloadBtn = new QPushButton("重载");
    openDirBtn = new QPushButton("目录");
    uninstallBtn = new QPushButton("卸载");
    setBtn = new QPushButton("设置");
    const int btnWidth = 60;
    loadBtn->setFixedWidth(btnWidth);
    reloadBtn->setFixedWidth(btnWidth);
    openDirBtn->setFixedWidth(btnWidth);
    uninstallBtn->setFixedWidth(btnWidth);
    setBtn->setFixedWidth(btnWidth);


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
            if (plugin.type != 0)  // 仅处理已启用的插件
                continue;

            text.append(QStringLiteral("插件: %1\n").arg(plugin.name));

            if (plugin.python.rules.isEmpty()) {
                text.append("  未注册任何指令\n");
            } else {
                // 1. 按类型分组
                QMap<MatchType, QStringList> groups;
                for (const auto &rule : plugin.python.rules) {
                    groups[rule.type].append(rule.key);
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
        }
        QMessageBox::warning(this,"",text);
        // 将结果显示到界面（请根据实际控件名替换）
        // 例如：ui->textEdit->setPlainText(text);
        // 或 qDebug() << text;
    });

    connect(ai_c_j, &QPushButton::clicked,this, [this]() {
        auto *w = new AppWindow();
        w->setAttribute(Qt::WA_DeleteOnClose);
        w->show();
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

    QString cmdLine = QString(
                          "cmd /c start \"pip install\" cmd /k \"echo 欢迎使用插件依赖安装工具 & echo 提示： "
                          "& echo   - \"Requirement already satisfied\" 表示库已存在，无需重复下载 "
                          "& echo   - \"Successfully installed\" 表示新库安装成功 "
                          "& echo. & \"%1\" -m pip install -r \"%2\" Pillow -i https://pypi.tuna.tsinghua.edu.cn/simple --trusted-host pypi.tuna.tsinghua.edu.cn & echo. & echo 安装完成，请检查上述输出，然后关闭此窗口.\""
                          ).arg(pythonExe, reqPath);


    return QProcess::startDetached(cmdLine)? "" :"无法启动终端窗口，请检查系统环境！";
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
    sendData32(11,info,joinIntListFast(info.appid,","));
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
    plugin_n2=false;
    for (int i = 0; i < m_pluginList.size(); ++i) {

        if (m_pluginList[i].type == 3) plugin_n++;
        if(m_pluginList[i].type!=0 && m_pluginList[i].enabled) plugin_n2= true;

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

bool matchRule(const Rule &rule, const MessageEvent &ev) {
    QString msg = ev.msg;
    switch (rule.type) {
    case MatchType::Equals:
        return rule.caseSensitive ? (msg == rule.key)
                                  : (msg.compare(rule.key, Qt::CaseInsensitive) == 0);
    case MatchType::StartsWith:
        return rule.caseSensitive ? msg.startsWith(rule.key)
                                  : msg.startsWith(rule.key, Qt::CaseInsensitive);
    case MatchType::EndsWith:
        return rule.caseSensitive ? msg.endsWith(rule.key)
                                  : msg.endsWith(rule.key, Qt::CaseInsensitive);
    case MatchType::Contains:
        return rule.caseSensitive ? msg.contains(rule.key)
                                  : msg.contains(rule.key, Qt::CaseInsensitive);
    case MatchType::Regex: {

        return rule.regex.match(msg).hasMatch();  // const 操作，线程安全
    }
    case MatchType::event:

        return ev.type == rule.key;
    }
    return false;
}
void PluginPage::onMessageReceived(MessageEvent &msg, int i) {
    try {
        // 3.14t 下必须持锁，保持原有的 acquire
        py::gil_scoped_acquire gil;

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
                else reply += "\n" + str;
            }
        };

        for (const Rule &rule : std::as_const(m_pluginList[i].python.rules)) {
            if (matchRule(rule, msg)) {
                py::object ret = rule.function(msg); // 如果是async，这里返回协程对象
                process_ret(ret);
            }
        }

        // 只有拼出了同步的 reply 才通过 C++ 发送，异步的交给后台自己跑就行了
        if (!reply.isEmpty()) {
            QQBotClient *client = m_botClients[msg.appid];
            if (client) {
                QString pname = "[" + m_pluginList[i].name + "|%1ms]";
                client->send_msgAsync(msg.type, msg.groupId, pname, reply, msg.msgId);
            }
            return;
        }

    } catch (const std::exception &e) {
        AppendEventLog("[Python] " + m_pluginList[i].name + " 错误: " + e.what(), 0xff);
    } catch (...) {
        AppendEventLog("[Python] " + m_pluginList[i].name + " 未知错误", 0xff);
    }
}
void PluginPage::dispatch_message(const QString &text, MessageEvent &msg)
{
    QByteArray utf8 = text.toUtf8();

    int _32=0;
    for (int i = 0; i < m_pluginList.size(); ++i) {
        if (!m_pluginList[i].enabled) continue;
        if(m_pluginList[i].appid.contains(msg.appid)) continue; //这个插件禁用
        if (m_pluginList[i].type == 0){
            onMessageReceived(msg,i);
            continue;
        }
        if(m_pluginList[i].type == 2)
        {
            _32++;
            continue;
        }else if (m_pluginList[i].type == 3) {
            NodePluginManager::instance().postEventAsync(m_pluginList[i].uuid,"on_message", text);

            continue;
        }
        if(m_pluginList[i].appid.contains(msg.appid)) continue;
        try {
            if (m_pluginList[i].DLL.onMessage) {
                m_pluginList[i].DLL.onMessage(utf8.data());
            }
        } catch (const std::exception &e) {
            AppendEventLog("[DLL] " + m_pluginList[i].name + " on_message: " + e.what() ,0xff);
        } catch (...) {
            AppendEventLog("[DLL] " + m_pluginList[i].name + " on_message: unknown exception" ,0xff);
        }
    }
    #ifdef _WIN32
    if(_32!=0 && bridge)
        bridge->writeResponseToBlock(2, utf8.constData());
    #endif
    if(msg.at_you && msg.subType==0)
        botnomsg(msg.appid,msg.type,msg.groupId,msg.msgId);
    if(msg.at_you && msg.subType==0) botnomsg(msg.appid,msg.type,msg.groupId,msg.msgId);

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
//更新右边面板
void PluginPage::updateDetailPanel(int index)
{
    if (index<=-1 && index>m_pluginList.length()) return;
    QPixmap pix(m_pluginList[index].icon);
    if (!pix.isNull())
        detailIconLabel->setPixmap(pix);
    else detailIconLabel->clear();
    detailNameLabel->setText(m_pluginList[index].name);
    QString typeStr;
    switch (m_pluginList[index].type) {
    case 0: typeStr = "Python"; break;
    case 1: typeStr = "DLL (64位)"; break;
    case 2: typeStr = "DLL (32位)"; break;
    case 3: typeStr = "JavaScript"; break;
    default: typeStr = "未知"; break;
    }
    detailTypeLabel->setText(typeStr);
    detailVersionLabel->setText("v" + m_pluginList[index].version);
    detailAuthorLabel->setText(m_pluginList[index].author.isEmpty() ? "未知" : m_pluginList[index].author);
    detailpathLabel->setText(getShortPath(m_pluginList[index].path,32));
    QString mdText = m_pluginList[index].description.isEmpty() ? "暂无说明" : m_pluginList[index].description;
    mdText.replace("\n", "  \n");               // 你之前加的换行处理
    mdText.replace("\n#", "\n# ");              // 换行后的#补空格
    mdText.replace("\r#", "\n# ");              // 换行后的#补空格
    if (mdText.startsWith("#") && mdText.length() > 1 && mdText[1] != ' ')
        mdText.insert(1, ' ');                  // 字符串开头的#补空格
    detailDescLabel->setMarkdown(mdText);

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
    for(int i=0;i<m_pluginList.size();++i)
        uninstall_Plugin(m_pluginList[i]);

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
        if (!info.loadedDllPath.isEmpty() && QFile::exists(info.loadedDllPath)) {
            QFile::remove(info.loadedDllPath);
            info.loadedDllPath.clear();
        }
    } else if (info.type == 2) {
        return sendData32(4, info) == "true";
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
        detailIconLabel->clear();
        detailNameLabel->clear();
        detailTypeLabel->clear();
        detailVersionLabel->clear();
        detailAuthorLabel->clear();
        detailDescLabel->clear();
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
    detailIconLabel->clear();
    detailNameLabel->clear();
    detailTypeLabel->clear();
    detailVersionLabel->clear();
    detailAuthorLabel->clear();
    detailDescLabel->clear();
    return true;
}
QString PluginPage::LoadPlugin(const QString &path,int type,bool enabled,QList<int> &array)  //运行时调用
{
    int index = findPluginIndex(path);
    if(index!=-1) return path + "\n插件已经 载入请勿重复载入";
    py::gil_scoped_acquire gil;
    PluginInfo info;
    info.path=path;
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

        //m_pipDialog->close();


    } else {
        // 安装失败，提示用户，也可选择不加载
        QMessageBox::warning(this, "安装依赖失败",
                             "pip install 失败，请检查网络或手动安装依赖。\n"
                             "您可以手动执行：pip install -r requirements.txt");
        // 失败后若想继续加载（依赖可能已存在），可调用 doLoadPythonPlugin，但一般不推荐
    }

    m_pipLog->append("\n安装已经结束请手动关闭窗口...\n至于为什么不自动关闭 因为可能有错误");
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
    } else {
        QMessageBox::warning(this, "安装依赖失败", "npm install 失败，请检查网络或手动安装依赖。");
    }
    m_npmLog->append("\n安装已经结束请手动关闭窗口...\n至于为什么不自动关闭 因为可能有错误");
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
        m_npmDialog->close();

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
    info.DLL.getPluginInfo = (GetPluginInfoFunc)lib->resolve("get_plugin_info");
    info.DLL.onMessage = (OnMessageFunc)lib->resolve("on_message");
    info.DLL.onEnable = (OnFunc0)lib->resolve("on_enable");
    info.DLL.onDisable = (OnFunc0)lib->resolve("on_disable");
    info.DLL.onUnload = (OnFunc0)lib->resolve("on_unload");
    info.DLL.onSet = (OnFunc0)lib->resolve("on_set");
    if (!info.DLL.getPluginInfo) return info.path + "\n get_plugin_info 函数不存在";
    if (!info.DLL.onMessage) return info.path + "\n on_message 函数不存在";
    QByteArray uuidBytes = info.uuid.toUtf8();
    uuidBytes.append('\0');
    // 假设 info_str 是 DLL 返回的 JSON 字符串
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

        } else {
            uninstall_Plugin(info);
            return info.path + " get_plugin_info 返回的内容非json 或不是标准json";
        }
    }
    if(info.name.isEmpty()) return info.path + "get_plugin_info 函数中未正确 返回插件名字";

    return QString();
}

QString PluginPage::sendData32(int type,PluginInfo &info,const QString &appidlist)
{
    #ifdef _WIN32
    QJsonObject reqJson;
    reqJson["type"] = type;                       // 加载插件
    reqJson["path"] = info.loadedDllPath;      // 新路径（临时目录）
    reqJson["uuid"] = info.uuid;              // 插件唯一标识（可能为空，由易语言处理）
    reqJson["e"]    = info.enabled;           // 是否启用（bool 型，易语言取逻辑值）
    reqJson["appid"]=appidlist;
    QByteArray reqData = QJsonDocument(reqJson).toJson(QJsonDocument::Compact);
    if(!bridge->writeResponseToBlock(1, reqData.constData()))
         return "发送加载命令失败（共享内存繁忙）";
    return bridge->processRequestsA(5000);
#else
    return QString();
#endif
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

    QByteArray data = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    bridge->writeResponseToBlock(1, data.constData());
    QString ret =bridge->processRequestsA(5000);
    if(ret.isEmpty()) return;
    AppendEventLog(ret);
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
        // 7. 解析 _plugin_commands（规则注册）
        info.python.rules.clear();
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
                    if (funcObj.is_none() || !(py::isinstance<py::function>(funcObj) || PyCallable_Check(funcObj.ptr()))) {
                        qWarning() << "指令/事件函数" << funName << "不存在或不可调用，跳过";
                        continue;
                    }
                    bool caseSensitive = true;
                    if (cmd.contains("case_sensitive")) {
                        caseSensitive = cmd["case_sensitive"].cast<bool>();
                    }
                    info.python.rules.append({matchType, keyStr, funcObj, caseSensitive});

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
                            if (funcObj.is_none()) {
                                qWarning() << "函数" << funName << "不存在或不可调用，跳过该规则";
                                continue;
                            }
                            bool caseSensitive = true;
                            if (ruleDict.contains("case_sensitive") && !ruleDict["case_sensitive"].is_none()) {
                                caseSensitive = ruleDict["case_sensitive"].cast<bool>();
                            }
                            info.python.rules.append({matchType, key, funcObj, caseSensitive});

                        }
                    }
                };
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

        return QString();

    } catch (const py::error_already_set &e) {
        return QString("%1 错误: %2").arg(info.path, e.what());
    }
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

    QVariantMap metadata = NodePluginManager::instance().loadPlugin(fullPath, info.uuid);
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

    return QString();
}
