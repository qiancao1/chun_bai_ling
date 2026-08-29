#include "nickreviewwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>
#include <QLabel>

NickReviewWidget::NickReviewWidget(LmdbKV *db, QWidget *parent)
    : QWidget(parent), m_db(db), m_model(new QStandardItemModel(this)) {
    setupUI();
    loadReviews();
}

NickReviewWidget::~NickReviewWidget() {}

void NickReviewWidget::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);

    // -------- 顶部：标题 + 启用开关 --------
    auto *topLayout = new QHBoxLayout;
    QLabel *titleLabel = new QLabel("昵称审核");
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    m_enableCheck = new QCheckBox("启用审核");
    m_enableCheck->setChecked(true);
    connect(m_enableCheck, &QCheckBox::toggled, this, &NickReviewWidget::onEnableToggled);

    topLayout->addWidget(titleLabel);
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
    QPushButton *btnInvert = new QPushButton("反选");
    QPushButton *btnSelectAll = new QPushButton("全选");
    QPushButton *btnSelectNone = new QPushButton("全不选");
    QPushButton *btnw = new QPushButton("加载审核列表");
    connect(btnApprove, &QPushButton::clicked, this, &NickReviewWidget::onApprove);
    connect(btnReject, &QPushButton::clicked, this, &NickReviewWidget::onReject);
    connect(btnInvert, &QPushButton::clicked, this, &NickReviewWidget::onInvert);
    connect(btnSelectAll, &QPushButton::clicked, this, &NickReviewWidget::onSelectAll);
    connect(btnSelectNone, &QPushButton::clicked, this, &NickReviewWidget::onSelectNone);
    connect(btnw, &QPushButton::clicked, this, &NickReviewWidget::loadReviews);
    btnLayout->addWidget(btnApprove);
    btnLayout->addWidget(btnReject);
    btnLayout->addWidget(btnInvert);
    btnLayout->addWidget(btnSelectAll);
    btnLayout->addWidget(btnSelectNone);
    btnLayout->addWidget(btnw);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);

    // 设置表头：复选框 | AppID | 用户ID | 新昵称 | 申请时间
    QStringList headers;
    headers << "AppID" << "用户ID" << "新昵称" << "申请时间";
    m_model->setHorizontalHeaderLabels(headers);

    // 初始状态
    onEnableToggled(m_enableCheck->isChecked());
}

void NickReviewWidget::onEnableToggled(bool checked) {
    m_tableView->setEnabled(checked);
    // 禁用/启用按钮（通过表格的父控件查找按钮？简便起见直接遍历子控件）
    for (QObject *child : children()) {
        if (auto *btn = qobject_cast<QPushButton*>(child)) {
            btn->setEnabled(checked);
        }
    }
    // 复选框自身保持可操作
}

// 生成键：appId(4字节) + userSeqId(4字节)
QByteArray NickReviewWidget::makeKey(uint32_t appId, uint32_t userSeqId) {
    QByteArray key;
    key.resize(8);
    memcpy(key.data(), &appId, 4);
    memcpy(key.data() + 4, &userSeqId, 4);
    return key;
}

void NickReviewWidget::parseKey(const QByteArray &key, uint32_t &appId, uint32_t &userSeqId) {
    if (key.size() == 8) {
        memcpy(&appId, key.constData(), 4);
        memcpy(&userSeqId, key.constData() + 4, 4);
    } else {
        appId = 0;
        userSeqId = 0;
    }
}

void NickReviewWidget::loadReviews() {
    m_model->removeRows(0, m_model->rowCount());
    if (!m_db) return;

    QList<QByteArray> keys = m_db->getAllKeysByteArray();
    for (const QByteArray &key : std::as_const(keys)) {
        if (key.size() != 8) continue; // 跳过非预期的键

        QByteArray value = m_db->get(key);
        if (value.isEmpty()) continue;

        ReviewRecord rec = ReviewRecord::deserialize(value);
        QList<QStandardItem*> row;

        // 复选框，存储 (appId, userSeqId)
        auto *checkItem = new QStandardItem();
        checkItem->setCheckable(true);
        checkItem->setCheckState(Qt::Unchecked);
        // 使用 QPair 存储，打包进 QVariant
        QVariant var = QVariant::fromValue(QPair<uint32_t, uint32_t>(rec.appId, rec.userSeqId));
        checkItem->setData(var, Qt::UserRole);

        checkItem->setText(QString::number(rec.appId));
        row << checkItem;
        // 用户ID列
        row << new QStandardItem(QString::number(rec.userSeqId));
        // 昵称列
        row << new QStandardItem(QString::fromUtf8(rec.nickname));
        // 时间列
        QDateTime dt = QDateTime::fromSecsSinceEpoch(rec.timestamp);
        row << new QStandardItem(dt.toString("yyyy-MM-dd hh:mm:ss"));

        m_model->appendRow(row);
    }
}

bool NickReviewWidget::addReview(uint32_t appId, uint32_t userSeqId, const QString &nickname, uint32_t timestamp) {
    if (!m_db) return false;

    ReviewRecord rec;
    rec.appId = appId;
    rec.userSeqId = userSeqId;
    rec.timestamp = timestamp;
    strncpy(rec.nickname, nickname.toUtf8().constData(), sizeof(rec.nickname) - 1);
    rec.nickname[sizeof(rec.nickname) - 1] = '\0';

    QByteArray key = makeKey(appId, userSeqId);
    bool ok = m_db->put(key, rec.serialize());
    if (ok) loadReviews();
    return ok;
}

void NickReviewWidget::removeReviews(const QList<QPair<uint32_t, uint32_t>> &idPairs) {
    if (!m_db) return;
    for (const auto &pair : idPairs) {
        QByteArray key = makeKey(pair.first, pair.second);
        m_db->remove(key);
    }
    loadReviews();
}

// ---------- 槽函数 ----------
void NickReviewWidget::onApprove() {
    if (!isEnabled()) {
        QMessageBox::warning(this, "提示", "审核功能已禁用");
        return;
    }

    QList<QPair<uint32_t, uint32_t>> idPairs;
    QStringList newNicknames;

    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *checkItem = m_model->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            auto pair = checkItem->data(Qt::UserRole).value<QPair<uint32_t, uint32_t>>();
            QString nick = m_model->item(row, 3)->text(); // 昵称列索引3
            idPairs << pair;
            newNicknames << nick;
        }
    }

    if (idPairs.isEmpty()) {
        QMessageBox::information(this, "提示", "请至少选择一项");
        return;
    }

    emit approveRequested(idPairs, newNicknames);
    removeReviews(idPairs);
}

void NickReviewWidget::onReject() {
    if (!isEnabled()) {
        QMessageBox::warning(this, "提示", "审核功能已禁用");
        return;
    }

    QList<QPair<uint32_t, uint32_t>> idPairs;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *checkItem = m_model->item(row, 0);
        if (checkItem && checkItem->checkState() == Qt::Checked) {
            auto pair = checkItem->data(Qt::UserRole).value<QPair<uint32_t, uint32_t>>();
            idPairs << pair;
        }
    }
    if (idPairs.isEmpty()) {
        QMessageBox::information(this, "提示", "请至少选择一项");
        return;
    }
    removeReviews(idPairs);
}

void NickReviewWidget::onSelectAll() {
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *item = m_model->item(row, 0);
        if (item) item->setCheckState(Qt::Checked);
    }
}

void NickReviewWidget::onSelectNone() {
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *item = m_model->item(row, 0);
        if (item) item->setCheckState(Qt::Unchecked);
    }
}

void NickReviewWidget::onInvert() {
    for (int row = 0; row < m_model->rowCount(); ++row) {
        auto *item = m_model->item(row, 0);
        if (item) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
        }
    }
}