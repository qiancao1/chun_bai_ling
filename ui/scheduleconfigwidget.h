#ifndef SCHEDULECONFIGWIDGET_H
#define SCHEDULECONFIGWIDGET_H

#include "qqbotclient.h"
#include <QWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QSplitter>
#include <QMap>
#include <qjsonarray.h>
#include <qjsonobject.h>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QDate>
#include <QTime>
#include <qlabel.h>

#include "placeholderlineedit.h"
#include "placeholdertextedit.h"   // 如果你也有 QTextEdit 的替换

// 全局替换宏
#define QLineEdit PlaceholderLineEdit
#define QTextEdit PlaceholderTextEdit
struct TimeRule {
    int year = -1;    // 年，-1 不限
    int month = -1;   // 月，-1 不限
    int day = -1;     // 日：>0 指定号数，<0 表示间隔（如 -3 每3天），-1 表示不限（每天）
    int hour = -1;    // 时：>0 指定小时，<0 表示间隔（如 -4 每4小时），-1 表示不限（但一般会设为0）
    int minute = -1;  // 分：>=0 指定，一般不支持间隔
    static TimeRule fromInputArray(const QString &input, bool &ok) {
        ok = true;
        TimeRule rule;
        rule.year = rule.month = rule.day = rule.hour = rule.minute = -1;
        QStringList parts = input.split(',', Qt::SkipEmptyParts);
        int len = parts.size();
        if (len == 0 || len > 5) { ok = false; return rule; }

        rule.minute = -1;
        rule.hour   = -1;
        rule.day    = -1;
        rule.month  = -1;
        rule.year   = -1;

        int index = len - 1;
        if (index >= 0) rule.minute = parts[index--].toInt();
        if (index >= 0) rule.hour   = parts[index--].toInt();
        if (index >= 0) rule.day    = parts[index--].toInt();
        if (index >= 0) rule.month  = parts[index--].toInt();
        if (index >= 0) rule.year   = parts[index--].toInt();

        return rule;
    }


    bool matches(const QDate &date, const QTime &time) const {
        // 1. 检查年份：-1 表示不限制（每年都触发），正数精确匹配
        if (year == -1) {
            // 不限制，跳过
        } else if (year > 0 && date.year() != year) {
            return false;
        }else if(year <-1)
        {
            if (qAbs(date.year()) % (-year) != 0) return false;
        }
        // 2. 检查月份：-1 表示不限制（每月都触发），正数精确匹配
        if (month == -1) {
            // 不限制，跳过
        } else if (month > 0 && date.month() != month) {
            return false;
        }else if(month <-1)
        {
            if (qAbs(date.month()) % (-month) != 0) return false;
        }
        // 3. 检查日：-1 表示不限制（每天），正数精确，小于 -1 为间隔（如 -2 隔天）
        if (day == -1) {
            // 不限制，跳过
        } else if (day > 0) {
            if (date.day() != day) return false;
        } else if (day < -1) {
            const QDate base(2020, 1, 1);
            int daysDiff = base.daysTo(date);
            if (qAbs(daysDiff) % (-day) != 0) return false;
        }

        // 4. 检查小时：-1 表示不限制（每小时都触发），正数精确，小于 -1 为间隔
        if (hour == -1) {
            // 不限制，跳过（实现了你说的 -1:10，即小时任意，分钟固定10分）
        } else if (hour > 0) {
            if (time.hour() != hour) return false;
        } else if (hour < -1) {
            // 间隔小时：如 -2 表示偶数小时触发，且必须落在整点（分秒为0）
            if (time.minute() != 0 || time.second() != 0) return false;
            if (time.hour() % (-hour) != 0) return false;
        }

        // 5. 检查分钟：-1 表示不限制（每分钟都触发），>=0 精确匹配
        if (minute == -1) {
            // 不限制，跳过
        } else if (minute >= 0 && time.minute() != minute) {
            return false;
        }

        return true;
    }

};

struct ScheduleTask {
    bool enabled = true;
    QString remark; //备注
    QList<TimeRule> scheduleTime;

    int executeCount = -1;          // -1表示无限次
    int jis =0;                       //已经执行次数
    int executeType = 0;
    int mark=0;
    int role=2;
    QString replyContent;           // 回复内容（可含Python代码）
    QString addSubscribeCmd;        // 添加订阅指令
    QString cancelSubscribeCmd;     // 取消订阅指令
    QString addSubscribe_text;      //订阅成功回复
    QString cancelSubscribe_text;    //取消成功回复
    QString groupId;                // 关联的群号（原“数据库标记”）

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["enabled"] = enabled;
        obj["remark"] = remark;
        obj["scheduleTime"] = TimetoString();
        obj["executeCount"] = executeCount;
        obj["executeType"] = executeType;
        obj["replyContent"] = replyContent;
        obj["text0"] = addSubscribeCmd;
        obj["text1"] = cancelSubscribeCmd;
        obj["text2"] = addSubscribe_text;
        obj["text3"] = cancelSubscribe_text;
        obj["groupId"] = groupId;
        obj["jis"] = jis;
        return obj;
    }
    static ScheduleTask fromJson(const QJsonObject &obj) {
        ScheduleTask task;
        task.enabled = obj["enabled"].toBool(true);
        task.remark = obj["remark"].toString();
        QString Str_time =obj["scheduleTime"].toString();
        QStringList list = Str_time.split("|");
        if(list.size()>0)
        {
            for (QString &t : list)
            {
                bool ok=false;
                TimeRule t2= TimeRule::fromInputArray(t,ok);
                if(ok) task.scheduleTime.append(t2);
            }
        }else task.scheduleTime.append(TimeRule());

        task.executeCount = obj["executeCount"].toInt(-1);
        task.executeType = obj["executeType"].toInt(0);
        task.replyContent = obj["replyContent"].toString();
        task.addSubscribeCmd = obj["text0"].toString();
        task.cancelSubscribeCmd = obj["text1"].toString();
        task.addSubscribe_text = obj["text2"].toString();
        task.cancelSubscribe_text = obj["text3"].toString();
        task.groupId = obj["groupId"].toString();
        task.jis = obj["jis"].toInt();
        return task;
    }
    QString TimetoString() const
    {
        QString time;
        for(auto &t : scheduleTime)
        {
            if(t.year!=-1) time+=QString("%1,%1,%1,%1,%1|").arg(t.year).arg(t.month).arg(t.day).arg(t.hour).arg(t.minute);
            else if(t.month!=-1) time+=QString("%1,%1,%1,%1|").arg(t.month).arg(t.day).arg(t.hour).arg(t.minute);
            else if(t.day!=-1) time+=QString("%1,%1,%1|").arg(t.day).arg(t.hour).arg(t.minute);
            else if(t.hour!=-1) time+=QString("%1,%1|").arg(t.hour).arg(t.minute);
            else time+=QString("%1|").arg(t.minute);
        }
        return time;
    }

    void StringToTime(QString &Str_time)
    {
        scheduleTime.clear();
        QStringList list = Str_time.split("|");
        for (QString &t : list)
        {
            bool ok=false;
            TimeRule t2= TimeRule::fromInputArray(t,ok);
            if(ok)
                scheduleTime.append(t2);
        }
    }


};

class ScheduleConfigWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ScheduleConfigWidget(QWidget *parent = nullptr);
    ~ScheduleConfigWidget();
    QString ppzl(const MessageEvent &ev,QString &订阅名);
    void 检查定时列表();
    // 数据
    int currentAppId = 0;
    QMap<int, QList<ScheduleTask>> tasksMap;
        QTableWidget *taskTable;            // 中间表格：只有一列，包含复选框+备注
signals:
    void scheduleConfigChanged();

private slots:
    void refreshRobotList();
    void onRobotSelectionChanged();
    void onTableRowSelected(int row);
    void onAddRow();
    void onDeleteRow();
    void onCopyRow();
    void onCopyAllRows();
    void onPasteFromClipboard();
    void onSaveToFile();
    void onUpdateTask();                // 从右侧面板更新当前任务

private:
    void setupUI();
    void initTable();
    void loadTasksForRobot(int appid);
    void saveCurrentTasksToMap();
    void setTaskToRow(int row, const ScheduleTask &task);
    ScheduleTask getTaskFromRow(int row) const;
    void addRowFromTask(const ScheduleTask &task);
    QStringList getTableAsTSV() const;
    void addRowsFromTSV(const QString &tsv);
    void saveAllTasksToFile(const QString &filePath = "schedule_tasks.json");
    void loadAllTasksFromFile(const QString &filePath = "schedule_tasks.json");
    void updateDetailPanelFromTask(const ScheduleTask &task);
    void applyDetailPanelToTask(ScheduleTask &task);
    void clearDetailPanel();
    // UI组件
    QSplitter *mainSplitter;
    QListWidget *robotListWidget;
    QPushButton *refreshRobotBtn;



    QWidget *detailPanel;
    QLineEdit *scheduleTimeEdit;
    QSpinBox *executeCountSpin;
    QLabel *executedCountLabel;
    QComboBox *executeTypeCombo,*triggerTypeCombo;
    QTextEdit *replyContentEdit,*addSubscribeReplyEdit,*cancelSubscribeReplyEdit;        // 多行回复框
    QLineEdit *addSubscribeEdit;
    QLineEdit *cancelSubscribeEdit;
    QPushButton *updateTaskBtn;

    QPushButton *addBtn;
    QPushButton *deleteBtn;
    QPushButton *copyRowBtn;
    QPushButton *copyAllBtn;
    QPushButton *pasteBtn;
    QPushButton *saveBtn;



    // 表格列索引（只有一列）
    enum Column {
        COL_ENABLED_REMARK = 0
    };
};

#endif // SCHEDULECONFIGWIDGET_H