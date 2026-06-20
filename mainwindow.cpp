

#include "MainWindow.h"
#include "AiWidget.h"
#include "BlacklistPage.h"
#include "ButtonEditor.h"
#include "HomePage.h"
#include "AccountPage.h"
#include "PluginPage.h"

#include "LogPage.h"
#include "ScheduleConfigWidget.h"
#include "ScreenshotSyncClient.h"
#include "botruleconfigwidget.h"
#include "chatpage.h"
#include "forbiddenwordpage.h"
#include "global.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QCursor>
#include <QEvent>
#include <QToolButton>
#include <QLabel>
#include <QMenu>
#include <QIcon>

#include <QStyle>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPainter>
#include <QFile>
#include <qgroupbox.h>
#include "htmltoimagewidget.h"
#include "sandboxwindow.h"
#include "set.h"
#include "textreplaceconfigwidget.h"
#include "keywordmatchconfigwidget.h"
#include <QNetworkReply>
#include <QProgressDialog>



#ifdef Q_OS_WIN
#include <windows.h>
#endif

#define APP_VERSION_STR "v1.0.4.5"
#define APP_BUILD_NUMBER 5

QString Homev=R"(
# 更新日志🌸
## v1.0.4.5 (2026-06-15)
- 好好好 茜草改名为 纯白铃
- 添加 订阅 也可以叫定时 可以推送订阅信息的群
- 修复 js子进程不会自动退出问题
- 优化 适配云崽 【部分】单js插件
- 添加 Html制图
- 添加 常用功能Ai

## v1.0.3.4 (2026-06-14)
- 增加对 JS 插件的支持
- 修复 发送失败时 无错误信息
- 其他 python 启用多线程 请注意编写py代码时多线程有没有问题
- 修复 沙盒保存到插件 编码问题
- 修复 被添加好友 删除好友时崩溃问题

## v1.0.2.3 (2026-06-10)
- 修复 代理类ws 不可用问题
- 修复 检查更新 下载失败问题

## v1.0.1.2 (2026-06-08)
- 降低版本 指qt 5.15.3 兼容多类 低版本心跳

## v1.0.0.1 (2026-06-07)
- 修复\[]\()语法无效问题

## v1.0.0 (2026-06-06)
- 初始版本发布

)";

// 全局指针（保持与你原有代码一致）
HomePage *homePage = nullptr;
set *setA=nullptr;
AccountPage *accountPage = nullptr;
LogPage *logPage = nullptr;
PluginPage *pluginPage = nullptr;
ChatPage *chatPage = nullptr;
SandboxWindow *Sandbox = nullptr;
ButtonEditor *buttonEditorPage=nullptr;
BotRuleConfigWidget *RuleConfigWidget=nullptr;
TextReplaceConfigWidget *TextReplace=nullptr;
KeywordMatchConfigWidget *keyword=nullptr;
BlacklistPage *Black=nullptr;
ForbiddenWordPage *forbidden=nullptr;
ScheduleConfigWidget *schedule=nullptr;
HtmlToImageWidget *htmltoimg =nullptr;
ScreenshotSyncClient *ScreenA=nullptr;
AiWidget *ai_ui = nullptr;
QListWidget *robotListWidget=nullptr;
int m_currentBotIndex = -1;
int 定时检查变量=0;


MainWindow::MainWindow(QWidget *parent): QMainWindow(parent), resizing(false), edgeMargin(5)
{
    // 无边框窗口
    setWindowFlags(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    resize(1040, 660);
    setMinimumSize(900, 560);
    setWindowTitle("纯白铃");

    setupUi();
    xr();
    applyStyleSheet();


    // 默认选中首页
    btnHome->setChecked(true);
    stackedWidget->setCurrentIndex(0);

    // 心跳定时器（你的原有逻辑）
    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(3000);
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        auto ss=stackedWidget->currentWidget();
        if (ss == accountPage) {
            for (CardWidget* card : std::as_const(g_CW)) {
                if (card) card->onTimeRefresh();
            }
        }
        schedule->检查定时列表();
        if(ss==homePage) homePage->refreshRuntimeStats();
        if (!bridge) return;
        if (miaomiao32 >= 2)
            AppendEventLog("与加载器通讯失败了.." + QString::number(miaomiao32));
        bridge->writeResponseToBlock(1, "{\"type\":7}");
        miaomiao32++;
        miaomiao++;
        if (miaomiao32 >= 4) {
            if (bridge->restartYiProcess()) {
                miaomiao32 = 0;
                miaomiao = 7;
            }
        }
        if ((miaomiao & 7) == 0)
            pluginPage->syncPluginsTo32();
    });
    m_heartbeatTimer->start();
}

void MainWindow::xr()
{
    m_kantoumusume = new QLabel(this);
    QPixmap pixmap(":/icons/qiancao1.png");
    m_kantoumusume->setPixmap(pixmap);
    m_kantoumusume->resize(pixmap.size());
    QGraphicsOpacityEffect* effect = new QGraphicsOpacityEffect();
    m_kantoumusume->setStyleSheet("background: transparent;");
    effect->setOpacity(0.5);
    m_kantoumusume->setGraphicsEffect(effect);
    m_kantoumusume->move(width() - m_kantoumusume->width() - 10,
                       height() - m_kantoumusume->height() + 40);
    m_kantoumusume->setAttribute(Qt::WA_TransparentForMouseEvents);
}

void showClickableLicenseInfo() {
    QMessageBox msgBox;
    msgBox.setWindowTitle("许可证信息");
    msgBox.setIcon(QMessageBox::Information);
    QString richText =
        "本程序使用 Qt 6.8.0 (LGPLv3)。<br><br>"
        "源代码、目标文件 (.obj) 及 LGPL 协议全文请访问：<br>"
        "<a href=\"https://github.com/qiancao1/qiancao/tree/master\">"
        "GitHub</a> 或 "
        "<a href=\"https://gitee.com/qiancao1/qiancao\">"
        "Gitee</a><br><br>"

        "Qt 是 The Qt Company 的注册商标。<br>"
        "本程序遵循 MIT 协议。<br><br>"
        "icons by <a href=\"https://icons8.com\">https://icons8.com</a><br><br>"
        "咸鱼Ai中转 <a href=\"https://allgpt.xianyuw.cn\">https://allgpt.xianyuw.cn</a><br><br>"
        "官方群 <a href=\"https://qm.qq.com/q/pPykIoOqGW\">827737534</a>";

    msgBox.setTextFormat(Qt::RichText);          // 设为富文本模式
    msgBox.setText(richText);
    msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
    msgBox.setCursor(Qt::PointingHandCursor);
    msgBox.exec();
}

MainWindow::~MainWindow()
{
}
QStackedWidget *stackedWidget=nullptr;

void MainWindow::setupUi()
{
    robotListWidget = new QListWidget;

    ScreenA= new ScreenshotSyncClient;
    networkManager = new QNetworkAccessManager(this);
    homePage = new HomePage;
    accountPage = new AccountPage;
    logPage = new LogPage;
    pluginPage = new PluginPage;
    chatPage = new ChatPage;
    Sandbox = new SandboxWindow;
    setA = new set;
    buttonEditorPage = new ButtonEditor;
    RuleConfigWidget = new BotRuleConfigWidget;
    TextReplace = new TextReplaceConfigWidget;
    keyword = new KeywordMatchConfigWidget;
    Black = new BlacklistPage;
    forbidden = new ForbiddenWordPage;
    schedule =new ScheduleConfigWidget;
    htmltoimg = new HtmlToImageWidget(this);
    ai_ui = new AiWidget;

    //aiContainer = new QWidget(this);          // 容器
    //aiUi.setupUi(aiContainer);                // 将 UI 加载到容器中



    QGroupBox *configGroupBox = new QGroupBox();   // 分组框，标题可自定义

    configGroupBox->setStyleSheet("QGroupBox { padding-top: 5px; margin-top: 0px; border: 0px; }");
    QWidget *nwid = new QWidget;
    QHBoxLayout *hxzsy = new QHBoxLayout(nwid);           // 选择夹
    hxzsy->setContentsMargins(0,0,0,0);



    robotListWidget->setMaximumWidth(220);
    hxzsy->addWidget(robotListWidget);
    QTabWidget *configTabWidget2 = new QTabWidget;           // 选择夹
    configTabWidget2->addTab(RuleConfigWidget, "按钮挂载");
    configTabWidget2->addTab(TextReplace, "自定义替换");
    configTabWidget2->addTab(keyword, "关键词回复");
    configTabWidget2->addTab(schedule, "订阅");
    configTabWidget2->addTab(ai_ui, "Ai");
    hxzsy->addWidget(configTabWidget2);



    QTabWidget *configTabWidget = new QTabWidget;           // 选择夹
    configTabWidget->addTab(setA, "基础设置");
    configTabWidget->addTab(buttonEditorPage, "按钮生成");
    configTabWidget->addTab(nwid, "内置功能");
    configTabWidget->addTab(Black, "黑名单管理");
    configTabWidget->addTab(forbidden, "违禁词过滤");
    configTabWidget->addTab(htmltoimg, "HTML制图");


    // 将选择夹放入分组框
    QVBoxLayout *groupLayout = new QVBoxLayout(configGroupBox);
    groupLayout->setContentsMargins(0, 0, 0, 0);

    groupLayout->addWidget(configTabWidget);



    // ========== 堆叠窗口 ==========
    stackedWidget = new QStackedWidget;
    stackedWidget->addWidget(homePage);          // index 0
    stackedWidget->addWidget(accountPage);       // index 1
    stackedWidget->addWidget(logPage);           // index 2
    stackedWidget->addWidget(pluginPage);        // index 3
    stackedWidget->addWidget(chatPage);          // index 4
    stackedWidget->addWidget(Sandbox);           // index 5
    stackedWidget->addWidget(configGroupBox); // index 6

    stackedWidget->setObjectName("contentStack");


    sideBar = new QWidget;
    sideBar->setFixedWidth(148);
    sideBar->setObjectName("sideBar");
    sideBar->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *sideLayout = new QVBoxLayout(sideBar);
    sideLayout->setContentsMargins(16, 0, 16, 18);
    sideLayout->setSpacing(8);
    sideLayout->setAlignment(Qt::AlignTop);


    QWidget *brandWidget = new QWidget;
    brandWidget->setObjectName("brandWidget");
    QHBoxLayout *brandLayout = new QHBoxLayout(brandWidget);
    brandLayout->setContentsMargins(0, 16, 0, 10);
    brandLayout->setSpacing(8);

    sideLayout->addWidget(brandWidget);


    auto createNavButton = [](const QString &text, const QIcon &icon) {
        QPushButton *btn = new QPushButton(text);
        btn->setIcon(icon);
        btn->setIconSize(QSize(20, 20));
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setObjectName("navBtn");
        btn->setMinimumHeight(40);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return btn;
    };


    btnHome = createNavButton("首页", QIcon(":/icons/home.png"));
    btnAccount = createNavButton("账号", QIcon(":/icons/account.png"));
    btnLog = createNavButton("日志", QIcon(":/icons/log.png"));
    btnPlugin = createNavButton("插件", QIcon(":/icons/plugin.png"));
    btnChat = createNavButton("聊天", QIcon(":/icons/chat.png"));
    QPushButton *Sandbox2 = createNavButton("沙盒", QIcon(":/icons/sandbox.png"));

    QPushButton *btnAdvancedConfig = createNavButton("高级配置", QIcon(":/icons/advanced.png"));

    btnGroup = new QButtonGroup(this);
    btnGroup->setExclusive(true);
    btnGroup->addButton(btnHome, 0);
    btnGroup->addButton(btnAccount, 1);
    btnGroup->addButton(btnLog, 2);
    btnGroup->addButton(btnPlugin, 3);
    btnGroup->addButton(btnChat, 4);
    btnGroup->addButton(Sandbox2, 5);
    btnGroup->addButton(btnAdvancedConfig, 6);


    connect(btnGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            [this](int id) {
                updateCurrentBotInfo();
                stackedWidget->setCurrentIndex(id);
                if (logPage) logPage->setActive(id == 2);
            });
    connect(robotListWidget, &QListWidget::currentRowChanged, [this](){
        QListWidgetItem *item = robotListWidget->currentItem();
        RuleConfigWidget->列表行被单击(item);
        TextReplace->列表行被单击(item);
        keyword->列表行被单击(item);
        schedule->列表行被单击(item);
        ai_ui->列表行被单击(item);
    });
    sideLayout->addWidget(btnHome);
    sideLayout->addWidget(btnAccount);
    sideLayout->addWidget(btnLog);
    sideLayout->addWidget(btnPlugin);
    sideLayout->addWidget(btnChat);
    sideLayout->addWidget(Sandbox2);
    sideLayout->addWidget(btnAdvancedConfig);   // 新按钮
    sideLayout->addStretch();


    checkUpdateBtn = new QPushButton("检查更新");
    checkUpdateBtn->setFlat(true);
    checkUpdateBtn->setObjectName("aaaaaaaa");
    checkUpdateBtn->setStyleSheet("QPushButton:hover { color: blue; }");
    connect(checkUpdateBtn, &QPushButton::clicked, [this](){

        checkUpdate();
    });
    sideLayout->addWidget(checkUpdateBtn);
    QPushButton *btnLicense = new QPushButton("关于");
    btnLicense->setObjectName("aaaaaaaa");
    btnLicense->setCursor(Qt::PointingHandCursor);
    btnLicense->setMinimumHeight(24);

    btnLicense->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sideLayout->addWidget(btnLicense);

    // 点击按钮时弹出 Qt 许可证声明
    connect(btnLicense, &QPushButton::clicked, this, [](){
        showClickableLicenseInfo();
    });
    createTitleBar();

    QHBoxLayout *mainContentLayout = new QHBoxLayout;
    mainContentLayout->setContentsMargins(0, 0, 0, 0);
    mainContentLayout->setSpacing(0);
    mainContentLayout->addWidget(sideBar);
    mainContentLayout->addWidget(stackedWidget, 1);

    QWidget *contentWidget = new QWidget;
    contentWidget->setObjectName("contentWidget");
    contentWidget->setLayout(mainContentLayout);

    QVBoxLayout *totalLayout = new QVBoxLayout;
    totalLayout->setContentsMargins(4, 4, 4, 4);
    totalLayout->setSpacing(4);
    totalLayout->addWidget(titleBar);
    totalLayout->addWidget(contentWidget, 1);

    QWidget *central = new QWidget;
    central->setObjectName("centralRoot");
    central->setLayout(totalLayout);

    central->setStyleSheet(
        "QWidget#centralRoot {"
        "   background: #FFF8EF;"
        "   border-radius: 10px;"
        "   border: 1px solid #A176C2;"      // 您可以根据喜好调整
        "}"
        );
    setCentralWidget(central);
    robotListWidget->setCurrentRow(0);


}



void MainWindow::createTitleBar()
{
    titleBar = new QWidget;
    titleBar->setFixedHeight(46);
    titleBar->setObjectName("titleBar");
    titleBar->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout *layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(16, 0, 14, 0);
    layout->setSpacing(8);

    // ========== 新增左侧区域：图标 + 双标签 ==========
    QWidget *leftWidget = new QWidget;
    leftWidget->setObjectName("leftInfoWidget");
    QHBoxLayout *leftLayout = new QHBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    // 左侧图标（可替换为真实图标资源）
    QLabel *iconLabel = new QLabel("🔔");
    iconLabel->setFixedSize(34, 34);
    iconLabel->setAlignment(Qt::AlignCenter);
    // 右侧垂直标签
    QVBoxLayout *textLayout = new QVBoxLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    QLabel *mainLabel = new QLabel(QString("纯白铃 %1").arg(APP_VERSION_STR));
    mainLabel->setObjectName("leftMainLabel");


    QLabel *subLabel = new QLabel("编写于2026年5月15号，");
    subLabel->setObjectName("leftSubLabel");

    leftWidget->setStyleSheet("background: transparent;");
    iconLabel->setStyleSheet("background: transparent; font-size: 20px;");
    mainLabel->setStyleSheet("background: transparent; font-size: 14px; font-weight: bold; color: #333;");
    subLabel->setStyleSheet("background: transparent; font-size: 11px; color: #888;");
    textLayout->addWidget(mainLabel);
    textLayout->addWidget(subLabel);

    leftLayout->addWidget(iconLabel);
    leftLayout->addLayout(textLayout);

    // ========== 原有机器人状态区域 ==========
    botStatusWidget = new QWidget;
    botStatusWidget->setObjectName("botStatusWidget");
    botStatusWidget->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *botStatusLayout = new QHBoxLayout(botStatusWidget);
    botStatusLayout->setContentsMargins(8, 4, 10, 4);
    botStatusLayout->setSpacing(8);

    titleAvatarLabel = new QLabel("B");
    titleAvatarLabel->setObjectName("titleAvatar");
    titleAvatarLabel->setFixedSize(34, 34);
    titleAvatarLabel->setAlignment(Qt::AlignCenter);

    QVBoxLayout *statusLayout = new QVBoxLayout;
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);
    titleBotNameLabel = new QLabel("未选择机器人");
    titleBotNameLabel->setObjectName("titleUserName");
    titleBotStatusLabel = new QLabel("点击选择机器人");
    titleBotStatusLabel->setObjectName("titleOnline");
    statusLayout->addWidget(titleBotNameLabel);
    statusLayout->addWidget(titleBotStatusLabel);
    botStatusLayout->addWidget(titleAvatarLabel);
    botStatusLayout->addLayout(statusLayout);

    // ========== 窗口控制按钮 ==========
    const int btnSize = 26;
    minBtn = new QToolButton;
    minBtn->setFixedSize(btnSize, btnSize);
    minBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    minBtn->setToolTip("最小化");
    connect(minBtn, &QToolButton::clicked, this, &QMainWindow::showMinimized);

    maxBtn = new QToolButton;
    maxBtn->setFixedSize(btnSize, btnSize);
    maxBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    maxBtn->setToolTip("最大化");
    connect(maxBtn, &QToolButton::clicked, this, &MainWindow::onMaximizeClicked);

    closeBtn = new QToolButton;
    closeBtn->setFixedSize(btnSize, btnSize);
    closeBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    closeBtn->setToolTip("关闭");
    connect(closeBtn, &QToolButton::clicked, [this](){
        QApplication::quit();
    });

    minBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMinButton));
    maxBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));

    int iconSize = 14;
    minBtn->setIconSize(QSize(iconSize, iconSize));
    maxBtn->setIconSize(QSize(iconSize, iconSize));
    closeBtn->setIconSize(QSize(iconSize, iconSize));

    QString btnStyle = R"(
    QToolButton {
        background-color: transparent;
        border: none;
        border-radius: 8px;
    }
    QToolButton:hover {
        background-color: #FFF0DE;
    }
    QToolButton:pressed {
        background-color: #FFE0BF;
    }
    )";
    minBtn->setStyleSheet(btnStyle);
    maxBtn->setStyleSheet(btnStyle);
    closeBtn->setStyleSheet(btnStyle + "QToolButton:hover { background-color: #FFE2DF; color: #D83B32; }");

    // ========== 组装布局 ==========
    layout->addWidget(leftWidget);            // 新增的左侧区域
    layout->addStretch();                     // 弹性空间，将后续元素推到右侧
    layout->addWidget(botStatusWidget);       // 机器人状态区域（保持原有位置）
    layout->addSpacing(12);
    layout->addWidget(minBtn);
    layout->addWidget(maxBtn);
    layout->addWidget(closeBtn);

    titleBar->installEventFilter(this);
    botStatusWidget->installEventFilter(this);
    updateCurrentBotInfo();
}


void MainWindow::updateCurrentBotInfo()
{
    if (!titleBotNameLabel || !titleBotStatusLabel || !titleAvatarLabel) return;
    if (m_currentBotIndex == -1) {
        titleAvatarLabel->setText("A");
        titleAvatarLabel->setStyleSheet(
            "background: #8A94A6; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;");
        titleBotNameLabel->setText("全部机器人");
        titleBotStatusLabel->setText("显示所有账号的日志");

        if (logPage) {
            logPage->setCurrentBot(0, QString());
        }

        return;
    }
    if (m_accounts.isEmpty()) {
        m_currentBotIndex = -1;
        titleAvatarLabel->setText("B");
        titleAvatarLabel->setStyleSheet(
            "background: #8A94A6; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;");
        titleBotNameLabel->setText("全部机器人");
        titleBotStatusLabel->setText("暂无账号，日志显示全部数据");
        if (logPage && !m_appliedLogBotId.isEmpty()) {
            m_appliedLogBotId.clear();
            logPage->setCurrentBot(0, QString());
        }

        return;
    }

    if (m_currentBotIndex < 0 || m_currentBotIndex >= m_accounts.size()) {
        m_currentBotIndex = 0;
        for (int i = 0; i < m_accounts.size(); ++i) {
            if (m_accounts.at(i)->online) {
                m_currentBotIndex = i;
                break;
            }
        }
    }

    const auto &info = m_accounts.at(m_currentBotIndex);

    const QString initial = info->nickname.isEmpty() ? "B" : info->nickname.left(1).toUpper();
    const QString avatarColor = info->online ? "#65B85A" : "#D83B32";

    if (!info->avatarPath.isEmpty() && QFile::exists(info->avatarPath)) {
        QPixmap pix(info->avatarPath);
        if (!pix.isNull()) {
            titleAvatarLabel->clear();
            titleAvatarLabel->setPixmap(pix.scaled(34, 34, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            titleAvatarLabel->setStyleSheet(
                "border-radius: 17px; border: none;");
        } else {
            titleAvatarLabel->setPixmap(QPixmap());
            titleAvatarLabel->setText(initial);
            titleAvatarLabel->setStyleSheet(QString(
                "background: %1; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;").arg(avatarColor));
        }
    } else {
        titleAvatarLabel->setPixmap(QPixmap());
        titleAvatarLabel->setText(initial);
        titleAvatarLabel->setStyleSheet(QString(
            "background: %1; border-radius: 17px; color: white; font-size: 16px; font-weight: bold;").arg(avatarColor));
    }

    titleBotNameLabel->setText(info->nickname.isEmpty() ? "未命名机器人" : info->nickname);
    titleBotStatusLabel->setText(QString("%1 · %2")
                                     .arg(info->online ? "● 在线" : "○ 离线"
                                     ,info->appid.isEmpty() ? "未配置 AppID" : info->appid));
    if (logPage && m_appliedLogBotId != info->appid) {
        m_appliedLogBotId = info->appid;
        logPage->setCurrentBot(info->appid_int, info->nickname);
    }

}

void MainWindow::cycleCurrentBot()
{
    if (m_accounts.isEmpty()) {
        m_currentBotIndex = -1;
    } else {
        m_currentBotIndex = (m_currentBotIndex + 1) % m_accounts.size();
    }
    m_appliedLogBotId.clear();
    updateCurrentBotInfo();
}

void MainWindow::showBotSelectorMenu()
{
    QMenu menu(this);
    menu.setObjectName("botSelectorMenu");
    menu.setStyleSheet(R"(
        QMenu {
            background: #FFFFFF; border: 1px solid #E0E0E0; border-radius: 8px;
            padding: 4px; font-size: 13px;
        }
        QMenu::item {
            padding: 8px 28px 8px 12px; border-radius: 4px; margin: 2px 4px;
            color: #333333;
        }
        QMenu::item:selected { background: #F5F5F5; color: #333333; }
        QMenu::item:disabled { color: #AAAAAA; }
        QMenu::separator { height: 1px; background: #F0F0F0; margin: 4px 8px; }
    )");
    if (m_accounts.isEmpty()) {
        QAction *act = menu.addAction("暂无账号");
        act->setEnabled(false);
    } else {
        for (int i = 0; i < m_accounts.size(); ++i) {
            const auto &info = m_accounts.at(i);
            const QString name = info->nickname.isEmpty()
                                     ? (info->botqq.isEmpty() ? "未命名" : info->botqq)
                                     : info->nickname;
            const QString botId = info->appid.isEmpty() ? "未配置AppID" : info->appid;
            const QString statusText = info->online ? "在线" : "离线";
            const QString statusColor = info->online ? "#65B85A" : "#D83B32";
            const QString initial = name.isEmpty() ? "B" : name.left(1).toUpper();

            QIcon menuIcon;
            if (!info->avatarPath.isEmpty() && QFile::exists(info->avatarPath)) {
                QPixmap pix(info->avatarPath);
                if (!pix.isNull()) {
                    menuIcon = QIcon(pix.scaled(28, 28, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                }
            }
            if (menuIcon.isNull()) {
                menuIcon = QIcon(generateBotAvatar(initial, statusColor).scaled(28, 28, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            }

            QAction *act = menu.addAction(menuIcon, QString("%1 · %2  [%3]")
                                                        .arg(name,botId,statusText));

            act->setData(i);
            if (i == m_currentBotIndex) {
                act->setCheckable(true);
                act->setChecked(true);
                QFont f = act->font(); f.setBold(true); act->setFont(f);
            }
        }
        menu.addSeparator();
        QAction *allAct = menu.addAction("全部机器人（显示所有日志）");
        allAct->setData(-1);
        if (m_currentBotIndex < 0) {
            allAct->setCheckable(true); allAct->setChecked(true);
            QFont f = allAct->font(); f.setBold(true); allAct->setFont(f);
        }
    }

    QAction *selected = menu.exec(botStatusWidget->mapToGlobal(QPoint(0, botStatusWidget->height())));
    if (selected && selected->data().isValid()) {
        switchToBot(selected->data().toInt());
    }
}


void MainWindow::switchToBot(int index)
{
    if (index == m_currentBotIndex) return;
    m_currentBotIndex = index;
    m_appliedLogBotId.clear();
    updateCurrentBotInfo();
}

QPixmap MainWindow::generateBotAvatar(const QString &initial, const QString &colorHex) const
{
    const int size = 68;
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    p.setBrush(QColor(colorHex));
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(28);
    f.setBold(true);
    p.setFont(f);
    p.drawText(pix.rect(), Qt::AlignCenter, initial.left(1));
    p.end();
    return pix;
}


void MainWindow::onMaximizeClicked()
{
    if (isMaximized())
        showNormal();
    else
        showMaximized();
}

// 监听窗口状态变化（最大化/还原时更新按钮图标）
void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        if (isMaximized()) {
            maxBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarNormalButton));
            maxBtn->setToolTip("还原");
        } else {
            maxBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarMaxButton));
            maxBtn->setToolTip("最大化");
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (qApp) qApp->quit();
#ifdef Q_OS_WIN
    ::TerminateProcess(::GetCurrentProcess(), 0);
#endif
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (m_kantoumusume) {
        m_kantoumusume->move(width() - m_kantoumusume->width() - 10,
                             height() - m_kantoumusume->height() - 10);
    }
}




// 窗口缩放与侧边栏拖拽
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        if (sideBar && sideBar->geometry().contains(pos)) {
            dragStartPos = event->globalPos() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!resizing && (event->buttons() & Qt::LeftButton) && !dragStartPos.isNull()) {
        move(event->globalPos() - dragStartPos);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resizing = false;
        resizeEdge = Qt::Edges();
        dragStartPos = QPoint();
    }
    QMainWindow::mouseReleaseEvent(event);
}
bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == botStatusWidget && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            showBotSelectorMenu();
            return true;
        }
    }
    if (obj == titleBar) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                dragStartPos = me->globalPos() - frameGeometry().topLeft();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->buttons() & Qt::LeftButton && !dragStartPos.isNull()) {
                move(me->globalPos() - dragStartPos);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            dragStartPos = QPoint();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::checkUpdate() {

    QUrl url("https://gitee.com/api/v5/repos/qiancao1/qiancao/releases/latest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Qt-UpdateChecker/1.0");

    QNetworkReply *reply = networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater(); // 自动清理

        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "检查更新", "网络请求失败: " + reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            QMessageBox::warning(this, "检查更新", "数据解析错误");
            return;
        }

        QJsonObject obj = doc.object();
        QString remoteTag = obj.value("tag_name").toString();  // 例如 "v1.0.1.12931"
        QString releaseNotes = obj.value("body").toString();   // 更新说明
        const QJsonArray assets = obj.value("assets").toArray();
        QString downloadUrl;
        for (const auto &asset : assets) {
            QJsonObject assetObj = asset.toObject();
            QString fileName = assetObj.value("name").toString();

            if (fileName.endsWith(".exe", Qt::CaseInsensitive)) {
                downloadUrl = assetObj.value("browser_download_url").toString();
                break; // 找到 exe 文件，退出循环
            }
        }


        int remoteBuild = 0;
        QStringList parts = remoteTag.split('.');
        if (!parts.isEmpty()) {
            QString last = parts.last();
            last.remove(QRegularExpression("[^0-9]")); // 去掉可能的 'v'
            remoteBuild = last.toInt();
        }

        if (remoteBuild > APP_BUILD_NUMBER) {
            showUpdateDialog(remoteTag, releaseNotes, downloadUrl);
        } else {
            QMessageBox::information(this, "检查更新", "当前已经是最新版本");
        }
    });
}

void MainWindow::startDownloadAndReplace(const QString &version, const QString &downloadUrl) {
    QString cleanVersion = version;
    if (cleanVersion.startsWith('v')) cleanVersion.remove(0, 1);
    QString exeName = QString("纯白铃-%1.exe").arg(cleanVersion);
    QString savePath = QCoreApplication::applicationDirPath() + "/" + exeName;

    if (QFile::exists(savePath) && !QFile::remove(savePath)) {
        QMessageBox::critical(this, "文件错误", "无法删除旧版本文件");
        return;
    }

    QProgressDialog *progressDialog = new QProgressDialog("正在下载更新...", "取消", 0, 100, this);
    progressDialog->setWindowTitle("更新");
    progressDialog->setMinimumDuration(0);
    progressDialog->setAutoReset(false);
    progressDialog->setAutoClose(false);
    progressDialog->show();

    QNetworkRequest request((QUrl(downloadUrl)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkAccessManager *downloadManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = downloadManager->get(request);

    connect(reply, &QNetworkReply::downloadProgress, [progressDialog](qint64 recv, qint64 total) {
        if (total > 0) {
            progressDialog->setValue(int(recv * 100 / total));
        } else {
            progressDialog->setValue(0);
        }
    });

    connect(progressDialog, &QProgressDialog::canceled, [reply, downloadManager, progressDialog]() {
        reply->abort();
        progressDialog->close();
    });

    connect(reply, &QNetworkReply::finished, [=]() {
        progressDialog->setValue(100);

        // 辅助清理 lambda
        auto cleanup = [reply, downloadManager, progressDialog]() {
            reply->deleteLater();
            downloadManager->deleteLater();
            progressDialog->deleteLater();
        };

        // 被取消
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            cleanup();
            return;
        }

        // 网络错误
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::critical(this, "下载失败", "网络错误: " + reply->errorString());
            cleanup();
            return;
        }

        // 处理 HTTP 状态码
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QUrl finalUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!finalUrl.isEmpty()) {
            qDebug() << "检测到重定向到：" << finalUrl.toString();
            QMessageBox::critical(this, "下载失败", "重定向未自动处理，请重试");
            cleanup();
            return;
        }

        QByteArray data = reply->readAll();
        qint64 actualSize = data.size();
        qint64 expectedSize = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();

        // 检查是否为 HTML 错误页面
        if (data.size() < 500 && (data.contains("redirected") || data.contains("<html"))) {
            QMessageBox::critical(this, "下载失败", "服务器返回了重定向页面，可能是网络问题");
            cleanup();
            return;
        }

        if (expectedSize > 0 && actualSize != expectedSize) {
            QMessageBox::critical(this, "下载不完整",
                                  QString("预期大小: %1 字节, 实际: %2 字节").arg(expectedSize).arg(actualSize));
            cleanup();
            return;
        }

        // 原子保存
        QSaveFile file(savePath);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, "写入失败", "无法创建文件: " + savePath);
            cleanup();
            return;
        }
        file.write(data);
        if (!file.commit()) {
            QMessageBox::critical(this, "写入失败", "保存文件失败");
            cleanup();
            return;
        }

        // 启动新程序
        if (!QProcess::startDetached(savePath, QStringList())) {
            QMessageBox::critical(this, "启动失败", "无法启动更新程序: " + savePath);
            cleanup();
            return;
        }

        // 关闭当前程序
        QCoreApplication::quit();
        cleanup();
    });
}

void MainWindow::showUpdateDialog(const QString &version, const QString &releaseNotes, const QString &downloadUrl) {
    QDialog dialog(this);
    dialog.setWindowTitle("发现新版本");
    dialog.setMinimumWidth(500);
    dialog.setMinimumHeight(400);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *titleLabel = new QLabel(QString("<h3>新版本: %1</h3>").arg(version));
    layout->addWidget(titleLabel);


    QTextEdit *notesEdit = new QTextEdit(&dialog);
    if (releaseNotes.isEmpty()) {
        notesEdit->setPlainText("暂无更新说明。");
    } else {

        notesEdit->setMarkdown(releaseNotes);
    }
    notesEdit->setReadOnly(true);
    layout->addWidget(notesEdit);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *downloadBtn = new QPushButton("前往下载", &dialog);
    QPushButton *cancelBtn = new QPushButton("以后再说", &dialog);
    btnLayout->addStretch();
    btnLayout->addWidget(downloadBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);


    connect(downloadBtn, &QPushButton::clicked, [this, version, downloadUrl, &dialog]() {
        // 1. 确认更新
        QMessageBox::StandardButton reply = QMessageBox::warning(&dialog,
                                                                 "确认更新",
                                                                 "更新需要关闭当前框架，确认更新吗？",
                                                                 QMessageBox::Yes | QMessageBox::No
                                                                 );
        if (reply != QMessageBox::Yes) return;

        dialog.accept();
        qDebug() << downloadUrl;
        startDownloadAndReplace(version, downloadUrl);
    });
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void MainWindow::applyStyleSheet()
{
    setStyleSheet(R"(
        QMainWindow {
            background: transparent;
        }
        QWidget#centralRoot {
            background: #FFF8EF;
            border-radius: 10px;
        }
        QWidget {
            color: #263241;
            font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
            font-size: 13px;
        }
        QWidget#titleBar {
            background: #FFF8EF;
            border: none;
            border-top-left-radius: 10px;
            border-top-right-radius: 10px;
        }
        QWidget#botStatusWidget {
            background: transparent;
            border: 1px solid #F2E8DE;
            border-radius: 10px;
        }
        QWidget#botStatusWidget:hover {
            background: #FFF7EA;
            border: 1px solid #FFCF9F;
        }
        QLabel#titleAvatar {
            background: #8A94A6;
            border-radius: 10px;
            color: white;
            font-size: 16px;
            font-weight: bold;
        }
        QLabel#titleUserName {
            color: #263241;
            font-weight: 700;
            font-size: 12px;
            background: transparent;
        }
        QLabel#titleOnline {
            color: #65B85A;
            font-size: 11px;
            background: transparent;
        }
        QWidget#contentWidget {
            background: #F7EFE5;
            border-bottom-left-radius: 10px;
            border-bottom-right-radius: 10px;
        }
        QWidget#sideBar {
            background: #FEFEFC;
            border-right: 1px solid #F4E8DA;
            border-top-right-radius: 10px;
            border-bottom-left-radius: 10px;
        }
        QLabel#brandLogoLabel {
            background: transparent;
            border: none;
        }
        QPushButton#navBtn {
            background: transparent;
            border: none;
            border-radius: 6px;
            color: #687589;
            font-size: 14px;
            font-weight: 600;
            padding: 0px 16px;
            text-align: left;
        }
        QPushButton#navBtn:hover {
            background: #FFF6EA;
            color: #FF914D;
        }
        QPushButton#navBtn:checked {
            background: #FFF0DE;
            color: #FF7F32;
        }
        QLabel#mascotImage {
            background: transparent;
            border: none;
        }
        QStackedWidget#contentStack {
            background: #F7EFE5;
            border: none;
            border-bottom-right-radius: 5px;
        }
        QFrame, QGroupBox {
            background: #FFFFFF;
            border: none; /* 移除残余硬边框 */
            border-radius: 5px;
        }
        QGroupBox {
            margin-top: 14px;
            padding-top: 18px;
            font-weight: 700;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 14px;
            padding: 0 8px;
            color: #263241;
        }
        QListWidget, QListView, QScrollArea {
            background: #FFFFFF;
            border: 1px solid #e0e0e0;   /* 宽度1px，实线，黑色 */
            border-radius: 4px;
            outline: none;
        }
        QListWidget::item, QListView::item {
            border: none;
            color: #596579;
        }
        QListWidget::item:selected, QListView::item:selected {
            background: #FFF0DE;
            color: #FF7F32;
            border-radius: 4px;
        }
        QListWidget::item:hover, QListView::item:hover {
            background: #FFF7EA;
            border-radius: 6px;
            /*background: transparent;*/
        }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox {
            background: #FeFeFe;

            border-radius: 4px;
            padding: 6px 10px;
            border: 1px solid #E0E0E0;   /* 宽度1px，实线，黑色 */
            selection-background-color: #FFB066;
            selection-color: #FFFFFF;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus {
            background: #FFFFFF;
            border: 1px solid #FFB066;
        }
        /* 新增 placeholder 颜色 */
        QLineEdit::placeholder, QTextEdit::placeholder, QPlainTextEdit::placeholder {
            color: #AfAfAf;
        }
        QPushButton {
            border: none;
            border-radius: 6px;
            padding: 6px 12px;
            background: #FFF0DE;
            color: #FF7F32;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #FFE5C8;
        }
        QPushButton:pressed {
            background: #FFD7A8;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 4px 2px 4px 2px;
        }
        QScrollBar::handle:vertical {
            background: #E7D9C8;
            border-radius: 4px;
            min-height: 40px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QComboBox {
            border: 1px solid #ccc;
            border-radius: 6px;
            padding: 5px 30px 5px 10px; /* 为按钮腾出右侧空间 */
            background: white;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 25px;
            border-left: 1px solid #ccc;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
            background: #f0f0f0;
        }
        QComboBox::down-arrow {
            image: url(:/arrow_down.png); /* 或自定义 */
            width: 12px;
            height: 12px;
        }
        /* 悬停效果 */
        QComboBox::drop-down:hover {
            background: #e0e0e0;
        }
        QTableWidget { border: 1px solid #AE8AB1; gridline-color: #d0d0d0; }
        QHeaderView::section { background-color: #f5f5f5; border: 1px solid #d0d0d0; }
    )");
}



void MainWindow::on_pushButton_clicked(bool checked)
{

}

