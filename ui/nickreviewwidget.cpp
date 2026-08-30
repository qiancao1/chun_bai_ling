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
        if (!g_reviewDb) {
            delete g_reviewDb;
            g_reviewDb = nullptr;
        }
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

    m_enableCheck = new QCheckBox("启用审核");
    m_enableCheck->setChecked(true);
    connect(m_enableCheck, &QCheckBox::toggled, this, &NickReviewWidget::onEnableToggled);

    topLayout->addWidget(titleLabel);
    topLayout->addWidget(m_modeCombo);
    topLayout->addStretch();
    topLayout->addWidget(m_enableCheck);
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
    m_btnCancel = new QPushButton("↩️ 取消通过");
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
    m_btnLoadPending = new QPushButton("加载待审核(name为空)");
    m_btnLoadApproved = new QPushButton("加载已审核(name非空)");
    m_btnPrevPage = new QPushButton("上一页");
    m_btnNextPage = new QPushButton("下一页");
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
         m_model->removeRows(0, 0);
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
    for (const QByteArray &key : keys) {
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
        row << checkItem;

        row << new QStandardItem(QString::number(rec.appId));
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
    qDebug() << "NickReviewWidget: onlyNameEmpty=" << m_onlyNameEmpty
             << ", offset=" << offset << ", limit=" << m_pageSize
             << ", total=" << total << ", returned=" << users.size();
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
    for (const auto &p : idPairs) {
        db->remove(makeKey(p.first, p.second));
    }
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
                nicks << m_model->item(r, 3)->text();
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