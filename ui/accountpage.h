#ifndef ACCOUNTPAGE_H
#define ACCOUNTPAGE_H

#include <QWidget>
#include <qpushbutton.h>
#include "accountinfo.h"

class AddAccountDialog;
class CardWidget;
class QScrollArea;
class QVBoxLayout;
class QLabel;

// 账号页（列表式布局）
//   左列：账号卡片（一列排布，整卡可点击选中）+ 底部 [+]
//   右列：选中账号的详细配置（复用 AddAccountDialog 的界面，嵌入显示）
//        底部 [保存当前账号配置]（删除走卡片上的「删除」按钮）
class AccountPage : public QWidget {
    Q_OBJECT
public:
    explicit AccountPage(QWidget *parent = nullptr);
    ~AccountPage();
    void refreshCards2(AccountInfo *info);
    void refreshCards();
    void extracted(QJsonArray &arr);
    void saveAccounts(const AccountInfo *info);

public slots:
    void onDeleteAccount(int appid);

private slots:
    void onAddAccount();          // [+] 进入“新建账号”状态
    void onCardClicked(int appid); // 选中某个账号并在右侧显示
    void onSaveSelected();        // 保存当前账号配置（或新增）

    void autoConnectBots();

private:
    CardWidget *addCardFor(AccountInfo *info);
    void selectAccount(int appid);
    void clearEditor();
    void updateSelectionStyle();
    void updateActionState();
    void loadAccounts();
    void onStatTick();
    AccountInfo* findAccount(int appid);

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_containerWidget = nullptr;
    QVBoxLayout *m_cardLayout = nullptr;
    QPushButton *m_addBtn = nullptr;

    AddAccountDialog *m_editor = nullptr;   // 右侧嵌入的配置面板
    QLabel *m_editorHint = nullptr;
    QPushButton *m_saveSelBtn = nullptr;

    int m_selectedAppid = 0;    // 0 = 没有选中任何账号
    bool m_newMode = false;     // 编辑器处于“新建账号”状态

    int m_lastTotalReceived = 0;
    int m_lastTotalSent = 0;
    QTimer *m_hourlyTimer = nullptr;
    QTimer *m_statTimer;
    void startHourlyRecord();
    void extracted(int &curTotalReceived, int &curTotalSent);
    void recordHourlyStats();
    void updateHourStat(QJsonObject &g_config, const QString &type, const QDate &date, int hour, int increment);
    void pruneOldStats(QJsonObject &g_config);   // 删除今天和昨天之外的数据
    void loadLastTotalsFromConfig();             // 程序启动时恢复基准值


};

#endif // ACCOUNTPAGE_H
