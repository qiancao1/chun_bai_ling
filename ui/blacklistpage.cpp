// BlacklistPage.cpp
#include "blacklistpage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFile>
#include <QDataStream>
#include <QDir>
#include <QSet>

// ---------------------------------------------------------------- 账号级存储
// 每个机器人一份：data/blacklist_<appid>.json（QDataStream 序列化的 QHash<ID, 备注>）
// 内存里按 appid 缓存，首次访问才读盘 —— 消息过滤每次都查，不能每次读文件。
namespace {
QHash<int, QHash<QString, QString>> g_blacklists;
QSet<int> g_loaded;
}

QString Blacklist::filePath(int appid)
{
    return QString("data/blacklist_%1.json").arg(appid);
}

QHash<QString, QString> &Blacklist::data(int appid)
{
    if (!g_loaded.contains(appid)) {
        g_loaded.insert(appid);
        QFile file(filePath(appid));
        if (file.open(QIODevice::ReadOnly)) {
            QDataStream stream(&file);
            stream.setVersion(QDataStream::Qt_5_15);
            stream >> g_blacklists[appid];
        } else if (appid > 0) {
            // 老版本只有一个全局 data/blacklist.json。第一次切到这个账号、
            // 而它还没有自己的文件时，把旧名单继承过来（旧文件保持原样不动）。
            QFile legacy("data/blacklist.json");
            if (legacy.exists() && legacy.open(QIODevice::ReadOnly)) {
                QDataStream stream(&legacy);
                stream.setVersion(QDataStream::Qt_5_15);
                stream >> g_blacklists[appid];
                if (!g_blacklists[appid].isEmpty())
                    save(appid);
            }
        }
    }
    return g_blacklists[appid];
}

bool Blacklist::save(int appid)
{
    if (appid <= 0) return true;          // 还没选账号，不产生垃圾文件
    QDir().mkpath("data");
    QFile file(filePath(appid));
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << data(appid);                // 直接序列化这个账号的哈希表
    return stream.status() == QDataStream::Ok;
}

bool Blacklist::contains(int appid, const QString &id)
{
    if (appid <= 0) return false;
    return data(appid).contains(id);
}

void Blacklist::forget(int appid)
{
    g_loaded.remove(appid);
    g_blacklists.remove(appid);
}

// ---------------------------------------------------------------- 页面
BlacklistPage::BlacklistPage(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    setAppId(0);
}

BlacklistPage::~BlacklistPage()
{
    saveToFile();        // 析构时保存，确保数据不丢失
}

void BlacklistPage::setAppId(int appid)
{
    if (m_appid == appid) {
        refreshTable();
        return;
    }
    saveToFile();        // 先把上一个账号的改动落盘
    m_appid = appid;
    if (m_titleLabel) {
        m_titleLabel->setText(appid > 0
            ? tr("黑名单管理：当前账号 %1，ID 与备注都会自动保存").arg(appid)
            : tr("黑名单管理：请先在上方选择机器人"));
    }
    refreshTable();
}

void BlacklistPage::setupUI()
{
    m_titleLabel = new QLabel(tr("黑名单管理：添加/删除黑名单ID，备注可编辑，自动保存"), this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("ID"), tr("备注")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);

    QString style = R"(
    QTableWidget::item:selected {
        background-color: #E3B0B9;
        color: white;
    }
    QTableWidget::item:hover {
        background-color: #e0e7ff;
        color: black;
    }
    )";
    m_table->setStyleSheet(style);

    QPushButton *addBtn = new QPushButton(tr("添加黑名单"), this);
    QPushButton *deleteBtn = new QPushButton(tr("删除选中"), this);
    connect(addBtn, &QPushButton::clicked, this, &BlacklistPage::onAddClicked);
    connect(deleteBtn, &QPushButton::clicked, this, &BlacklistPage::onDeleteClicked);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(deleteBtn);
    btnLayout->addStretch();

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_titleLabel);
    mainLayout->addWidget(m_table);
    mainLayout->addLayout(btnLayout);

    connect(m_table, &QTableWidget::itemChanged, this, &BlacklistPage::onItemChanged);
}

void BlacklistPage::refreshTable()
{
    disconnect(m_table, &QTableWidget::itemChanged, this, &BlacklistPage::onItemChanged);

    const QHash<QString, QString> &bl = Blacklist::data(m_appid);

    m_table->clearContents();
    m_table->setRowCount(bl.size());

    int row = 0;
    for (auto it = bl.begin(); it != bl.end(); ++it, ++row) {
        QTableWidgetItem *idItem = new QTableWidgetItem(it.key());
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, idItem);

        QTableWidgetItem *remarkItem = new QTableWidgetItem(it.value());
        remarkItem->setFlags(remarkItem->flags() | Qt::ItemIsEditable);
        m_table->setItem(row, 1, remarkItem);
    }

    connect(m_table, &QTableWidget::itemChanged, this, &BlacklistPage::onItemChanged);
}

bool BlacklistPage::saveToFile()
{
    return Blacklist::save(m_appid);
}

// ---------------- WebUI 桥接 ----------------
// 黑名单文件是 QDataStream 序列化的 QHash<QString,QString>，网页端无法解析，
// 所以读写都放在这里：对外只暴露「ID → 备注」的普通容器。appid 由调用方给。
QHash<QString, QString> BlacklistPage::webuiSnapshot(int appid) const
{
    return Blacklist::data(appid);
}

bool BlacklistPage::webuiApply(int appid, const QHash<QString, QString> &data)
{
    Blacklist::data(appid) = data;
    // 先落盘再刷界面：表格是从哈希表重建的
    const bool ok = Blacklist::save(appid);
    if (appid == m_appid)
        refreshTable();
    return ok;
}

void BlacklistPage::onAddClicked()
{
    if (m_appid <= 0) {
        QMessageBox::information(this, tr("提示"), tr("请先在上方选择机器人，黑名单是按账号区分的"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("添加黑名单"));
    QFormLayout form(&dialog);
    QLineEdit *idEdit = new QLineEdit(&dialog);
    QLineEdit *remarkEdit = new QLineEdit(&dialog);
    form.addRow(tr("ID:"), idEdit);
    form.addRow(tr("备注:"), remarkEdit);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    form.addRow(&buttonBox);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QString id = idEdit->text().trimmed();
        if (id.isEmpty()) {
            QMessageBox::warning(this, tr("警告"), tr("ID不能为空！"));
            return;
        }
        QHash<QString, QString> &bl = Blacklist::data(m_appid);
        if (bl.contains(id)) {
            QMessageBox::warning(this, tr("警告"), tr("该ID已在黑名单中！"));
            return;
        }
        QString remark = remarkEdit->text();
        bl.insert(id, remark);
        refreshTable();
        saveToFile();   // 添加后自动保存
    }
}

void BlacklistPage::onDeleteClicked()
{
    QList<QTableWidgetItem*> selectedItems = m_table->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选中要删除的行"));
        return;
    }

    QSet<int> rows;
    for (QTableWidgetItem *item : std::as_const(selectedItems)) {
        rows.insert(item->row());
    }

    if (QMessageBox::question(this, tr("确认删除"), tr("确定要删除选中的 %1 条黑名单记录吗？").arg(rows.size()))
        != QMessageBox::Yes) {
        return;
    }

    QHash<QString, QString> &bl = Blacklist::data(m_appid);
    for (int row : rows) {
        QTableWidgetItem *idItem = m_table->item(row, 0);
        if (idItem) {
            bl.remove(idItem->text());
        }
    }
    refreshTable();
    saveToFile();   // 删除后自动保存
}

void BlacklistPage::onItemChanged(QTableWidgetItem *item)
{
    if (!item) return;
    int row = item->row();
    int col = item->column();
    if (col == 1) {
        QTableWidgetItem *idItem = m_table->item(row, 0);
        if (idItem) {
            QString id = idItem->text();
            QString newRemark = item->text();
            QHash<QString, QString> &bl = Blacklist::data(m_appid);
            if (bl.contains(id)) {
                bl[id] = newRemark;
                saveToFile();   // 编辑备注后自动保存
            }
        }
    }
}
