#include "accountpage.h"
#include "cardwidget.h"
#include "addaccountdialog.h"
#include "flowlayout.h"
#include "global.h"
#include "homepage.h"
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageBox>
#include <QTimer>
#include <memory>

QList<std::shared_ptr<AccountInfo>> m_accounts;
extern HomePage *homePage;

int accinfo(int appid) {
    for (int i =0;i<m_accounts.size();++i) {
        if(m_accounts[i]->appid_int == appid) return i;
    }
    return -1;
}
void AccountPage::extracted(int &curTotalReceived, int &curTotalSent) {
    for (const auto &acc : std::as_const(m_accounts)) {
        curTotalReceived += acc->message_received;
        curTotalSent += acc->message_sent;
    }
}
void AccountPage::recordHourlyStats() {

    int curTotalReceived = 0, curTotalSent = 0;
    extracted(curTotalReceived, curTotalSent);

    int deltaReceived = curTotalReceived - m_lastTotalReceived;
    int deltaSent = curTotalSent - m_lastTotalSent;

    QDateTime now = QDateTime::currentDateTime();
    QDate date = now.date();
    int hour = now.time().hour();

    updateHourStat(g_config, "Received", date, hour, deltaReceived);
    updateHourStat(g_config, "Sent", date, hour, deltaSent);

    pruneOldStats(g_config);

    m_lastTotalReceived = curTotalReceived;
    m_lastTotalSent = curTotalSent;
    g_config["LastTotalReceived"] = m_lastTotalReceived;
    g_config["LastTotalSent"] = m_lastTotalSent;

    homePage->updateChartData();
    saveConfig();
}

// 更新某个类型（Received/Sent）在指定日期和小时的数值（累加）
void AccountPage::updateHourStat(QJsonObject &config, const QString &type, const QDate &date, int hour, int increment)
{
    if (increment == 0) return;  // 无变化可不记录

    QJsonObject typeObj = config.value(type).toObject();
    QString dateStr = date.toString(Qt::ISODate);  // "2026-06-04"

    QJsonArray hourArray;
    if (typeObj.contains(dateStr)) {
        hourArray = typeObj[dateStr].toArray();
        while (hourArray.size() <= hour)
            hourArray.append(0);
        int oldVal = hourArray[hour].toInt();
        hourArray[hour] = oldVal + increment;
    } else {
        for (int i = 0; i < 24; ++i)
            hourArray.append(0);
        hourArray[hour] = increment;
    }
    typeObj[dateStr] = hourArray;
    config[type] = typeObj;
}

// 删除不是今天也不是昨天的日期条目
void AccountPage::pruneOldStats(QJsonObject &config)
{
    QDate today = QDate::currentDate();
    QDate yesterday = today.addDays(-1);

    auto pruneOneType = [&](const QString &type) {
        QJsonObject obj = config.value(type).toObject();
        QList<QString> toRemove;
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            QDate date = QDate::fromString(it.key(), Qt::ISODate);
            if (date.isValid() && date != today && date != yesterday) {
                toRemove.append(it.key());
            }
        }
        for (const QString &key : toRemove)
            obj.remove(key);
        config[type] = obj;
    };

    pruneOneType("Received");
    pruneOneType("Sent");
}

AccountPage::AccountPage(QWidget *parent)
    : QWidget(parent) {
    m_statTimer = new QTimer(this);
    connect(m_statTimer, &QTimer::timeout, this, &AccountPage::onStatTick);
    m_statTimer->start(60000);  // 60秒


    QTimer::singleShot(1000, this, &AccountPage::onStatTick);
    setObjectName("accountPage");
    setStyleSheet(R"(
        QWidget#accountPage {
            background: #F7EFE5;
        }
        QScrollArea {
            border: none;
            background: transparent;
        }
        QScrollArea > QWidget > QWidget {
            background: transparent;
        }
        QPushButton#addAccountCard {
            font-size: 36px;
            border: 2px dashed #F0B680;
            border-radius: 10px;
            background-color: rgba(255, 253, 249, 220);
            color: #FF914D;
        }
        QPushButton#addAccountCard:hover {
            background: #FFF0DE;
            border-color: #FF914D;
        }
    )");

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_containerWidget = new QWidget;
    m_flowLayout = new FlowLayout(m_containerWidget, 5);
    m_scrollArea->setWidget(m_containerWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);
    mainLayout->addWidget(m_scrollArea, 1);
    setLayout(mainLayout);

    loadAccounts();
    refreshCards();

    QTimer::singleShot(500, this, &AccountPage::autoConnectBots);

    for (const auto &acc : std::as_const(m_accounts)) {
        m_lastTotalReceived += acc->message_received;
        m_lastTotalSent += acc->message_sent;
    }

    m_hourlyTimer = new QTimer(this);
    connect(m_hourlyTimer, &QTimer::timeout, this, &AccountPage::recordHourlyStats);
    m_hourlyTimer->start(3600 * 1000);  // 每小时触发一次
}

AccountPage::~AccountPage() {

}
void AccountPage::onStatTick()
{
    uint32_t nowMinute = QDateTime::currentSecsSinceEpoch() / 60;

    for (auto &acc : m_accounts) {
        if (!acc->online) continue;

        auto *db = g_botdb[acc->appid_int];
        if (!db) continue;

        // 构造统计快照
        AccountStats stats;
        memset(&stats, 0, sizeof(stats));
        stats.minute_index = nowMinute;
        stats.appid = acc->appid_int;

        // 账号自身统计
        stats.message_received = acc->received_day;
        stats.message_sent = acc->sent_day;
        stats.今日加群数量 = acc->今日加群数量;
        stats.今日退群数量 = acc->今日退群数量;
        stats.今日好友数量 = acc->今日好友数量;
        stats.今日删除好友数量 = acc->今日删除好友数量;
        stats.今日频道数量 = acc->今日频道数量;
        stats.今日退出频道数量 = acc->今日退出频道数量;

        // 全系统统计（加锁读取）
        {
            QMutexLocker locker(&db->m_msgMutex);  // 假设 m_statMutex 是公开的或提供接口
            stats.active_users = db->m_userDailyMsg.size();
            stats.active_groups = db->m_groupDailyMsg.size();
        }

        db->saveAccountStats(acc->appid_int, nowMinute, stats);
        AccountStats diff;

        if (db->getTodayDiff(acc->appid_int, stats, diff)) {
            // 计算净增/净减
            int netGroup = diff.今日加群数量 - diff.今日退群数量;
            int netFriend = diff.今日好友数量 - diff.今日删除好友数量;
            int netChannel = diff.今日频道数量 - diff.今日退出频道数量;

            QString statText;
            statText += "📊 状态统计\n\n";

            // 消息统计
            statText += QString("📨 接收消息：%1（%2%3）\n")
                            .arg(stats.message_received)
                            .arg(diff.message_received >= 0 ? "+" : "-")
                            .arg(diff.message_received);

            statText += QString("📤 发送消息：%1（%2%3）\n")
                            .arg(stats.message_sent)
                            .arg(diff.message_sent >= 0 ? "+" : "-")
                            .arg(diff.message_sent);

            // 活跃用户/群（来自 BotDB 全局缓存）
            statText += QString("👤 活跃用户：%1（%2%3）\n")
                            .arg(stats.active_users)
                            .arg(diff.active_users >= 0 ? "+" : "-")
                            .arg(diff.active_users);

            statText += QString("💬 活跃群聊：%1（%2%3）\n")
                            .arg(stats.active_groups)
                            .arg(diff.active_groups >= 0 ? "+" : "-")
                            .arg(diff.active_groups);

            // 群组变动
            statText += QString("\n🏠 群组变动（净增：%1%2）\n")
                            .arg(netGroup >= 0 ? "+" : "-")
                            .arg(netGroup);
            statText += QString("  新增加群：%1（%2%3）\n")
                            .arg(stats.今日加群数量)
                            .arg(diff.今日加群数量 >= 0 ? "+" : "-")
                            .arg(diff.今日加群数量);
            statText += QString("  退出群聊：%1（%2%3）\n")
                            .arg(stats.今日退群数量)
                            .arg(diff.今日退群数量 >= 0 ? "+" : "-")
                            .arg(diff.今日退群数量);

            // 好友变动
            statText += QString("\n👥 好友变动（净增：%1%2）\n")
                            .arg(netFriend >= 0 ? "+" : "-")
                            .arg(netFriend);
            statText += QString("  新加好友：%1（%2%3）\n")
                            .arg(stats.今日好友数量)
                            .arg(diff.今日好友数量 >= 0 ? "+" : "-")
                            .arg(diff.今日好友数量);
            statText += QString("  删除好友：%1（%2%3）\n")
                            .arg(stats.今日删除好友数量)
                            .arg(diff.今日删除好友数量 >= 0 ? "+" : "-")
                            .arg(diff.今日删除好友数量);

            // 频道变动
            statText += QString("\n📡 频道变动（净增：%1%2）\n")
                            .arg(netChannel >= 0 ? "+" : "")
                            .arg(netChannel);
            statText += QString("  新加频道：%1（%2%3）\n")
                            .arg(stats.今日频道数量)
                            .arg(diff.今日频道数量 >= 0 ? "+" : "-")
                            .arg(diff.今日频道数量);
            statText += QString("  退出频道：%1（%2%3）\n")
                            .arg(stats.今日退出频道数量)
                            .arg(diff.今日退出频道数量 >= 0 ? "+" : "-")
                            .arg(diff.今日退出频道数量);

            // 更新时间
            statText += QString("\n⏰ 更新时间：%1")
                            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

            acc->StatT = statText;
        } else {
            // 如果获取差异失败（比如昨天同一分钟没有数据），显示当前累计值
            QString statText;
            statText += "📊 状态统计（仅今日累计）\n\n";
            statText += QString("📨 接收消息：%1\n").arg(stats.message_received);
            statText += QString("📤 发送消息：%1\n").arg(stats.message_sent);
            statText += QString("👤 活跃用户：%1\n").arg(stats.active_users);
            statText += QString("💬 活跃群：%1\n").arg(stats.active_groups);
            statText += QString("🏠 加群：%1  退群：%2\n").arg(stats.今日加群数量).arg(stats.今日退群数量);
            statText += QString("👥 好友：%1  删除好友：%2\n").arg(stats.今日好友数量).arg(stats.今日删除好友数量);
            statText += QString("📡 频道：%1  退出频道：%2\n").arg(stats.今日频道数量).arg(stats.今日退出频道数量);
            statText += QString("\n⏰ 更新时间：%1")
                            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
            acc->StatT = statText;
        }
    }
}

void AccountPage::loadAccounts() {
    // 1. 迁移旧文件（如果存在）
    QFile file("data/accounts.json");
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isArray()) {
                QJsonArray arr = doc.array();
                for (const QJsonValue &val : std::as_const(arr)) {
                    if (val.isObject()) {
                        QJsonObject obj = val.toObject();
                        QString appid = obj["appid"].toString();
                        if(!appid.isEmpty())
                        accdb->put(appid, QJsonDocument(obj).toJson(QJsonDocument::Compact));
                    }
                }
                qDebug() << "旧文件数据已迁移到数据库，共" << arr.size() << "条";
            }
            if (!file.remove()) {
                qWarning() << "无法删除旧文件，请手动处理";
            }
        } else {
            qWarning() << "无法打开旧文件，继续从数据库加载";
        }

    }


    m_accounts.clear();
    QStringList appidList = accdb->getAllKeys();
    for (const QString &key : std::as_const(appidList)) {
        if (key.isEmpty()) continue;
        QByteArray value = accdb->get(key).toUtf8();  // 通过 key 获取值
        if (value.isEmpty()) continue;

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(value, &err);
        if (err.error != QJsonParseError::NoError) continue;
        QJsonObject obj = doc.object();

        auto acc = std::make_shared<AccountInfo>();
        AccountInfo::fromJson(obj, *acc);
        m_accounts.append(acc);
    }
    qDebug() << "从数据库加载了" << m_accounts.size() << "条数据";
}
void AccountPage::saveAccounts(const AccountInfo *info) {

    if(框架退出) return;

    accdb->put(info->appid,info->toJson());
}
void AccountPage::refreshCards2(AccountInfo *info) {
    // 1. 添加新卡片（不影响已有卡片）
    CardWidget *card = new CardWidget(info);
    connect(card, &CardWidget::settingClicked, this, &AccountPage::onEditAccount);
    connect(card, &CardWidget::deleteClicked, this, &AccountPage::onDeleteAccount);

    m_flowLayout->addWidget(card);
    g_CW.insert(info->appid_int, card);
    QListWidgetItem *item = new QListWidgetItem;
    if(info->nickname.isEmpty())
        item->setText(info->appid);
    else
        item->setText(info->nickname);
    item->setData(Qt::UserRole,info->appid_int);
    robotListWidget->addItem(item);


    QPushButton *addBtn = nullptr;

    for (int i = 0; i < m_flowLayout->count(); ++i) {
        QWidget *w = m_flowLayout->itemAt(i)->widget();
        if (w && w->objectName() == "addAccountCard") {
            addBtn = qobject_cast<QPushButton*>(w);
            break;
        }
    }

    if (!addBtn) {
        // 不存在则创建
        addBtn = new QPushButton("+");
        addBtn->setObjectName("addAccountCard");
        addBtn->setFixedSize(110, 110);
        addBtn->setCursor(Qt::PointingHandCursor);
        connect(addBtn, &QPushButton::clicked, this, &AccountPage::onAddAccount);
        m_flowLayout->addWidget(addBtn);
    } else {
        // 已存在：将其移到末尾（先移除再添加）
        m_flowLayout->removeWidget(addBtn);
        m_flowLayout->addWidget(addBtn);
    }
}
void AccountPage::refreshCards() {
    QLayoutItem *child;
    while ((child = m_flowLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    g_CW.clear();
    for (const auto& infoPtr : std::as_const(m_accounts)) {
        CardWidget *card = new CardWidget(infoPtr.get());
        connect(card, &CardWidget::settingClicked, this, &AccountPage::onEditAccount);
        connect(card, &CardWidget::deleteClicked, this, &AccountPage::onDeleteAccount);


        QListWidgetItem *item = new QListWidgetItem;
        item->setText(infoPtr->nickname);
        item->setData(Qt::UserRole,infoPtr->appid_int);
        robotListWidget->addItem(item);
        m_flowLayout->addWidget(card);
        g_CW.insert(infoPtr->appid_int,card);

    }



    m_addBtn = new QPushButton("+");
    m_addBtn->setObjectName("addAccountCard");
    m_addBtn->setFixedSize(110, 110);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &AccountPage::onAddAccount);
    m_flowLayout->addWidget(m_addBtn);


}

void AccountPage::openAccountEditor(const AccountInfo &info, bool editMode) {
    AddAccountDialog dialog(info, this);
    dialog.setWindowTitle(editMode ? "编辑机器人账号" : "添加机器人账号");
    if (dialog.exec() != QDialog::Accepted) return;


    auto newInfo = std::make_shared<AccountInfo>();
    dialog.getAccountInfo(newInfo.get());

    if (newInfo->appid_int==0) {   // 假设 appid 是 QString，用 isEmpty() 判断
        QMessageBox::warning(this, "提示", "AppID 不能为空");
        return;
    }

    int existingIndex = -1;
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i]->appid_int == newInfo->appid_int) {
            existingIndex = i;
            break;
        }
    }

    if (!editMode) {
        if (existingIndex != -1) {
            QMessageBox::warning(this, "重复", "AppID 已存在");
            return;
        }

        m_accounts.append(newInfo);
        refreshCards2(newInfo.get());

    } else {

        int oldIndex = -1;
        for (int i = 0; i < m_accounts.size(); ++i) {
            if (m_accounts[i]->appid_int == info.appid_int) {
                oldIndex = i;
                break;
            }
        }
        if (oldIndex == -1) return;

        if (newInfo->appid_int != info.appid_int && existingIndex != -1) {
            QMessageBox::warning(this, "重复", "AppID 已存在");
            return;
        }

        auto oldInfoPtr = m_accounts[oldIndex];

        oldInfoPtr->secret = newInfo->secret;
        oldInfoPtr->botqq =   newInfo->botqq;
        oldInfoPtr->wsAddress = newInfo->wsAddress;
        oldInfoPtr->botsettext = newInfo->botsettext;
        oldInfoPtr->type =newInfo->type;

        oldInfoPtr->markdown = newInfo->markdown;
        oldInfoPtr->markdown_pd = newInfo->markdown_pd;
        oldInfoPtr->markdown_pd_mb = newInfo->markdown_pd_mb;
        oldInfoPtr->wsIntents = newInfo->wsIntents;

        saveAccounts(oldInfoPtr.get());
    }


}

void AccountPage::onAddAccount() {
    openAccountEditor(AccountInfo(), false);
}


void AccountPage::onEditAccount(int appid) {
    AccountInfo *info = findAccount(appid);
    if (!info) return;
    openAccountEditor(*info, true);
}

void AccountPage::onDeleteAccount(int appid) {
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i]->appid_int == appid) {
            // 1. 删除界面上的卡片控件
            if (g_CW.contains(appid)) {
                CardWidget *card = g_CW.take(appid);   // 从映射中取出
                m_flowLayout->removeWidget(card);      // 从布局中移除
                card->deleteLater();                   // 安全删除（或在当前函数 delete card）
            }
            if(m_botClients.contains(appid))
            {
                m_botClients.remove(appid);
                doWork(500); //等待断开
            }

            accdb->remove(m_accounts[i]->appid);
            m_accounts.removeAt(i);

            for(int i=0 ;i<robotListWidget->count();++i)
            {
                auto *item = robotListWidget->item(i);
                if(item->data(Qt::UserRole)==appid)
                {
                    robotListWidget->takeItem(i);
                    break;
                }
            }

            break;
        }
    }


}


AccountInfo* AccountPage::findAccount(int appid) {
    for (const auto& infoPtr : std::as_const(m_accounts)) {
        if (infoPtr->appid_int == appid) return infoPtr.get();
    }
    return nullptr;
}


void AccountPage::autoConnectBots() {
    for (const auto &info : std::as_const(m_accounts)) {
        if (info->autoConnect && !info->online) {
            if(!g_CW.contains(info->appid_int)) continue;
            CardWidget *cw =g_CW[info->appid_int];
            cw->triggerLogin();
        }
    }
}
