// BlacklistPage.cpp
#include "BlacklistPage.h"
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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

static const QString BLACKLIST_FILE = "data/blacklist.json"; // 保存在程序运行目录
QHash<QString, QString> m_blacklist; // 黑名单哈希表

BlacklistPage::BlacklistPage(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    loadFromFile();      // 启动时加载
    refreshTable();
}

BlacklistPage::~BlacklistPage()
{
    saveToFile();        // 析构时保存，确保数据不丢失
}

void BlacklistPage::setupUI()
{
    QLabel *titleLabel = new QLabel(tr("黑名单管理：添加/删除黑名单ID，备注可编辑，自动保存"), this);

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
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(m_table);
    mainLayout->addLayout(btnLayout);

    connect(m_table, &QTableWidget::itemChanged, this, &BlacklistPage::onItemChanged);
}

void BlacklistPage::refreshTable()
{
    disconnect(m_table, &QTableWidget::itemChanged, this, &BlacklistPage::onItemChanged);

    m_table->clearContents();
    m_table->setRowCount(m_blacklist.size());

    int row = 0;
    for (auto it = m_blacklist.begin(); it != m_blacklist.end(); ++it, ++row) {
        QTableWidgetItem *idItem = new QTableWidgetItem(it.key());
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, idItem);

        QTableWidgetItem *remarkItem = new QTableWidgetItem(it.value());
        remarkItem->setFlags(remarkItem->flags() | Qt::ItemIsEditable);
        m_table->setItem(row, 1, remarkItem);
    }

    connect(m_table, &QTableWidget::itemChanged, this, &BlacklistPage::onItemChanged);
}

// 替换 BlacklistPage.cpp 中的 loadFromFile() 和 saveToFile()

bool BlacklistPage::loadFromFile()
{
    QFile file(BLACKLIST_FILE);
    if (!file.exists())
        return true;  // 首次运行无文件

    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    // 直接反序列化 QHash<QString, QString>
    stream >> m_blacklist;
    return stream.status() == QDataStream::Ok;
}

bool BlacklistPage::saveToFile()
{
    QFile file(BLACKLIST_FILE);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    stream << m_blacklist;   // 直接序列化整个哈希表
    return stream.status() == QDataStream::Ok;
}

void BlacklistPage::onAddClicked()
{
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
        if (m_blacklist.contains(id)) {
            QMessageBox::warning(this, tr("警告"), tr("该ID已在黑名单中！"));
            return;
        }
        QString remark = remarkEdit->text();
        m_blacklist.insert(id, remark);
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

    for (int row : rows) {
        QTableWidgetItem *idItem = m_table->item(row, 0);
        if (idItem) {
            QString id = idItem->text();
            m_blacklist.remove(id);
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
            if (m_blacklist.contains(id)) {
                m_blacklist[id] = newRemark;
                saveToFile();   // 编辑备注后自动保存
            }
        }
    }
}