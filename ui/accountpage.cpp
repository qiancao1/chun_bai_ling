#include "accountpage.h"
#include "cardwidget.h"
#include "addaccountdialog.h"
#include "global.h"
#include "devlogin.h"      // 开放平台扫码登录 + connect_info/modify（cookie 只在内存）
#include "themecolors.h"   // 页面自带样式表也要跟着主题走
#include "homepage.h"
#include "qqbotclient.h"
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
#include <QDialog>
#include <QPixmap>
#include <memory>

// webhook 服务是否在跑（db/image-server.cpp）
bool is_server();

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

    // 每分钟跑一次，没有新增就什么都不做：不写盘、不重绘（空闲时零开销）
    if (deltaReceived == 0 && deltaSent == 0) return;

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

    if (homePage) homePage->updateChartData();
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
    // 走 ThemeColors::applyTo：颜色仍是下面写死的默认色，但用户换配色时会自动重套
    ThemeColors::applyTo(this, QString::fromUtf8(R"(
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
        QPushButton#accountGhostBtn {
            background: #FFF9F2;
            color: #FF7F32;
            border: 1px solid #F0B680;
            border-radius: 9px;
            padding: 6px 14px;
            min-height: 30px;
            font-weight: 700;
        }
        QPushButton#accountGhostBtn:hover {
            background: #FFF0DE;
            border-color: #FF914D;
        }
        QPushButton#accountGhostBtn:disabled {
            color: #B9B1A6;
            border-color: #E6D4C0;
            background: #FAF5EE;
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
    )"));

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

    // 右侧表单里直接点 WebSocket / Webhook 单选 → 问一句要不要同步改开放平台
    m_editor->setModeSwitchHandler([this](int targetType) { onConnectModeRequested(targetType); });

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
    // ⚠ 必须是「每分钟」，不能是「每小时」：
    //   ① 每小时触发时 now.hour() 已经跨进下一个小时，这一小时的增量被记到下一格 —— 整张图错位一小时；
    //   ② 正在进行的这一小时要等下次触发才结算，界面上那一格就一直是 0（「9 点没满所以不显示」）。
    //   本函数按「当前累计 − 上次累计」的增量记账，提高频率不会重复计数，所以改成每分钟最省事：
    //   增量落进当前小时格，进行中的这一小时也能实时长柱子。
    m_hourlyTimer->start(60 * 1000);
}

AccountPage::~AccountPage() {
    // 卡片控件随本对象一起销毁，这里把全局映射清掉避免留下野指针
    g_CW.clear();
}
void AccountPage::onStatTick()
{
    uint32_t nowMinute = QDateTime::currentSecsSinceEpoch() / 60;

    for (auto &acc : m_accounts) {
        if (!acc) continue;

        // 跨天清零：这里每分钟跑一次，不受「当前停在哪一页」影响。
        // 以前只在读取配置时清零，程序连跑几天「今日收发」就一直累加，
        // 首页那几张「今日」卡片看着跟累计一样。清完把新的日期标记落盘。
        if (acc->checkDailyReset())
            saveAccounts(acc.get());

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

    for (const auto& infoPtr : std::as_const(m_accounts)) {
        addCardFor(infoPtr.get());
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
    bool qh = (ai->sandbox != tmp.sandbox);
    ai->sandbox = tmp.sandbox;
    // 注意：botsettext 由 QQ 回调写入，表单里没有对应控件，这里保持原值不清空

    saveAccounts(ai);

    if (g_CW.contains(ai->appid_int) && g_CW[ai->appid_int]){
        if(qh && m_botClients.contains(ai->appid_int))
        {
            auto *c = m_botClients[ai->appid_int];
            if(c->m_info->online){
                c->stop();
                QMessageBox::warning(this,"沙盒切换","检测到切换沙盒 自动下线 请点击登录按钮 进行登录");
            }
        }
        g_CW[ai->appid_int]->refreshDisplay();
    }

    // 让表单回显落库后的真实值（例如 AppID 被拒绝修改的情况）
    m_editor->setAccountInfo(*ai);

    updateActionState();
    AppendEventLog(QString("已保存账号配置 %1").arg(ai->appid), 0x2E9E5B);
}

// 删除账号：不能直接删。
// QQBotClient 是卡片的子对象，连接还在收包 / 重连时把卡片（连同 client）
// 析构掉就会崩。所以流程固定为：先下线 → 等 1 秒让网络事件收尾 → 再真正删除。
void AccountPage::onDeleteAccount(int appid) {
    if (appid == 0 || m_pendingDelete.contains(appid))
        return;   // 已经在等删除的，忽略重复点击

    AccountInfo *ai = findAccount(appid);
    if (!ai)
        return;

    m_pendingDelete.insert(appid);

    // 卡片给个「删除中…」的反馈，同时挡住登录按钮和重复点删除
    if (CardWidget *card = g_CW.value(appid, nullptr))
        card->setDeletePending(true);

    // 1) 先下线：stop() 会关 websocket、停心跳、断掉重连定时器
    if (QQBotClient *client = m_botClients.value(appid, nullptr)) {
        client->stop();
        AppendEventLog(QString("账号 %1 已下线，1 秒后删除").arg(ai->appid), 0xFF7F32);
    }

    // 2) 1 秒后再删 —— 这段延迟就是留给连接收尾的
    QTimer::singleShot(1000, this, [this, appid]() {
        m_pendingDelete.remove(appid);
        removeAccountNow(appid);
    });
}

// 真正执行删除。唯一调用点是上面那个定时器，此时连接已经收尾，删卡片才安全。
void AccountPage::removeAccountNow(int appid) {
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i]->appid_int != appid)
            continue;

        // 1. 删除界面上的卡片控件
        if (g_CW.contains(appid)) {
            CardWidget *card = g_CW.take(appid);   // 从映射中取出
            m_cardLayout->removeWidget(card);      // 从布局中移除
            card->deleteLater();                   // 安全删除（或在当前函数 delete card）
        }

        // 2. 网络客户端：前面已经 stop 过，这里只从映射摘掉，销毁交给事件循环
        //    （不再用 doWork 阻塞等待，延迟已经等过了）
        if (QQBotClient *client = m_botClients.take(appid))
            client->deleteLater();

        const QString appidStr = m_accounts[i]->appid;
        accdb->remove(appidStr);
        m_accounts.removeAt(i);

        // 删掉的正好是选中项：自动切到第一个剩余账号
        if (m_selectedAppid == appid) {
            m_selectedAppid = 0;
            m_newMode = false;
            if (!m_accounts.isEmpty())
                selectAccount(m_accounts.first()->appid_int);
            else
                clearEditor();
        }

        AppendEventLog(QString("已删除账号 %1").arg(appidStr), 0xD9534F);
        break;
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

// ===========================================================================
// 切换 WS / Webhook
//
// 平台接口：POST https://bot.q.qq.com/cgi-bin/v2/bot_dev_setting/connect_info/modify
//   WS      -> {"bot_appid":appid,"action":3,"webhook_url":""}
//   Webhook -> {"bot_appid":appid,"action":1,"webhook_url":"http://ip:端口/webhook/appid"}
//
// 这条接口要的是「QQ 开放平台的开发者登录态」，不是机器人的 access_token，
// 所以点按钮的流程是：先扫码登录（cookie 只在内存，见 core/devlogin.h）→ 再调接口。
// ===========================================================================

// 拼平台侧要填的回调地址：http(s)://<对外链接>:<webhook端口>/webhook/<appid>
// 「对外链接」里已经带协议（比如 https://abc.com）就原样当基址，不再补协议/端口。
static QString buildWebhookUrl(const QString &appid)
{
    QString base = g_config.value(QStringLiteral("local_server_ip")).toString().trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    if (base.isEmpty())
        return QString();

    if (!base.contains(QStringLiteral("://"))) {
        const bool ssl = g_config.value(QStringLiteral("SSL")).toBool();
        int port = g_config.value(QStringLiteral("webhook_p")).toInt();
        if (port <= 0) port = 8080;
        base = (ssl ? QStringLiteral("https://") : QStringLiteral("http://"))
             + base + QLatin1Char(':') + QString::number(port);
    }
    return base + QStringLiteral("/webhook/") + appid;
}

// ===========================================================================
// 右侧表单里点「WebSocket / Webhook 旁边的小图标」
//
// 注意：点单选按钮本身**不会**走到这里（那只是本地配置，保存时写库）。
// 只有点小图标才问一句「要不要把开放平台上的连接方式也改掉」，
// 同意才走扫码 → connect_info/modify。
// ===========================================================================
void AccountPage::onConnectModeRequested(int targetType)
{
    if (m_newMode || m_selectedAppid == 0) {
        QMessageBox::information(this, "先保存账号",
            "这个账号还没保存。先点「保存当前账号配置」，之后再用小图标同步平台连接方式。");
        return;
    }
    AccountInfo *ai = findAccount(m_selectedAppid);
    if (!ai || !m_editor)
        return;

    const QString curName = (ai->type == 1) ? QStringLiteral("Webhook") : QStringLiteral("WebSocket");
    const QString newName = (targetType == 1) ? QStringLiteral("Webhook") : QStringLiteral("WebSocket");

    // ⚠ 不拦「已经是这个模式」：平台接口是幂等的，重复设置一遍没有副作用。
    //   用户也可能就是想重新覆盖一次（比如平台上被别的操作改回去了），随时可点。
    const auto ret = QMessageBox::question(this, "同步到开放平台？",
        QString("账号 %1 当前是 %2 模式。\n\n"
                "把 QQ 开放平台上的连接方式设置为 %3？\n"
                "（会先扫码登录开放平台，再调用平台接口修改）%4")
            .arg(ai->appid, curName, newName,
                 targetType == 1
                     ? QString("\n\n⚠ 切到 Webhook 之后还要去开放平台订阅事件，否则收不到任何消息。")
                     : QString()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (ret != QMessageBox::Yes) {
        // 点图标不改单选，所以这里不用回滚；也别重建表单，免得丢掉用户正在改的内容
        return;
    }

    QString webhookUrl;
    if (targetType == 1) {
        if (!is_server()) {
            QMessageBox::warning(this, "还没启用 webhook",
                "切到 Webhook 之前，先到「设置 → 网络与服务」：\n\n"
                "· 勾选「启动用webhook」（端口默认 8080，确认后开始监听）；\n"
                "· 填好「对外链接」（公网域名或 IP）。");
            return;
        }
        webhookUrl = buildWebhookUrl(ai->appid);
        if (webhookUrl.isEmpty()) {
            QMessageBox::warning(this, "还没填对外链接",
                "「设置 → 网络与服务 → 对外链接」是空的。\n\n"
                "Webhook 回调地址要用它拼成：http://<对外链接>:<端口>/webhook/<appid>。");
            return;
        }
    }

    runConnectModeSwitch(ai->appid_int, (targetType == 1) ? 1 : 3, webhookUrl);
}

void AccountPage::runConnectModeSwitch(int appid, int action, const QString &webhookUrl, bool forceLogin)
{
    auto callApi = [this, appid, action, webhookUrl]() {
        DevLogin::instance().modifyConnectInfo(appid, action, webhookUrl,
            [this, appid, action, webhookUrl](bool ok, const QString &body, const QString &err) {
                AccountInfo *ai = findAccount(appid);
                if (!ai) {
                    updateActionState();
                    return;
                }

                if (!ok) {
                    updateActionState();

                    // 会话在调用前被摘掉了（并发 / 401 清理）：直接走扫码
                    if (err == QLatin1String("NEED_LOGIN")) {
                        runConnectModeSwitch(appid, action, webhookUrl, true);
                        return;
                    }

                    // 登录态过期（{"retcode":401,"msg":"miss ticket"}）
                    const bool expired = err.contains(QStringLiteral("ticket"), Qt::CaseInsensitive)
                                      || body.contains(QStringLiteral("401"));

                    // ⚠ 多账号：B 机器人如果不在当前登录的开发者账号名下，平台会直接拒。
                    //   这里把「当前用的是哪个开发者」显示出来，并提供换号重扫。
                    const QString who = DevLogin::instance().sessionLabelFor(appid);

                    // 多账号：B 机器人不在当前身份下时平台会直接拒 —— 换对应 QQ 号重扫即可。
                    QMessageBox box(this);
                    box.setIcon(QMessageBox::Question);
                    box.setWindowTitle(expired ? "登录态已过期" : "切换失败");
                    box.setText(QString("错误：%1\n\n原始返回：\n%2\n\n当前使用的登录账号：%3")
                                    .arg(err, body.left(400), who));
                    box.setInformativeText(
                        QStringLiteral("如果提示无权限，通常是这个机器人不在该开发者账号名下，\n"
                                       "换它所属的 QQ 号重新扫码登录一次即可。"));
                    QPushButton *bRelogin = box.addButton(QStringLiteral("重新扫码登录"),
                                                         QMessageBox::ActionRole);
                    box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
                    box.exec();

                    if (box.clickedButton() == bRelogin) {
                        // 只解绑这个机器人，不动别的账号的 ck
                        DevLogin::instance().clearSessionForAppid(appid);
                        runConnectModeSwitch(appid, action, webhookUrl, true);   // 强制重扫
                    }
                    return;
                }

                // ---- 成功：把本地配置同步成新模式 ----
                const int  newType    = (action == 1) ? 1 : 0;
                const bool typeChanged = (ai->type != newType);

                ai->type = newType;
                saveAccounts(ai);
                m_editor->setAccountInfo(*ai);          // 单选框跟着回显
                if (g_CW.contains(appid) && g_CW[appid])
                    g_CW[appid]->refreshDisplay();      // 卡片上的 ws / webhook 角标
                updateActionState();
                AppendEventLog(QString("账号 %1 连接方式已切到 %2")
                                   .arg(ai->appid, newType == 1 ? QStringLiteral("Webhook")
                                                                : QStringLiteral("WebSocket")),
                               0x2E9E5B);

                // 在线的话先下线：模式变了，旧连接没意义
                if (typeChanged) {
                    if (QQBotClient *c = m_botClients.value(appid, nullptr)) {
                        if (c->m_info && c->m_info->online)
                            c->stop();
                    }
                }

                // 平台原样返回（正常是 {"retcode":0,"msg":"success","data":{}}），弹框里带出来
                const QString raw = body.trimmed();

                if (newType == 1) {
                    QMessageBox::information(this, "已切换到 Webhook",
                        QString("平台侧已改成 Webhook。\n\n"
                                "回调地址：%1\n\n"
                                "⚠ 还要去 QQ 开放平台后台订阅事件（开发设置 → 消息订阅/\n"
                                "事件订阅），Webhook 模式下不订阅就收不到任何消息。\n\n"
                                "之后回卡片上点一次「登录」，让账号按 Webhook 模式上线。\n\n"
                                "接口返回：%2")
                            .arg(webhookUrl, raw));
                } else {
                    QMessageBox::information(this, "已切换到 WebSocket",
                        QString("平台侧已改成 WebSocket（webhook 地址已清空）。\n\n"
                                "之后回卡片上点一次「登录」，让账号按 WebSocket 模式上线。\n\n"
                                "接口返回：%1")
                            .arg(raw));
                }
            });
    };

    // 这个机器人已经有可用登录态（绑定过的开发者，或能复用的最近会话）就直接调；
    // 否则（或用户选了「换号重扫」）先扫码。扫码成功后 devlogin 会把 appid 绑到新账号上。
    if (!forceLogin && DevLogin::instance().hasSessionFor(appid)) {
        callApi();
        return;
    }
    showDevQrDialog(appid, [this, callApi](bool ok) {
        if (ok) { callApi(); return; }
        // 扫码取消/失败：恢复界面状态（调用方可能已把单选/表单置成目标模式）
        updateActionState();
    });
}

void AccountPage::showDevQrDialog(int appid, std::function<void(bool)> afterLogin)
{
    if (m_devQrDialog) {
        m_devQrDialog->raise();
        m_devQrDialog->activateWindow();
        // 已经有一个扫码窗口在等着：不重复开，直接回一次 false 让调用方收尾
        if (afterLogin) afterLogin(false);
        return;
    }

    auto *dlg = new QDialog(this);
    m_devQrDialog = dlg;
    dlg->setWindowTitle(QString("QQ 开放平台扫码登录 · AppID %1").arg(appid));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModal(false);
    dlg->resize(350, 450);

    auto *lay = new QVBoxLayout(dlg);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(8);

    auto *tip = new QLabel(QString("用手机 QQ 扫码登录 QQ 开放平台。\n"
                                   "登录的是「AppID %1 所属的那个开发者账号」——\n"
                                   "别的开发者账号下的机器人，得换成对应 QQ 号再扫一次。\n"
                                   "登录态只保存在内存里，不会写入任何文件。").arg(appid), dlg);
    tip->setWordWrap(true);

    auto *qr = new QLabel("正在获取二维码…", dlg);
    qr->setAlignment(Qt::AlignCenter);
    qr->setFixedSize(300, 300);

    auto *stat = new QLabel("等待扫码…", dlg);
    stat->setAlignment(Qt::AlignCenter);

    lay->addWidget(tip);
    lay->addWidget(qr, 0, Qt::AlignHCenter);
    lay->addWidget(stat);

    connect(dlg, &QDialog::finished, this, [this](int) {
        // 先把指针摘掉再取消：cancelLogin() 会同步回调进来，
        // 那边看到 m_devQrDialog 已经空了就不会再 close 一次（避免重入）。
        m_devQrDialog = nullptr;
        // 关窗口 = 取消这次扫码（已拿到的 cookie 留在内存，下次还能直接用）
        DevLogin::instance().cancelLogin();
    });

    QPointer<QLabel> qrSafe   = qr;
    QPointer<QLabel> statSafe = stat;
    QPointer<QDialog> dlgSafe = dlg;

    // ⚠ QDialog 不会自己显示：少了这句就是「点了按钮什么都没发生」
    dlg->show();
    dlg->raise();
    dlg->activateWindow();

    DevLogin::instance().startLogin(appid,
        [this, dlg, dlgSafe, afterLogin, statSafe](bool ok, const QString &err) {
            if (!ok) {
                if (m_devQrDialog == dlg) {
                    m_devQrDialog = nullptr;
                    dlg->close();                 // WA_DeleteOnClose，自己回收
                }
                if (!err.contains(QStringLiteral("取消")))
                    QMessageBox::warning(this, "扫码登录失败", err);
                // 取消/失败也要通知调用方，不然界面会一直停在未完成状态
                if (afterLogin) afterLogin(false);
                return;
            }

            // 成功：先在窗口里明确写「登录成功」，停 0.9 秒再关，关掉之后才去调平台接口。
            // 否则窗口一闪就没了，用户完全看不出扫码到底成没成。
            if (statSafe) {
                statSafe->setText(QStringLiteral("扫码登录成功，正在同步开放平台设置…"));
                statSafe->setStyleSheet(QStringLiteral("color:#27AE60; font-weight:600;"));
            }

            QTimer::singleShot(900, this, [this, dlgSafe, afterLogin]() {
                if (QDialog *d = dlgSafe) {
                    if (m_devQrDialog == d) m_devQrDialog = nullptr;
                    d->close();
                }
                if (afterLogin) afterLogin(true);
            });
        },
        [qrSafe, statSafe](const QByteArray &png) {
            if (!qrSafe || !statSafe) return;
            QPixmap pix;
            if (pix.loadFromData(png))
                qrSafe->setPixmap(pix.scaled(qrSafe->size(), Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
            else
                qrSafe->setText("二维码加载失败");
            statSafe->setText("等待扫码…（二维码过期会自动刷新）");
        },
        // 轮询状态：66=未扫 / 67=已扫码待确认 —— 以前不给任何反馈，用户会以为卡在「等待扫码」
        [statSafe](const QString &text) {
            if (statSafe) statSafe->setText(text);
        });
}

