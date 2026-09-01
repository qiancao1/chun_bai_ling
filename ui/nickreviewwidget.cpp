#include "nickreviewwidget.h"
#include "global.h"          // 假设 g_botdb 在此定义
#include "lmdbkv.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>
#include <QLabel>
#include <QDir>

// ---------- 审核记录结构体 ----------
struct ReviewRecord {
    uint32_t appId;
    uint32_t userSeqId;
    uint32_t timestamp;
    char nickname[64];

    QByteArray serialize() const {
        QByteArray data;
        data.resize(sizeof(ReviewRecord));
        memcpy(data.data(), this, sizeof(ReviewRecord));
        return data;
    }
    static ReviewRecord deserialize(const QByteArray &data) {
        ReviewRecord rec;
        if (data.size() >= sizeof(ReviewRecord))
            memcpy(&rec, data.constData(), sizeof(ReviewRecord));
        else
            memset(&rec, 0, sizeof(ReviewRecord));
        return rec;
    }
};

// ---------- 全局审核数据库单例 ----------
static LmdbKV* g_reviewDb = nullptr;
static LmdbKV* globalReviewDb() {
    if (!g_reviewDb) {
        QString path = "./reviewdb";
        QDir dir;
        if (!dir.mkpath(path)) {
            qCritical() << "无法创建审核数据库目录:" << path;
            return nullptr;
        }
        g_reviewDb = new LmdbKV(path, nullptr);

    }
    return g_reviewDb;
}

// ---------- 工具函数 ----------
static QByteArray makeKey(uint32_t appId, uint32_t userSeqId) {
    QByteArray key;
    key.resize(8);
    memcpy(key.data(), &appId, 4);
    memcpy(key.data() + 4, &userSeqId, 4);
    return key;
}

// ---------- NickReviewWidget 实现 ----------
NickReviewWidget::NickReviewWidget(QWidget *parent)
    : QWidget(parent), m_model(new QStandardItemModel(this))
{

    setupUI();
}

NickReviewWidget::~NickReviewWidget() {
    // 全局数据库不在此释放
}

void NickReviewWidget::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);

    // -------- 顶部：标题 + 模式选择 + 启用开关 --------
    auto *topLayout = new QHBoxLayout;
    QLabel *titleLabel = new QLabel("昵称审核");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold;");

    m_modeCombo = new QComboBox;
    m_modeCombo->addItem("申请审核", (int)Mode::Application);
    m_modeCombo->addItem("批量审核", (int)Mode::BatchUser);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NickReviewWidget::onModeChanged);



    m_aimode = new QComboBox;


    m_enableCheck = new QCheckBox("启用审核");//无用的
    m_enableCheck->setChecked(true);
    connect(m_enableCheck, &QCheckBox::toggled, this, &NickReviewWidget::onEnableToggled);
    强制审核昵称 = g_config["qzsync"].toBool();
    m_qzsy = new QCheckBox("强制插件使用审核后昵称");

    m_qzsy->setChecked(强制审核昵称);
    QPushButton *btnAis = new QPushButton("Ai审核");
    //m_ss = new QLineEdit;
    //m_ss->setPlaceholderText("搜索昵称 #开始搜索ID");
    //QPushButton *btnss = new QPushButton("搜索昵称");



    connect(m_qzsy, &QCheckBox::toggled, [this](){
        强制审核昵称 = m_qzsy->isChecked();
        g_config["qzsync"] = 强制审核昵称;
        saveConfig();
    });
    //connect(btnss, &QPushButton::clicked, this, &NickReviewWidget::onSearch);

    connect(btnAis, &QPushButton::clicked, this, &NickReviewWidget::onAiApprove);
    topLayout->addWidget(titleLabel);
    topLayout->addWidget(m_modeCombo);
    topLayout->addWidget(m_aimode);
    topLayout->addWidget(btnAis);
    //topLayout->addWidget(m_ss);

    //topLayout->addWidget(btnss);
    topLayout->addStretch();
    topLayout->addWidget(m_qzsy);
    mainLayout->addLayout(topLayout);

    // -------- 表格 --------
    m_tableView = new QTableView;
    m_tableView->setModel(m_model);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::MultiSelection);
    mainLayout->addWidget(m_tableView);

    // -------- 按钮栏 --------
    auto *btnLayout = new QHBoxLayout;
    QPushButton *btnApprove = new QPushButton("✅ 通过");
    QPushButton *btnReject = new QPushButton("❌ 拒绝");
    m_btnCancel = new QPushButton("取消通过");
    QPushButton *btnInvert = new QPushButton("反选");
    QPushButton *btnSelectAll = new QPushButton("全选");
    QPushButton *btnSelectNone = new QPushButton("全不选");

    connect(btnApprove, &QPushButton::clicked, this, &NickReviewWidget::onApprove);
    connect(btnReject, &QPushButton::clicked, this, &NickReviewWidget::onReject);
    connect(m_btnCancel, &QPushButton::clicked, this, &NickReviewWidget::onCancel);
    connect(btnInvert, &QPushButton::clicked, this, &NickReviewWidget::onInvert);
    connect(btnSelectAll, &QPushButton::clicked, this, &NickReviewWidget::onSelectAll);
    connect(btnSelectNone, &QPushButton::clicked, this, &NickReviewWidget::onSelectNone);

    btnLayout->addWidget(btnApprove);
    btnLayout->addWidget(btnReject);
    btnLayout->addWidget(m_btnCancel);
    btnLayout->addWidget(btnInvert);
    btnLayout->addWidget(btnSelectAll);
    btnLayout->addWidget(btnSelectNone);

    // --- 批量模式专用按钮 ---
    m_btnLoadPending = new QPushButton("加载待审核");
    m_btnLoadApproved = new QPushButton("加载已审核");
    m_btnPrevPage = new QPushButton("<");
    m_btnNextPage = new QPushButton(">");
    m_pageLabel = new QLabel("第 1 页");

    connect(m_btnLoadPending, &QPushButton::clicked, this, &NickReviewWidget::onLoadPending);
    connect(m_btnLoadApproved, &QPushButton::clicked, this, &NickReviewWidget::onLoadApproved);
    connect(m_btnPrevPage, &QPushButton::clicked, this, &NickReviewWidget::prevPage);
    connect(m_btnNextPage, &QPushButton::clicked, this, &NickReviewWidget::nextPage);

    btnLayout->addWidget(m_btnLoadPending);
    btnLayout->addWidget(m_btnLoadApproved);
    btnLayout->addWidget(m_btnPrevPage);
    btnLayout->addWidget(m_pageLabel);
    btnLayout->addWidget(m_btnNextPage);

    // --- 申请模式专用按钮 ---
    m_btnLoadApp = new QPushButton("加载审核列表");
    connect(m_btnLoadApp, &QPushButton::clicked, this, &NickReviewWidget::refresh);
    btnLayout->addWidget(m_btnLoadApp);

    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // 初始状态
    m_mode = Mode::Application;
    updateButtonsVisibility();
    onEnableToggled(m_enableCheck->isChecked());
}




void NickReviewWidget::updateButtonsVisibility() {
    bool batch = (m_mode == Mode::BatchUser);
    m_btnLoadPending->setVisible(batch);
    m_btnLoadApproved->setVisible(batch);
    m_btnPrevPage->setVisible(batch);
    m_btnNextPage->setVisible(batch);
    m_pageLabel->setVisible(batch);
    m_btnLoadApp->setVisible(!batch);
    m_btnCancel->setVisible(batch);   // 取消通过仅在批量模式可用

    // 批量模式下，根据当前显示的是待审核还是已审核，启用/禁用取消按钮
    if (batch) {
        // 如果当前显示的是已审核（name 非空），取消按钮可用；否则禁用
        m_btnCancel->setEnabled(!m_onlyNameEmpty);
    } else {
        m_btnCancel->setEnabled(false);
    }
}

void NickReviewWidget::setAppId(uint32_t appid) {
    m_appid = appid;
    m_botDb = g_botdb.value(appid, nullptr);
    if (!m_botDb && m_mode == Mode::BatchUser) {
        m_model->removeRows(0, m_model->rowCount());  // 清空所有行
        qWarning() << "NickReviewWidget: 未找到 appid=" << appid << " 对应的 BotDB";
    }
    // 重置分页
    m_currentPage = 0;
    m_onlyNameEmpty = true;
    refresh();
}

void NickReviewWidget::refresh() {
    if (m_mode == Mode::Application)
        loadFromApplicationDB();
    else
        loadFromBotDB();
    updateButtonsVisibility();
}

// ---------- 模式切换 ----------
void NickReviewWidget::onModeChanged(int index) {
    m_mode = static_cast<Mode>(m_modeCombo->itemData(index).toInt());
    // 切换模式时重置分页
    m_currentPage = 0;
    m_onlyNameEmpty = true;
    updateButtonsVisibility();
    if (m_appid != 0)
        refresh();
}

// ---------- 加载申请审核 ----------
void NickReviewWidget::loadFromApplicationDB() {
    m_model->removeRows(0, m_model->rowCount());
    LmdbKV* db = globalReviewDb();
    if (!db) return;

    QStringList headers;
    headers <<  "AppID" << "用户ID" << "新昵称" << "申请时间";
    m_model->setHorizontalHeaderLabels(headers);

    QList<QByteArray> keys = db->getAllKeysByteArray();
    for (const QByteArray &key : std::as_const(keys)) {
        if (key.size() != 8) continue;
        QByteArray value = db->get(key);
        if (value.isEmpty()) continue;

        ReviewRecord rec = ReviewRecord::deserialize(value);
        if (rec.appId != m_appid) continue;

        QList<QStandardItem*> row;
        auto *checkItem = new QStandardItem();
        checkItem->setCheckable(true);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setData(QVariant::fromValue(QPair<uint32_t,uint32_t>(rec.appId, rec.userSeqId)), Qt::UserRole);

        checkItem->setText(QString::number(rec.appId));
         row << checkItem;
        row << new QStandardItem(QString::number(rec.userSeqId));
        row << new QStandardItem(QString::fromUtf8(rec.nickname));
        row << new QStandardItem(QDateTime::fromSecsSinceEpoch(rec.timestamp).toString("yyyy-MM-dd hh:mm:ss"));
        m_model->appendRow(row);
    }
}

// ---------- 加载批量审核 ----------
void NickReviewWidget::loadFromBotDB() {
    if (!m_botDb){
        m_botDb = g_botdb.value(g_appid, nullptr);
        if (!m_botDb) {
            m_model->removeRows(0, 0);
            qWarning() << "NickReviewWidget: 未找到 appid=" << g_appid << " 对应的 BotDB";
            return;
        }

    }
    m_model->removeRows(0, m_model->rowCount());

    QStringList headers;
    headers <<   "用户ID" << "原始昵称(nickname)" << "正式昵称(name)";
    m_model->setHorizontalHeaderLabels(headers);

    int offset = m_currentPage * m_pageSize;
    QList<UserRecord> users;
    int total = 0;
    if (!m_botDb->getUsersByPage(m_onlyNameEmpty, offset, m_pageSize, users, total)) {
        QMessageBox::warning(this, "错误", "加载用户数据失败");
        return;
    }
    m_totalCount = total;
    updatePageLabel();

    for (const UserRecord &rec : std::as_const(users)) {
        QList<QStandardItem*> row;
        auto *checkItem = new QStandardItem();
        checkItem->setCheckable(true);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setData(rec.seq_id, Qt::UserRole);
        checkItem->setText(QString::number(rec.seq_id));
        row << checkItem;
        row << new QStandardItem(QString::fromUtf8(rec.nickname));
        QString nameStr = QString::fromUtf8(rec.name);
        if (nameStr.isEmpty()) nameStr = "(空)";
        row << new QStandardItem(nameStr);
        m_model->appendRow(row);
    }
}

void NickReviewWidget::updatePageLabel() {
    int totalPages = (m_totalCount + m_pageSize - 1) / m_pageSize;
    if (totalPages == 0) totalPages = 1;
    m_pageLabel->setText(QString("第 %1 / %2 页").arg(m_currentPage + 1).arg(totalPages));
}

// ---------- 删除审核记录 ----------
void NickReviewWidget::removeReviews(const QList<QPair<uint32_t, uint32_t>>& idPairs) {
    LmdbKV* db = globalReviewDb();
    if (!db) return;
    QList<QByteArray> keys;
    keys.reserve(idPairs.size());
    for (const auto &p : idPairs) {
        keys.append(makeKey(p.first, p.second));
    }
    db->removeBatch(keys);
}


bool NickReviewWidget::addReview(uint32_t appId, uint32_t userSeqId, const QString &newNickname, uint32_t timestamp)
{
    LmdbKV* db = globalReviewDb();  // 获取全局审核数据库单例
    if (!db) {
        qWarning() << "NickReviewWidget::addReview: 无法打开审核数据库";
        return false;
    }

    // 构造记录
    ReviewRecord rec;
    rec.appId = appId;
    rec.userSeqId = userSeqId;
    rec.timestamp = timestamp;
    QByteArray nickBytes = newNickname.toUtf8();
    size_t copyLen = std::min<size_t>(nickBytes.size(), sizeof(rec.nickname) - 1);
    memcpy(rec.nickname, nickBytes.constData(), copyLen);
    rec.nickname[copyLen] = '\0';

    // 生成键：appId(4字节) + userSeqId(4字节)
    QByteArray key;
    key.resize(8);
    memcpy(key.data(), &appId, 4);
    memcpy(key.data() + 4, &userSeqId, 4);

    // 写入数据库（覆盖已有记录）
    bool ok = db->put(key, rec.serialize());
    if (ok) {
        // 如果当前显示的是申请模式且 appid 匹配，可刷新显示
        if (m_appid == appId && m_modeCombo->currentIndex() == 0) {
            refresh();
        }
    }
    return ok;
}
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QMessageBox>

void NickReviewWidget::onAiApprove()
{
    // 1. 检查启用状态
    if (!m_enableCheck->isChecked()) {
        QMessageBox::warning(this, "提示", "审核功能已禁用");
        return;
    }

    int rowCount = m_model->rowCount();
    if (rowCount == 0) {
        QMessageBox::information(this, "提示", "当前表格没有数据");
        return;
    }

    // 2. 判断当前模式（根据下拉框索引）
    // 假设索引 0 为申请审核，索引 1 为批量审核
    bool isApplicationMode = (m_modeCombo->currentIndex() == 0);

    // 3. 收集当前页所有行数据
    QStringList inputLines;
    QList<uint32_t> ids;  // 存储所有行ID

    for (int row = 0; row < rowCount; ++row) {
        uint32_t id = 0;
        QString nick;

        if (isApplicationMode) {
            // 申请模式：列0复选框，1 AppID，2 用户ID，3 新昵称，4 时间
            auto *checkItem = m_model->item(row, 0);
            if (!checkItem) continue;
            auto pair = checkItem->data(Qt::UserRole).value<QPair<uint32_t, uint32_t>>();
            id = pair.second;
            nick = m_model->item(row, 2)->text();   // 新昵称
        } else {

            auto *checkItem = m_model->item(row, 0);
            if (!checkItem) continue;
            id = checkItem->data(Qt::UserRole).toUInt();
            nick = m_model->item(row, 1)->text();   // 原始昵称
        }

        inputLines.append(QString("%1.%2").arg(id).arg(nick));
        ids.append(id);
    }

    if (inputLines.isEmpty()) {
        QMessageBox::information(this, "提示", "没有有效数据");
        return;
    }

    // 4. 构造 AI 指令：只输出不通过的ID（逗号分隔）
    QString aiInput = inputLines.join("\n");
    QString aiPrompt = "请审核以下昵称申请，只输出不通过的ID（用逗号分隔）。如果全部通过，输出 '全部通过' 防止api返回空。\n"
                       "注意禁止包含 现代政治家人名 包括其他国家 禁止放行 特朗普，斯大林等国家人物名，以及国名，以及可能同音字，如草泥马，曹尼玛等同音字,将不通过的id输出以下格式\n1,50,24,68\n通过的 就不输出\n下面是需要审核的 昵称\n" + aiInput;

    // 5. 在线程中执行 AI 请求
    QFutureWatcher<QString> *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [=]() {
        QString aiResult = watcher->result();
        qDebug() << aiResult;
        watcher->deleteLater();
        if(aiResult.isEmpty()) {
            QMessageBox::information(this, "AI 建议", "ai似乎返回报错 或接口为空");
            return ;
        }else if(aiResult.contains("全部通过"))
        {
            aiResult.clear();
        }


        // 6. 解析不通过的 ID（提取所有数字）
        QSet<uint32_t> rejectedIds;
        QRegularExpression re("\\d+");
        QRegularExpressionMatchIterator it = re.globalMatch(aiResult);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            bool ok;
            uint32_t id = match.captured(0).toUInt(&ok);
            if (ok) rejectedIds.insert(id);
        }

        // 7. 设置复选框：默认全勾选，然后取消勾选被拒绝的
        for (int row = 0; row < m_model->rowCount(); ++row) {
            auto *checkItem = m_model->item(row, 0);
            if (!checkItem) continue;

            uint32_t rowId = 0;
            if (isApplicationMode) {
                auto pair = checkItem->data(Qt::UserRole).value<QPair<uint32_t, uint32_t>>();
                rowId = pair.second;
            } else {
                rowId = checkItem->data(Qt::UserRole).toUInt();
            }

            checkItem->setCheckState(rejectedIds.contains(rowId) ? Qt::Unchecked : Qt::Checked);
        }

        QMessageBox::information(this, "AI 建议", "AI 分析完成：不通过的已取消勾选，其余已勾选，请手动确认并点击相应按钮。\nresp:"+aiResult);
    });

    QFuture<QString> future = QtConcurrent::run([=]() -> QString {
        return ai_ui->Ai_post(m_aimode->currentText(), aiPrompt, 0);
    });
    watcher->setFuture(future);

    // 8. 显示等待提示（带“取消”按钮）
    QMessageBox *msgBox = new QMessageBox(QMessageBox::Information, "AI 审核", "正在等待 AI 响应...", QMessageBox::Cancel, this);
    connect(watcher, &QFutureWatcher<QString>::finished, msgBox, &QMessageBox::accept);
    connect(msgBox, &QMessageBox::buttonClicked, this, [=](QAbstractButton *button) {
        if (button == msgBox->button(QMessageBox::Cancel))
            msgBox->close();
    });
    msgBox->exec();
}





// ---------- 槽函数 ----------
void NickReviewWidget::onApprove() {
    if (!m_enableCheck->isChecked()) {
        QMessageBox::warning(this, "提示", "审核功能已禁用");
        return;
    }

    if (m_mode == Mode::Application) {
        QList<QPair<uint32_t,uint32_t>> idPairs;
        QStringList nicks;
        for (int r = 0; r < m_model->rowCount(); ++r) {
            auto *item = m_model->item(r, 0);
            if (item && item->checkState() == Qt::Checked) {
                auto pair = item->data(Qt::UserRole).value<QPair<uint32_t,uint32_t>>();
                idPairs << pair;
                nicks << m_model->item(r, 2)->text();
            }
        }
        if (idPairs.isEmpty()) {
            QMessageBox::information(this, "提示", "请至少选择一项");
            return;
        }
        emit approveRequested(idPairs, nicks);
        removeReviews(idPairs);      // 删除审核记录
        refresh();
    } else {
        // 批量模式，只对 name 为空的用户有效（即待审核）
        if (!m_onlyNameEmpty) {
            QMessageBox::information(this, "提示", "当前显示的是已审核列表，请切换到待审核列表进行通过操作");
            return;
        }
        QList<uint32_t> seqIds;
        QStringList nicks;
        for (int r = 0; r < m_model->rowCount(); ++r) {
            auto *item = m_model->item(r, 0);
            if (item && item->checkState() == Qt::Checked) {
                seqIds << item->data(Qt::UserRole).toUInt();
                nicks << m_model->item(r, 1)->text();
            }
        }
        if (seqIds.isEmpty()) {
            QMessageBox::information(this, "提示", "请至少选择一项");
            return;
        }
        emit batchApproveRequested(seqIds, nicks);
        refresh();
    }
}

void NickReviewWidget::onReject() {
    if (!m_enableCheck->isChecked()) {
        QMessageBox::warning(this, "提示", "审核功能已禁用");
        return;
    }

    if (m_mode == Mode::Application) {
        QList<QPair<uint32_t,uint32_t>> idPairs;
        for (int r = 0; r < m_model->rowCount(); ++r) {
            auto *item = m_model->item(r, 0);
            if (item && item->checkState() == Qt::Checked) {
                idPairs << item->data(Qt::UserRole).value<QPair<uint32_t,uint32_t>>();
            }
        }
        if (idPairs.isEmpty()) {
            QMessageBox::information(this, "提示", "请至少选择一项");
            return;
        }
        removeReviews(idPairs);  // 直接删除，不发射信号
        refresh();
    } else {
        // 批量模式拒绝：仅对 name 为空的用户有效（即待审核）
        if (!m_onlyNameEmpty) {
            QMessageBox::information(this, "提示", "当前显示的是已审核列表，无法拒绝");
            return;
        }
        QList<uint32_t> seqIds;
        for (int r = 0; r < m_model->rowCount(); ++r) {
            auto *item = m_model->item(r, 0);
            if (item && item->checkState() == Qt::Checked) {
                seqIds << item->data(Qt::UserRole).toUInt();
            }
        }
        if (seqIds.isEmpty()) {
            QMessageBox::information(this, "提示", "请至少选择一项");
            return;
        }
        emit batchRejectRequested(seqIds);
        refresh();
    }
}

void NickReviewWidget::onCancel() {
    if (!m_enableCheck->isChecked()) {
        QMessageBox::warning(this, "提示", "审核功能已禁用");
        return;
    }
    if (m_mode != Mode::BatchUser) {
        QMessageBox::information(this, "提示", "取消通过仅适用于批量审核模式");
        return;
    }
    // 取消通过：仅对 name 非空的用户（已审核）有效
    if (m_onlyNameEmpty) {
        QMessageBox::information(this, "提示", "当前显示的是待审核列表，请切换到已审核列表进行取消操作");
        return;
    }
    QList<uint32_t> seqIds;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto *item = m_model->item(r, 0);
        if (item && item->checkState() == Qt::Checked) {
            seqIds << item->data(Qt::UserRole).toUInt();
        }
    }
    if (seqIds.isEmpty()) {
        QMessageBox::information(this, "提示", "请至少选择一项");
        return;
    }
    emit batchCancelRequested(seqIds);
    refresh();
}

void NickReviewWidget::onSelectAll() {
    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto *item = m_model->item(r, 0);
        if (item) item->setCheckState(Qt::Checked);
    }
}

void NickReviewWidget::onSelectNone() {
    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto *item = m_model->item(r, 0);
        if (item) item->setCheckState(Qt::Unchecked);
    }
}

void NickReviewWidget::onInvert() {
    for (int r = 0; r < m_model->rowCount(); ++r) {
        auto *item = m_model->item(r, 0);
        if (item) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    }
}

void NickReviewWidget::onEnableToggled(bool checked) {
    m_tableView->setEnabled(checked);
    for (QObject *child : children()) {
        if (auto *btn = qobject_cast<QPushButton*>(child)) {
            btn->setEnabled(checked);
        }
    }
    // 取消按钮的启用状态还需额外逻辑，在 updateButtonsVisibility 中处理
    updateButtonsVisibility();
}

void NickReviewWidget::onLoadPending() {
    m_onlyNameEmpty = true;
    m_currentPage = 0;
    refresh();
    updateButtonsVisibility();
}

void NickReviewWidget::onLoadApproved() {
    m_onlyNameEmpty = false;
    m_currentPage = 0;
    refresh();
    updateButtonsVisibility();
}

void NickReviewWidget::prevPage() {
    if (m_currentPage > 0) {
        --m_currentPage;
        refresh();
    }
}

void NickReviewWidget::nextPage() {
    int totalPages = (m_totalCount + m_pageSize - 1) / m_pageSize;
    if (m_currentPage + 1 < totalPages) {
        ++m_currentPage;
        refresh();
    }
}