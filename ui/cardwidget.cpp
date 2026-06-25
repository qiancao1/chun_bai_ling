#include "cardwidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QFile>
#include <QRandomGenerator>
#include <QTimer>
#include <QDateTime>
#include "global.h"
#include "qqbotclient.h"

CardWidget::CardWidget(AccountInfo *info, QWidget *parent): QWidget(parent), m_info(info) {
    setupUI();
    refreshDisplay();
}

CardWidget::~CardWidget() {

}

void CardWidget::setupUI() {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedSize(270, 110);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QVBoxLayout *mainVLayout = new QVBoxLayout(this);
    mainVLayout->setContentsMargins(4, 4, 4, 4);
    mainVLayout->setSpacing(4);

    // 上半部分：水平布局（左侧头像 + 右侧信息）
    QHBoxLayout *topLayout = new QHBoxLayout;
    topLayout->setSpacing(4);

    // 头像
    m_avatarLabel = new QLabel;
    m_avatarLabel->setFixedSize(56, 56);
    m_avatarLabel->setScaledContents(true);
    m_avatarLabel->setStyleSheet("border-radius: 10px; background-color: #E5DED5;");
    m_avatarLabel->setContentsMargins(4, 4, 4, 4);
    topLayout->addWidget(m_avatarLabel, 0, Qt::AlignTop);

    // 右侧信息区
    QWidget *infoWidget = new QWidget;
    infoWidget->setStyleSheet("background: transparent;");
    QVBoxLayout *infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(4, 4, 4, 4);
    infoLayout->setSpacing(4);

    QHBoxLayout *nameLayout = new QHBoxLayout;
    nameLayout->setSpacing(5);
    m_nicknameLabel = new QLabel;
    m_nicknameLabel->setStyleSheet("background: transparent; color: #17202A; font-weight: 800; font-size: 14px;");
    m_typeLabel = new QLabel;
    m_typeLabel->setStyleSheet("color: #6E7D92; font-size: 10px; background-color: #EDF3F8; padding: 2px 8px; border-radius: 8px;");
    QLabel *appidLabel = new QLabel;
    appidLabel->setStyleSheet("background: transparent; color: #8A94A6; font-size: 10px;");
    appidLabel->hide();
    nameLayout->addWidget(m_nicknameLabel);
    nameLayout->addWidget(m_typeLabel);
    nameLayout->addWidget(appidLabel);
    nameLayout->addStretch();
    infoLayout->addLayout(nameLayout);

    // 第二行：在线时长
    m_durationLabel = new QLabel;
    m_durationLabel->setStyleSheet("background: transparent; color: #596579; font-size: 12px;");
    infoLayout->addWidget(m_durationLabel);

    // 第三行：收/发统计
    QHBoxLayout *statsLayout = new QHBoxLayout;
    statsLayout->setSpacing(4);
    m_receivedLabel = new QLabel;
    m_sentLabel = new QLabel;
    m_receivedLabel->setStyleSheet("background: transparent; color: #596579; font-size: 12px;");
    m_sentLabel->setStyleSheet("background: transparent; color: #596579; font-size: 12px;");
    statsLayout->addWidget(m_receivedLabel);
    statsLayout->addWidget(m_sentLabel);
    statsLayout->addStretch();
    infoLayout->addLayout(statsLayout);

    topLayout->addWidget(infoWidget, 1);
    mainVLayout->addLayout(topLayout);

    // 下半部分：三个按钮
    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(2);
    btnLayout->setContentsMargins(4, 4, 4, 4);
    m_settingBtn = new QPushButton("设置");
    m_loginBtn = new QPushButton(m_info->online ? "登出" : "登录");
    m_deleteBtn = new QPushButton("删除");
    QString btnStyle =
        "QPushButton { background-color: #FFE9D3; border: none; border-radius: 9px; padding: 4px 0px; font-size: 12px; color: #F26F2A; font-weight: 800; }"
        "QPushButton:hover { background-color: #FFD9B6; }";
    m_settingBtn->setStyleSheet(btnStyle);
    m_loginBtn->setStyleSheet(btnStyle);
    m_deleteBtn->setStyleSheet(btnStyle);
    m_settingBtn->setFixedHeight(24);
    m_loginBtn->setFixedHeight(24);
    m_deleteBtn->setFixedHeight(24);
    btnLayout->addWidget(m_settingBtn);
    btnLayout->addWidget(m_loginBtn);
    btnLayout->addWidget(m_deleteBtn);
    mainVLayout->addLayout(btnLayout);

    connect(m_loginBtn, &QPushButton::clicked, this, &CardWidget::onLoginButtonA);
    connect(m_settingBtn, &QPushButton::clicked, this, &CardWidget::onSettingButton);
    connect(m_deleteBtn, &QPushButton::clicked, this, &CardWidget::onDeleteButton);
    updateBorder();
    m_appidLabel = appidLabel;
}
void CardWidget::updateBorder() {
    QString borderColor = m_info->online ? "#A8D89B" : "#F0C7B8";
    QString bgColor = m_info->online ? "#F7FFF2" : "#FFF7F0";
    QString style = QString(
                        "CardWidget {"
                        "  background-color: %1;"
                        "  border: 1px solid %2;"
                        "  border-radius: 10px;"
                        "}"
                        "CardWidget QLabel { background: transparent; }"
                        ).arg(bgColor, borderColor);
    setStyleSheet(style);
}

void CardWidget::refreshDisplay() {
    // 头像
    QPixmap pix;
    if (!m_info->avatarPath.isEmpty() && QFile::exists(m_info->avatarPath)) {
        pix.load(m_info->avatarPath);
    } else {
        pix = QPixmap(56, 56);
        pix.fill(Qt::transparent);
    }
    m_avatarLabel->setPixmap(pix.scaled(56, 56, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));

    m_nicknameLabel->setText(m_info->nickname.isEmpty() ? "未命名" : m_info->nickname);
    m_typeLabel->setText(m_info->type == 0 ? "ws" : "webhook");

    onTimeRefresh();  // 刷新时长
    m_loginBtn->setText(m_info->online ? "登出" : "登录");
    updateBorder();
}


void CardWidget::onTimeRefresh() {
    if (m_info->online && m_info->startup_time > 0) {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        qint64 elapsed = now - m_info->startup_time;
        m_durationLabel->setText("在线:" + formatDuration(elapsed));

        m_receivedLabel->setText(QString("累计收发:%1,%2").arg(m_info->message_received).arg(m_info->message_sent ));
        m_sentLabel->setText(QString("运行收发:%1,%2").arg(m_info->received).arg(m_info->sent));
    } else {
        m_durationLabel->setText("离线");
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
    connect(client, &QQBotClient::loginFailed, this, [this, appid](const QString &reason) {
        AppendEventLog(QString("机器人 %1 登录失败: %2").arg(appid).arg(reason), Qt::red);
    });
    m_botClients[appid] = client;

    return client;
}
void CardWidget::initbotdb(AccountInfo *info)
{
    int appid = info->appid_int;
    if (g_botdb.contains(appid))
        return ;
    BotDB *client = new BotDB(QString("botdb/%1_db").arg(info->appid));
    client->open();
    g_botdb[appid] = client;
    return ;
}
void CardWidget::onLoginButtonA() {
    QQBotClient *client = getOrCreateClient(m_info);
    client->m_reconnectAttempts=0; //重置登录次数
    m_info->startup_time= QDateTime::currentSecsSinceEpoch();
    onLoginButton();
}
void CardWidget::onLoginButton() {
    QQBotClient *client = getOrCreateClient(m_info);
    initbotdb(m_info);
    if (m_info->online) {
        client->stop();

    } else {

        client->start();
    }

    refreshDisplay();
    emit loginClicked(m_info->appid_int);
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
    emit loginClicked(m_info->appid_int);
    accountPage->saveAccounts(m_info);
    AppendEventLog((m_info->nickname.isEmpty() ? m_info->appid : m_info->nickname) + "->登录成功", 0xF83834);
}

//断开
void CardWidget::onBotDisconnected()
{
    refreshDisplay();
    emit loginClicked(m_info->appid_int);
}



void CardWidget::onSettingButton() {
    emit settingClicked(m_info->appid_int);
}

void CardWidget::onDeleteButton() {
    emit deleteClicked(m_info->appid_int);
}