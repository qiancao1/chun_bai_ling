#ifndef ACCOUNTPAGE_H
#define ACCOUNTPAGE_H

#include <QWidget>
#include <qpushbutton.h>
#include "accountinfo.h"

class FlowLayout;
class QScrollArea;

class AccountPage : public QWidget {
    Q_OBJECT
public:
    explicit AccountPage(QWidget *parent = nullptr);
    ~AccountPage();
    void refreshCards2(AccountInfo *info);
    void refreshCards();
    void extracted(QJsonArray &arr);
    void saveAccounts(const AccountInfo *info);

private slots:
    void onAddAccount();
    void onEditAccount(int appid);
    void onDeleteAccount(int appid);
    void onLoginStateChanged(int appid);
    void autoConnectBots();

private:
    void openAccountEditor(const AccountInfo &info, bool editMode);
    void loadAccounts();
    AccountInfo* findAccount(int appid);
    FlowLayout *m_flowLayout;
    QWidget *m_containerWidget;
    QScrollArea *m_scrollArea;
    QPushButton *m_addBtn;
    int m_lastTotalReceived = 0;
    int m_lastTotalSent = 0;
    QTimer *m_hourlyTimer = nullptr;
    void startHourlyRecord();
    void extracted(int &curTotalReceived, int &curTotalSent);
    void recordHourlyStats();
    void updateHourStat(QJsonObject &g_config, const QString &type, const QDate &date, int hour, int increment);
    void pruneOldStats(QJsonObject &g_config);   // 删除今天和昨天之外的数据
    void loadLastTotalsFromConfig();             // 程序启动时恢复基准值


};

#endif // ACCOUNTPAGE_H
