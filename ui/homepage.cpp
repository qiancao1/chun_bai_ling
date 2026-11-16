#include "homepage.h"


#define PSAPI_VERSION 2


#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QProgressBar>
#include <QGraphicsDropShadowEffect>
#include <QTextDocument>
#include <QFontMetrics>
#include <QSysInfo>
#include <QStringList>
#include <QList>
#include <QPair>
#include <algorithm>

#include <QDialog>
#include <QScrollArea>
#include <QComboBox>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "global.h"
#include "themecolors.h"   // 页面自带样式表也要跟着主题走
#include <QMutex>          // 读 BotDB 今日活跃表要加锁（m_msgMutex）

// 「插件消息统计」面板最多展示的插件数（按发送量降序取前 N）
static constexpr int kTopPluginCount = 6;

#ifdef _WIN32
#include <windows.h>
#include <Psapi.h>
#include <vector>      // NtQuerySystemInformation 的缓冲
#include <cstddef>     // offsetof / size_t
#else
#include <unistd.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <fstream>
#include <sstream>
#include <vector>
#endif


extern QString Homev;

QString formatBytes(long long bytes, const QString& prefix) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    int unitIdx = 0;
    double val = bytes;
    while (val >= 1024.0 && unitIdx < 3) {
        val /= 1024.0;
        unitIdx++;
    }
    return QString("%1: %2 %3").arg(prefix).arg(val, 0, 'f', 1).arg(units[unitIdx]);
}

// 统计数字加千分位（首页小卡片宽度有限，五位数以上不分开根本读不出量级）
static QString formatCount(qint64 n)
{
    if (n < 0) return QString::number(n);
    const QString s = QString::number(n);
    QString out;
    out.reserve(s.size() + s.size() / 3 + 1);
    int cnt = 0;
    for (int i = s.size() - 1; i >= 0; --i) {
        out.prepend(s.at(i));
        if (++cnt % 3 == 0 && i > 0) out.prepend(',');
    }
    return out;
}

// 小卡片右上角的副值用短格式（万/亿），否则「累计 12,345,678」会把标题行挤爆
static QString formatCountShort(qint64 n)
{
    if (n >= 100000000LL) return QStringLiteral("%1亿").arg(n / 100000000.0, 0, 'f', 1);
    if (n >= 10000LL)     return QStringLiteral("%1万").arg(n / 10000.0, 0, 'f', 1);
    return formatCount(n);
}

QString formatRate(double bytesPerSec, const QString& prefix) {
    const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int unitIdx = 0;
    double val = bytesPerSec;
    while (val >= 1024.0 && unitIdx < 3) {
        val /= 1024.0;
        unitIdx++;
    }
    return QString("%1: %2 %3").arg(prefix).arg(val, 0, 'f', 1).arg(units[unitIdx]);
}
HomePage::HomePage(QWidget *parent)
    : QWidget(parent)
{
#ifdef _WIN32
    m_hProcess = GetCurrentProcess();
    IO_COUNTERS ioCounters;
    if (GetProcessIoCounters(m_hProcess, &ioCounters)) {
        m_lastRead = ioCounters.ReadTransferCount;
        m_lastWrite = ioCounters.WriteTransferCount;
    }
    m_lastTime = GetTickCount64();
#else
    // Linux 下不需要进程句柄，直接读 /proc/self/io 初始化累计值
    std::ifstream ioFile("/proc/self/io");
    std::string line;
    qint64 readBytes = 0, writeBytes = 0;
    while (std::getline(ioFile, line)) {
        if (line.compare(0, 6, "rchar:") == 0) {
            readBytes = std::stoll(line.substr(6));
        } else if (line.compare(0, 6, "wchar:") == 0) {
            writeBytes = std::stoll(line.substr(6));
        }
    }
    m_lastRead = readBytes;
    m_lastWrite = writeBytes;
    // 时间戳用 clock_gettime
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    m_lastTime = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
#endif

    setupUI();
    setStyleSheetA();
}


void HomePage::setStyleSheetA()
{
    // 走 ThemeColors::applyTo：颜色仍是下面写死的默认色，但用户换配色时会自动重套
    ThemeColors::applyTo(this, QString::fromUtf8(R"(
        QWidget#homePage {
            background: #FFF8EF;
        }
        QFrame#dashboardPanel, QFrame#statCard, QFrame#heroPanel, QFrame#chartPanel, QFrame#recentPanel, QFrame#statusPanel, QFrame#quickPanel, QFrame#changelogPanel {
            background: #FFFFFF;
            border: 1px solid #F3E7DA;
            border-radius: 18px;
        }
        /* 统计卡改成紧凑小卡：单独把圆角收小（不要动上面那条共用规则） */
        QFrame#statCard {
            border-radius: 12px;
        }
        QLabel#pageTitle {
            font-size: 28px;
            font-weight: 800;
            color: #17202A;
            background: transparent;
        }
        QLabel#pageSubTitle, QLabel#mutedText {
            color: #8A94A6;
            font-size: 13px;
            background: transparent;
        }
        /* 「消息趋势」标题行里的收发统计小胶囊 */
        QLabel#chartStat {
            color: #4A5568;
            font-size: 12px;
            background: #FFF4E8;
            border-radius: 8px;
            padding: 2px 8px;
        }
        QLabel#sectionTitle {
            color: #17202A;
            font-size: 15px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#statIcon {
            min-width: 28px;
            min-height: 28px;
            max-width: 28px;
            max-height: 28px;
            border-radius: 9px;
            background: #FFF0DE;
            color: #FF914D;
            font-size: 15px;
        }
        QLabel#statTitle {
            color: #7A8496;
            font-size: 11px;
            background: transparent;
        }
        QLabel#statSub {
            color: #AEB6C2;
            font-size: 10px;
            background: transparent;
        }
        QLabel#statValue, QLabel#pluginCountValue {
            color: #17202A;
            font-size: 19px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#trendUp {
            color: #FF7F32;
            font-size: 12px;
            background: transparent;
        }
        QLabel#statusValue {
            color: #263241;
            font-size: 14px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#heroArt {
            background: transparent;
        }
        QLabel#conversationName {
            color: #263241;
            font-size: 13px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#badge {
            background: #FF914D;
            color: white;
            border-radius: 10px;
            padding: 2px 7px;
            font-size: 11px;
        }
        /* ---- 插件消息榜单 ---- */
        QLabel#rankBadge {
            background: #FFF1E2;
            color: #D9822B;
            border-radius: 7px;
            font-size: 10px;
            font-weight: 700;
        }
        QLabel#rankBadgeTop {
            background: #FF914D;
            color: #FFFFFF;
            border-radius: 7px;
            font-size: 10px;
            font-weight: 700;
        }
        QLabel#rankBadgeMuted {
            background: #F5F1EC;
            color: #C7BFB5;
            border-radius: 7px;
            font-size: 10px;
            font-weight: 700;
        }
        QLabel#rankCount {
            color: #FF7F32;
            font-size: 12px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#rankCountMuted {
            color: #D8D2C9;
            font-size: 12px;
            background: transparent;
        }
        QLabel#rankEmpty {
            color: #B7AFA6;
            font-size: 12px;
            background: transparent;
        }
        /* 更新日志里的 QTextBrowser 直接铺在卡片上，不要再套一层灰底描边 */
        QTextEdit#changelogTextEdit {
            background: transparent;
            border: none;
            padding: 0px;
            color: #4B5563;
            font-size: 12px;
        }

        QProgressBar {
            background: #F8EFE6;
            border: none;
            border-radius: 6px;
            height: 8px;
            text-align: center;
            color: transparent;
        }
        QProgressBar::chunk {
            background: #FFB066;
            border-radius: 6px;
        }
    )"));

}

static void applyShadow(QWidget* widget) {
    auto shadow = new QGraphicsDropShadowEffect(widget);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 15));
    shadow->setBlurRadius(10);
    widget->setGraphicsEffect(shadow);
}

static QFrame *createPanel(const QString &objectName = "")
{
    auto panel = new QFrame;
    panel->setObjectName(objectName.isEmpty() ? "dashboardPanel" : objectName);
    panel->setAttribute(Qt::WA_StyledBackground, true);
    applyShadow(panel);
    return panel;
}

static QLabel *createLabel(const QString &text, const QString &objectName)
{
    auto label = new QLabel(text);
    label->setObjectName(objectName);
    return label;
}


static QLabel* createStatusLabel(const QString& title, const QString& value) {
    QLabel* label = new QLabel(QString("%1: %2").arg(title, value));
    label->setObjectName("statusLabel");
    return label;
}

// ---------- 实现 HomePage 的私有方法 ----------



void HomePage::createStatCards(QGridLayout* statsLayout) {
    // 紧凑卡：左图标、右边「标题 + 副值 / 数值」两行，一排 5 张。
    //
    // 原来最左边那三张（启动以来 消息/发送/接收）已经挪到「消息趋势」的标题行里
    // （见 createChartPanel），这里放的是：活跃数据 / 插件数量 / 在线账号 / 广告观看 / 群好友增删。
    // 「活跃数据」那两个数就是「数据统计」指令里的 活跃用户 / 活跃群聊
    // （BotDB 的今日活跃用户表 / 群表）。
    //
    // 宽度统一：卡里的文字长短差很多（「群+0/-0 好友+0/-0」比「4」长得多），
    // 一旦让 label 的 sizeHint 参与布局，QGridLayout 就会按内容把各列撑成不同宽度。
    // 所以标题 / 数值 label 的水平策略设成 Ignored（宽度不再由文字长度决定），
    // 每张卡再统一一个最小宽度 + 列 stretch 相同 → 5 张卡等宽。
    // （右上角副值是 Fixed + 固定宽度，别用 Ignored —— 那样它会整个不显示。）
    const int kCardMinWidth = 150;
    const int kSubWidth     = 62;    // 右上角「累计 x」固定宽度，别把标题挤掉

    auto createStatCard = [&](int col, const QString &icon, const QString &title,
                              const QString &value, const QString &sub, const QString &iconBg,
                              const QString &tip) {
        auto card = createPanel("statCard");
        card->setToolTip(tip);
        card->setMinimumWidth(kCardMinWidth);

        auto layout = new QHBoxLayout(card);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(8);

        auto iconLabel = createLabel(icon, "statIcon");
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet(QString("QLabel#statIcon { background: %1; font-size: 15px; }").arg(iconBg));

        auto textLayout = new QVBoxLayout;
        textLayout->setSpacing(0);
        textLayout->setContentsMargins(0, 0, 0, 0);

        // 第一行：标题 +（右对齐的）累计值
        auto titleRow = new QHBoxLayout;
        titleRow->setSpacing(4);
        titleRow->setContentsMargins(0, 0, 0, 0);
        QLabel *titleLabel = createLabel(title, "statTitle");
        titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        titleRow->addWidget(titleLabel, 1);
        if (!sub.isEmpty()) {
            QLabel *subLabel = createLabel(sub, "statSub");
            subLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            // 固定宽度 + Fixed 策略：宽度不随文字变（「累计 0」和「累计 27.7万」一样宽），
            // 也不要 Ignored —— 那条会把 sizeHint 吃掉，实测副值会整个不显示。
            subLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            subLabel->setFixedWidth(kSubWidth);
            titleRow->addWidget(subLabel);
            if (title == "广告观看") m_adSub = subLabel;
        }
        textLayout->addLayout(titleRow);

        auto valueLabel = createLabel(value, "statValue");
        valueLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        if (title == "在线账号") m_onlineAccountValue = valueLabel;
        if (title == "插件数量") m_pluginCountValue = valueLabel;
        if (title == "广告观看") m_adValue      = valueLabel;
        if (title == "活跃数据" || title == "群/好友 增删" || title == "广告观看") {
            // 一张卡里放两个以上数值：13px 一行。
            // 卡宽现在是统一的，字号再大就会把后面的数裁掉。
            // min-height 撑到和 19px 大数值一样高，否则这张卡矮一截、标题行跟别的卡错位。
            // 「广告观看」主数值是「累计次数 / 本月金额」，同理按 13px 走。
            valueLabel->setStyleSheet("font-size: 13px; font-weight: 600; color: #17202A; min-height: 22px;");
            if (title == "活跃数据")   m_activeDataValue = valueLabel;
            if (title == "群/好友 增删") m_addDelValue  = valueLabel;
        }
        textLayout->addWidget(valueLabel, 1);

        layout->addWidget(iconLabel);
        layout->addLayout(textLayout, 1);

        // 「广告观看」整张卡可点：点开看每日明细（本月 / 上月）。
        // QFrame 没有 clicked 信号，这里挂事件过滤器；子控件（图标/标题/数值）一律
        // 转成鼠标透明，否则点在字上时事件被 QLabel 吃掉，卡片收不到。
        if (title == "广告观看") {
            m_adCard = card;
            card->setCursor(Qt::PointingHandCursor);
            card->installEventFilter(this);
            for (QWidget *child : card->findChildren<QWidget *>())
                child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        }

        statsLayout->addWidget(card, 0, col);
        statsLayout->setColumnStretch(col, 1);
    };

    // 一排 5 张：活跃数据是「今日」口径（跟数据统计指令一致），其余是启动以来 + 累计
    createStatCard(0, "📊", "活跃数据", "用户 0 · 群 0", "", "#E8F4FD",
                   "今日活跃用户数 · 活跃群数（=「数据统计」指令里的 活跃用户 / 活跃群聊，BotDB 今日活跃表）");
    createStatCard(1, "🧩", "插件数量", QString::number(m_pluginList.size()), "", "#FDF1E3",
                   "已加载的插件数量");
    createStatCard(2, "🤖", "在线账号", "0 / 0", "", "#F3E5F5",
                   "已登录 / 已配置 的账号数");
    createStatCard(3, "📺", "广告观看", "0 / ¥0.00", "今日 0", "#FDEBE8",
                   "累计观看次数 / 本月预计广告收入（元，未扣平台分成/服务费/税）· 点开看每日明细");
    createStatCard(4, "👥", "群/好友 增删", "0/0 0/0", "", "#EAF0FF",
                   "本次启动以来：群 加/退、好友 加/删");
}

QFrame* HomePage::createChartPanel() {
    auto chartPanel = createPanel("chartPanel");
    auto chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(14, 10, 14, 8);   // 圆角 18px，标题别贴着边框
    chartLayout->setSpacing(4);

    // 标题行：左边标题，右边一排收发统计（原来那三张卡片挪过来的）+ 图表说明
    auto chartTop = new QHBoxLayout;
    chartTop->setSpacing(8);
    chartTop->addWidget(createLabel("消息趋势", "sectionTitle"));
    chartTop->addStretch();

    m_chartRecvLabel  = createLabel("接收 0", "chartStat");
    m_chartSentLabel  = createLabel("发送 0", "chartStat");
    m_chartTotalLabel = createLabel("合计 0 · 累计 0", "chartStat");
    m_chartRecvLabel->setToolTip("本次启动以来机器人收到的消息数（合计后面是历史累计 message_received）");
    m_chartSentLabel->setToolTip("本次启动以来机器人发送的消息数（合计后面是历史累计 message_sent）");
    m_chartTotalLabel->setToolTip("启动以来收发合计 / 历史累计（message_received + message_sent）");
    chartTop->addWidget(m_chartRecvLabel);
    chartTop->addWidget(m_chartSentLabel);
    chartTop->addWidget(m_chartTotalLabel);
    chartTop->addWidget(createLabel("每小时收发量  ", "mutedText"));
    chartLayout->addLayout(chartTop);

    // 图表容器
    auto chartAreaLayout = new QVBoxLayout();
    chartAreaLayout->setContentsMargins(0, 0, 0, 0);   // 留白由 StatusChartWidget 自己算
    // 注意：原代码写成了 chartLayout->setSpacing(4)，应该是 chartAreaLayout->setSpacing(4)
    chartAreaLayout->setSpacing(4);

    // ---------- 使用 StatusChartWidget 替代 QChartView ----------
    m_chartWidget = new StatusChartWidget(this);
    m_chartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // 可以设置固定高度或让布局自动拉伸
    // m_chartWidget->setMinimumHeight(200);

    chartAreaLayout->addWidget(m_chartWidget, 1);
    chartLayout->addLayout(chartAreaLayout, 1);

    // 初次加载数据（需要在类中实现 updateChartData 的新版本）
    updateChartData();

    return chartPanel;
}

void HomePage::updateChartData() {


    QDate today = QDate::currentDate();
    QString dateStr = today.toString(Qt::ISODate);  // "2026-06-04"

    // 读取接收数组
    QJsonArray receiveArray;
    if (g_config.contains("Received")) {
        QJsonObject recvObj = g_config["Received"].toObject();
        if (recvObj.contains(dateStr)) {
            receiveArray = recvObj[dateStr].toArray();
        }
    }

    // 读取发送数组
    QJsonArray sendArray;
    if (g_config.contains("Sent")) {
        QJsonObject sentObj = g_config["Sent"].toObject();
        if (sentObj.contains(dateStr)) {
            sendArray = sentObj[dateStr].toArray();
        }
    }

    // 构建两个 QVector<int>，长度为24
    QVector<int> receiveData(24, 0);
    QVector<int> sendData(24, 0);

    for (int hour = 0; hour < 24; ++hour) {
        if (hour < receiveArray.size()) {
            receiveData[hour] = receiveArray[hour].toInt();
        }
        if (hour < sendArray.size()) {
            sendData[hour] = sendArray[hour].toInt();
        }
    }

    // 更新柱状图
    if (m_chartWidget) {
        m_chartWidget->setData(receiveData, sendData);
    }
}

QFrame* HomePage::createRecentPanel() {
    auto recentPanel = createPanel("recentPanel");
    recentPanel->setMinimumWidth(238);

    auto recentLayout = new QVBoxLayout(recentPanel);
    recentLayout->setContentsMargins(14, 12, 14, 12);
    recentLayout->setSpacing(10);

    auto recentTop = new QHBoxLayout;
    recentTop->setSpacing(6);
    recentTop->addWidget(createLabel("插件消息 TOP 6", "sectionTitle"));
    recentTop->addStretch();
    m_pluginRankSummary = createLabel("", "mutedText");
    recentTop->addWidget(m_pluginRankSummary);
    recentLayout->addLayout(recentTop);

    // 标题和榜单之间加一条极淡的分割线，卡里不至于一片空白
    auto divider = new QFrame;
    divider->setObjectName("panelDivider");
    divider->setFixedHeight(1);
    ThemeColors::applyTo(divider, "QFrame#panelDivider { background: #F5EDE3; border: none; }");
    recentLayout->addWidget(divider);

    m_pluginMessageLayout = new QVBoxLayout;
    m_pluginMessageLayout->setSpacing(2);
    m_pluginMessageLayout->setContentsMargins(0, 0, 0, 0);
    m_pluginMessageLayout->addStretch();
    recentLayout->addLayout(m_pluginMessageLayout, 1);
    return recentPanel;
}

QFrame* HomePage::createStatusPanel() {
    // 底部三卡并排：系统状态 / 更新日志 / 插件消息 TOP6
    QFrame* container = new QFrame;
    container->setObjectName("statusContainer");
    container->setAttribute(Qt::WA_StyledBackground, true);
    ThemeColors::applyTo(container,
        "QFrame#statusContainer { background: transparent; border: none; }");

    QHBoxLayout* mainLayout = new QHBoxLayout(container);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(8);

    // ========== 左侧：系统状态面板 ==========
    QFrame* leftPanel = createPanel("statusPanel");
    leftPanel->setMinimumWidth(292);
    QVBoxLayout* statusLayout = new QVBoxLayout(leftPanel);
    statusLayout->setContentsMargins(15, 12, 15, 12);
    statusLayout->setSpacing(12);

    // 辅助函数：创建一个指标行（24x24 图标 + 内容区域）
    auto createMetricRow = [](const QString& iconPath, QWidget* contentWidget) -> QWidget* {
        QWidget* row = new QWidget;
        QHBoxLayout* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(12);

        QLabel* iconLabel = new QLabel;
        iconLabel->setFixedSize(24, 24);
        iconLabel->setScaledContents(true);
        if (!iconPath.isEmpty()) {
            QPixmap pixmap(iconPath);
            if (!pixmap.isNull()) iconLabel->setPixmap(pixmap);
        }
        rowLayout->addWidget(iconLabel);
        rowLayout->addWidget(contentWidget, 1);
        return row;
    };

    // ----- CPU（标签 + 进度条）-----
    QWidget* cpuContent = new QWidget;
    QVBoxLayout* cpuLayout = new QVBoxLayout(cpuContent);
    cpuLayout->setContentsMargins(0, 0, 0, 0);
    cpuLayout->setSpacing(4);
    m_cpuTextLabel = new QLabel("CPU消耗: 0%");
    m_cpuProgressBar = new QProgressBar;
    m_cpuProgressBar->setRange(0, 100);
    m_cpuProgressBar->setValue(0);
    m_cpuProgressBar->setTextVisible(true);
    m_cpuProgressBar->setFormat("%p%");
    m_cpuProgressBar->setFixedHeight(12);
    ThemeColors::applyTo(m_cpuProgressBar,
        "QProgressBar { border: none; background-color: #E0D6CC; border-radius: 6px; "
        "text-align: center; color: #5D4037; font-size: 10px; }"
        "QProgressBar::chunk { background-color: #FFA726; border-radius: 6px; }"
        );
    cpuLayout->addWidget(m_cpuTextLabel);
    cpuLayout->addWidget(m_cpuProgressBar);
    statusLayout->addWidget(createMetricRow(":/icons/cpu.png", cpuContent));

    // ----- 内存（标签 + 进度条）-----
    QWidget* ramContent = new QWidget;
    QVBoxLayout* ramLayout = new QVBoxLayout(ramContent);
    ramLayout->setContentsMargins(0, 0, 0, 0);
    ramLayout->setSpacing(4);
    m_ramTextLabel = new QLabel("内存消耗: 0 MB");
    m_ramProgressBar = new QProgressBar;
    m_ramProgressBar->setRange(0, 100);
    m_ramProgressBar->setValue(0);
    m_ramProgressBar->setTextVisible(true);
    m_ramProgressBar->setFormat("%p%");
    m_ramProgressBar->setFixedHeight(12);
    ThemeColors::applyTo(m_ramProgressBar,
        "QProgressBar { border: none; background-color: #E0D6CC; border-radius: 6px; "
        "text-align: center; color: #5D4037; font-size: 10px; }"
        "QProgressBar::chunk { background-color: #66BB6A; border-radius: 6px; }"
        );
    ramLayout->addWidget(m_ramTextLabel);
    ramLayout->addWidget(m_ramProgressBar);
    statusLayout->addWidget(createMetricRow(":/icons/ram.png", ramContent));

    // ----- 磁盘读（速率 + 累计）-----
    QWidget* diskReadContent = new QWidget;
    QVBoxLayout* diskReadLayout = new QVBoxLayout(diskReadContent);
    diskReadLayout->setContentsMargins(0, 0, 0, 0);
    diskReadLayout->setSpacing(4);
    m_diskReadRateLabel = new QLabel("读速率: 0 B/s");
    m_diskReadTotalLabel = new QLabel("累计读: 0 B");
    diskReadLayout->addWidget(m_diskReadRateLabel);
    diskReadLayout->addWidget(m_diskReadTotalLabel);
    statusLayout->addWidget(createMetricRow(":/icons/disk_read.png", diskReadContent));

    // ----- 磁盘写（速率 + 累计）-----
    QWidget* diskWriteContent = new QWidget;
    QVBoxLayout* diskWriteLayout = new QVBoxLayout(diskWriteContent);
    diskWriteLayout->setContentsMargins(0, 0, 0, 0);
    diskWriteLayout->setSpacing(4);
    m_diskWriteRateLabel = new QLabel("写速率: 0 B/s");
    m_diskWriteTotalLabel = new QLabel("累计写: 0 B");
    diskWriteLayout->addWidget(m_diskWriteRateLabel);
    diskWriteLayout->addWidget(m_diskWriteTotalLabel);
    statusLayout->addWidget(createMetricRow(":/icons/disk_write.png", diskWriteContent));

    statusLayout->addStretch();

    // ========== 中间：更新日志（独立白卡，不再嵌在灰底容器里） ==========
    QFrame* logPanel = createPanel("changelogPanel");
    auto logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(14, 12, 12, 10);
    logLayout->setSpacing(8);

    // 正文第一行「# 更新日志🌸」由卡片标题代替，最新一版号提到标题右侧
    QString versionTag;
    QString changelogMarkdown = Homev;
    {
        QStringList lines = changelogMarkdown.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).startsWith("## ")) {
                versionTag = lines.at(i).mid(3).trimmed();
                lines.removeAt(i);
                break;
            }
        }
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).startsWith("# ")) {
                lines.removeAt(i);
                break;
            }
        }
        changelogMarkdown = lines.join('\n').trimmed();
    }

    auto logTop = new QHBoxLayout;
    logTop->setSpacing(6);
    logTop->addWidget(createLabel("更新日志", "sectionTitle"));
    logTop->addStretch();
    if (!versionTag.isEmpty()) logTop->addWidget(createLabel(versionTag, "mutedText"));
    logLayout->addLayout(logTop);

    QTextBrowser* changelogEdit = new QTextBrowser(logPanel);
    changelogEdit->setObjectName("changelogTextEdit");
    changelogEdit->setReadOnly(true);
    changelogEdit->setFrameShape(QFrame::NoFrame);
    changelogEdit->viewport()->setAutoFillBackground(false);
    changelogEdit->document()->setDocumentMargin(0);
    changelogEdit->setOpenExternalLinks(true);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    changelogEdit->setMarkdown(changelogMarkdown);
#else
    changelogEdit->setHtml("<pre>" + changelogMarkdown.toHtmlEscaped() + "</pre>");
#endif
    logLayout->addWidget(changelogEdit, 1);

    mainLayout->addWidget(leftPanel, 0);
    mainLayout->addWidget(logPanel, 1);
    mainLayout->addWidget(createRecentPanel(), 0);

    return container;
}









void HomePage::setupUI()
{
    setObjectName("homePage");

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(8);

    auto statsLayout = new QGridLayout;
    statsLayout->setSpacing(8);          // 一排 5 张等宽统计卡，间距 8 排满
    statsLayout->setContentsMargins(0, 0, 0, 0);
    createStatCards(statsLayout);
    mainLayout->addLayout(statsLayout);

    // 消息趋势独占一整行：24 小时横轴铺开才看得清；
    // 「插件消息 TOP6」挪到底部三栏的右列（原来挤在图表右边，上下都留一大片空白）
    mainLayout->addWidget(createChartPanel(), 3);
    mainLayout->addWidget(createStatusPanel(), 2);

    pluginGridLayout = nullptr;
    pluginGridWidget = nullptr;


}



QLabel *HomePage::createStatusLabel(const QString &title, const QString &value)
{
    auto label = createLabel(QString("%1\n%2").arg(title, value), "mutedText");
    label->setAlignment(Qt::AlignCenter);
    label->setProperty("statusTitle", title);
    return label;
}


#ifdef _WIN32
// ===================== 本进程内存 =====================
// 面板要显示的是「专用工作集」—— 任务管理器「内存」列那个数：只算本进程私有、且当前真正驻留的页。
// ⚠ 绝对不能用 WorkingSetSize 顶替：本框架把 LMDB 整库 mmap 进来、还嵌了 Python / Qt，
//   映射进来的库页和共享 DLL 页全算在工作集里（它们不是私有页），
//   实测同一进程「工作集 1650 MB / 专用工作集 230 MB」，差 7 倍 —— 首页数字就是这么跟任务管理器对不上的。
struct WinMemSnapshot {
    double workingSetMB = 0.0;   // 工作集（含共享页 / mmap 的文件页）
    double privateWsMB  = 0.0;   // 专用工作集；0 = 没取到
    double commitMB     = 0.0;   // 提交大小
    int    privateSrc   = 0;     // 1 = EX2 字段，2 = NtQuerySystemInformation；0 = 取不到
};

// 路径①：PROCESS_MEMORY_COUNTERS_EX2.PrivateWorkingSetSize
//   ⚠ 有系统门槛：Win10 22H2 / Win11 22H2 打上 2023-09 累积更新才算有；Server 2016/2019/2022 全够不到。
//   老系统上 K32GetProcessMemoryInfo 可能返回 TRUE 却根本不写这个字段（缓冲区我们清零过 → 读出来是 0），
//   所以必须再判 > 0，不能只看返回值。
static bool queryMemViaEx2(WinMemSnapshot &snap)
{
    struct PmcEx2 {
        DWORD     cb;
        DWORD     PageFaultCount;
        SIZE_T    PeakWorkingSetSize;
        SIZE_T    WorkingSetSize;
        SIZE_T    QuotaPeakPagedPoolUsage;
        SIZE_T    QuotaPagedPoolUsage;
        SIZE_T    QuotaPeakNonPagedPoolUsage;
        SIZE_T    QuotaNonPagedPoolUsage;
        SIZE_T    PagefileUsage;
        SIZE_T    PeakPagefileUsage;
        SIZE_T    PrivateUsage;
        SIZE_T    PrivateWorkingSetSize;
        ULONGLONG SharedCommitUsage;
    } pmc2;
    ZeroMemory(&pmc2, sizeof(pmc2));
    pmc2.cb = sizeof(pmc2);
    if (!K32GetProcessMemoryInfo(GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc2),
                                 sizeof(pmc2)))
        return false;

    const double mb = 1024.0 * 1024.0;
    snap.workingSetMB = pmc2.WorkingSetSize / mb;
    snap.commitMB     = pmc2.PrivateUsage / mb;
    if (pmc2.PrivateWorkingSetSize > 0 && pmc2.PrivateWorkingSetSize <= pmc2.WorkingSetSize) {
        snap.privateWsMB = pmc2.PrivateWorkingSetSize / mb;
        snap.privateSrc  = 1;
        return true;
    }
    return false;
}

// 路径②：NtQuerySystemInformation(SystemProcessInformation).WorkingSetPrivateSize
//   任务管理器自己就是这个字段；Vista 起所有内核都会填，不受累积更新影响 —— 服务器上也准。
//   SYSTEM_PROCESS_INFORMATION 是变长链表（每条后面还挂着线程/模块数组），这里只读到 WorkingSetSize 为止。
#if defined(_WIN64)
// UNICODE_STRING 在部分头文件组合下不可见，直接按布局写一份（2+2+对齐+指针 = 16 字节）
struct SpiUnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
};
struct SpiHead {
    ULONG           NextEntryOffset;
    ULONG           NumberOfThreads;
    LONGLONG        WorkingSetPrivateSize;
    ULONG           HardFaultCount;
    ULONG           NumberOfThreadsHighWatermark;
    ULONGLONG       CycleTime;
    LONGLONG        CreateTime;
    LONGLONG        UserTime;
    LONGLONG        KernelTime;
    SpiUnicodeString ImageName;
    LONG            BasePriority;
    // 编译器在这里自动补 4 字节，和内核布局一致
    HANDLE          UniqueProcessId;
    HANDLE          InheritedFromUniqueProcessId;
    ULONG           HandleCount;
    ULONG           SessionId;
    ULONG_PTR       UniqueProcessKey;
    SIZE_T          PeakVirtualSize;
    SIZE_T          VirtualSize;
    ULONG           PageFaultCount;
    SIZE_T          PeakWorkingSetSize;
    SIZE_T          WorkingSetSize;   // 后面还有别字段，我们不用
};
static_assert(sizeof(SpiHead) == 152, "SYSTEM_PROCESS_INFORMATION 头部布局变了");
static_assert(offsetof(SpiHead, UniqueProcessId) == 80, "SYSTEM_PROCESS_INFORMATION 偏移变了");

static bool queryMemViaSystemInfo(WinMemSnapshot &snap)
{
    typedef LONG (NTAPI *FnNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);
    static FnNtQuerySystemInformation fn = []() -> FnNtQuerySystemInformation {
        HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
        return ntdll ? reinterpret_cast<FnNtQuerySystemInformation>(
                           ::GetProcAddress(ntdll, "NtQuerySystemInformation"))
                     : nullptr;
    }();
    if (!fn) return false;

    static const ULONG kSystemProcessInformation = 5;
    static const ULONG kInfoLengthMismatch = 0xC0000004UL;

    static std::vector<char> buf;   // 几 MB，复用一份，别每两秒重新开
    ULONG need = 0;
    LONG st = fn(kSystemProcessInformation,
                 buf.empty() ? nullptr : buf.data(),
                 static_cast<ULONG>(buf.size()), &need);
    for (int i = 0; i < 3 && static_cast<ULONG>(st) == kInfoLengthMismatch; ++i) {
        buf.resize(static_cast<size_t>(need) + 64 * 1024);
        st = fn(kSystemProcessInformation, buf.data(),
                static_cast<ULONG>(buf.size()), &need);
    }
    if (st != 0 || buf.empty()) return false;

    const ULONG_PTR selfPid = static_cast<ULONG_PTR>(::GetCurrentProcessId());
    const double mb = 1024.0 * 1024.0;
    size_t off = 0;
    while (off + sizeof(SpiHead) <= buf.size()) {
        const SpiHead *e = reinterpret_cast<const SpiHead *>(buf.data() + off);
        if (reinterpret_cast<ULONG_PTR>(e->UniqueProcessId) == selfPid) {
            snap.workingSetMB = static_cast<double>(e->WorkingSetSize) / mb;
            if (e->WorkingSetPrivateSize > 0
                && static_cast<unsigned long long>(e->WorkingSetPrivateSize)
                       <= static_cast<unsigned long long>(e->WorkingSetSize)) {
                snap.privateWsMB = static_cast<double>(e->WorkingSetPrivateSize) / mb;
                snap.privateSrc  = 2;
                return true;
            }
            return false;
        }
        if (e->NextEntryOffset == 0) break;
        off += e->NextEntryOffset;
    }
    return false;
}
#endif  // _WIN64

// 本进程内存快照：专用工作集优先（EX2 → NtQuerySystemInformation），
// 两条路都拿不到才退回工作集（数值会明显偏大，tooltip 里会标「不是任务管理器那个数」）。
static WinMemSnapshot queryWinMemSnapshot()
{
    WinMemSnapshot snap;
    queryMemViaEx2(snap);
#if defined(_WIN64)
    if (snap.privateWsMB <= 0.0) queryMemViaSystemInfo(snap);
#endif
    return snap;
}

// 机器物理内存总量（MB）；拿不到返回 0，由调用方决定怎么显示
static double queryTotalMemMB()
{
    MEMORYSTATUSEX memStatus;
    ZeroMemory(&memStatus, sizeof(memStatus));
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus) && memStatus.ullTotalPhys > 0)
        return memStatus.ullTotalPhys / (1024.0 * 1024.0);
    return 0.0;
}
#endif


void HomePage::updateProcessStats()
{
    // ========== 1. 内存占用（带进度条） ==========
    double memMB = 0.0;        // 本进程占用（专用工作集 = 任务管理器「内存」列那个数）
    double totalMB = 0.0;      // 机器（或容器）总量
    QString memTooltip;        // 鼠标悬停看明细，免得再有人拿工作集跟任务管理器比
#ifdef _WIN32
    const WinMemSnapshot memSnap = queryWinMemSnapshot();
    static const double s_totalMemMB = queryTotalMemMB();
    totalMB = s_totalMemMB;
    // ⚠ 只有「专用工作集」才和任务管理器对得上；两条路都取不到才退回工作集，
    //    并且 tooltip 会明确写出来（工作集含 mmap 的库页/共享页，能大好几倍）
    memMB = (memSnap.privateWsMB > 0.0) ? memSnap.privateWsMB : memSnap.workingSetMB;
    if (memSnap.privateWsMB > 0.0) {
        memTooltip = QStringLiteral("专用工作集（任务管理器「内存」列）: %1 MB\n"
                                    "工作集（含共享页 / mmap 映射页）: %2 MB\n"
                                    "提交大小: %3 MB\n取数来源: %4")
                         .arg(memSnap.privateWsMB, 0, 'f', 1)
                         .arg(memSnap.workingSetMB, 0, 'f', 1)
                         .arg(memSnap.commitMB, 0, 'f', 1)
                         .arg(memSnap.privateSrc == 1 ? QStringLiteral("PROCESS_MEMORY_COUNTERS_EX2")
                                                      : QStringLiteral("NtQuerySystemInformation"));
    } else {
        memTooltip = QStringLiteral("取不到专用工作集，当前显示的是工作集（含共享页 / mmap 映射页），"
                                    "会比任务管理器「内存」列大好几倍。\n工作集: %1 MB")
                         .arg(memSnap.workingSetMB, 0, 'f', 1);
    }
#else
    // Linux: 读取 /proc/self/status 中的 VmRSS
    long rss = 0;  // 单位 kB
    {
        std::ifstream statusFile("/proc/self/status");
        std::string line;
        while (std::getline(statusFile, line)) {
            if (line.compare(0, 6, "VmRSS:") == 0) {
                std::istringstream iss(line.substr(6));
                iss >> rss;
                break;
            }
        }
    }
    if (rss <= 0) {
        // 兜底：/proc/self/statm 第 2 个字段 = 常驻页数
        std::ifstream statmFile("/proc/self/statm");
        long totalPages = 0, residentPages = 0;
        if ((statmFile >> totalPages >> residentPages) && residentPages > 0) {
            long pageSz = sysconf(_SC_PAGE_SIZE);
            if (pageSz > 0) rss = residentPages * pageSz / 1024;
        }
    }
    memMB = rss / 1024.0;

    static const double s_totalMemMB = []() -> double {
        long pages = sysconf(_SC_PHYS_PAGES);
        long pageSize = sysconf(_SC_PAGE_SIZE);
        double total = (pages > 0 && pageSize > 0) ? (pages * pageSize) / (1024.0 * 1024.0) : 0.0;
        // 容器（docker / lxc）里 _SC_PHYS_PAGES 报的是宿主机总量，占比会小得离谱；
        // 有 cgroup 限额就以限额为准
        const char* cgPaths[] = {"/sys/fs/cgroup/memory.max",                    // cgroup v2
                                 "/sys/fs/cgroup/memory/memory.limit_in_bytes"};  // cgroup v1
        for (const char* cgPath : cgPaths) {
            std::ifstream cgFile(cgPath);
            unsigned long long limit = 0;
            if ((cgFile >> limit) && limit > 0 && limit < (1ULL << 62)) {  // "max" 解析失败，直接跳过
                double limitMB = limit / (1024.0 * 1024.0);
                if (limitMB > 0 && (total <= 0.0 || limitMB < total)) total = limitMB;
                break;
            }
        }
        return total > 0.0 ? total : 8192.0;
    }();
    totalMB = s_totalMemMB;
#endif

    // 无论取没取到都必须刷新界面：宁可显示「获取失败」，也不要一直挂着初始值不动
    if (m_ramTextLabel) {
        if (memMB > 0.0) {
            int memPercent = (totalMB > 0.0)
                                 ? qBound(0, static_cast<int>((memMB / totalMB) * 100.0), 100)
                                 : 0;
            m_ramTextLabel->setText(QString("内存消耗: %1 MB / %2 MB (%3%)")
                                        .arg(memMB, 0, 'f', 1)
                                        .arg(totalMB, 0, 'f', 0)
                                        .arg(memPercent));
            if (m_ramProgressBar) m_ramProgressBar->setValue(memPercent);
            if (!memTooltip.isEmpty()) m_ramTextLabel->setToolTip(memTooltip);
        } else {
            m_ramTextLabel->setText(QStringLiteral("内存消耗: 获取失败"));
            if (m_ramProgressBar) m_ramProgressBar->setValue(0);
        }
    }

    // ========== 2. CPU 使用率（带进度条） ==========
    static bool isFirstCpuSample = true;
    static unsigned long long lastUser = 0;
    static unsigned long long lastSystem = 0;
    static unsigned long long lastTotal = 0;   // Linux 系统总 jiffies
    static qint64 lastTickMs = 0;

    qint64 nowTick = QDateTime::currentMSecsSinceEpoch();
    double cpuPercent = 0.0;

#ifdef _WIN32
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
        unsigned long long user = ((unsigned long long)userTime.dwHighDateTime << 32) + userTime.dwLowDateTime;
        unsigned long long system = ((unsigned long long)kernelTime.dwHighDateTime << 32) + kernelTime.dwLowDateTime;
        static int processorCount = []() {
            SYSTEM_INFO sysInfo;
            GetSystemInfo(&sysInfo);
            return (int)sysInfo.dwNumberOfProcessors;
        }();

        if (!isFirstCpuSample && lastTickMs != 0 && nowTick > lastTickMs) {
            unsigned long long deltaMs = nowTick - lastTickMs;
            unsigned long long curTotal = user + system;
            unsigned long long deltaCpuMs = (curTotal - (lastUser + lastSystem)) / 10000;  // 100ns → ms
            cpuPercent = (double)deltaCpuMs / deltaMs / processorCount * 100.0;
            cpuPercent = qBound(0.0, cpuPercent, 100.0);
        }
        lastUser = user;
        lastSystem = system;
    }
#else
    // Linux: 读取进程 CPU jiffies
    unsigned long long user = 0, system = 0;
    std::ifstream statFile("/proc/self/stat");
    std::string statLine;
    if (std::getline(statFile, statLine)) {
        std::istringstream iss(statLine);
        std::vector<std::string> fields;
        std::string field;
        while (iss >> field)
            fields.push_back(field);
        if (fields.size() >= 17) {
            user = std::stoull(fields[13]);
            system = std::stoull(fields[14]);
        }
    }

    // 读取系统总 CPU jiffies
    unsigned long long totalCpu = 0;
    std::ifstream sysStatFile("/proc/stat");
    std::string cpuLine;
    if (std::getline(sysStatFile, cpuLine) && cpuLine.compare(0, 3, "cpu") == 0) {
        std::istringstream cpuIss(cpuLine.substr(3));
        unsigned long long userTotal, niceTotal, systemTotal, idleTotal, iowaitTotal, irqTotal, softirqTotal, stealTotal;
        cpuIss >> userTotal >> niceTotal >> systemTotal >> idleTotal >> iowaitTotal >> irqTotal >> softirqTotal >> stealTotal;
        totalCpu = userTotal + niceTotal + systemTotal + idleTotal + iowaitTotal + irqTotal + softirqTotal + stealTotal;
    }

    if (!isFirstCpuSample && lastTickMs != 0 && nowTick > lastTickMs) {
        unsigned long long curTotal = user + system;
        unsigned long long deltaTotalCpu = totalCpu - lastTotal;
        unsigned long long deltaProcCpu = curTotal - (lastUser + lastSystem);
        if (deltaTotalCpu > 0) {
            cpuPercent = (double)deltaProcCpu / deltaTotalCpu * 100.0;
        }
        cpuPercent = qBound(0.0, cpuPercent, 100.0);
    }
    lastUser = user;
    lastSystem = system;
    lastTotal = totalCpu;
#endif

    m_cpuTextLabel->setText(QString("CPU消耗: %1%").arg(cpuPercent, 0, 'f', 1));
    m_cpuProgressBar->setValue(static_cast<int>(cpuPercent));
    lastTickMs = nowTick;
    isFirstCpuSample = false;

    // ========== 3. 磁盘读写（速率 + 累计） ==========
    qint64 currentRead = 0, currentWrite = 0;   // 声明在外部，供后续统一处理

#ifdef _WIN32
    IO_COUNTERS io;
    if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
        currentRead = io.ReadTransferCount;
        currentWrite = io.WriteTransferCount;
    }
#else
    // Linux: 读取 /proc/self/io
    std::ifstream ioFile("/proc/self/io");
    std::string ioLine;
    while (std::getline(ioFile, ioLine)) {
        if (ioLine.compare(0, 6, "rchar:") == 0) {
            std::istringstream iss(ioLine.substr(6));
            iss >> currentRead;
        } else if (ioLine.compare(0, 6, "wchar:") == 0) {
            std::istringstream iss(ioLine.substr(6));
            iss >> currentWrite;
        }
    }
#endif

    // 格式化输出函数
    auto formatBytes = [](qint64 bytes, const QString& prefix) -> QString {
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unitIdx = 0;
        double val = bytes;
        while (val >= 1024.0 && unitIdx < 3) {
            val /= 1024.0;
            unitIdx++;
        }
        return QString("%1: %2 %3").arg(prefix).arg(val, 0, 'f', 1).arg(units[unitIdx]);
    };

    // 累计值显示
    m_diskReadRateLabel->setText(formatBytes(currentRead, "累计读"));
    m_diskWriteRateLabel->setText(formatBytes(currentWrite, "累计写"));

    // 计算平均速率（总累计 / 运行秒数）
    qint64 elapsedSec = QDateTime::currentSecsSinceEpoch() - g_totalRuntime;
    if (elapsedSec <= 0) elapsedSec = 1;
    double avgReadSpeed = static_cast<double>(currentRead) / elapsedSec;
    double avgWriteSpeed = static_cast<double>(currentWrite) / elapsedSec;

    // 实时速率：使用两次采样之间的差值
    static qint64 lastSampleTime = QDateTime::currentSecsSinceEpoch();
    qint64 nowSec = QDateTime::currentSecsSinceEpoch();
    qint64 deltaSec = nowSec - lastSampleTime;
    if (deltaSec <= 0) deltaSec = 1;
    quint64 readDelta = currentRead - m_lastReadBytes;
    quint64 writeDelta = currentWrite - m_lastWriteBytes;
    double realReadSpeed = static_cast<double>(readDelta) / deltaSec;
    double realWriteSpeed = static_cast<double>(writeDelta) / deltaSec;

    m_diskReadTotalLabel->setText(formatBytes(static_cast<qint64>(avgReadSpeed), "平均") + "/s, " +
                                  formatBytes(static_cast<qint64>(realReadSpeed), "实时"));
    m_diskWriteTotalLabel->setText(formatBytes(static_cast<qint64>(avgWriteSpeed), "平均") + "/s, " +
                                   formatBytes(static_cast<qint64>(realWriteSpeed), "实时"));

    // 更新保存值
    m_lastReadBytes = currentRead;
    m_lastWriteBytes = currentWrite;
    lastSampleTime = nowSec;
}

void HomePage::refreshRuntimeStats()
{
    int onlineCount = 0;
    qint64 runRecv = 0, runSent = 0;
    qint64 totalRecv = 0, totalSent = 0;
    int gAddRun = 0, gDelRun = 0, fAddRun = 0, fDelRun = 0;   // 启动以来 群/好友 增删
    // 「广告观看」卡：左边 累计次数（落盘口径），右上角 今日次数，后面的 / 本月金额走接口
    qint64 adTotal = 0, adToday = 0;
    qint64 activeUsers = 0, activeGroups = 0;                 // 今日活跃用户 / 群（各账号求和）
    for (const auto &account : std::as_const(m_accounts)) {
        if (!account) continue;
        if (account->online) onlineCount++;
        // 首页只看两个口径（AccountInfo 里是独立变量）：
        //   received / sent              → 本次启动以来（进程内，不落盘）
        //   message_received / _sent     → 历史累计（落盘）
        // （今日 received_day/sent_day 首页不直接显示了，插件那套接口照旧用得到）
        runRecv   += account->received;
        runSent   += account->sent;
        totalRecv += account->message_received;
        totalSent += account->message_sent;
        gAddRun += account->加群本次;
        gDelRun += account->退群本次;
        fAddRun += account->好友本次;
        fDelRun += account->删好友本次;
        adTotal += account->ad_watched_total;
        adToday += account->ad_watched_day;
        // 活跃用户 / 活跃群聊：和「数据统计」指令同一个来源
        // （BotDB 里当天的用户表 / 群表，表的大小就是去重后的活跃数）
        if (auto *db = g_botdb.value(account->appid_int, nullptr)) {
            QMutexLocker locker(&db->m_msgMutex);
            activeUsers  += db->m_userDailyMsg.size();
            activeGroups += db->m_groupDailyMsg.size();
        }
    }

    // 图表标题行里的收发三个数（原来是最左边三张卡）
    if (m_chartRecvLabel)     m_chartRecvLabel->setText(
        QString("接收 %1").arg(formatCount(runRecv)));
    if (m_chartSentLabel)     m_chartSentLabel->setText(
        QString("发送 %1").arg(formatCount(runSent)));
    if (m_chartTotalLabel)    m_chartTotalLabel->setText(
        QString("合计 %1 · 累计 %2").arg(formatCountShort(runRecv + runSent),
                                        formatCountShort(totalRecv + totalSent)));

    // 卡片：活跃按今日口径，其余启动以来 + 累计
    if (m_activeDataValue)    m_activeDataValue->setText(
        QString("用户 %1 · 群 %2").arg(formatCountShort(activeUsers), formatCountShort(activeGroups)));
    if (m_pluginCountValue)   m_pluginCountValue->setText(QString::number(m_pluginList.size()));
    if (m_onlineAccountValue) m_onlineAccountValue->setText(QString("%1 / %2").arg(onlineCount).arg(m_accounts.size()));


    m_adTotalCount = adTotal;
    m_adTodayCount = adToday;
    applyAdCardText();
    fetchAdMonth(QDate::currentDate().toString("yyyyMM"));

    if (m_addDelValue)        m_addDelValue->setText(
        QString("%1/%2 %3/%4")
            .arg(formatCountShort(gAddRun), formatCountShort(gDelRun),
                 formatCountShort(fAddRun), formatCountShort(fDelRun)));


    // 插件发送量 TOP 6 榜单（见 refreshPluginMessageRanking）
    refreshPluginMessageRanking();
    updateProcessStats();
}

// 递归清空布局（含子布局里的控件）；末尾的 stretch 也会一起被清掉
static void clearLayoutItems(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->setParent(nullptr);
            w->deleteLater();
            delete item;                  // 包装用的 QWidgetItem，和控件是两块内存
        } else if (QLayout *sub = item->layout()) {
            clearLayoutItems(sub);
            // ⚠ sub 就是 item 本身（QLayout 继承自 QLayoutItem，takeAt 取出来的
            //    子布局就是这个 QLayout 的地址）—— 删一次就够了。
            // 旧代码这里 delete sub 之后又落到下面的 delete item，
            // 同一块堆内存释放两遍 → ntdll 报 c0000374（堆损坏）→ fail-fast，
            // 进程当场闪退、连 dump 都留不下。首页榜单重建时必踩。
            delete sub;
        } else {
            delete item;                  // spacer / stretch 之类
        }
    }
}

void HomePage::updatePluginMessageCount(const QString &pluginName, int count)
{
    if (!m_pluginMessageLayout) return;

    if (m_pluginMessageCounts.contains(pluginName)) {
        m_pluginMessageCounts[pluginName]->setText(QString::number(count));
        return;
    }

    // 名次 = 已建好的条目数 + 1（重建时按榜单顺序逐条调用，天然有序）
    const int rank = m_pluginMessageCounts.size() + 1;

    auto row = new QWidget;
    row->setFixedHeight(28);
    auto rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto rankLabel = createLabel(QString::number(rank), rank == 1 ? "rankBadgeTop" : "rankBadge");
    rankLabel->setAlignment(Qt::AlignCenter);
    rankLabel->setFixedSize(18, 18);
    rowLayout->addWidget(rankLabel);

    // 插件名过长时中间省略（卡片窄，硬截断很难看），完整名放 tooltip
    // 字号/字重要和 QSS 里 #conversationName 一致，量字宽才准（QSS 字体要 polish 后才生效，
    // 建的时候拿不到，所以这里按同样参数手工构造一份用于测量）
    auto nameLabel = new QLabel;
    nameLabel->setObjectName("conversationName");
    QFont nameFont = nameLabel->font();
    nameFont.setPixelSize(13);
    nameFont.setBold(true);
    nameLabel->setText(QFontMetrics(nameFont).elidedText(pluginName, Qt::ElideMiddle, 126));
    nameLabel->setToolTip(pluginName);
    nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rowLayout->addWidget(nameLabel, 1);

    auto countLabel = createLabel(QString::number(count), "rankCount");
    countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    countLabel->setMinimumWidth(22);
    m_pluginMessageCounts[pluginName] = countLabel;
    rowLayout->addWidget(countLabel);

    m_pluginMessageLayout->addWidget(row);
}

void HomePage::refreshPluginMessageRanking()
{
    if (!m_pluginMessageLayout) return;

    // 1. 收集发过消息（SendQuantity > 0）的插件，按发送量降序；并列时按名称，保证顺序稳定
    QList<QPair<int, QString>> ranked;
    ranked.reserve(m_pluginList.size());
    for (const PluginInfo &plugin : std::as_const(m_pluginList)) {
        if (plugin.SendQuantity <= 0) continue;   // 没发过消息的不占榜单名额
        ranked.append(QPair<int, QString>(plugin.SendQuantity,
                                          plugin.name.isEmpty() ? plugin.path : plugin.name));
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  if (a.first != b.first) return a.first > b.first;
                  return a.second < b.second;
              });
    // 只留前 kTopPluginCount 个（Qt5 的 QList 没有 resize，reserve 又不会缩短，
    // 原来那句 `ranked.reserve(...)` 等于没限制，榜单一长就会把所有插件都画出来）
    while (ranked.size() > kTopPluginCount) ranked.removeLast();

    QStringList order;
    order.reserve(ranked.size());
    int totalSent = 0;
    for (const auto &item : ranked) {
        order << item.second;
        totalSent += item.first;
    }

    // 标题右侧的小计
    if (m_pluginRankSummary) {
        m_pluginRankSummary->setText(ranked.isEmpty()
                                         ? QString()
                                         : QStringLiteral("%1 条").arg(formatCount(totalSent)));
    }

    // 2. 榜单成员和顺序都没变：只刷新数字（首页定时器每秒会调到这里，重建会闪）
    if (m_pluginRankReady && order == m_pluginRankOrder) {
        for (const auto &item : ranked) {
            if (QLabel *label = m_pluginMessageCounts.value(item.second))
                label->setText(QString::number(item.first));
        }
        return;
    }

    // 3. 有变化（含首次、插件上下榜、排名互换）：整体重建
    m_pluginRankOrder = order;
    m_pluginRankReady = true;
    clearLayoutItems(m_pluginMessageLayout);
    m_pluginMessageCounts.clear();

    if (ranked.isEmpty()) {
        m_pluginMessageLayout->addStretch(1);
        auto hint = createLabel(QStringLiteral("暂无插件发送记录"), "rankEmpty");
        hint->setAlignment(Qt::AlignCenter);
        m_pluginMessageLayout->addWidget(hint);
        m_pluginMessageLayout->addStretch(1);
        return;
    }

    for (const auto &item : ranked)
        updatePluginMessageCount(item.second, item.first);

    // 空位补满 6 格：榜单只有一两行时，右下角不至于空出一大块白
    for (int i = ranked.size() + 1; i <= kTopPluginCount; ++i) {
        auto row = new QWidget;
        row->setFixedHeight(28);
        auto rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto rankLabel = createLabel(QString::number(i), "rankBadgeMuted");
        rankLabel->setAlignment(Qt::AlignCenter);
        rankLabel->setFixedSize(18, 18);
        rowLayout->addWidget(rankLabel);

        rowLayout->addWidget(createLabel(QStringLiteral("—"), "rankEmpty"), 1);

        auto countLabel = createLabel(QString(), "rankCountMuted");
        countLabel->setMinimumWidth(22);
        rowLayout->addWidget(countLabel);

        m_pluginMessageLayout->addWidget(row);
    }
    m_pluginMessageLayout->addStretch();
}

void HomePage::refreshPluginList()
{
    refreshRuntimeStats();
}



static QString adMoneyText(double yuan)
{
    return QStringLiteral("¥%1").arg(yuan, 0, 'f', 2);
}

static QString adRangeText(const QDate &start, const QDate &end)
{
    if (!start.isValid() || !end.isValid()) return QString();
    return QString("%1 ~ %2").arg(start.toString("yyyy-MM-dd"), end.toString("yyyy-MM-dd"));
}

// 某个月、某个账号的每日明细；appid <= 0 = 所有账号按天求和。
// 卡片和明细窗的「全部」都走这里，选了具体账号就只取那一只的。
QHash<QString, AdDayStat> HomePage::adDays(const QString &ym, int appid) const
{
    if (appid > 0) return m_adDaily.value(appid).value(ym);

    QHash<QString, AdDayStat> out;
    for (auto acc = m_adDaily.constBegin(); acc != m_adDaily.constEnd(); ++acc) {
        const QHash<QString, AdDayStat> days = acc.value().value(ym);
        for (auto d = days.constBegin(); d != days.constEnd(); ++d) {
            AdDayStat &dst = out[d.key()];
            dst.exposure += d.value().exposure;
            dst.income   += d.value().income;
        }
    }
    return out;
}

// 卡片文案：累计次数 / 本月金额
void HomePage::applyAdCardText()
{
    if (!m_adValue && !m_adSub) return;

    const QString ym = QDate::currentDate().toString("yyyyMM");
    const QHash<QString, AdDayStat> bucket = adDays(ym);      // 卡片永远看「全部账号」
    QString money;
    if (m_adLoading.contains(ym)) {
        money = QStringLiteral("¥0");                       // 正在取
    } else if (!bucket.isEmpty()) {
        // 有数据就显示数据（哪怕这轮后面某个账号被拒也无所谓，已拿到的照算）
        double sum = 0.0;
        for (auto it = bucket.constBegin(); it != bucket.constEnd(); ++it)
            sum += it.value().income;
        money = adMoneyText(sum);
    } else if (m_adFailDate.isValid() && m_adFailDate == QDate::currentDate()) {
        money = QStringLiteral("¥0");                        // 平台明确拒绝过，当天不再请求
    } else if (m_adFailed.contains(ym)) {
        money = QStringLiteral("¥0");                        // 整轮失败（多半没登录 / 没广告权限）
    } else {
        money = QStringLiteral("¥0");                       // 还没取过
    }

    if (m_adValue) m_adValue->setText(QString("%1 / %2").arg(formatCount(m_adTotalCount), money));
    if (m_adSub)   m_adSub->setText(QString("今日 %1").arg(formatCountShort(m_adTodayCount)));
}

// 按月份取数：ym = "yyyyMM"。force=true 跳过 10 分钟节流。
void HomePage::fetchAdMonth(const QString &ym, bool force)
{
    if (ym.size() != 6) return;
    const int y  = ym.left(4).toInt();
    const int mo = ym.mid(4, 2).toInt();
    if (y < 2000 || mo < 1 || mo > 12) return;

    // 一轮一次：明细窗切月、首页 3 秒刷新都可能同一时刻来，别把队列冲掉
    if (!m_adFetchYm.isEmpty()) return;

    const QDate today = QDate::currentDate();

    // ── 平台明确拒绝过（返回体带 message，比如「能力暂未开放」）→ 当天一个请求都不发 ──
    //    和接口的其它失败不一样：这种重试一万次也是同一个结果，只会白白刷 trace_id。
    //    日期落在 g_config 里，重启也认；到第二天自动解除（也可以手动清配置里的这项）。
    if (!m_adFailDate.isValid()) {
        m_adFailDate = QDate::fromString(g_config.value(QStringLiteral("ad_api_fail_date")).toString(),
                                         QStringLiteral("yyyyMMdd"));
    }
    if (!force && m_adFailDate.isValid() && m_adFailDate == today) return;

    QDate start(y, mo, 1);
    QDate end = start.addMonths(1).addDays(-1);
    if (end > today) end = today;        // 未来的日子平台肯定没数据，别白问
    if (start > today) return;           // 还没到那个月

    const QString curYm = today.toString("yyyyMM");
    if (!force && m_adFetchedAt.contains(ym)) {
        // 上一轮整轮失败（启动时 token 还没就绪、或该机器人没广告权限）→ 1 分钟后再试，别干等 10 分钟
        const int gateSec = m_adFailed.contains(ym) ? 60 : 600;
        if (ym != curYm && gateSec >= 600) return;   // 历史月份：一天就那一份数，取过就不动
        if (m_adFetchedAt.value(ym).secsTo(QDateTime::currentDateTime()) < gateSec) return;
    }

    // 只问「已登录、client 还在」的账号：没 token 的发出去也是 401，白占一轮
    QStringList appids;
    for (const auto &account : std::as_const(m_accounts)) {
        if (!account) continue;
        if (!m_botClients.value(account->appid_int, nullptr)) continue;
        appids << QString::number(account->appid_int);
    }

    if (appids.isEmpty()) {
        // 没有账号可问（一个都没登录 / client 已经没了）→ 记一笔，别让首页每 3 秒重试
        m_adFetchedAt.insert(ym, QDateTime::currentDateTime());
        m_adFailed.insert(ym);
        applyAdCardText();
        if (m_adDialog) rebuildAdDialog();
        return;
    }

    m_adFetchYm    = ym;
    m_adFetchStart = start;
    m_adFetchEnd   = end;
    m_adFetchQueue = appids;
    // 整月重算：先把这一轮要问的账号的旧桶清掉，不跟旧值叠加
    for (const QString &id : std::as_const(appids))
        m_adDaily[id.toInt()].remove(ym);
    m_adFetchOk = false;                 // 有任何一条成功就算这一轮成功（见 mergeAdRows）
    m_adLoading.insert(ym);
    adFetchNext();
}

// 串行问下一个账号：回调里只解析 JSON，状态改动一律回主线程
void HomePage::adFetchNext()
{
    if (m_adFetchYm.isEmpty()) return;

    while (!m_adFetchQueue.isEmpty()) {
        const int appid = m_adFetchQueue.takeFirst().toInt();
        QQBotClient *client = m_botClients.value(appid, nullptr);
        if (!client) continue;           // 这一轮里账号被删了，跳过

        const QString s = m_adFetchStart.toString("yyyyMMdd");
        const QString e = m_adFetchEnd.toString("yyyyMMdd");
        QPointer<HomePage> self(this);
        client->getAdData(s, e,
            [self, appid, s, e](const QString &resp, QNetworkReply::NetworkError err) {
                // ⚠ 这个回调在线程池里跑：只做纯局部解析，别碰任何成员 / UI
                QHash<QString, AdDayStat> days;
                QString hardMsg;
                const QJsonObject root = QJsonDocument::fromJson(resp.toUtf8()).object();
                if (root.contains(QStringLiteral("message"))) {
                    hardMsg = root.value(QStringLiteral("message")).toString();
                    if (hardMsg.isEmpty()) hardMsg = QStringLiteral("平台返回了错误（无描述）");
                }
                if (hardMsg.isEmpty() && err == QNetworkReply::NoError) {
                    const QJsonArray list = root.value("list").toArray();
                    for (const QJsonValue &v : list) {
                        const QJsonObject o = v.toObject();
                        const QString date = o.value("date").toString();
                        if (date.size() != 8) continue;
                        AdDayStat st;
                        st.exposure = static_cast<qint64>(o.value("exposure_count").toDouble());
                        st.income   = o.value("income").toDouble();
                        days.insert(date, st);
                    }
                } else if (hardMsg.isEmpty()) {
                    qDebug() << "[广告数据] 拉取失败 appid=" << appid
                             << " 区间=" << s << e << " 错误=" << err << resp.left(200);
                }
                if (!self) return;
                QMetaObject::invokeMethod(self.data(),
                    [self, appid, days, ok = (err == QNetworkReply::NoError), hardMsg]() {
                        if (!self) return;
                        self->mergeAdRows(appid, days, ok, hardMsg);
                    }, Qt::QueuedConnection);
            });
        return;                          // 一次只发一条，回包后再发下一条
    }

    finishAdFetch();                     // 队列空了 = 这一轮结束
}

bool HomePage::eventFilter(QObject *watched, QEvent *event)
{
    // 「广告观看」卡片：松开鼠标就弹明细窗（子控件已设成鼠标透明，事件都能落到这里）
    if (watched == m_adCard && event->type() == QEvent::MouseButtonRelease) {
        showAdDetail();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void HomePage::mergeAdRows(int appid, const QHash<QString, AdDayStat> &days, bool ok, const QString &hardMsg)
{
    if (m_adFetchYm.isEmpty()) return;

    if (!hardMsg.isEmpty()) {
        // 平台明确拒绝：这一轮剩下的账号也不用问了，直接收摊，并且当天封盘
        m_adHardFail = true;
        m_adFailMsg  = hardMsg;
        m_adFetchQueue.clear();
        adFetchNext();                   // 队列已空 → finishAdFetch
        return;
    }

    if (ok) m_adFetchOk = true;
    // 按账号分开存：明细窗可以单选某一只机器人看它自己的收益
    QHash<QString, AdDayStat> &bucket = m_adDaily[appid][m_adFetchYm];
    for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
        AdDayStat &dst = bucket[it.key()];
        dst.exposure += it.value().exposure;
        dst.income   += it.value().income;
    }
    adFetchNext();
}

void HomePage::finishAdFetch()
{
    const QString ym = m_adFetchYm;
    m_adFetchYm.clear();
    m_adFetchStart = QDate();
    m_adFetchEnd   = QDate();
    if (ym.isEmpty()) return;

    m_adLoading.remove(ym);
    m_adFetchedAt.insert(ym, QDateTime::currentDateTime());

    if (m_adHardFail) {
        // 「能力暂未开放」这类：当天不再发任何请求（落盘，重启也认；到第二天自动解除）
        m_adHardFail = false;
        m_adFailed.insert(ym);
        m_adFailDate = QDate::currentDate();
        g_config[QStringLiteral("ad_api_fail_date")] = m_adFailDate.toString(QStringLiteral("yyyyMMdd"));
        saveConfig();
        qDebug() << "[广告数据] 平台明确拒绝，今天不再请求：" << m_adFailMsg;
    } else if (m_adFetchOk) {
        m_adFailed.remove(ym);
        m_adFailMsg.clear();
        // 之前封过盘、这次（手动「再试一次」）拿到了数据 → 解除封盘
        if (m_adFailDate.isValid() && m_adFailDate == QDate::currentDate()) {
            m_adFailDate = QDate();
            g_config.remove(QStringLiteral("ad_api_fail_date"));
            saveConfig();
        }
    } else {
        m_adFailed.insert(ym);           // 整轮全失败（网络/token）→ 1 分钟后再试
    }

    applyAdCardText();
    if (m_adDialog) rebuildAdDialog();

    // 明细窗停在被排队挤掉的月份（刚才一轮在途时点过上月）→ 补一次
    // 被平台拒绝的情况不补：当天已经封盘，fetchAdMonth 自己会挡住
    if (m_adDialog && m_adDialogMonth != ym && !m_adFetchedAt.contains(m_adDialogMonth))
        fetchAdMonth(m_adDialogMonth);
}

void HomePage::showAdDetail()
{
    if (!m_adDialog) {
        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(QStringLiteral("广告数据"));
        dlg->resize(620, 600);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setStyleSheet(QStringLiteral(
            "QDialog { background: #FFF8EF; }"
            "QScrollArea { background: transparent; }"
            "QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
            "QScrollBar::handle:vertical { background: #EADFD2; border-radius: 4px; min-height: 24px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));
        auto *lay = new QVBoxLayout(dlg);
        lay->setContentsMargins(16, 14, 16, 14);
        lay->setSpacing(10);

        m_adDialog       = dlg;
        m_adDialogLayout = lay;
        connect(dlg, &QObject::destroyed, this, [this]() {
            m_adDialogLayout = nullptr;      // m_adDialog 是 QPointer，自己会置空
        });
    }

    m_adDialogMonth = QDate::currentDate().toString("yyyyMM");
    fetchAdMonth(m_adDialogMonth);           // 有缓存 / 在途都安全（内部节流）
    rebuildAdDialog();
    m_adDialog->show();
    m_adDialog->raise();
    m_adDialog->activateWindow();
}

void HomePage::rebuildAdDialog()
{
    if (!m_adDialog || !m_adDialogLayout) return;

    const QDate today   = QDate::currentDate();
    const QString curYm  = today.toString("yyyyMM");
    const QString prevYm = today.addMonths(-1).toString("yyyyMM");
    const QString ym     = m_adDialogMonth.isEmpty() ? curYm : m_adDialogMonth;

    clearLayoutItems(m_adDialogLayout);

    // ---- 标题行：标题 + 区间 + 本月/上月 ----
    auto monthRange = [today](const QString &m6, QDate &s, QDate &e) {
        const int y  = m6.left(4).toInt();
        const int mo = m6.mid(4, 2).toInt();
        s = QDate(y, mo, 1);
        e = s.addMonths(1).addDays(-1);
        if (e > today) e = today;
    };
    QDate rs, re;
    monthRange(ym, rs, re);

    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    auto *title = new QLabel(QStringLiteral("📺 广告数据"), m_adDialog);
    title->setStyleSheet("font-size:16px; font-weight:800; color:#17202A; background:transparent;");
    head->addWidget(title);
    auto *rangeLab = new QLabel(adRangeText(rs, re), m_adDialog);
    rangeLab->setStyleSheet("color:#8A94A6; font-size:12px; background:transparent;");
    head->addWidget(rangeLab);
    head->addStretch();

    auto makeMonthBtn = [this, ym](const QString &text, const QString &target) {
        auto *btn = new QPushButton(text, m_adDialog);
        const bool on = (ym == target);
        btn->setCheckable(true);
        btn->setChecked(on);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(on
            ? QStringLiteral("QPushButton{background:#FF914D;color:#FFFFFF;border:none;border-radius:9px;padding:4px 12px;font-size:12px;font-weight:700;}")
            : QStringLiteral("QPushButton{background:#FFFFFF;color:#7A8496;border:1px solid #F3E7DA;border-radius:9px;padding:4px 12px;font-size:12px;}"
                             "QPushButton:hover{color:#FF914D;border-color:#FFD3B0;}"));
        connect(btn, &QPushButton::clicked, this, [this, target]() {
            m_adDialogMonth = target;
            fetchAdMonth(target);
            rebuildAdDialog();
        });
        return btn;
    };
    head->addWidget(makeMonthBtn(QStringLiteral("本月"), curYm));
    head->addWidget(makeMonthBtn(QStringLiteral("上月"), prevYm));
    m_adDialogLayout->addLayout(head);

    // ---- 合计（选了具体机器人就只统计它）----
    const QHash<QString, AdDayStat> bucket = adDays(ym, m_adDialogAppid);
    qint64 sumExposure = 0;
    double sumIncome = 0.0, maxIncome = 0.0;
    for (auto it = bucket.constBegin(); it != bucket.constEnd(); ++it) {
        sumExposure += it.value().exposure;
        sumIncome   += it.value().income;
        if (it.value().income > maxIncome) maxIncome = it.value().income;
    }

    auto *sumRow = new QHBoxLayout;
    sumRow->setSpacing(8);

    // 机器人下拉：全部（求和）/ 某一只（只看它自己的收益）
    auto *accBox = new QComboBox(m_adDialog);
    accBox->addItem(QStringLiteral("全部机器人"), 0);
    for (const auto &acc : std::as_const(m_accounts)) {
        if (!acc) continue;
        const QString name = acc->nickname.isEmpty() ? QString::number(acc->appid_int) : acc->nickname;
        accBox->addItem(QStringLiteral("%1 · %2").arg(name, QString::number(acc->appid_int)), acc->appid_int);
    }
    int selIdx = 0;                        // 原来选的账号被删了 → 回落「全部」
    for (int i = 0; i < accBox->count(); ++i) {
        if (accBox->itemData(i).toInt() == m_adDialogAppid) { selIdx = i; break; }
    }
    accBox->setCurrentIndex(selIdx);
    m_adDialogAppid = accBox->itemData(selIdx).toInt();
    accBox->setMinimumWidth(170);
    accBox->setCursor(Qt::PointingHandCursor);
    accBox->setStyleSheet(QStringLiteral(
        "QComboBox{background:#FFFFFF;border:1px solid #F3E7DA;border-radius:9px;padding:3px 8px;font-size:12px;color:#17202A;}"
        "QComboBox:hover{border-color:#FFD3B0;}"
        "QComboBox::drop-down{border:none;width:16px;}"
        "QComboBox QAbstractItemView{background:#FFFFFF;border:1px solid #F3E7DA;outline:none;"
        "selection-background-color:#FFF1E6;selection-color:#D9542B;}"));
    // ⚠ 这里重建会把 accBox 自己（以及它的弹窗）删掉，所以只用 deleteLater，见 clearLayoutItems
    connect(accBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, accBox](int idx) {
                m_adDialogAppid = accBox->itemData(idx).toInt();
                rebuildAdDialog();
            });
    sumRow->addWidget(accBox);

    auto addChip = [&](const QString &text, bool strong) {
        auto *l = new QLabel(text, m_adDialog);
        l->setStyleSheet(strong
            ? QStringLiteral("background:#FDEBE8; color:#D9542B; border-radius:8px; padding:3px 9px; font-size:12px; font-weight:800;")
            : QStringLiteral("background:#FFF4E8; color:#4A5568; border-radius:8px; padding:3px 9px; font-size:12px;"));
        sumRow->addWidget(l);
    };
    addChip(QStringLiteral("预计收入 %1").arg(adMoneyText(sumIncome)), true);
    addChip(QStringLiteral("曝光 %1 PV").arg(formatCount(sumExposure)), false);
    addChip(QStringLiteral("有数据 %1 天").arg(bucket.size()), false);
    sumRow->addStretch();
    m_adDialogLayout->addLayout(sumRow);

    // ---- 每日明细（日期 · 收入柱条 · 曝光 · 金额）----
    auto *scroll = new QScrollArea(m_adDialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *listHost = new QWidget;
    auto *listLay = new QVBoxLayout(listHost);
    listLay->setContentsMargins(0, 0, 6, 0);
    listLay->setSpacing(5);

    auto addHint = [&](const QString &text) {
        auto *h = new QLabel(text, listHost);
        h->setAlignment(Qt::AlignCenter);
        h->setWordWrap(true);
        h->setStyleSheet("color:#AEB6C2; font-size:13px; padding:28px 10px; background:transparent;");
        listLay->addWidget(h);
    };

    if (m_adLoading.contains(ym)) {
        addHint(QStringLiteral("正在向平台查询广告数据…"));
    } else if (bucket.isEmpty()) {
        QString hint;
        if (m_adFailDate.isValid() && m_adFailDate == today) {
            // 平台明确拒绝过：这句话得说清楚，不然用户会以为是本软件没取到
            hint = m_adFailMsg.isEmpty()
                ? QStringLiteral("平台拒绝了广告数据请求（能力暂未开放 / 无权限）\n今天不再请求，明天会自动再试")
                : QStringLiteral("平台返回：%1\n该能力未开放，重试也没用 —— 今天不再请求，明天会自动再试")
                      .arg(m_adFailMsg);
        } else if (m_adFailed.contains(ym)) {
            hint = QStringLiteral("拉取失败：账号未登录，或网络不通\n（每分钟会自动重试一次）");
        } else {
            hint = QStringLiteral("这段时间没有广告曝光数据\n（本月还没人看广告，或该机器人未开通广告能力）");
        }
        addHint(hint);

        // 被封盘时留一个手动口子：平台哪天开能力了不用等第二天，也不用去改 config.json
        if (m_adFailDate.isValid() && m_adFailDate == today) {
            auto *retryBtn = new QPushButton(QStringLiteral("再试一次"), listHost);
            retryBtn->setCursor(Qt::PointingHandCursor);
            retryBtn->setStyleSheet(QStringLiteral(
                "QPushButton{background:#FFFFFF;color:#D9542B;border:1px solid #F3C6B4;border-radius:9px;"
                "padding:5px 16px;font-size:12px;}"
                "QPushButton:hover{background:#FFF1EA;}"));
            connect(retryBtn, &QPushButton::clicked, this, [this, ym]() {
                m_adFailDate = QDate();      // 手动放行这一次
                m_adFailMsg.clear();
                g_config.remove(QStringLiteral("ad_api_fail_date"));
                fetchAdMonth(ym, true);
                rebuildAdDialog();
            });
            listLay->addWidget(retryBtn, 0, Qt::AlignHCenter);
        }
    } else {
        QStringList dates = bucket.keys();
        std::sort(dates.begin(), dates.end(), [](const QString &a, const QString &b) { return a > b; });
        for (const QString &date : dates) {
            const AdDayStat st = bucket.value(date);

            auto *row = new QWidget(listHost);
            auto *rl = new QHBoxLayout(row);
            rl->setContentsMargins(6, 3, 6, 3);
            rl->setSpacing(8);

            auto *dateLab = new QLabel(QString("%1-%2-%3").arg(date.left(4), date.mid(4, 2), date.mid(6, 2)), row);
            dateLab->setStyleSheet("color:#4A5568; font-size:12px; background:transparent;");
            dateLab->setFixedWidth(88);
            rl->addWidget(dateLab);

            auto *bar = new QProgressBar(row);
            bar->setRange(0, 1000);
            bar->setValue(maxIncome > 0.0 ? int(qRound(st.income * 1000.0 / maxIncome)) : 0);
            bar->setTextVisible(false);
            bar->setFixedHeight(6);
            bar->setStyleSheet(QStringLiteral(
                "QProgressBar{background:#F5ECE1;border:none;border-radius:3px;}"
                "QProgressBar::chunk{background:#FF914D;border-radius:3px;}"));
            rl->addWidget(bar, 1);

            auto *pvLab = new QLabel(QString("%1 PV").arg(formatCount(st.exposure)), row);
            pvLab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pvLab->setStyleSheet("color:#8A94A6; font-size:11px; background:transparent;");
            pvLab->setFixedWidth(80);
            rl->addWidget(pvLab);

            auto *moneyLab = new QLabel(adMoneyText(st.income), row);
            moneyLab->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            moneyLab->setStyleSheet("color:#17202A; font-size:12px; font-weight:700; background:transparent;");
            moneyLab->setFixedWidth(78);
            rl->addWidget(moneyLab);

            listLay->addWidget(row);
        }
    }
    listLay->addStretch();
    scroll->setWidget(listHost);
    m_adDialogLayout->addWidget(scroll, 1);

    auto *note = new QLabel(QStringLiteral(
        "金额为平台每日预计广告收入（元），未扣平台分成、服务费及税务成本，不是实际结算收入；"
        "多账号已合并，曝光量与金额均为各账号之和。"), m_adDialog);
    note->setWordWrap(true);
    note->setStyleSheet("color:#AEB6C2; font-size:11px; background:transparent;");
    m_adDialogLayout->addWidget(note);
}
