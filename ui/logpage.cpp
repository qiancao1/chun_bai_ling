#include "LogPage.h"
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
#include "global.h"


RingBuffer<LogEntry> m_logStore[5];
int Color_0=0;
int Color_1=0;

LogListModel::LogListModel(const QList<LogColumn> &columns, RingBuffer<LogEntry> *buffer, QObject *parent)
    : QAbstractTableModel(parent), m_columns(columns), m_buffer(buffer)
{
    m_syncTimer = new QTimer(this);
    connect(m_syncTimer, &QTimer::timeout, this, &LogListModel::onSyncTimer);
    rebuildAll();
}

LogListModel::~LogListModel()
{
    stopAutoSync();
}

void LogListModel::setFilter(int botId, bool enable)
{
    m_filterBotId = botId;
    m_filterEnabled = enable;
    m_needFullRebuild = true;
    // 下次定时器触发时会全量重建
    if (m_syncTimer->isActive())
        onSyncTimer(); // 立即触发
    else
        rebuildAll();
}

void LogListModel::startAutoSync(int intervalMs)
{
    m_syncTimer->start(intervalMs);
}

void LogListModel::stopAutoSync()
{
    m_syncTimer->stop();
}

void LogListModel::onSyncTimer()
{
    if (m_needFullRebuild) {
        rebuildAll();
        m_needFullRebuild = false;
    } else {
        syncWithBuffer();
    }
}

void LogListModel::rebuildAll()
{
    int head = m_buffer->totalWritten();
    if (head == 0) {
        beginResetModel();
        m_logicalIndices.clear();
        m_lastHead = 0;
        m_lastFirstLogical = 0;
        endResetModel();
        return;
    }

    int capacity = m_buffer->capacity();
    int firstLogical = (head > capacity) ? (head - capacity) : 0;
    int totalAvailable = head - firstLogical;
    const int MAX_DISPLAY = 500;  // 最大显示条数，可配置
    int displayCount = qMin(totalAvailable, MAX_DISPLAY);
    int startLogical = head - displayCount;

    QVector<int> indices;
    indices.reserve(displayCount);
    for (int logical = startLogical; logical < head; ++logical) {
        const LogEntry &entry = m_buffer->at(logical % capacity);
        if (!m_filterEnabled || matchesFilter(entry)) {
            indices.append(logical);
        }
    }

    beginResetModel();
    m_logicalIndices.swap(indices);
    m_lastHead = head;
    m_lastFirstLogical = firstLogical;
    endResetModel();
}

void LogListModel::syncWithBuffer()
{
    int head = m_buffer->totalWritten();
    int capacity = m_buffer->capacity();
    if(capacity==0) return;
    int firstLogical = (head > capacity) ? (head - capacity) : 0;


    if (head == m_lastHead && firstLogical == m_lastFirstLogical)
        return;

    if (firstLogical > m_lastFirstLogical) {
        auto it = std::lower_bound(m_logicalIndices.begin(), m_logicalIndices.end(), firstLogical);
        int removePos = it - m_logicalIndices.begin();
        if (removePos > 0) {
            beginRemoveRows(QModelIndex(), 0, removePos - 1);
            m_logicalIndices.erase(m_logicalIndices.begin(), it);
            endRemoveRows();
        }
    }


    if (head > m_lastHead) {
        int newCount = head - m_lastHead;

        const int MAX_DISPLAY = 500;
        QVector<int> newIndices;
        newIndices.reserve(newCount);
        for (int logical = m_lastHead; logical < head; ++logical) {
            const LogEntry &entry = m_buffer->at(logical % capacity);
            if (!m_filterEnabled || matchesFilter(entry)) {
                newIndices.append(logical);
            }
        }
        if (!newIndices.isEmpty()) {
            int startRow = m_logicalIndices.size();
            beginInsertRows(QModelIndex(), startRow, startRow + newIndices.size() - 1);
            m_logicalIndices.append(newIndices);
            endInsertRows();
        }
        // 如果总行数超过 MAX_DISPLAY，需要移除最旧的行
        if (m_logicalIndices.size() > MAX_DISPLAY) {
            int removeCount = m_logicalIndices.size() - MAX_DISPLAY;
            beginRemoveRows(QModelIndex(), 0, removeCount - 1);
            m_logicalIndices.erase(m_logicalIndices.begin(), m_logicalIndices.begin() + removeCount);
            endRemoveRows();
        }
    }

    // 更新记录的状态
    m_lastHead = head;
    m_lastFirstLogical = firstLogical;
}
void LogListModel::refreshNow()
{

    rebuildAll();
}
bool LogListModel::matchesFilter(const LogEntry &entry) const
{
    if (!m_filterEnabled) return true;
    if (m_filterBotId==0) return true;
    return entry.appid == m_filterBotId;
}

int LogListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_logicalIndices.size();
}

int LogListModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_columns.size();
}

QVariant LogListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    if (section < 0 || section >= m_columns.size())
        return QVariant();
    return m_columns.at(section).title;
}

QVariant LogListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_logicalIndices.size())
        return QVariant();

    int logical = m_logicalIndices.at(index.row());
    const LogEntry &entry = m_buffer->at(logical % m_buffer->capacity());
    int col = index.column();
    if (col < 0 || col >= m_columns.size())
        return QVariant();

    LogField field = m_columns.at(col).field;

    if (role == Qt::DisplayRole) {
        switch (field) {
        case Field_Time:     return entry.time;
        case Field_BotName:  return entry.botName.isEmpty() ? "-" : entry.botName;
        case Field_TargetId: return entry.groupId;
        case Field_SenderId: return entry.user_name.isEmpty() ? (entry.user.isEmpty() ? "-" : entry.user) : entry.user_name;
        case Field_Content:  return entry.msg;
        case Field_Direction:return entry.direction;
        default: return QVariant();
        }
    }
    if (role == Qt::ForegroundRole) {
        return QColor(entry.color);
    }
    if (role == Qt::TextAlignmentRole) {
        if (field == Field_Content || field == Field_Direction) {
            return int(Qt::AlignLeft | Qt::AlignVCenter);
        }
        return int(Qt::AlignCenter);
    }
    if (role == Qt::ToolTipRole && field == Field_Content) {
        return entry.msg;
    }
    return QVariant();
}

LogEntry LogListModel::entryAt(int row) const
{
    if (row < 0 || row >= m_logicalIndices.size())
        return LogEntry();
    int logical = m_logicalIndices.at(row);
    return m_buffer->at(logical % m_buffer->capacity());
}

// ---------- LogPage 实现 ----------
LogPage::LogPage(QWidget *parent) : QWidget(parent)
{
    int configCapacity = g_config.value("logs").toInt(100000);  // 不存在时默认 100000
    if(configCapacity<1000 && configCapacity>0)
        configCapacity=1000;
    if(configCapacity>0)
    {
        for (int i = 0; i < 5; ++i) {
            m_logStore[i].setCapacity(configCapacity);
        }
    }

    setupUi();
    applyStyleSheet();   // 你的样式设置函数
}

LogPage::~LogPage() {}

void LogPage::switchTab(int index)
{
    currentTabIndex = index;
    tabStack->setCurrentIndex(index);

    QList<QPushButton*> btns = {btnEventTab, btnGroupTab, btnPrivateTab, btnChannelTab, btnChannelPrivateTab};
    for (int i = 0; i < btns.size(); ++i) {
        btns[i]->setChecked(i == index);
    }

    // 更新当前模型的过滤条件
    LogListModel *model = currentModel();
    if (model) {
        bool needFilter = (currentTabIndex != 0 && m_currentBotId!=0);
        model->setFilter(m_currentBotId, needFilter);
    }
}

void LogPage::setCurrentBot(int botId, const QString &botName)
{
    m_currentBotId = botId;
    m_currentBotName = botName;
    LogListModel *model = currentModel();
    if (model && m_active) {
        bool needFilter = (currentTabIndex != 0 && m_currentBotId!=0);
        model->setFilter(m_currentBotId, needFilter);
    }
    //chatPage->btnsetChecked();
}


void LogPage::setActive(bool active)
{
    m_active = active;
}

bool LogPage::entryMatchesCurrentBot(const LogEntry &entry) const
{
    return m_currentBotId==0 || entry.appid==0 || entry.appid == m_currentBotId;
}

QString LogPage::currentBotLabel() const
{
    if (m_currentBotId==0) return "全部机器人";
    if (!m_currentBotName.isEmpty()) return m_currentBotName;
    return QString::number(m_currentBotId);
}

void LogPage::updateCountDisplay()
{
    LogListModel *model = currentModel();
    if (model) {
        logCountLabel->setText(QString("当前机器人: %1  日志条数: %2")
                                   .arg(currentBotLabel())
                                   .arg(model->count()));
    }
}

// ---------- UI 初始化（基本保持原样，只删除滚动加载绑定）----------
void LogPage::setupUi()
{
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
    btnEventTab->setChecked(true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(0);

    QFrame *tablePanel = new QFrame;
    tablePanel->setObjectName("logTablePanel");
    QVBoxLayout *panelLayout = new QVBoxLayout(tablePanel);
    panelLayout->setContentsMargins(4, 4, 4, 4);
    panelLayout->setSpacing(4);

    QHBoxLayout *tabLayout = new QHBoxLayout;
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(4);
    tabLayout->addWidget(btnEventTab);
    tabLayout->addWidget(btnGroupTab);
    tabLayout->addWidget(btnPrivateTab);
    tabLayout->addWidget(btnChannelTab);
    tabLayout->addWidget(btnChannelPrivateTab);
    tabLayout->addStretch();
    panelLayout->addLayout(tabLayout);

    tabStack = new QStackedWidget;
    tabStack->setObjectName("logStack");

    auto createLogView = [&](QTableView *&view, LogListModel *&model, int tabIdx) {
        QList<LogColumn> columns;
        switch (tabIdx) {
        case 0:
            columns = {
                {Field_Time,     "时间",        100},
                {Field_BotName,  "机器人",      110},
                {Field_TargetId, "群/目标 ID",  148},
                {Field_SenderId, "发送人 ID",   140},
                {Field_Content,  "消息内容",    300}
            };
            break;
        case 1:
        case 2:
            columns = {
                {Field_Time,     "时间",        100},
                {Field_BotName,  "机器人",      110},
                {Field_TargetId, "群号",        140},
                {Field_SenderId, "发送人",      140},
                {Field_Content,  "接收内容",    150},
                {Field_Direction,"回应",        300}
            };
            break;
        case 3:
        case 4:
            columns = {
                {Field_Time,     "时间",        100},
                {Field_BotName,  "机器人",      110},
                {Field_SenderId, "发送人",      140},
                {Field_Content,  "接收内容",    150},
                {Field_Direction,"回应",        300}
            };
            break;
        default: break;
        }

        model = new LogListModel(columns, &m_logStore[tabIdx], this);
        model->startAutoSync(500);   // 每500ms检查一次环形缓冲区变化，增量更新

        view = new QTableView;
        view->setModel(model);


        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setFocusPolicy(Qt::NoFocus);
        view->setShowGrid(true);
        view->setGridStyle(Qt::SolidLine);
        view->setAlternatingRowColors(true);
        view->setWordWrap(false);
        view->setObjectName("logTableView");
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        view->verticalHeader()->hide();
        view->horizontalHeader()->setStretchLastSection(false);
        view->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);

        for (int i = 0; i < columns.size(); ++i)
            view->setColumnWidth(i, columns.at(i).defaultWidth);
        if (columns.size() > 0)
            view->horizontalHeader()->setStretchLastSection(true);

        view->verticalHeader()->setDefaultSectionSize(28);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QTableView::doubleClicked,
                this, [this, view, model](const QModelIndex &index) {
                    if (!index.isValid())
                        return;
                    const LogEntry entry = model->entryAt(index.row());
                    QString text = entry.msg + "\n\n-----------------------------------\n\n" + entry.direction;
                    QMessageBox::information(this, "消息内容", text);
                });
        // 右键菜单（代码与原有一致，略作简化）
        connect(view, &QTableView::customContextMenuRequested, this, [this, view, model](const QPoint &pos) {
            QModelIndex index = view->indexAt(pos);
            if (!index.isValid()) return;
            const LogEntry entry = model->entryAt(index.row());
            QMenu menu(view);
            QAction *qlrhmd = menu.addAction("将群拉入黑名单");
            QAction *qychmd = menu.addAction("将群移出黑名单");
            QAction *lrhmd = menu.addAction("将用户拉入黑名单");
            QAction *ychmd = menu.addAction("将用户移出黑名单");
            menu.addSeparator();
            QAction *copyContent = menu.addAction("复制消息内容");
            QAction *copyTargetId = menu.addAction("复制群id");
            QAction *copySenderId = menu.addAction("复制发送人id");
            QAction *copyRow = menu.addAction("复制整行内容");
            menu.addSeparator();
            QAction *zdhh = menu.addAction("转到会话");
            QAction *viewContent = menu.addAction("查看消息内容");
            QAction *selected = menu.exec(view->viewport()->mapToGlobal(pos));
            if (!selected) return;
            if (selected == copyContent) {
                QApplication::clipboard()->setText(entry.msg);
            } else if (selected == copyTargetId) {
                QApplication::clipboard()->setText(entry.groupId);
            } else if (selected == copySenderId) {
                QApplication::clipboard()->setText(entry.user);
            } else if (selected == copyRow) {
                QStringList parts;
                for (int col = 0; col < model->columnCount(); ++col) {
                    QModelIndex idx = model->index(index.row(), col);
                    parts << model->data(idx, Qt::DisplayRole).toString();
                }
                QApplication::clipboard()->setText(parts.join(" | "));
            } else if (selected == viewContent) {
                QString text = entry.msg +"\n\n-----------------------------------\n\n"+entry.direction;
                QMessageBox::information(this, "消息内容", text);
            }else if (selected == qlrhmd) {
                bool ok;
                QString multiLineText = QInputDialog::getMultiLineText(
                    this,                         // 父窗口
                    "输入备注",                    // 对话框标题
                    "拉入黑名单备注",
                    "默认内容\n第二行",&ok);
                if(!ok) return;
                QDateTime now = QDateTime::currentDateTime();
                QString dateTimeText = now.toString("yyyy-MM-dd hh:mm:ss")+"\n"+multiLineText;

                m_blacklist.insert(entry.groupId,multiLineText);
                Black->saveToFile();
            }
            else if (selected == qychmd) {
                m_blacklist.remove(entry.groupId);
                Black->saveToFile();
            }
            else if (selected == lrhmd) {
                bool ok;
                QString multiLineText = QInputDialog::getMultiLineText(this,"输入备注","拉入黑名单备注",
                    "无备注\n无备注",&ok);
                if(!ok) return;
                QDateTime now = QDateTime::currentDateTime();
                QString dateTimeText = now.toString("yyyy-MM-dd hh:mm:ss")+"\n"+multiLineText;
                m_blacklist.insert(entry.user,dateTimeText);
                Black->saveToFile();

            }
            else if (selected == ychmd) {
                m_blacklist.remove(entry.user);
                Black->saveToFile();

            }else if (selected == zdhh) {
                if(currentTabIndex==0) return;
                stackedWidget->setCurrentIndex(4);
                if(currentTabIndex==1)
                    chatPage->onGroupChatClicked();
                else if(currentTabIndex==2)
                    chatPage->onChannelChatClicked();
                else if(currentTabIndex==3)
                    chatPage->onPrivateChatClicked();
                else if(currentTabIndex==4)
                    chatPage->onChannelPrivateClicked();
                chatPage->onContactItemClicked2(entry.appid,entry.groupId,currentTabIndex-1);
            }
        });

        tabStack->addWidget(view);
    };

    createLogView(eventListView, eventModel, 0);
    createLogView(groupListView, groupModel, 1);
    createLogView(channelListView, channelModel, 2);
    createLogView(privateListView, privateModel, 3);
    createLogView(channelPrivateListView, channelPrivateModel, 4);

    // 按钮信号
    connect(btnEventTab, &QPushButton::clicked, this, [this]{ switchTab(0); });
    connect(btnGroupTab, &QPushButton::clicked, this, [this]{ switchTab(1); });
    connect(btnPrivateTab, &QPushButton::clicked, this, [this]{ switchTab(2); });
    connect(btnChannelTab, &QPushButton::clicked, this, [this]{ switchTab(3); });
    connect(btnChannelPrivateTab, &QPushButton::clicked, this, [this]{ switchTab(4); });

    panelLayout->addWidget(tabStack, 1);

    logCountLabel = new QLabel("日志条数: 0");
    logCountLabel->setObjectName("logCountLabel");

    QHBoxLayout *bottomLayout = new QHBoxLayout;
    bottomLayout->addWidget(logCountLabel);
    bottomLayout->addStretch();
    panelLayout->addLayout(bottomLayout);

    mainLayout->addWidget(tablePanel, 1);
    switchTab(0);  // 初始显示全部tab
}

LogListModel* LogPage::currentModel()
{
    switch (currentTabIndex) {
    case 0: return eventModel;
    case 1: return groupModel;
    case 2: return channelModel;
    case 3: return privateModel;
    case 4: return channelPrivateModel;
    default: return nullptr;
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
