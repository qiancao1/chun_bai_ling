#include "HomePage.h"


#define PSAPI_VERSION 2

#include <windows.h>
#include <Psapi.h> // 仍然需要这个头文件，用于 PROCESS_MEMORY_COUNTERS 结构体声明
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QProgressBar>
#include <QGraphicsDropShadowEffect>
#include <QSysInfo>
#include "global.h"

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
    m_hProcess = GetCurrentProcess();
    // 初始化磁盘 IO 历史
    IO_COUNTERS ioCounters;
    if (GetProcessIoCounters(m_hProcess, &ioCounters)) {
        m_lastRead = ioCounters.ReadTransferCount;
        m_lastWrite = ioCounters.WriteTransferCount;
    }
    m_lastTime = GetTickCount64();
    setupUI();
    setStyleSheetA();
}
void HomePage::setStyleSheetA()
{
    setStyleSheet(R"(
        QWidget#homePage {
            background: #FFF8EF;
        }
        QFrame#dashboardPanel, QFrame#statCard, QFrame#heroPanel, QFrame#chartPanel, QFrame#recentPanel, QFrame#statusPanel, QFrame#quickPanel {
            background: #FFFFFF;
            border: 1px solid #F3E7DA;
            border-radius: 18px;
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
            min-width: 44px;
            min-height: 44px;
            max-width: 44px;
            max-height: 44px;
            border-radius: 14px;
            background: #FFF0DE;
            color: #FF914D;
            font-size: 22px;
        }
        QLabel#statTitle {
            color: #687589;
            font-size: 12px;
            background: transparent;
        }
        QLabel#statValue, QLabel#pluginCountValue {
            color: #17202A;
            font-size: 24px;
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
        QPushButton#quickButton {
            background: #FFF7EA;
            color: #687589;
            border-radius: 16px;
            min-height: 62px;
            font-weight: 700;
        }
        QPushButton#quickButton:hover {
            background: #FFF0DE;
            color: #FF7F32;
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
    )");

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
    auto createStatCard = [&](int col, const QString &icon, const QString &title,
                              const QString &value, const QString &trend, const QString &iconBg) {
        auto card = createPanel("statCard");
        auto layout = new QHBoxLayout(card);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(14);
        auto iconLabel = createLabel(icon, "statIcon");
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setStyleSheet(QString("QLabel#statIcon { background: %1; font-size: 16px; }").arg(iconBg));
        auto textLayout = new QVBoxLayout;
        textLayout->setSpacing(3);
        textLayout->addWidget(createLabel(title, "statTitle"));
        auto valueLabel = createLabel(value, "statValue");
        if (title == "今日消息数") m_todayMessageValue = valueLabel;
        if (title == "在线账号数") m_onlineAccountValue = valueLabel;
        if (title == "日志数量") m_logCountValue = valueLabel;
        if (title == "插件数量") {
            valueLabel->setObjectName("pluginCountValue");
            m_pluginCountValue = valueLabel;
        }
        textLayout->addWidget(valueLabel);
        textLayout->addWidget(createLabel(trend, "trendUp"));
        layout->addWidget(iconLabel);
        layout->addLayout(textLayout, 1);
        statsLayout->addWidget(card, 0, col);
    };
    createStatCard(0, "💬", "今日消息数", "0", "等待真实消息", "#E8F4FD");
    createStatCard(1, "👤", "在线账号数", "0", "等待账号登录", "#E8F8EE");
    createStatCard(2, "🧩", "插件数量", QString::number(m_pluginList.size()), "来自已加载配置", "#FFF3E0");
    createStatCard(3, "📋", "日志数量", "0", "等待运行日志", "#F3E5F5");
}

QFrame* HomePage::createChartPanel() {
    auto chartPanel = createPanel("chartPanel");
    auto chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(4, 4, 4, 4);
    chartLayout->setSpacing(1);

    // 标题行（保持不变）
    auto chartTop = new QHBoxLayout;
    chartTop->addWidget(createLabel("消息趋势", "sectionTitle"));
    chartTop->addStretch();
    chartTop->addWidget(createLabel("每小时收发量  ", "mutedText"));
    chartLayout->addLayout(chartTop);

    // 图表容器
    auto chartAreaLayout = new QVBoxLayout();
    chartAreaLayout->setContentsMargins(2, 2, 2, 2);
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
    extern QJsonObject g_config;

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
    //recentPanel->setMaximumWidth(400);               // 限制右侧宽度，让图表区域更宽
    auto recentLayout = new QVBoxLayout(recentPanel);
    recentLayout->setContentsMargins(10, 10, 10, 10);
    recentLayout->setSpacing(10);

    auto recentTop = new QHBoxLayout;
    recentTop->addWidget(createLabel("插件消息统计", "sectionTitle"));
    recentLayout->addLayout(recentTop);

    m_pluginMessageLayout = new QVBoxLayout;
    m_pluginMessageLayout->setSpacing(10);
    m_pluginMessageLayout->addStretch();
    recentLayout->addLayout(m_pluginMessageLayout);
    return recentPanel;
}

QFrame* HomePage::createStatusPanel() {
    QFrame* container = createPanel("statusContainer");
    container->setObjectName("statusMainContainer");
    container->setStyleSheet(
        "QFrame#statusMainContainer {"
        "    background: #F7EFE5;"
        "    border-radius: 18px;"
        "    border: 1px solid #F3E7DA;"
        "}"
        );
    QHBoxLayout* mainLayout = new QHBoxLayout(container);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);

    // ========== 左侧：系统状态面板 ==========
    QFrame* leftPanel = createPanel("statusPanel");
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
    m_cpuProgressBar->setStyleSheet(
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
    m_ramProgressBar->setStyleSheet(
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

    QTextBrowser* changelogEdit = new QTextBrowser(this);
    changelogEdit->setReadOnly(true);
    changelogEdit->setObjectName("changelogTextEdit");
    changelogEdit->setStyleSheet(
        "QTextEdit { background-color: #f8f8f8; border: 1px solid #ddd; "
        "border-radius: 4px; padding: 8px; font-family: monospace; }"
        );
    QString changelogMarkdown = Homev;

    changelogEdit->setOpenExternalLinks(true);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    changelogEdit->setMarkdown(changelogMarkdown);
#else
    changelogEdit->setHtml("<pre>" + changelogMarkdown.toHtmlEscaped() + "</pre>");
#endif

    mainLayout->addWidget(leftPanel, 1);
    mainLayout->addWidget(changelogEdit, 2);

    return container;
}









void HomePage::setupUI()
{
    setObjectName("homePage");

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    auto statsLayout = new QGridLayout;
    statsLayout->setSpacing(12);
    createStatCards(statsLayout);
    mainLayout->addLayout(statsLayout);

    auto middleLayout = new QHBoxLayout;
    middleLayout->setSpacing(4);
    // 调整拉伸因子：左侧 3，右侧 2，右侧不会太窄
    middleLayout->addWidget(createChartPanel(), 12);
    middleLayout->addWidget(createRecentPanel(), 5);
    mainLayout->addLayout(middleLayout, 2);

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
void HomePage::updateProcessStats() {
    // ========== 1. 内存占用（带进度条） ==========
    PROCESS_MEMORY_COUNTERS pmc;
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        double memMB = pmc.WorkingSetSize / (1024.0 * 1024.0);

        // 获取系统总物理内存（建议在初始化时缓存，避免频繁调用）
        static double totalMemMB = []() {
            MEMORYSTATUSEX memStatus;
            memStatus.dwLength = sizeof(memStatus);
            if (GlobalMemoryStatusEx(&memStatus)) {
                return memStatus.ullTotalPhys / (1024.0 * 1024.0);
            }
            return 8192.0; // 降级默认 8GB
        }();

        int memPercent = (totalMemMB > 0) ? static_cast<int>((memMB / totalMemMB) * 100) : 0;
        memPercent = qBound(0, memPercent, 100);

        m_ramTextLabel->setText(QString("%1 MB / %2 MB (%3%)")
                                    .arg(memMB, 0, 'f', 1)
                                    .arg(totalMemMB, 0, 'f', 0)
                                    .arg(memPercent));
        m_ramProgressBar->setValue(memPercent);
    }

    // ========== 2. CPU 使用率（带进度条） ==========
    static int processorCount = []() {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        return (int)sysInfo.dwNumberOfProcessors;
    }();

    static ULONGLONG lastTotalCpuTime = 0;
    static ULONGLONG lastTick = 0;
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
        ULONGLONG curKernel = ((ULONGLONG)kernelTime.dwHighDateTime << 32) + kernelTime.dwLowDateTime;
        ULONGLONG curUser   = ((ULONGLONG)userTime.dwHighDateTime << 32) + userTime.dwLowDateTime;
        ULONGLONG curTotal  = curKernel + curUser;
        ULONGLONG nowTick   = GetTickCount64();

        double cpuPercent = 0.0;
        if (lastTotalCpuTime != 0 && lastTick != 0 && nowTick > lastTick) {
            ULONGLONG deltaMs = nowTick - lastTick;
            ULONGLONG deltaCpu100ns = curTotal - lastTotalCpuTime;
            double cpuMs = deltaCpu100ns / 10000.0;
            cpuPercent = (cpuMs / deltaMs) / processorCount * 100.0;
            cpuPercent = qBound(0.0, cpuPercent, 100.0);
        }

        m_cpuTextLabel->setText(QString("CPU消耗: %1%").arg(cpuPercent, 0, 'f', 1));
        m_cpuProgressBar->setValue(static_cast<int>(cpuPercent));

        lastTotalCpuTime = curTotal;
        lastTick = nowTick;
    }

    // ========== 3. 磁盘读写（速率 + 累计） ==========
    IO_COUNTERS io;
    if (GetProcessIoCounters(GetCurrentProcess(), &io)) {
        qint64 currentRead = io.ReadTransferCount;
        qint64 currentWrite = io.WriteTransferCount;
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

        m_diskReadRateLabel->setText(formatBytes(currentRead, "累计读"));
        m_diskWriteRateLabel->setText(formatBytes(currentWrite, "累计写"));

        qint64 elapsedSec = QDateTime::currentSecsSinceEpoch() - g_totalRuntime;
        quint64 readDelta = currentRead - m_lastReadBytes;
        quint64 writeDelta = currentWrite - m_lastWriteBytes;

        double readSpeed = static_cast<double>(readDelta) / static_cast<double>(elapsedSec);
        double writeSpeed = static_cast<double>(writeDelta) / static_cast<double>(elapsedSec);

        m_diskReadTotalLabel->setText(formatBytes(currentRead/elapsedSec,"平均")+"/s,"+formatBytes(readSpeed, "实时"));
        m_diskWriteTotalLabel->setText(formatBytes(currentWrite/elapsedSec,"平均")+"/s,"+formatBytes(writeSpeed, "实时"));

        m_lastReadBytes = currentRead;
        m_lastWriteBytes = currentWrite;

    }
}

void HomePage::refreshRuntimeStats()
{
    int onlineCount = 0;
    int messageCount = 0;
    for (const auto &account : std::as_const(m_accounts)) {
        if (account->online) onlineCount++;
        messageCount += account->received + account->sent;
    }
    const int pluginCount = m_pluginList.size();
    int logCount=0;
    if (m_todayMessageValue) m_todayMessageValue->setText(QString::number(messageCount));
    if (m_onlineAccountValue) m_onlineAccountValue->setText(QString::number(onlineCount));
    if (m_logCountValue) m_logCountValue->setText(QString::number(logCount));
    if (m_pluginCountValue) m_pluginCountValue->setText(QString::number(pluginCount));


    for (const PluginInfo &plugin : std::as_const(m_pluginList)) {
        updatePluginMessageCount(plugin.name.isEmpty() ? plugin.path : plugin.name, plugin.SendQuantity);
    }
    updateProcessStats();
}

void HomePage::updatePluginMessageCount(const QString &pluginName, int count)
{
    if (!m_pluginMessageLayout) return;

    if (m_pluginMessageCounts.contains(pluginName)) {
        m_pluginMessageCounts[pluginName]->setText(QString::number(count));
    } else {
        // 创建新的插件消息统计项
        auto row = new QHBoxLayout;
        row->setSpacing(10);
        auto iconLabel = createLabel("🔌", "statIcon"); // 可以根据需要用插件图片替代
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setFixedSize(32, 32);
        iconLabel->setStyleSheet("QLabel#statIcon { background: #EEF9E9; font-size: 16px; border-radius: 10px; }");
        
        auto textLayout = new QVBoxLayout;
        textLayout->setSpacing(3);
        textLayout->addWidget(createLabel(pluginName, "conversationName"));
        textLayout->addWidget(createLabel("已发送消息数", "mutedText"));
        
        auto right = new QVBoxLayout;
        auto countLabel = createLabel(QString::number(count), "badge");
        countLabel->setAlignment(Qt::AlignCenter);
        m_pluginMessageCounts[pluginName] = countLabel;
        right->addWidget(countLabel);
        
        row->addWidget(iconLabel);
        row->addLayout(textLayout, 1);
        row->addLayout(right);
        
        // 插入到伸缩弹簧之前
        m_pluginMessageLayout->insertLayout(m_pluginMessageLayout->count() - 1, row);
    }
}

void HomePage::refreshPluginList()
{
    if (m_pluginCountValue) m_pluginCountValue->setText(QString::number(m_pluginList.size()));

    refreshRuntimeStats();
}
