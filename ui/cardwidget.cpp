#include "cardwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QFile>
#include <QRandomGenerator>
#include <QTimer>
#include <QDateTime>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QMessageBox>
#include <QDebug>
#include "global.h"
#include "qqbotclient.h"

CardWidget::CardWidget(AccountInfo *info, QWidget *parent): QWidget(parent), m_info(info) {
    setupUI();
    refreshDisplay();
}

CardWidget::~CardWidget() {

}

// 列表式单行卡片： [头像] 昵称+类型 / 时长 / 统计 .... [登录] [删除]
void CardWidget::setupUI() {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(74);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(8, 6, 8, 6);
    mainLayout->setSpacing(8);

    // 头像
    m_avatarLabel = new QLabel;
    m_avatarLabel->setFixedSize(42, 42);
    m_avatarLabel->setScaledContents(true);
    m_avatarLabel->setStyleSheet("border-radius: 9px; background-color: #E5DED5;");
    mainLayout->addWidget(m_avatarLabel, 0, Qt::AlignVCenter);

    // 中间信息区
    QWidget *infoWidget = new QWidget;
    infoWidget->setStyleSheet("background: transparent;");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(1);

    // 第一行：昵称 + 类型标签
    QHBoxLayout *nameLayout = new QHBoxLayout;
    nameLayout->setSpacing(5);
    m_nicknameLabel = new QLabel;
    m_nicknameLabel->setStyleSheet("background: transparent; color: #17202A; font-weight: 800; font-size: 13px;");
    m_typeLabel = new QLabel;
    m_typeLabel->setStyleSheet("color: #6E7D92; font-size: 10px; background-color: #EDF3F8; padding: 1px 7px; border-radius: 8px;");
    m_appidLabel = new QLabel;
    m_appidLabel->setStyleSheet("background: transparent; color: #8A94A6; font-size: 10px;");
    m_appidLabel->hide();
    nameLayout->addWidget(m_nicknameLabel);
    nameLayout->addWidget(m_typeLabel);
    nameLayout->addWidget(m_appidLabel);
    nameLayout->addStretch();
    infoLayout->addLayout(nameLayout);

    // 第二行：在线时长
    m_durationLabel = new QLabel;
    m_durationLabel->setStyleSheet("background: transparent; color: #596579; font-size: 11px;");
    infoLayout->addWidget(m_durationLabel);

    // 第三行：收/发统计
    QHBoxLayout *statsLayout = new QHBoxLayout;
    statsLayout->setSpacing(6);
    m_receivedLabel = new QLabel;
    m_sentLabel = new QLabel;
    m_receivedLabel->setStyleSheet("background: transparent; color: #596579; font-size: 11px;");
    m_sentLabel->setStyleSheet("background: transparent; color: #596579; font-size: 11px;");
    statsLayout->addWidget(m_receivedLabel);
    statsLayout->addWidget(m_sentLabel);
    statsLayout->addStretch();
    infoLayout->addLayout(statsLayout);

    mainLayout->addWidget(infoWidget, 1, Qt::AlignVCenter);

    // 右侧按钮区：登录 / 删除
    QString loginStyle =
        "QPushButton { background-color: #FFE9D3; border: none; border-radius: 8px; font-size: 12px; color: #F26F2A; font-weight: 800; }"
        "QPushButton:hover { background-color: #FFD9B6; }";
    QString delStyle =
        "QPushButton { background-color: #FDECEA; border: none; border-radius: 8px; font-size: 12px; color: #D9534F; font-weight: 800; }"
        "QPushButton:hover { background-color: #F9D6D3; }";

    m_loginBtn = new QPushButton(m_info->online ? "登出" : "登录");
    m_loginBtn->setStyleSheet(loginStyle);
    m_loginBtn->setFixedSize(58, 26);
    m_loginBtn->setCursor(Qt::PointingHandCursor);
    m_loginBtn->setToolTip("登录 / 登出该机器人账号");

    m_deleteBtn = new QPushButton("删除");
    m_deleteBtn->setStyleSheet(delStyle);
    m_deleteBtn->setFixedSize(58, 26);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setToolTip("从框架中删除该账号");

    // 右侧按钮区：登录在上、删除在下（竖向排列）
    QWidget *btnColumn = new QWidget;
    btnColumn->setStyleSheet("background: transparent;");
    QVBoxLayout *btnColumnLayout = new QVBoxLayout(btnColumn);
    btnColumnLayout->setContentsMargins(0, 0, 0, 0);
    btnColumnLayout->setSpacing(4);
    btnColumnLayout->addWidget(m_loginBtn);
    btnColumnLayout->addWidget(m_deleteBtn);

    mainLayout->addWidget(btnColumn, 0, Qt::AlignVCenter);

    connect(m_loginBtn, &QPushButton::clicked, this, &CardWidget::onLoginButtonA);
    connect(m_deleteBtn, &QPushButton::clicked, this, &CardWidget::onDeleteButton);
    updateBorder();
}

void CardWidget::setSelected(bool selected) {
    if (m_selected == selected) return;
    m_selected = selected;
    updateBorder();
}

// 账号已经下线、进入「1 秒后删除」的等待期：把按钮锁住并给出文字反馈
void CardWidget::setDeletePending(bool pending) {
    m_deletePending = pending;

    if (m_loginBtn) {
        m_loginBtn->setText(pending ? "下线中…" : (m_info->online ? "登出" : "登录"));
        m_loginBtn->setEnabled(!pending);
    }
    if (m_deleteBtn) {
        m_deleteBtn->setText(pending ? "删除中…" : "删除");
        m_deleteBtn->setEnabled(!pending);
    }
}

void CardWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_info ? m_info->appid_int : 0);
    }
    QWidget::mousePressEvent(event);
}

void CardWidget::updateBorder() {
    QString borderColor = m_info->online ? "#A8D89B" : "#F0C7B8";
    QString bgColor = m_info->online ? "#F7FFF2" : "#FFF7F0";
    if (m_selected) {
        borderColor = "#FF914D";
        bgColor = "#FFF1E2";
    }
    QString style = QString(
                        "CardWidget {"
                        "  background-color: %1;"
                        "  border: 2px solid %2;"
                        "  border-radius: 10px;"
                        "}"
                        "CardWidget:hover {"
                        "  border: 2px solid #FFB066;"
                        "}"
                        "CardWidget QLabel { background: transparent; }"
                        ).arg(bgColor, borderColor);
    setStyleSheet(style);
}

void CardWidget::refreshDisplay() {
    // 头像
    int appid = m_info ->appid_int;
    if(accinfo(appid)==-1) return;
    QPixmap pix;
    if (!m_info->avatarPath.isEmpty() && QFile::exists(m_info->avatarPath)) {
        pix.load(m_info->avatarPath);
    } else {
        pix = QPixmap(42, 42);
        pix.fill(Qt::transparent);
    }
    m_avatarLabel->setPixmap(pix.scaled(42, 42, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));

    m_nicknameLabel->setText(m_info->nickname.isEmpty() ? "未命名" : m_info->nickname);
    m_typeLabel->setText(m_info->type == 0 ? "ws" : "webhook");

    onTimeRefresh();  // 刷新时长
    m_loginBtn->setText(m_deletePending ? "下线中…" : (m_info->online ? "登出" : "登录"));
    updateBorder();
}


void CardWidget::onTimeRefresh() {
    if (m_info->online && m_info->startup_time > 0) {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        qint64 elapsed = now - m_info->startup_time;
        m_durationLabel->setText("在线:" + formatDuration(elapsed));
        m_receivedLabel->setText(QString("累计:%1,%2").arg(m_info->message_received).arg(m_info->message_sent ));
        m_sentLabel->setText(QString("收发:%1,%2").arg(m_info->received).arg(m_info->sent));
    } else {
        m_durationLabel->setText("离线");
        m_receivedLabel->setText(QString("累计:%1,%2").arg(m_info->message_received).arg(m_info->message_sent ));
        m_sentLabel->setText("");
    }
}

QString CardWidget::formatDuration(qint64 seconds) const {
    const qint64 DAY_SECS = 86400;
    qint64 days = seconds / DAY_SECS;
    qint64 remainder = seconds % DAY_SECS;
    qint64 hours = remainder / 3600;
    qint64 minutes = (remainder % 3600) / 60;
    qint64 secs = remainder % 60;

    return QString("%1天%2时%3分%4秒")
        .arg(days)
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}
QQBotClient* CardWidget::getOrCreateClient(AccountInfo *info)
{
    int appid = info->appid_int;
    if (m_botClients.contains(appid))
        return m_botClients[appid];

    QQBotClient *client = new QQBotClient(info, this);
    connect(client, &QQBotClient::loginSuccess, this, &CardWidget::onBotLoginSuccess);
    connect(client, &QQBotClient::avatarDownloaded, this, &CardWidget::refreshDisplay);
    connect(client, &QQBotClient::disconnected, this, &CardWidget::onBotDisconnected);

    m_botClients[appid] = client;

    return client;
}
bool is_server();
void CardWidget::initbotdb(AccountInfo *info)
{
    int appid = info->appid_int;
    if (g_botdb.contains(appid))
        return ;
    BotDB *client = new BotDB(QString("botdb/%1_db").arg(info->appid));
    if (!client->open()) {
        // 打开失败时不要放进 g_botdb：否则后续 getOrUpdateUser 会因为 m_env 为空
        // 而始终读不到记录，把老用户当成新用户，重复分配 ID
        qWarning() << "initbotdb: BotDB 打开失败" << info->appid;
        delete client;
        return ;
    }
    g_botdb[appid] = client;
    return ;
}
void CardWidget::onLoginButtonA() {
    QQBotClient *client = getOrCreateClient(m_info);
    client->m_reconnectAttempts=0; //重置登录次数
    m_info->startup_time= QDateTime::currentSecsSinceEpoch();
    onLoginButton();
    if(m_info->type==1)
    {
        if(!is_server()){
            QMessageBox::warning(this,"","请在高级设置 运行webhook服务器后再次尝试 登录webhook账号");
        }
    }
    QTimer::singleShot(1500, this, [=]() {


        if(!m_info->err.isEmpty())
        {
            QMessageBox::warning(this,"疑似登录失败","错误内容："+m_info->err);
        }
    });
    accountPage->saveAccounts(m_info);
}
void CardWidget::onLoginButton() {
    QQBotClient *client = getOrCreateClient(m_info);
    initbotdb(m_info);

    if (m_info->online) {
        client->stop();
        refreshDisplay();
    } else {

        client->start();
    }



}

void CardWidget::triggerLogin() {
    if (!m_info->online) {
        if(m_info->startup_time==0)
            m_info->startup_time= QDateTime::currentSecsSinceEpoch();
        onLoginButton();
    }
}
//登录成功
void CardWidget::onBotLoginSuccess()
{
    refreshDisplay();

    accountPage->saveAccounts(m_info);
    AppendEventLog((m_info->nickname.isEmpty() ? m_info->appid : m_info->nickname) + "->登录成功", 0xF83834);
}

//断开
void CardWidget::onBotDisconnected()
{
    refreshDisplay();
}

void CardWidget::onDeleteButton() {
    const QString name = m_info->nickname.isEmpty() ? m_info->appid : m_info->nickname;
    if (QMessageBox::question(this, "删除账号",
                              QString("确定删除账号 %1（AppID %2）吗？\n该操作会删除本地账号配置，且不可恢复。")
                                  .arg(name, m_info->appid),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    emit deleteClicked(m_info->appid_int);
}
