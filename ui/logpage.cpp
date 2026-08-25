#include "logpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QHeaderView>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QScrollBar>
#include <QThread>
#include <QInputDialog>
#include <QDateTime>
#include "global.h"

int Color_0=0;
int Color_1=0;

LogPage::LogPage(QWidget *parent) : QWidget(parent)
{
    m_model = new QStandardItemModel(this); //先加载 因为setipUi 会动这个 类
    g_logdb[0]->cleanDatabase(200);
    Message mes;
    mes.Color_0=0xF3312F;
    int logs = g_config["logs"].toInt(100000);
    for(int i=0 ;i<5;++i)
    {
        if(i>0)
            g_logdb[i]->cleanDatabase(logs);
        QList<QPair<QString, Message>> list = g_logdb[i]->getLatestMessagesWithOffset(0,1,0);
        if (!list.isEmpty()) {
            QPair<QString, Message> pair = list.first();
            Message m = pair.second;
            if(!m.msg.isEmpty())
                g_logdb[i]->appendLog("0", "0", mes);
        }
    }
    setupUi();
    applyStyleSheet();
    m_flushTimer = new QTimer(this);
    connect(m_flushTimer, &QTimer::timeout, this, &LogPage::flushPendingRows);
    m_flushTimer->start(1000);
}

LogPage::~LogPage() {
    m_flushTimer->stop();
}

void LogPage::switchTab(int index)
{
    currentTabIndex = index;
    leiji=0;
    tabStack->setCurrentIndex(index);

    QList<QPushButton*> btns = {btnEventTab, btnGroupTab, btnPrivateTab, btnChannelTab, btnChannelPrivateTab};
    for (int i = 0; i < btns.size(); ++i) {
        btns[i]->setChecked(i == index);
    }
    {
        QMutexLocker locker(&m_mutex);
        m_pendingRows.clear();
    }
    // 重置分页状态，重新加载
    resetAndLoad(BATCH_SIZE);
    QTableView* view = currentListView();
    if (view) {
        view->scrollToBottom();
    }
}
void LogPage::switchTabEx(int index,int limit)
{
    if(limit<=0) limit =0;
    resetAndLoad(limit);
    QTableView* view = currentListView();
    if (view) {
        view->scrollToBottom();
    }
}
void LogPage::setCurrentBot(int botId, const QString &botName)
{
    m_currentBotId = botId;
    m_currentBotName = botName;
    // 如果当前页面是激活的，刷新
    if (m_active) {
        resetAndLoad(BATCH_SIZE);
    }
}

void LogPage::setActive(bool active)
{
    m_active = active;
    leiji=0;
    if (m_active) {
        resetAndLoad(BATCH_SIZE);
        QTableView* view = currentListView();
        if (view) {
            view->scrollToBottom();
        }
    }else{
        if (m_model) {
            m_model->clear();
        }
    }
}

void LogPage::resetAndLoad(int limit)
{
    m_offset = 0;
    m_hasMore = true;
    m_loading = false;
    if (m_model) {
        m_model->clear();
    }
    setTableHeaders();
    if(limit<=0) return;
    loadMore(limit); // 初始加载最新一批
}
int accinfo(int appid);
QString getBotName(int appid){
    int index=accinfo(appid);
    if(index==-1) return "-";
    return m_accounts[index]->nickname;
}

void LogPage::loadMore(int limit)
{
    if (m_loading || !m_hasMore || !m_model) return;
    m_loading = true;

    LogDB *db = g_logdb [currentTabIndex].get();
    if (!db) {
        m_loading = false;
        return;
    }


    int appidFilter = m_currentBotId; // 0 表示全部
    QList<QPair<QString, Message>> data = db-> getLatestMessagesWithOffset(appidFilter, limit, m_offset);



    if (data.size() < limit) {
        m_hasMore = false;
    }

    for (const auto &pair : std::as_const(data)) {
        const QString &key = pair.first;
        const Message &msg = pair.second;

        QStringList parts = key.split(':');
        if (parts.size() != 3) continue;
        int appid = parts[1].toInt();
        QString groupId;
        if(parts[2]!="0") groupId = parts[2];
        //uint64_t seq = parts[2].toULongLong();


        if(!groupId.isEmpty() && chatPage)
        {
            QString name = msg.Gname;
            if(!name.isEmpty()) groupId = name;
        }
        QString botName = getBotName(appid);

        QList<QStandardItem*> rowItems;
        switch (currentTabIndex) {
        case 0: // 事件
            rowItems << new QStandardItem(msg.timestamp)
                     << new QStandardItem(botName)
                     << new QStandardItem(groupId)
                     << new QStandardItem(msg.name.isEmpty() ? msg.user : msg.name)
                     << new QStandardItem(msg.msg);
            break;
        case 1:
        case 2: {
            rowItems << new QStandardItem(msg.timestamp)
                     << new QStandardItem(botName)
                     << new QStandardItem(msg.Gname.isEmpty() ? groupId : msg.Gname)
                     << new QStandardItem(msg.name.isEmpty() ? msg.user : msg.name)
                     << new QStandardItem(msg.msg)
                     << new QStandardItem(msg.direction);
            break;
        }
        case 3:
        case 4: {
            rowItems << new QStandardItem(msg.timestamp)
                     << new QStandardItem(botName)
                     << new QStandardItem(msg.name.isEmpty() ? msg.user : msg.name)
                     << new QStandardItem(msg.msg)
                     << new QStandardItem(msg.direction);
            break;
        }
        default: break;
        }
        if(msg.Color_0!=0)
        {
            //QColor rowColor = QColor(msg.Color_0);

            //for (QStandardItem *item : std::as_const(rowItems)) {
            //    item->setBackground(rowColor);
            //}
            QColor textColor = QColor(msg.Color_0); // 如果 Color_0 是文本颜色值
            if (textColor.isValid() && textColor.alpha() > 0) {
                for (QStandardItem *item : std::as_const(rowItems)) {
                    if (item) {
                        item->setForeground(textColor);
                    }
                }
            }
        }
        // 创建 rowItems 后
        if(currentTabIndex>=2 || currentTabIndex==0)
        {

            for (int i=0;i< 4;++i) {
                    rowItems[i]->setTextAlignment(Qt::AlignCenter);

            }
        }else{
            for (int i=0;i< 5;++i) {
                rowItems[i]->setTextAlignment(Qt::AlignCenter);

            }
        }
        // 然后 appendRow
        m_model->appendRow(rowItems);
        int newRow = m_model->rowCount() - 1;   // 获取最新行号
        QModelIndex idx = m_model->index(newRow, 0);
        m_model->setData(idx,key,Qt::UserRole);

    }

    m_offset += data.size();
    m_loading = false;

    // 如果数据很少，但还有更多，可以再自动加载一批（可选）
    if (m_hasMore && m_model->rowCount() < 50) {
        loadMore(limit);
    }
}

void LogPage::onNewLogAdded(const QString &text){

    Message msg;
    QDateTime currentDateTime = QDateTime::currentDateTime();
    msg.timestamp = currentDateTime.toString("yyyy-MM-dd HH:mm:ss");
    msg.msg = text;
    logPage->onNewLogAdded(0,0,0,"",msg);
}


void LogPage::onNewLogAdded(int type, uint64_t seq, int appid, const QString& groupId, const Message& msg)
{
    // 1. 页面可见性 或 类型tab是否对得上
    if (!m_active || currentTabIndex != type) return;
    if(type!=0)
    {
        if (m_currentBotId != 0 && appid != m_currentBotId) return;
    }
    QMutexLocker locker(&m_mutex);
    m_pendingRows.append({type, seq, appid, groupId, msg});
    return;

}
void LogPage::flushPendingRows()
{
    // 1. 交换取出数据，立刻释放锁
    QList<PendingRowData> rows;
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingRows.isEmpty())
            return;
        rows.swap(m_pendingRows);   // 交换后 m_pendingRows 为空
    }
    // 现在 rows 拥有所有待插入数据，锁已释放

    // 2. 构建 QStandardItem 行数据（与原有逻辑相同，但使用 rows）
    int insertCount = rows.size();
    QList<QList<QStandardItem*>> allRows;
    allRows.reserve(insertCount);

    for (const auto& data : rows) {
        QString botName = getBotName(data.appid);
        QList<QStandardItem*> rowItems;

        QString openid = data.groupId;
        if (!openid.isEmpty()) {
            QString name = data.msg.Gname;
            if (!name.isEmpty()) openid = name;
        }

        // 根据 type 构建列（注意：使用 data.type 即当时 tab 索引）
        switch (data.type) {
        case 1: // 群聊
        case 2: // 频道
            rowItems << new QStandardItem(data.msg.timestamp)
                     << new QStandardItem(botName)
                     << new QStandardItem(openid)
                     << new QStandardItem(data.msg.name.isEmpty() ? data.msg.user : data.msg.name)
                     << new QStandardItem(data.msg.msg)
                     << new QStandardItem(data.msg.direction);
            break;
        case 3: // 私聊
        case 4: // 频道私聊
            rowItems << new QStandardItem(data.msg.timestamp)
                     << new QStandardItem(botName)
                     << new QStandardItem(data.msg.name.isEmpty() ? data.msg.user : data.msg.name)
                     << new QStandardItem(data.msg.msg)
                     << new QStandardItem(data.msg.direction);
            break;
        case 0:
            rowItems << new QStandardItem(data.msg.timestamp)
                     << new QStandardItem(botName)
                     << new QStandardItem(openid)
                     << new QStandardItem(data.msg.name.isEmpty() ? data.msg.user : data.msg.name)
                     << new QStandardItem(data.msg.msg);
            break;
        default:
            continue;
        }

        // 颜色设置
        if (data.msg.Color_0 != 0) {
            QColor textColor = QColor(data.msg.Color_0);
            if (textColor.isValid() && textColor.alpha() > 0) {
                for (QStandardItem *item : std::as_const(rowItems)) {
                    if (item) item->setForeground(textColor);
                }
            }
        }

        // 对齐设置
        if (data.type >= 2 || data.type == 0) {
            for (int i = 0; i < 4 && i < rowItems.size(); ++i)
                rowItems[i]->setTextAlignment(Qt::AlignCenter);
        } else {
            for (int i = 0; i < 5 && i < rowItems.size(); ++i)
                rowItems[i]->setTextAlignment(Qt::AlignCenter);
        }

        allRows.append(rowItems);
    }

    // 3. 批量追加到 model（主线程执行）
    for (auto& row : allRows) {
        m_model->appendRow(row);
    }

    // 4. 行数控制（超过10500删除最早的三分之一）
    int rowCount = m_model->rowCount();
    if (rowCount > 10500) {
        int removeCount = rowCount / 3;
        if (removeCount > 0) {
            m_model->removeRows(0, removeCount);
        }
    }

    // 5. 设置每行的 UserRole（key, seq）
    int currentRowCount = m_model->rowCount();
    int startRow = currentRowCount - insertCount; // 新插入的行位于末尾
    for (int i = 0; i < insertCount; ++i) {
        const auto& data = rows.at(i);
        int row = startRow + i;
        QModelIndex idx = m_model->index(row, 0);
        QString key = g_logdb[0]->makeKey(QString::number(data.appid), data.groupId, data.seq);
        m_model->setData(idx, key, Qt::UserRole);
        m_model->setData(idx, static_cast<qulonglong>(data.seq), Qt::UserRole + 1);
    }

    // 6. 更新 m_offset
    m_offset += insertCount;

    // 7. 滚动到底部（如果当前在底部）
    QTableView* view = currentListView();
    if (view) {
        QScrollBar* vScroll = view->verticalScrollBar();
        if (vScroll) {
            int maxVal = vScroll->maximum();
            int curVal = vScroll->value();
            if (curVal >= maxVal - 5) {
                view->scrollToBottom();
            }
        }
    }
}

QTableView* LogPage::currentListView()
{
    switch (currentTabIndex) {
    case 0: return eventListView;
    case 1: return groupListView;
    case 2: return channelListView;
    case 3: return privateListView;
    case 4: return channelPrivateListView;
    default: return nullptr;
    }
}

void LogPage::findRowBySeq(int type, int appid, uint64_t targetSeq, const QString &direction)
{

    if (!m_active || currentTabIndex != type) return;
    if (m_currentBotId != 0 && appid != m_currentBotId) return;

    // 第一步：加锁查 pending 队列
    {
        QMutexLocker locker(&m_mutex);
        for (auto &data : m_pendingRows) {
            if (data.seq == targetSeq) {
                data.msg.direction = direction;   // 直接修改，刷新时会使用新值
                return;
            }
        }
    }


    QMetaObject::invokeMethod(this, [=]() {
        for (int row = m_model->rowCount() - 1; row >= 0; --row) {
            QModelIndex idx = m_model->index(row, 0);
            uint64_t seq = m_model->data(idx, Qt::UserRole + 1).toULongLong();
            if (seq == targetSeq) {
                int col = m_model->columnCount() - 1; // 最后一列为 direction
                QStandardItem *item = m_model->item(row, col);
                if (item) item->setText(direction);
                return;
            }
        }

    });
}

void LogPage::setTableHeaders()
{
    if(!m_model) return;
    QStringList headers;
    switch (currentTabIndex) {
    case 0: headers << "时间" << "机器人" << "群/目标 ID" << "发送人 ID" << "消息内容"; break;
    case 1:
    case 2: headers << "时间" << "机器人" << "群号" << "发送人" << "接收内容" << "回应"; break;
    case 3:
    case 4: headers << "时间" << "机器人" << "发送人" << "接收内容" << "回应"; break;
    default: headers.clear(); return;
    }

    m_model->setHorizontalHeaderLabels(headers);

    // --- 在此处设置列宽 ---
    QTableView* view = currentListView();
    if (!view) return;

    QHeaderView* hHeader = view->horizontalHeader();
    hHeader->setStretchLastSection(false);   // 不要自动拉伸最后一列

    int colCount = m_model->columnCount();
    // 为每一列设置固定宽度，可根据实际需要调整数值
    if (colCount >= 1) view->setColumnWidth(0, 170);
    if (colCount >= 2) view->setColumnWidth(1, 120);

    if (colCount >= 3) view->setColumnWidth(2, 120);
    if (colCount >= 4) view->setColumnWidth(3, 120);
    if(currentTabIndex==1 || currentTabIndex==2)

        hHeader->setSectionResizeMode(5, QHeaderView::Stretch);
    else
        hHeader->setSectionResizeMode(4, QHeaderView::Stretch);
}
int LogPage::getColumnIndex(LogField field) const
{
    switch (currentTabIndex) {
    case 0: // 事件
        switch (field) {
        case Field_Time: return 0;
        case Field_BotName: return 1;
        case Field_TargetId: return 2;
        case Field_Sender: return 3;
        case Field_Content: return 4;
        default: return -1;
        }
    case 1: // 群聊
    case 2: // 频道
        switch (field) {
        case Field_Time: return 0;
        case Field_BotName: return 1;
        case Field_TargetId: return 2;
        case Field_Sender: return 3;
        case Field_Content: return 4;
        case Field_Direction: return 5;
        default: return -1;
        }
    case 3: // 私聊
    case 4: // 频道私聊
        switch (field) {
        case Field_Time: return 0;
        case Field_BotName: return 1;
        case Field_Sender: return 2;
        case Field_Content: return 3;
        case Field_Direction: return 4;
        default: return -1;
        }
    default:
        return -1;
    }
}

QString LogPage::getFieldText(int row, LogField field) const
{
    int col = getColumnIndex(field);
    if (col < 0 || row < 0 || row >= m_model->rowCount()) return QString();
    QModelIndex idx = m_model->index(row, col);
    return m_model->data(idx, Qt::DisplayRole).toString();
}


// ---------- UI 初始化 ----------
void LogPage::setupUi()
{
    // 创建模型（所有表格共用同一个模型，切换时更换表头和数据）
    m_model = new QStandardItemModel(this);

    // ---------- 标签按钮 ----------
    auto makeTabBtn = [&](const QString &text) {
        QPushButton *btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setObjectName("logTabBtn");
        return btn;
    };
    btnEventTab = makeTabBtn("事件");
    btnGroupTab = makeTabBtn("群聊");
    btnPrivateTab = makeTabBtn("频道");
    btnChannelTab = makeTabBtn("私聊");
    btnChannelPrivateTab = makeTabBtn("频道私聊");
    qbload = makeTabBtn("加载这些数量");
    chbox = new QCheckBox("显示完整json");
    btnEventTab->setChecked(true);
    logs = new QLineEdit();
    logs->setText("10000");
    // ---------- 主布局 ----------
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(0);

    QFrame *tablePanel = new QFrame;
    tablePanel->setObjectName("logTablePanel");
    QVBoxLayout *panelLayout = new QVBoxLayout(tablePanel);
    panelLayout->setContentsMargins(4, 4, 4, 4);
    panelLayout->setSpacing(4);

    // 标签栏
    QHBoxLayout *tabLayout = new QHBoxLayout;
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(4);
    tabLayout->addWidget(btnEventTab);
    tabLayout->addWidget(btnGroupTab);
    tabLayout->addWidget(btnPrivateTab);
    tabLayout->addWidget(btnChannelTab);
    tabLayout->addWidget(btnChannelPrivateTab);
    tabLayout->addStretch();
    tabLayout->addWidget(chbox);
    tabLayout->addWidget(logs);
    tabLayout->addWidget(qbload);
    panelLayout->addLayout(tabLayout);

    // ---------- 堆叠窗口 ----------
    tabStack = new QStackedWidget;
    tabStack->setObjectName("logStack");

    // ---------- 创建5个表格视图（懒加载，共用模型） ----------
    auto createLogView = [&](QTableView *&view, int tabIdx) {
        view = new QTableView;
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setFocusPolicy(Qt::NoFocus);
        view->setShowGrid(true);
        view->setGridStyle(Qt::SolidLine);
        //view->setAlternatingRowColors(true);
        view->setWordWrap(false);
        view->setObjectName("logTableView");
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        view->verticalHeader()->hide();
        view->horizontalHeader()->setStretchLastSection(false);
        view->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
        view->verticalHeader()->setDefaultSectionSize(28);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        view->horizontalHeader()->setStretchLastSection(false);
        // 为每列设置固定宽度
        //view->setColumnWidth(0, 180);
        //view->setColumnWidth(1, 150);


        // 设置模型
        view->setModel(m_model);

        // 双击事件
        connect(view, &QTableView::doubleClicked,
                this, [this, view](const QModelIndex &index) {
                    if (!index.isValid()) return;
                    int row = index.row();

                    QString content = getFieldText(row, Field_Content);
                    QString direction = getFieldText(row, Field_Direction);

                    QString text = content + "\n\n-----------------------------------\n\n" + direction;
                    QMessageBox::information(this, "消息内容", text);
                });

        // 右键菜单（简化版，因为黑名单等需要额外数据，只保留复制和查看）
        connect(view, &QTableView::customContextMenuRequested, this, [this, view](const QPoint &pos) {
            QModelIndex index = view->indexAt(pos);
            if (!index.isValid()) return;

            QMenu menu(view);
            QAction *copyContent = menu.addAction("复制消息内容");
            QAction *copyTargetId = menu.addAction("复制群id");
            QAction *copySenderId = menu.addAction("复制发送人id");
            QAction *copyRow = menu.addAction("复制整行内容");

            menu.addSeparator();
            QAction *setbotadmin = menu.addAction("添加为机器人管理");
            QAction *delbotadmin = menu.addAction("从管理员列表删除");
            QAction *ch = menu.addAction("撤回本条");
            QAction *chbr = menu.addAction("撤回别人");
            QAction *viewContent = menu.addAction("查看消息内容");

            QAction *selected = menu.exec(view->viewport()->mapToGlobal(pos));
            if (!selected) return;

            if (selected == delbotadmin) {
                QModelIndex idx = m_model->index(index.row(), 0);

                QString key = m_model->data(idx, Qt::UserRole).toString();
                QStringList list = key.split(":");
                int appid = list[1].toInt();
                int index = accinfo(appid);
                if(index!=-1)
                {
                    if(currentTabIndex==0)
                    {
                        QMessageBox::warning(this,"删除失败","事件日志不能删除 管理员");
                        return;
                    }
                    Message msg;
                    g_logdb[currentTabIndex]->readLog(key,msg);


                    QString &admin = m_accounts[index]->admin;
                    if(admin.contains(msg.user))
                    {
                        admin.remove(msg.user);
                        admin.replace("  "," ");
                        admin.replace("  "," ");
                        admin.replace("  "," ");
                        accountPage->saveAccounts(m_accounts[index].get());
                        QMessageBox::warning(this,"删除成功",QString("删除成功 %1 于 %2 的管理员").arg(msg.user,m_accounts[index]->nickname));
                        return;
                    }
                    QMessageBox::warning(this,"删除失败","不存在管理员 无法删除");
                    return ;
                }
            }
            if (selected == setbotadmin) {
                QModelIndex idx = m_model->index(index.row(), 0);

                QString key = m_model->data(idx, Qt::UserRole).toString();
                QStringList list = key.split(":");
                int appid = list[1].toInt();
                int index = accinfo(appid);
                if(index!=-1)
                {
                    if(currentTabIndex==0)
                    {
                        QMessageBox::warning(this,"添加失败","事件日志不能添加 管理员");
                        return;
                    }
                    Message msg;
                    g_logdb[currentTabIndex]->readLog(key,msg);


                    QString &admin = m_accounts[index]->admin;
                    if(admin.contains(msg.user))
                    {
                        QMessageBox::warning(this,"已经存在","已经存在 不需要重复添加");
                        return;
                    }
                    admin.append(" ");
                    admin.append(msg.user);
                    admin.replace("  "," ");
                    admin.replace("  "," ");
                    admin.replace("  "," ");
                    accountPage->saveAccounts(m_accounts[index].get());
                    QMessageBox::warning(this,"添加成功",QString("已经添加 %1 为 %2 管理员").arg(msg.user,m_accounts[index]->nickname));
                    return ;
                }
            }
            if (selected == copyContent) {
                QString content = getFieldText(index.row(), Field_Content);
                content+="\n-----------------------\n"+getFieldText(index.row(), Field_Direction);

                QApplication::clipboard()->setText(content);
            } else if (selected == copyTargetId) {
                QModelIndex idx = m_model->index(index.row(), 0);
                QString key = m_model->data(idx, Qt::UserRole).toString();


                QApplication::clipboard()->setText(key);
            } else if (selected == copySenderId) {
                QModelIndex idx = m_model->index(index.row(), 0);

                QString key = m_model->data(idx, Qt::UserRole).toString();
                Message msg;
                g_logdb[currentTabIndex]->readLog(key,msg);
                QApplication::clipboard()->setText(msg.user);

            } else if (selected == copyRow) {
                QStringList parts;
                for (int col = 0; col < m_model->columnCount(); ++col) {
                    QModelIndex idx = m_model->index(index.row(), col);
                    parts << m_model->data(idx, Qt::DisplayRole).toString();
                }
                QApplication::clipboard()->setText(parts.join(" | "));
            } else if (selected == viewContent) {
                QString content = getFieldText(index.row(), Field_Content);
                QString direction = getFieldText(index.row(), Field_Direction);
                QString text = content + "\n\n-----------------------------------\n\n" + direction;
                QMessageBox::information(this, "消息内容", text);
            }else if(ch == selected)
            {
                QModelIndex idx = m_model->index(index.row(), 0);

                QString key = m_model->data(idx, Qt::UserRole).toString();

                Message msg;
                g_logdb[currentTabIndex]->readLog(key,msg);
                if(msg.ch.isEmpty())
                {
                    QMessageBox::warning(this,"","撤回失败 msgid为空 不能撤回");
                    return ;
                }
                QStringList list = key.split(":");
                int appid = list[1].toInt();
                if(m_botClients.contains(appid))
                {
                    QString res = m_botClients[appid]->delete_messages(currentTabIndex-1,list[2],msg.plugin_ch);
                    if(res.contains("message")) {
                        QMessageBox::warning(this,"撤回失败",res);
                        return;
                    }
                    msg.plugin_ch="[已撤回]";
                    msg.direction = "[已撤回]\n"+msg.direction;
                    msg.Color_0 = 0xAB56F6;
                    g_logdb[currentTabIndex]->updateLog(key,msg);
                    return ;
                }
                QMessageBox::warning(this,"","撤回失败 指定appid:"+QString::number(appid)+" 不在线");
                return ;
            }else if(chbr == selected)
            {
                QModelIndex idx = m_model->index(index.row(), 0);

                QString key = m_model->data(idx, Qt::UserRole).toString();

                Message msg;
                g_logdb[currentTabIndex]->readLog(key,msg);
                if(msg.ch.isEmpty())
                {
                    QMessageBox::warning(this,"","撤回失败 msgid为空 不能撤回");
                    return ;
                }
                QStringList list = key.split(":");
                int appid = list[1].toInt();
                if(m_botClients.contains(appid))
                {
                    QString res = m_botClients[appid]->delete_messages(currentTabIndex-1,list[2],msg.ch);
                    qDebug()<< res<< "||||" << msg.ch;
                    if(res.contains("message")) {
                        QMessageBox::warning(this,"撤回失败",res);
                        return;
                    }
                    msg.ch="[已撤回]";
                    msg.msg = "[已撤回]\n"+msg.msg;
                    msg.Color_0 = 0x20A362;
                    g_logdb[currentTabIndex]->updateLog(key,msg);
                    return ;
                }
                QMessageBox::warning(this,"","撤回失败 指定appid:"+QString::number(appid)+" 不在线");
                return ;
            }
        });

        // 滚动加载更多
        /*
        connect(view->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this, view](int value) {
                    QScrollBar *bar = view->verticalScrollBar();
                    if (bar->value() == bar->minimum() && !m_loading && m_hasMore) {
                        loadMore();
                    }
                });
        */
        tabStack->addWidget(view);
    };

    // 创建各个标签页的视图
    createLogView(eventListView, 0);
    createLogView(groupListView, 1);
    createLogView(channelListView, 2);
    createLogView(privateListView, 3);
    createLogView(channelPrivateListView, 4);



    panelLayout->addWidget(tabStack, 1);
    mainLayout->addWidget(tablePanel, 1);

    // ---------- 按钮信号 ----------

    connect(chbox, &QCheckBox::clicked, this, [this]{
        wanzjson = chbox->isChecked();
    });
    connect(btnEventTab, &QPushButton::clicked, this, [this]{ switchTab(0); });
    connect(btnGroupTab, &QPushButton::clicked, this, [this]{ switchTab(1); });
    connect(btnPrivateTab, &QPushButton::clicked, this, [this]{ switchTab(2); });
    connect(btnChannelTab, &QPushButton::clicked, this, [this]{ switchTab(3); });
    connect(btnChannelPrivateTab, &QPushButton::clicked, this, [this]{ switchTab(4); });

    connect(qbload, &QPushButton::clicked, this, [this]{ switchTabEx(currentTabIndex,logs->text().toInt()); });

    // 默认选中第一个
    switchTab(0);
}


void LogPage::applyStyleSheet()
{
    setObjectName("logPage");
    setStyleSheet(R"(
        QWidget#logPage {
            background: #FFF8EF;
        }
        QLabel#logPageTitle {
            color: #17202A;
            font-size: 24px;
            font-weight: 800;
            background: transparent;
        }
        QLabel#logPageSubTitle {
            color: #8A94A6;
            font-size: 12px;
            background: transparent;
        }
        QFrame#logTablePanel {
            background: #FFFFFF;
            border: 1px solid #F2E8DE;
            border-radius: 10px;
        }
        QPushButton#logTabBtn {
            background: transparent;
            border: none;
            border-radius: 10px;
            padding: 6px 14px;
            font-size: 12px;
            font-weight: 700;
            color: #8A94A6;
        }
        QPushButton#logTabBtn:checked {
            background: #FFF0DE;
            color: #FF7F32;
        }
        QPushButton#logTabBtn:hover {
            background: #FFF7EA;
            color: #FF914D;
        }
        QPushButton#debugLogBtn {
            background: #FFF0DE;
            color: #FF7F32;
            border: none;
            border-radius: 10px;
            padding: 6px 14px;
            font-size: 12px;
            font-weight: 800;
        }
        QPushButton#debugLogBtn:hover {
            background: #FFE5C8;
        }
        QStackedWidget#logStack {
            background: transparent;
            border: none;
        }
        QTableView#logTableView {
            background: #FFFFFF;
            alternate-background-color: #FFFCF8;
            border: 1px solid #F2E8DE;
            border-radius: 10px;
            gridline-color: #F1E3D5;
            font-size: 12px;
            outline: none;
            color: #263241;
            font-family: "Cascadia Mono", "Consolas", "Microsoft YaHei";
        }
        QTableView#logTableView::item:selected {
            background-color: #969AF6;
            color: #000;
        }
        QTableView#logTableView::item {
            padding: 6px 8px;
            border: none;
        }
        QHeaderView::section {
            background: #FFF9F2;
            color: #8A94A6;
            border: none;
            border-right: 1px solid #F1E3D5;
            border-bottom: 1px solid #F1E3D5;
            padding: 9px 8px;
            font-size: 12px;
            font-weight: 800;
        }
        QLabel#logCountLabel {
            font-size: 12px;
            color: #8A94A6;
            background: transparent;
        }
        QPushButton#clearBtn {
            background-color: #FF914D;
            color: white;
            border: none;
            border-radius: 10px;
            padding: 7px 18px;
            font-size: 12px;
            font-weight: 800;
        }
        QPushButton#clearBtn:hover {
            background-color: #FF7F32;
        }
    )");
}