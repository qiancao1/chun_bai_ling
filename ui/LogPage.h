#ifndef LOGPAGE_H
#define LOGPAGE_H

#include "chatpage.h"
#include <QWidget>
#include <QTableView>
#include <QStackedWidget>
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <qstandarditemmodel.h>


// 列字段类型枚举

enum LogField {
    Field_Time,
    Field_BotName,
    Field_TargetId,
    Field_Sender,
    Field_Content,
    Field_Direction,
    Field_Count
};
// 列描述：字段类型、标题、默认宽度
struct LogColumn {
    LogField field;
    QString title;
    int defaultWidth;
};


class LogPage : public QWidget
{
    Q_OBJECT
public:
    explicit LogPage(QWidget *parent = nullptr);
    ~LogPage();

    // 设置当前选中的机器人
    void setCurrentBot(int botId, const QString &botName);
    void setActive(bool active);
    void onNewLogAdded(int type,uint64_t seq, int appid, const QString& groupId, const Message& msg);
    void findRowBySeq(int type,int appid,uint64_t targetSeq,const QString &direction);
    // 环形缓冲区（5个tab的数据源）
    bool m_active = true;
    int currentTabIndex = 0;

private slots:

    void switchTab(int index);  // 切换标签页


private:
    void setupUi();
    void applyStyleSheet();     // 你的样式表函数（假设存在）


    QString currentBotLabel() const;

    QTableView* currentListView();

private:
    int getColumnIndex(LogField field) const;
    // 获取指定行指定字段的显示文本
    QString getFieldText(int row, LogField field) const;
    // UI组件
    QStackedWidget *tabStack = nullptr;
    QPushButton *btnEventTab = nullptr;
    QPushButton *btnGroupTab = nullptr;
    QPushButton *btnPrivateTab = nullptr;
    QPushButton *btnChannelTab = nullptr;
    QPushButton *btnChannelPrivateTab = nullptr;

    // 五个Tab对应的View和Model
    QTableView *eventListView = nullptr;
    QTableView *groupListView = nullptr;
    QTableView *channelListView = nullptr;
    QTableView *privateListView = nullptr;
    QTableView *channelPrivateListView = nullptr;


    int m_currentBotId=0;
    QString m_currentBotName;


    QStandardItemModel *m_model=nullptr;

    int m_offset = 0;
    bool m_loading = false;
    bool m_hasMore = true;
    const int BATCH_SIZE = 200;
    void setTableHeaders();
    void loadMore();
    void resetAndLoad();
    void onScrollBarValueChanged(int value);
};

#endif // LOGPAGE_H