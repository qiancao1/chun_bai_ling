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
#include "global.h"
#include "themecolors.h"   // 页面自带样式表也要跟着主题走

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
    // 紧凑卡：左图标、右边「标题 + 副值 / 大数值」两行。整卡高约 52px（原来 4 个大卡约 96px），
    // 省下来的横向空间就用来多摆两张卡。
    auto createStatCard = [&](int col, const QString &icon, const QString &title,
                              const QString &value, const QString &sub, const QString &iconBg,
                              const QString &tip) {
        auto card = createPanel("statCard");
        card->setToolTip(tip);

        auto layout = new QHBoxLayout(card);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(8);

        auto iconLabel = createLabel(icon, "statIcon");
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet(QString("QLabel#statIcon { background: %1; font-size: 15px; }").arg(iconBg));

        auto textLayout = new QVBoxLayout;
        textLayout->setSpacing(0);
        textLayout->setContentsMargins(0, 0, 0, 0);

        // 第一行：标题 +（右对齐的）副值
        auto titleRow = new QHBoxLayout;
        titleRow->setSpacing(4);
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->addWidget(createLabel(title, "statTitle"));
        titleRow->addStretch();
        if (!sub.isEmpty()) {
            QLabel *subLabel = createLabel(sub, "statSub");
            subLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            titleRow->addWidget(subLabel);
            if (title == "今日消息")   m_totalMessageValue = subLabel;
            if (title == "今日加群")   m_todayQuitValue   = subLabel;
            if (title == "今日发送")   m_todaySentSub     = subLabel;
            if (title == "今日接收")   m_todayReceivedSub = subLabel;
        }
        textLayout->addLayout(titleRow);

        auto valueLabel = createLabel(value, "statValue");
        if (title == "今日消息")   m_todayMessageValue  = valueLabel;
        if (title == "今日发送")   m_todaySentValue     = valueLabel;
        if (title == "今日接收")   m_todayReceivedValue = valueLabel;
        if (title == "在线账号")   m_onlineAccountValue = valueLabel;
        if (title == "今日加群")   m_logCountValue      = valueLabel;
        if (title == "插件数量") {
            valueLabel->setObjectName("pluginCountValue");
            m_pluginCountValue = valueLabel;
        }
        textLayout->addWidget(valueLabel);

        layout->addWidget(iconLabel);
        layout->addLayout(textLayout, 1);

        statsLayout->addWidget(card, 0, col);
        statsLayout->setColumnStretch(col, 1);
    };

    // 三个口径：主数值 = 当天（跨天清零）、右下副值 = 累计 / 本次运行
    createStatCard(0, "💬", "今日消息", "0", "累计 0", "#E8F4FD",
                   "今日收发消息合计（右下为程序里的历史累计量 message_received+sent）");
    createStatCard(1, "📤", "今日发送", "0", "本次 0", "#FFF3E0",
                   "今日由机器人发送的消息数（右下为本次运行以来的发送量 sent）");
    createStatCard(2, "📥", "今日接收", "0", "本次 0", "#E8F8EE",
                   "今日机器人收到的消息数（右下为本次运行以来的接收量 received）");
    createStatCard(3, "🤖", "在线账号", "0 / 0", "", "#F3E5F5",
                   "已登录 / 已配置 的账号数");
    createStatCard(4, "🧩", "插件数量", QString::number(m_pluginList.size()), "", "#FDF1E3",
                   "已加载的插件数量");
    createStatCard(5, "🏠", "今日加群", "0", "退 0", "#EDF3FB",
                   "今日加群数（副值为今日退群数）");
}

QFrame* HomePage::createChartPanel() {
    auto chartPanel = createPanel("chartPanel");
    auto chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(14, 10, 14, 8);   // 圆角 18px，标题别贴着边框
    chartLayout->setSpacing(4);

    // 标题行（保持不变）
    auto chartTop = new QHBoxLayout;
    chartTop->addWidget(createLabel("消息趋势", "sectionTitle"));
    chartTop->addStretch();
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
    statsLayout->setSpacing(8);          // 6 张紧凑卡，间距收一点才排得开
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
    qint64 todayRecv = 0, todaySent = 0, totalMsg = 0;
    qint64 runRecv = 0, runSent = 0;
    int jiaq = 0, tq = 0;
    for (const auto &account : std::as_const(m_accounts)) {
        if (!account) continue;
        if (account->online) onlineCount++;
        // 三个口径（AccountInfo 里是三组独立变量）：
        //   received_day / sent_day      → 当天，跨天清零（AccountInfo::checkDailyReset）
        //   received / sent              → 本次运行以来（进程内，不落盘）
        //   message_received / _sent     → 历史累计（落盘）
        todayRecv += account->received_day;
        todaySent += account->sent_day;
        runRecv   += account->received;
        runSent   += account->sent;
        totalMsg  += account->message_received + account->message_sent;   // 累计
        jiaq += account->今日加群数量;
        tq   += account->今日退群数量;
    }

    const int pluginCount = m_pluginList.size();

    if (m_todayMessageValue)  m_todayMessageValue->setText(formatCount(todayRecv + todaySent));
    if (m_totalMessageValue)  m_totalMessageValue->setText(QString("累计 %1").arg(formatCountShort(totalMsg)));
    if (m_todaySentValue)     m_todaySentValue->setText(formatCount(todaySent));
    if (m_todayReceivedValue) m_todayReceivedValue->setText(formatCount(todayRecv));
    if (m_todaySentSub)       m_todaySentSub->setText(QString("本次 %1").arg(formatCountShort(runSent)));
    if (m_todayReceivedSub)   m_todayReceivedSub->setText(QString("本次 %1").arg(formatCountShort(runRecv)));
    if (m_onlineAccountValue) m_onlineAccountValue->setText(QString("%1 / %2").arg(onlineCount).arg(m_accounts.size()));
    if (m_logCountValue)      m_logCountValue->setText(formatCount(jiaq));
    if (m_todayQuitValue)     m_todayQuitValue->setText(QString("退 %1").arg(formatCount(tq)));
    if (m_pluginCountValue)   m_pluginCountValue->setText(QString::number(pluginCount));


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
    if (m_pluginCountValue) m_pluginCountValue->setText(QString::number(m_pluginList.size()));

    refreshRuntimeStats();
}
