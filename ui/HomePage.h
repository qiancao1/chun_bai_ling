
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
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QDate>

#include <QProgressBar>


class QResizeEvent;
class QDialog;
class QEvent;


// 多账号求和后存一份，首页卡片只读「本月」那一桶，明细窗按月份切。
struct AdDayStat {
    qint64 exposure = 0;      // 广告曝光 PV
    double income   = 0.0;    // 当日预计广告总收入（元，未扣平台分成/服务费/税）
};

class HomePage : public QWidget
{
    Q_OBJECT
public:
    explicit HomePage(QWidget *parent = nullptr);

    void refreshRuntimeStats();
    void updateChartData();  // 刷新图表数据
    // 新增：更新插件发送信息数量
    void updatePluginMessageCount(const QString &pluginName, int count);

protected:
    // 「广告观看」卡片点了要弹明细窗（QFrame 没有 clicked，走事件过滤器）
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    QLabel *m_adValue = nullptr;                // 「广告观看」主数值 = 累计次数 / 本月金额
    QLabel *m_adSub = nullptr;                  // 「广告观看」右上角：今日次数
    QLabel *m_addDelValue = nullptr;            // 「群/好友 增删」：一行小字（启动以来 加/退）
    QLabel *m_systemStatusValue = nullptr;
    QLabel *m_cpuStatusValue = nullptr;
    QLabel *m_ramStatusValue = nullptr;
    QLabel *m_diskStatusValue = nullptr;

    StatusChartWidget *m_chartWidget = nullptr;  // 替换原来的 m_chartView


    QFrame *m_adCard = nullptr;                             // 装了事件过滤器的卡片本体
    qint64 m_adTotalCount = 0;                              // 累计观看次数（所有账号求和）
    qint64 m_adTodayCount = 0;                              // 今日观看次数（右上角副值）
    // 广告数据按「账号」分开存：appid → yyyyMM →（yyyyMMdd → 明细）
    // 卡片显示的是各账号之和，明细窗可以选「全部」或某一只机器人
    QHash<int, QHash<QString, QHash<QString, AdDayStat>>> m_adDaily;
    QHash<QString, QDateTime> m_adFetchedAt;                // yyyyMM → 上次取数成功时间（节流用）
    QSet<QString> m_adFailed;                               // 整轮全失败的月份（1 分钟后就重试，别等 10 分钟）
    bool m_adFetchOk = true;                                // 这一轮里有没有账号真正回了数据
    bool m_adHardFail = false;                              // 这一轮被平台明确拒绝（返回体带 message）
    QDate m_adFailDate;                                     // 被拒绝的日期：当天不再请求（落 g_config）
    QString m_adFailMsg;                                    // 平台原话（如「能力暂未开放」）
    QSet<QString> m_adLoading;                              // 正在取的月份
    QString m_adFetchYm;                                    // 这一轮在取哪个月（空 = 没有在途请求）
    QStringList m_adFetchQueue;                             // 还没问的账号 appid（串行拉，主线程状态）
    QDate m_adFetchStart, m_adFetchEnd;                      // 这一轮的日期区间
    QPointer<QDialog> m_adDialog;                           // 明细窗（关掉即销毁）
    QVBoxLayout *m_adDialogLayout = nullptr;                // 明细窗根布局（整块重建）
    QString m_adDialogMonth;                                // 明细窗当前月份 yyyyMM
    int m_adDialogAppid = 0;                                // 明细窗选中的机器人 appid；0 = 全部合并

    // 某个月、某个账号的每日明细；appid <= 0 = 所有账号按天求和
    QHash<QString, AdDayStat> adDays(const QString &ym, int appid = 0) const;
    void applyAdCardText();                                 // 卡片文案（次数 / 本月金额）
    void fetchAdMonth(const QString &ym, bool force = false); // 按月份取数（节流 + 单轮在途）
    void adFetchNext();                                     // 串行问下一个账号
    void mergeAdRows(int appid, const QHash<QString, AdDayStat> &days, bool ok, const QString &hardMsg);
    void finishAdFetch();                                   // 一轮结束：落缓存、刷 UI
    void showAdDetail();                                    // 弹明细窗（本月 / 上月）
    void rebuildAdDialog();                                 // 重画明细窗内容




    int m_messageDataCount = 0;      // 数据点序号，用作 X 轴
    QTimer *m_chartUpdateTimer = nullptr;  // 可选，模拟动态数据

};

#endif // HOMEPAGE_H