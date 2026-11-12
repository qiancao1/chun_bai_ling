
#ifndef HOMEPAGE_H
#define HOMEPAGE_H
#include "statuschartwidget.h"



#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QLabel>

#include <QProgressBar>


class QResizeEvent;

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);

    void refreshRuntimeStats();
    void updateChartData();  // 刷新图表数据
    // 新增：更新插件发送信息数量
    void updatePluginMessageCount(const QString &pluginName, int count);

private:
    void setupUI();
    void updateProcessStats();
    void refreshPluginList();
    // 插件发送量 TOP N 榜单：按 SendQuantity 降序，只保留前 6 个
    void refreshPluginMessageRanking();
    QLabel *createStatusLabel(const QString &title, const QString &value);

    QFrame* createHeroPanel();
    void createStatCards(QGridLayout* statsLayout);
    QFrame* createChartPanel();
    QFrame* createRecentPanel();
    QFrame* createStatusPanel();

    Qt::HANDLE m_hProcess;          // 当前进程句柄
    qint64 m_lastRead, m_lastWrite;   // 上次磁盘读写字节数
    qint64 m_lastTime;                 // 上次采样时间 (ms)
    QLabel* m_cpuTextLabel;
    QLabel* m_ramTextLabel;
    QLabel* m_diskReadTextLabel;
    QLabel* m_diskWriteTextLabel;

    QProgressBar* m_cpuProgressBar;
    QProgressBar* m_ramProgressBar;
    QGridLayout *pluginGridLayout;  // 插件网格布局
    QWidget *pluginGridWidget;      // 插件网格容器


    QLabel* m_diskReadRateLabel;
    QLabel* m_diskReadTotalLabel;
    QLabel* m_diskWriteRateLabel;
    QLabel* m_diskWriteTotalLabel;

    quint64 m_lastReadBytes;
    quint64 m_lastWriteBytes;





    void setStyleSheetA();
    // 记录插件消息数量面板内部容器
    QVBoxLayout *m_pluginMessageLayout = nullptr;
    QMap<QString, QLabel*> m_pluginMessageCounts; // 映射插件名称到其显示的计数标签
    QStringList m_pluginRankOrder;                // 当前榜单顺序（变了才重建，避免定时刷新闪烁）
    QLabel *m_pluginRankSummary = nullptr;        // 标题右侧「共 N 条」小计
    bool m_pluginRankReady = false;               // 榜单是否已经建过一次（空榜也要建一次显示提示）
    // 收发三个数原来是最左边三张卡，现在挪到「消息趋势」图表的标题行（见 createChartPanel）
    QLabel *m_chartRecvLabel = nullptr;         // 图表标题行：启动以来接收
    QLabel *m_chartSentLabel = nullptr;         // 图表标题行：启动以来发送
    QLabel *m_chartTotalLabel = nullptr;        // 图表标题行：合计 + 历史累计
    QLabel *m_activeDataValue = nullptr;        // 「活跃数据」：一行两值（用户 x · 群 y，今日口径，同「数据统计」指令）
    QLabel *m_pluginCountValue = nullptr;       // 「插件数量」主数值
    QLabel *m_onlineAccountValue = nullptr;
    QLabel *m_adValue = nullptr;                // 「广告观看」主数值 = 启动以来
    QLabel *m_adSub = nullptr;                  // 「广告观看」右上角：累计
    QLabel *m_addDelValue = nullptr;            // 「群/好友 增删」：一行小字（启动以来 加/退）
    QLabel *m_systemStatusValue = nullptr;
    QLabel *m_cpuStatusValue = nullptr;
    QLabel *m_ramStatusValue = nullptr;
    QLabel *m_diskStatusValue = nullptr;

    StatusChartWidget *m_chartWidget = nullptr;  // 替换原来的 m_chartView




    int m_messageDataCount = 0;      // 数据点序号，用作 X 轴
    QTimer *m_chartUpdateTimer = nullptr;  // 可选，模拟动态数据

};

#endif // HOMEPAGE_H