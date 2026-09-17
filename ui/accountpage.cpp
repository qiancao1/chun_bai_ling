#include "accountpage.h"
#include "cardwidget.h"
#include "addaccountdialog.h"
#include "global.h"
#include "homepage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
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
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(R"(
        QWidget#accountPage {
            background: #F7EFE5;
        }
        QScrollArea#accountCardScroll {
            border: none;
            background: transparent;
        }
        QScrollArea#accountCardScroll > QWidget > QWidget {
            background: transparent;
        }
        QPushButton#addAccountCard {
            font-size: 20px;
            border: 2px dashed #F0B680;
            border-radius: 10px;
            background-color: rgba(255, 253, 249, 220);
            color: #FF914D;
            font-weight: 800;
        }
        QPushButton#addAccountCard:hover {
            background: #FFF0DE;
            border-color: #FF914D;
        }
        QLabel#acctEditorHint {
            color: #7A8798;
            font-size: 12px;
            background: transparent;
        }
        QPushButton#accountPrimaryBtn {
            background: #FF914D;
            color: #FFFFFF;
            border: none;
            border-radius: 9px;
            padding: 6px 16px;
            min-height: 30px;
            font-weight: 800;
        }
        QPushButton#accountPrimaryBtn:hover {
            background: #FF7F32;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 2px;
        }
        QScrollBar::handle:vertical {
            background: #D9C9B8;
            border-radius: 5px;
            min-height: 36px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )");

    QHBoxLayout *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // ================= 左列：账号卡片（一列） =================
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("accountCardScroll");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFixedWidth(340);

    m_containerWidget = new QWidget;
    m_containerWidget->setObjectName("accountCardContainer");
    m_cardLayout = new QVBoxLayout(m_containerWidget);
    m_cardLayout->setContentsMargins(2, 2, 2, 2);
    m_cardLayout->setSpacing(6);
    m_scrollArea->setWidget(m_containerWidget);

    // [+] 添加账号（常驻在卡片列表末尾）
    m_addBtn = new QPushButton("+  添加账号");
    m_addBtn->setObjectName("addAccountCard");
    m_addBtn->setFixedHeight(42);
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &AccountPage::onAddAccount);
    m_cardLayout->addWidget(m_addBtn);
    m_cardLayout->addStretch();

    rootLayout->addWidget(m_scrollArea, 0);

    // ================= 右列：账号详细配置 =================
    QWidget *rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    m_editor = new AddAccountDialog(AccountInfo(), rightPanel);
    rightLayout->addWidget(m_editor, 1);
    // 注意顺序：先入布局再改窗口标志（setWindowFlags 可能把控件隐藏掉）
    m_editor->setEmbeddedMode(true);
    m_editor->show();

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(2, 0, 2, 0);
    btnRow->setSpacing(4);

    m_editorHint = new QLabel("未选择账号");
    m_editorHint->setObjectName("acctEditorHint");
    btnRow->addWidget(m_editorHint);
    btnRow->addStretch();

    m_saveSelBtn = new QPushButton("保存当前账号配置");
    m_saveSelBtn->setObjectName("accountPrimaryBtn");
    m_saveSelBtn->setCursor(Qt::PointingHandCursor);

    btnRow->addWidget(m_saveSelBtn);

    rightLayout->addLayout(btnRow);
    rootLayout->addWidget(rightPanel, 1);

    connect(m_saveSelBtn, &QPushButton::clicked, this, &AccountPage::onSaveSelected);

    loadAccounts();
    refreshCards();

    // 默认选中第一个账号
    if (!m_accounts.isEmpty())
        selectAccount(m_accounts.first()->appid_int);
    else
        clearEditor();

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
    // 卡片控件随本对象一起销毁，这里把全局映射清掉避免留下野指针
    g_CW.clear();
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

// 在左侧卡片列中插入一张账号卡片（插在 [+] 之前）
CardWidget *AccountPage::addCardFor(AccountInfo *info) {
    CardWidget *card = new CardWidget(info);
    connect(card, &CardWidget::clicked, this, &AccountPage::onCardClicked);
    connect(card, &CardWidget::deleteClicked, this, &AccountPage::onDeleteAccount);

    int idx = m_cardLayout->indexOf(m_addBtn);
    if (idx < 0)
        m_cardLayout->addWidget(card);
    else
        m_cardLayout->insertWidget(idx, card);

    g_CW.insert(info->appid_int, card);
    return card;
}

void AccountPage::refreshCards2(AccountInfo *info) {
    if (!info) return;

    CardWidget *card = g_CW.value(info->appid_int, nullptr);
    if (card) {
        card->refreshDisplay();
    } else {
        addCardFor(info);

        QListWidgetItem *item = new QListWidgetItem;
        item->setText(info->nickname.isEmpty() ? info->appid : info->nickname);
        item->setData(Qt::UserRole, info->appid_int);
        robotListWidget->addItem(item);
    }

    // 新增/外部变更的账号直接选中，右侧立即显示它的配置
    selectAccount(info->appid_int);
}

void AccountPage::refreshCards() {
    // 只清掉卡片，[+] 按钮保留（它也在 m_cardLayout 里）
    QLayoutItem *child;
    while ((child = m_cardLayout->takeAt(0)) != nullptr) {
        QWidget *w = child->widget();
        if (w && w != m_addBtn)
            delete w;
        delete child;
    }
    g_CW.clear();
    robotListWidget->clear();

    for (const auto& infoPtr : std::as_const(m_accounts)) {
        addCardFor(infoPtr.get());

        QListWidgetItem *item = new QListWidgetItem;
        item->setText(infoPtr->nickname.isEmpty() ? infoPtr->appid : infoPtr->nickname);
        item->setData(Qt::UserRole, infoPtr->appid_int);
        robotListWidget->addItem(item);
    }

    m_cardLayout->addWidget(m_addBtn);
    m_cardLayout->addStretch();
}

// 选中某个账号：右侧显示它的配置
void AccountPage::selectAccount(int appid) {
    AccountInfo *ai = findAccount(appid);
    if (!ai) {
        clearEditor();
        return;
    }
    m_newMode = false;
    m_selectedAppid = appid;
    m_editor->setAccountInfo(*ai);
    updateSelectionStyle();
    updateActionState();
}

// 清空编辑器（没有账号 / 删除完最后一个账号）
void AccountPage::clearEditor() {
    m_newMode = false;
    m_selectedAppid = 0;
    m_editor->setAccountInfo(AccountInfo());
    updateSelectionStyle();
    updateActionState();
}

void AccountPage::updateSelectionStyle() {
    for (CardWidget *card : std::as_const(g_CW)) {
        if (!card || !card->m_info) continue;
        card->setSelected(card->m_info->appid_int == m_selectedAppid && m_selectedAppid != 0);
    }
}

void AccountPage::updateActionState() {
    if (!m_editorHint) return;

    if (m_newMode) {
        m_editorHint->setText("新建账号 —— 填写后点击「保存当前账号配置」");
        return;
    }
    if (m_selectedAppid == 0) {
        m_editorHint->setText("未选择账号");
        return;
    }
    AccountInfo *ai = findAccount(m_selectedAppid);
    if (!ai) {
        m_editorHint->setText("未选择账号");
        return;
    }
    m_editorHint->setText(QString("当前账号：%1（AppID %2）")
                              .arg(ai->nickname.isEmpty() ? ai->appid : ai->nickname, ai->appid));
}

// [+] ：进入“新建账号”状态，右侧表单清空待填
void AccountPage::onAddAccount() {
    m_newMode = true;
    m_selectedAppid = 0;
    m_editor->setAccountInfo(AccountInfo());
    updateSelectionStyle();
    updateActionState();
    m_editor->focusAppId();
}

void AccountPage::onCardClicked(int appid) {
    if (appid == 0) return;
    selectAccount(appid);
}

void AccountPage::onSaveSelected() {
    if (!m_editor) return;

    AccountInfo tmp;
    m_editor->getAccountInfo(&tmp);

    if (tmp.appid.trimmed().isEmpty() || tmp.appid_int == 0) {
        QMessageBox::warning(this, "提示", "AppID 不能为空，且必须是数字");
        return;
    }

    // ---------- 新增 ----------
    if (m_newMode) {
        if (findAccount(tmp.appid_int)) {
            QMessageBox::warning(this, "重复", "AppID 已存在");
            return;
        }

        auto np = std::make_shared<AccountInfo>();
        m_editor->getAccountInfo(np.get());

        m_accounts.append(np);
        saveAccounts(np.get());
        refreshCards2(np.get());   // 插入卡片并选中

        m_newMode = false;
        m_selectedAppid = np->appid_int;
        updateSelectionStyle();
        updateActionState();
        AppendEventLog(QString("新增账号 %1").arg(np->appid), 0x2E9E5B);
        return;
    }

    // ---------- 修改 ----------
    if (m_selectedAppid == 0) {
        QMessageBox::warning(this, "提示", "请先在左侧选择一个账号，或点击「+ 添加账号」新建");
        return;
    }

    AccountInfo *ai = findAccount(m_selectedAppid);
    if (!ai) {
        QMessageBox::warning(this, "提示", "选中的账号已不存在");
        updateActionState();
        return;
    }

    if (tmp.appid_int != ai->appid_int) {
        // AppID 同时是 LMDB 的 key 和 botdb 目录名，这里不做重命名
        QMessageBox::information(this, "提示",
                                 QString("AppID 不可修改，已保留原 AppID：%1").arg(ai->appid));
    }

    ai->secret = tmp.secret;
    ai->botqq = tmp.botqq;

    ai->type = tmp.type;
    ai->markdown = tmp.markdown;
    ai->markdown_pd = tmp.markdown_pd;
    ai->markdown_pd_mb = tmp.markdown_pd_mb;
    ai->wsIntents = tmp.wsIntents;
    ai->sandbox = tmp.sandbox;
    // 注意：botsettext 由 QQ 回调写入，表单里没有对应控件，这里保持原值不清空

    saveAccounts(ai);

    if (g_CW.contains(ai->appid_int) && g_CW[ai->appid_int])
        g_CW[ai->appid_int]->refreshDisplay();

    for (int i = 0; i < robotListWidget->count(); ++i) {
        auto *item = robotListWidget->item(i);
        if (item->data(Qt::UserRole).toInt() == ai->appid_int) {
            item->setText(ai->nickname.isEmpty() ? ai->appid : ai->nickname);
            break;
        }
    }

    // 让表单回显落库后的真实值（例如 AppID 被拒绝修改的情况）
    m_editor->setAccountInfo(*ai);

    updateActionState();
    AppendEventLog(QString("已保存账号配置 %1").arg(ai->appid), 0x2E9E5B);
}

void AccountPage::onDeleteAccount(int appid) {
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i]->appid_int == appid) {
            // 1. 删除界面上的卡片控件
            if (g_CW.contains(appid)) {
                CardWidget *card = g_CW.take(appid);   // 从映射中取出
                m_cardLayout->removeWidget(card);      // 从布局中移除
                card->deleteLater();                   // 安全删除（或在当前函数 delete card）
            }
            if(m_botClients.contains(appid))
            {
                m_botClients.remove(appid);
                doWork(500); //等待断开
            }

            accdb->remove(m_accounts[i]->appid);
            m_accounts.removeAt(i);

            for(int j = 0; j < robotListWidget->count(); ++j)
            {
                auto *item = robotListWidget->item(j);
                if(item->data(Qt::UserRole).toInt() == appid)
                {
                    robotListWidget->takeItem(j);
                    delete item;
                    break;
                }
            }

            // 删掉的正好是选中项：自动切到第一个剩余账号
            if (m_selectedAppid == appid) {
                m_selectedAppid = 0;
                m_newMode = false;
                if (!m_accounts.isEmpty())
                    selectAccount(m_accounts.first()->appid_int);
                else
                    clearEditor();
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
