#include "aifujia.h"
#include "global.h"
#include "ui_aifujia.h"
#include <QDir>
#include <QSet>
#include <qjsonarray.h>
#include <qjsondocument.h>



Aifujia::Aifujia(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Aifujia)
{
    ui->setupUi(this);

    // ---------- 表格初始化 ----------
    ui->tableWidget->setColumnCount(1);                     // 只要1列
    ui->tableWidget->setHorizontalHeaderLabels({"指令名称"}); // 表头
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);

    // 连接 itemChanged 信号（同时捕获文本修改和复选框切换）
    connect(ui->tableWidget, &QTableWidget::itemChanged,
            this, &Aifujia::on_tableWidget_itemChanged);

    load_data(); // 加载已有数据
}

Aifujia::~Aifujia()
{
    delete ui;
}

// ---------- 从文件加载数据（全量刷新） ----------
void Aifujia::load_data()
{
    QFile file("data/fujia.json");
    if (!file.open(QIODevice::ReadOnly)) {
        fujiaList.clear();

        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        QMessageBox::warning(this, "错误", "配置文件格式错误");
        fujiaList.clear();

        return;
    }

    const QJsonArray arr = doc.array();
    fujiaList.clear();
    for (const QJsonValue &val : arr) {
        QJsonObject obj = val.toObject();
        fujia f;
        f.name = obj["name"].toString();
        f.role = obj["role"].toString();
        f.model = obj["mode"].toString();
        fujiaList.append(f);
    }

    ui->tableWidget->setRowCount(fujiaList.size());
    for (int row = 0; row < fujiaList.size(); ++row) {
        const fujia &f = fujiaList.at(row);
        QTableWidgetItem *item = new QTableWidgetItem(f.name);


        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        ui->tableWidget->setItem(row, 0, item);
    }
}


// ---------- 添加行（增量追加，不刷新整个表格） ----------
void Aifujia::on_add_row_clicked()
{
    // 1. 构造新数据
    fujia f;
    f.name = QString("指令%1").arg(fujiaList.size() + 1);
    f.role = "";
    f.model = "";

    // 2. 加入列表
    fujiaList.append(f);

    // 3. 直接在表格末尾插入一行（增量更新）
    int row = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(row);
    QTableWidgetItem *item = new QTableWidgetItem(f.name);
    item->setCheckState(Qt::Unchecked);
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    ui->tableWidget->setItem(row, 0, item);

    // 4. 自动保存
    save_data();
}

// ---------- 删除行（增量删除，不刷新整个表格） ----------
void Aifujia::on_removs_row_clicked()
{
    QList<QTableWidgetItem*> selection = ui->tableWidget->selectedItems();
    if (selection.isEmpty()) {
        QMessageBox::information(this, "提示", "请先选择要删除的行");
        return;
    }

    // 收集需要删除的行号（去重）
    QSet<int> rowsToRemove;
    for (QTableWidgetItem *item : std::as_const(selection)) {
        rowsToRemove.insert(item->row());
    }

    // 按行号从大到小排序，避免删除时索引错乱
    QList<int> sortedRows = rowsToRemove.values();
    std::sort(sortedRows.rbegin(), sortedRows.rend());

    // 从列表和表格中分别删除
    for (int row : std::as_const(sortedRows)) {
        fujiaList.removeAt(row);
        ui->tableWidget->removeRow(row);
    }

    save_data();
}

// ---------- 保存数据到文件 ----------
void Aifujia::on_save_data_clicked()
{
    // 1. 获取当前选中的行
    int row = ui->tableWidget->currentRow();
    if (row < 0 || row >= fujiaList.size()) {
        QMessageBox::warning(this, "提示", "请先选中一行数据再进行修改！");
        return;
    }

    // 2. 从 UI 控件读取新值
    QString newRole = ui->textEdit->toPlainText();
    QString newModel = ui->model_comboBox->currentText();  // 或者 currentData() 根据需要

    // 3. 更新内存列表中的对应行
    fujiaList[row].role = newRole;
    fujiaList[row].model = newModel;

    // 4. 保存到文件 (复用你已有的 save_data)
    save_data();

    // (可选) 5. 如果表格中需要同步显示 role/model，可以刷新表格。但因为你的表格只有一列，只显示 name，
    // 所以这里不需要刷新表格显示，数据已经存在 fujiaList 和文件里了。
    // 如果你希望提示用户保存成功：
    QMessageBox::information(this, "成功", "当前行数据已更新并保存");
}

void Aifujia::save_data()
{
    QJsonArray arr;
    for (const auto &f : std::as_const(fujiaList)) {
        QJsonObject obj;
        obj["name"] = f.name;
        obj["role"] = f.role;
        obj["mode"] = f.model;
        arr.append(obj);
    }

    // 确保目录存在
    QDir dir;
    if (!dir.mkpath("data")) {
        QMessageBox::warning(this, "错误", "无法创建数据目录");
        return;
    }

    if (!W_file("data/fujia.json", QJsonDocument(arr).toJson(QJsonDocument::Indented))) {
        QMessageBox::warning(this, "错误", "保存文件失败");
    } else {
        // 可以弹窗提示，也可去掉（看需求）
        // QMessageBox::information(this, "成功", "数据已保存");
    }
}

// ---------- 核心：监听表格变化，同步更新到 fujiaList ----------
void Aifujia::on_tableWidget_itemChanged(QTableWidgetItem *item)
{
    if (!item) return;
    int row = item->row();
    if (row < 0 || row >= fujiaList.size()) return;

    QString newName = item->text();

    if (fujiaList[row].name != newName) {

        fujiaList[row].name = newName;   // 更新内存
        save_data();                     // 只有改名字时才写 fujia.json
    }

    int index = accinfo(g_appid);
    if (index == -1) return;
    m_accounts[index]->fujia.clear();
    for (int i = 0; i < ui->tableWidget->rowCount(); ++i) {
        QTableWidgetItem *it = ui->tableWidget->item(i, 0);
        if (it && it->checkState() == Qt::Checked) {
            m_accounts[index]->fujia.append(it->text());
        }
    }
    accountPage->saveAccounts(m_accounts[index].get());
}
QString Aifujia::fujia_jy(QString &zl, QString &role)
{
    for(auto & f : fujiaList)
    {
        if(f.name==zl)
        {
            role = f.role;
            return f.model;
        }
    }
    return QString();
}

void Aifujia::initdata(AccountInfo *acc)
{

    for (int row = 0; row < fujiaList.size(); ++row) {

        QTableWidgetItem *item = ui->tableWidget->item(row, 0);
        if (item) item->setCheckState(Qt::Unchecked);
    }


    QHash<QString, int> nameToRow;
    for (int row = 0; row < fujiaList.size(); ++row) {
        nameToRow[fujiaList[row].name] = row;
    }


    for (const QString &toolName : std::as_const(acc->fujia)) {
        auto it = nameToRow.find(toolName);
        if (it != nameToRow.end()) {
            int row = *it;

            QTableWidgetItem *item = ui->tableWidget->item(row, 0);
            if (item) item->setCheckState(Qt::Checked);
        }
    }
}

void Aifujia::on_tableWidget_itemClicked(QTableWidgetItem *item)
{
    if (!item) return;
    int row = item->row();
    if (row < 0 || row >= fujiaList.size()) return;

    // 1. 获取当前行的数据
    const fujia &f = fujiaList.at(row);

    // 2. 回显到 UI 控件
    ui->textEdit->setPlainText(f.role);               // 回显角色
    ui->model_comboBox->setCurrentText(f.model);      // 回显模型 (假设 comboBox 可以输入或选择)
    int index = ui->model_comboBox->findText(f.model);
    if (index != -1)
        ui->model_comboBox->setCurrentIndex(index);

}

void Aifujia::initmode(QList<ModelData> &modelist)
{
    QString currentText = ui->model_comboBox->currentText();  // 假设 comboModel 是 QComboBox*
    ui->model_comboBox->clear();
    for (const auto &m : std::as_const(modelist))
    {
        ui->model_comboBox->addItem(m.name);
    }
    int index = ui->model_comboBox->findText(currentText);
    if (index != -1)
        ui->model_comboBox->setCurrentIndex(index);
}