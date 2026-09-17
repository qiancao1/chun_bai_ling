#ifndef CARDWIDGET_H
#define CARDWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "accountinfo.h"
#include "qqbotclient.h"

class QMouseEvent;

// 列表式紧凑卡片：[头像] 昵称 + 类型 + 状态 ............ [登录] [删除]
// 整卡可点击 -> 通知 AccountPage 在右侧显示该账号的详细配置
class CardWidget : public QWidget {
    Q_OBJECT
public:
    explicit CardWidget(AccountInfo *info, QWidget *parent = nullptr);
    ~CardWidget();

    void refreshDisplay();
    void triggerLogin();
    void onTimeRefresh();       // 刷新在线时长显示
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    AccountInfo *m_info;

public slots:
    void onLoginButton();
    void onLoginButtonA();

signals:
    void clicked(int appid);
    void deleteClicked(int appid);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void onDeleteButton();

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
    QPushButton *m_deleteBtn;
    QLabel* m_appidLabel;

    bool m_selected = false;


};

#endif // CARDWIDGET_H
