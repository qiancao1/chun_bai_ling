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
#include "PluginDepDialog.h"
#include "global.h"
#include "node_plugin_manager.h"

#include <QListWidget>

QList<LogEntry> g_EventLogs;
QList<LogEntry> g_channelLogs;
QList<LogEntry> g_privateLogs;
QList<LogEntry> g_channel_privateLogs;
QList<LogEntry> g_groupLogs;

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
    setFixedHeight(66);
    setStyleSheet("background: transparent;");

    QHBoxLayout *hLayout = new QHBoxLayout(this);
    hLayout->setContentsMargins(8, 4, 8, 4);

    // 图标
    iconLabel = new QLabel;
    iconLabel->setFixedSize(48, 48);
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

// ==================== PluginPage 实现 ====================
PluginPage::PluginPage(QWidget *parent) : QWidget(parent)
{
    setupUi();
    loadPlugins();
    //setStyleSheet("* { border: 1px solid red; }");
}
#include <QMenu>
void PluginPage::setupUi()
{
    QHBoxLayout *mainLayout = new QHBoxLayout(this);

    // ========== 左侧：插件列表（完全不变） ==========
    QVBoxLayout *leftLayout = new QVBoxLayout;
    QLabel *listTitle = new QLabel("插件列表");
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

    addPluginBtn  = new QPushButton("添加(DLL)");
    addPluginBtn2 = new QPushButton("添加(Python)");
    addPluginBtn3 = new QPushButton("添加(JS)");


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
    pypip = new QPushButton("插件引用库");
    pypip->setFixedWidth(100);   // 保持原宽度

    formLayout2->addWidget(detailNameLabel);
    formLayout2->addWidget(pypip);
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
        currentSelected_index=-1;
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
            sendData32(9,m_pluginList[currentSelected_index]);
        }
    });
    connect(pypip, &QPushButton::clicked,this, [this]() {
        if (currentSelected_index < 0 || currentSelected_index >= m_pluginList.size()) {
            QMessageBox::warning(this, "提示", "请先选择一个插件。");
            return;
        }
        const PluginInfo &plugin = m_pluginList[currentSelected_index];
        if (plugin.type != 0) { // 假设类型0为Python插件
            QMessageBox::information(this, "提示", "只有 Python 插件才有依赖库管理。");
            return;
        }
        // 获取 requires 列表（假设为 QStringList）
        QStringList requires = plugin.python.requires; // 需要确保 PluginInfo 中有此成员
        if (requires.isEmpty()) {
            QMessageBox::information(this, "提示", "当前插件没有声明任何依赖库。");
            return;
        }
        PluginDepDialog dlg(requires, plugin.name, this);
        dlg.exec();
    });
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
    py::gil_scoped_acquire gil;
    m_pluginList.append(info);
    addPluginItemToUI(m_pluginList.size() - 1, info);
}

// 在指定位置插入
void PluginPage::insertPlugin(int index, const PluginInfo &info) {
    py::gil_scoped_acquire gil;
    m_pluginList.insert(index, info);
    insertPluginItemToUI(index, info);
}

void PluginPage::removePlugin(int index) {
    if (index < 0 || index >= m_pluginList.size()) return;
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
    item->setSizeHint(QSize(0, 66));
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

        if (m_pluginList[i].type == 3) plugin_n++;

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


void PluginPage::dispatch_message(const QString &text,const MessageEvent &msg)
{
    QByteArray utf8 = text.toUtf8();
    int _32=0;
    for (int i = 0; i < m_pluginList.size(); ++i) {
        if (!m_pluginList[i].enabled) continue;
        if(m_pluginList[i].appid.contains(msg.appid)) continue; //这个插件禁用
        if (m_pluginList[i].type == 0){
            try {
                if (m_pluginList[i].python.instance) {
                    py::gil_scoped_acquire gil;
                    py::object ret = m_pluginList[i].python.instance(msg);
                    if (!ret.is_none() && py::isinstance<py::str>(ret)) {
                        QString reply = QString::fromStdString(py::str(ret).cast<std::string>());
                        if (!reply.isEmpty()) {
                            QQBotClient *client = m_botClients[msg.appid];
                            if (client) {
                                QString contactId = msg.groupId;
                                QString msgIdNormal = msg.msgId;
                                QString msgIdRetry = msg.msgId;
                                QString nickname = client->m_info->nickname;
                                SendMessageTask *task = new SendMessageTask(client, msg.type, contactId, reply,
                                                                            msgIdNormal, msgIdRetry, nickname,false);
                                QThreadPool::globalInstance()->start(task);
                            }
                        }
                    }
                }
            } catch (const std::exception &e) {
                AppendEventLog("[Python] " + m_pluginList[i].name + " on_message: " + e.what() ,0xff);
            } catch (...) {
                AppendEventLog("[Python] " + m_pluginList[i].name + " on_message: unknown exception" ,0xff);
            }
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

    if(_32!=0 && bridge)
        bridge->writeResponseToBlock(2, utf8.constData());
    else if(msg.at_you || !msg.fullType)
        botnomsg(msg.type,msg.groupId,msg.msgId);

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
    if (index<=-1 && index>m_pluginList.length()) return false;
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

    if (index<=-1 && index>m_pluginList.length()) return false;
    if (QMessageBox::question(this, "确认卸载",QString("确定要卸载插件 '%1' 吗？此操作不可恢复。").arg(m_pluginList[index].name))!= QMessageBox::Yes)return false;
    AppendEventLog("[卸载插件]"+m_pluginList[index].name);
    if(!uninstall_Plugin(m_pluginList[index]))
    {
        showAutoCloseMessageBox("卸载失败","32位加载器没有响应");

        return false;
    }
    removePlugin(index);
    currentSelected_index=-1;
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
        if (err.contains("加载 DLL 失败:"))
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
    appendPlugin(info);
    plug_tji();
    return QString();
}

void PluginPage::LoadPlugin_DLL() //按钮
{
    QString path=QFileDialog::getOpenFileName(this, tr("选择 DLL 插件"), "", tr("动态链接库 (*.dll)"));
    if(path.isEmpty()) return;
    path.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath())+"/");
    path.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath())+"\\");
    QList<int> empty{};
    QString err = LoadPlugin(path,1,false,empty);
    if(!err.isEmpty())
    {
        AppendEventLog("[载入插件]"+path+" 错误信息："+err ,0xff);
        QMessageBox::about(this,"载入插件","[载入插件]"+path+" 错误信息："+err);
        return;
    }
    AppendEventLog("[载入插件]"+path);
    savePlugins();
}

void PluginPage::LoadPlugin_Python() //按钮
{
    QString dir = QFileDialog::getExistingDirectory(this, "选择 Python 插件文件夹");
    if (dir.isEmpty()) return;
    if (!QFile::exists(dir + "/main.py")) {

        showAutoCloseMessageBox("错误","所选文件夹中缺少 main.py");
        return;
    }
    dir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath())+"/");
    dir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath())+"\\");
    QList<int> empty{};
    QString err = LoadPlugin(dir,0,false,empty);
    if(!err.isEmpty())
    {
        AppendEventLog("[载入插件]"+dir+" 错误信息："+err ,0xff);
        QMessageBox::about(this,"错误",err);
        return;
    }
    savePlugins();
    AppendEventLog("[载入插件]"+dir);
}
void PluginPage::LoadPlugin_JS() { // 按钮点击槽
    QString dir = QFileDialog::getExistingDirectory(this, "选择 JS 插件文件夹");
    if (dir.isEmpty()) return;
    if (!QFile::exists(dir + "/main.js")) {
        QMessageBox::warning(this, "错误", "所选文件夹中缺少 main.js");
        return;
    }
    dir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()) + "/");
    dir.remove(QDir::fromNativeSeparators(QCoreApplication::applicationDirPath()) + "\\");
    QList<int> empty{};
    QString err = LoadPlugin (dir,3,false, empty);
    if (!err.isEmpty()) {
        AppendEventLog("加载JS插件失败: " + err ,0xff);
        QMessageBox::warning(this, "错误", err);
        return;
    }
    savePlugins();
    AppendEventLog("加载JS插件: " + dir);
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
    QString newFileName = baseName + "_" + timestamp + ".dll";
    info.loadedDllPath = tmpDir.filePath(newFileName);


    if (!QFile::copy(srcAbsPath, info.loadedDllPath)) return QString("复制 DLL 到临时目录失败: %1 -> %2").arg(info.path, info.loadedDllPath);
    QLibrary* lib = new QLibrary(info.loadedDllPath);
    if (!lib->load()) {
        QString errorMsg = "加载 DLL 失败: " + lib->errorString();
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
        } else {
            uninstall_Plugin(info);
            return info.path + " get_plugin_info 返回的内容非json 或不是标准json";
        }
    }
    if(info.name.isEmpty()) return info.path + "get_plugin_info 函数中未正确 返回插件名字";
    if(info.enabled)
    {
        if(info.DLL.onEnable) info.DLL.onEnable();
    }
    return QString();
}

QString PluginPage::sendData32(int type,PluginInfo &info,const QString &appidlist)
{

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
    QString newFileName = baseName + "_" + timestamp + ".dll";
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
    info.type=2;
    return QString();   // 成功
}

void PluginPage::syncPluginsTo32()
{
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
}

QString PluginPage::LoadPlugin_py(PluginInfo &info)
{
    QString mainPy = info.path+"/main.py";
    if (!QFile::exists(mainPy)) return info.path+"/main.py 文件不存在";

    try {
        // 设置 sys.path
        py::exec(QString("import sys; sys.path.insert(0, '%1')").arg(info.path).toStdString());

        // 创建插件的独立全局命名空间
        py::dict plugin_globals;
        plugin_globals["__builtins__"] = py::module_::import("builtins");
        plugin_globals["__name__"] = py::str(info.path.toStdString());
        plugin_globals["qq_api"] = py::module_::import("qq_api");
        plugin_globals["sys"] = py::module_::import("sys");

        // 执行文件，将 locals 也设为 plugin_globals，保证所有定义都进入同一个字典
        py::eval_file(mainPy.toStdString(), plugin_globals, plugin_globals);

        // 从 plugin_globals 中获取 on_message
        if (!plugin_globals.contains("on_message"))
            return info.path+"\\main.py 中 on_message 函数不存在";
        py::object func = plugin_globals["on_message"];
        if (!py::isinstance<py::function>(func))
            return info.path+"\\main.py 中 on_message 函数不存在";
        info.python.instance = func;

        // 同样从 plugin_globals 获取其他函数
        auto getCb = [&](const char *name) -> py::object {
            if (plugin_globals.contains(name)) {
                py::object obj = plugin_globals[name];
                return (py::isinstance<py::function>(obj) || PyCallable_Check(obj.ptr())) ? obj : py::object();
            }
            return {};
        };

        if(info.uuid.isEmpty()) {
            QUuid uuid = QUuid::createUuid();
            info.uuid = uuid.toString(QUuid::WithoutBraces);
        }
        info.python.onSet = getCb("on_set");
        info.python.onEnable = getCb("on_enable");
        info.python.onDisable = getCb("on_disable");
        info.python.onUnload = getCb("on_unload");
        info.python.requires.clear();
        // 获取插件信息
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

                if (dict.contains("requires") && py::isinstance<py::list>(dict["requires"])) {
                    py::list reqs = dict["requires"].cast<py::list>();
                    for (py::handle h : reqs) {
                        if (py::isinstance<py::str>(h)) {
                            info.python.requires << QString::fromStdString(h.cast<std::string>());
                        }
                    }
                }
            } catch (const py::error_already_set &e) {
                return QString("执行 %1/main.py 中 get_plugin_info 函数异常：%2").arg(info.path, e.what());
            }
        }

        if (info.name.isEmpty())
            return info.path+"/main.py 中 get_plugin_info 函数未返回插件名称";


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
            AppendEventLog("[载入插件] 成功："+path);
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
    info.type = 3;

    return QString();
}
