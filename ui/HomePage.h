
#ifndef HOMEPAGE_H
#define HOMEPAGE_H
#include "StatusChartWidget.h"



#include <QWidget>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QMap>
#include <QLabel>
#include <QString>
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
    QLabel *m_todayMessageValue = nullptr;
    QLabel *m_onlineAccountValue = nullptr;
    QLabel *m_logCountValue = nullptr;
    QLabel *m_systemStatusValue = nullptr;
    QLabel *m_cpuStatusValue = nullptr;
    QLabel *m_ramStatusValue = nullptr;
    QLabel *m_diskStatusValue = nullptr;
    QLabel *m_pluginCountValue = nullptr;

    StatusChartWidget *m_chartWidget = nullptr;  // 替换原来的 m_chartView




    int m_messageDataCount = 0;      // 数据点序号，用作 X 轴
    QTimer *m_chartUpdateTimer = nullptr;  // 可选，模拟动态数据

};

#endif // HOMEPAGE_H