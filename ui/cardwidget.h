#ifndef CARDWIDGET_H
#define CARDWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "accountinfo.h"
#include "qqbotclient.h"
class CardWidget : public QWidget {
    Q_OBJECT
public:
    explicit CardWidget(AccountInfo *info, QWidget *parent = nullptr);
    ~CardWidget();

    void refreshDisplay();
    void triggerLogin();
    void onTimeRefresh();       // 刷新在线时长显示
    AccountInfo *m_info;
public slots:
    void onLoginButton();

signals:
    void settingClicked(int appid);
    void deleteClicked(int appid);
    void loginClicked(int appid);   // 登录/登出后通知外部更新数据

private slots:

    void onSettingButton();
    void onDeleteButton();
    void onLoginButtonA();

    void onBotLoginSuccess();      // 某个机器人登录成功后的处理

    void onBotDisconnected();


private:
    void setupUI();
    void updateBorder();

    QString formatDuration(qint64 seconds) const;
    void initbotdb(AccountInfo *info);
    QQBotClient* getOrCreateClient(AccountInfo *info);

    QLabel *m_avatarLabel;
    QLabel *m_nicknameLabel;
    QLabel *m_typeLabel;        // ws / webhook
    QLabel *m_durationLabel;    // 在线时长
    QLabel *m_receivedLabel;    // 接收数量
    QLabel *m_sentLabel;        // 发送数量
    QPushButton *m_loginBtn;
    QPushButton *m_settingBtn;
    QPushButton *m_deleteBtn;
    QLabel* m_appidLabel;



};

#endif // CARDWIDGET_H