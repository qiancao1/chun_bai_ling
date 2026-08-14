#include "menupanelwidget.h"
#include "global.h"
#include "qqbotclient.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkReply>
#include <QHeaderView>
#include <QComboBox>
#include <QCheckBox>



MenuPanelWidget::MenuPanelWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void MenuPanelWidget::setupUI()
{
    QVBoxLayout *totalLayout = new QVBoxLayout(this);

    // ============ 菜单区域 ============

    QHBoxLayout *menuLayout = new QHBoxLayout( );

    // 左：树
    QVBoxLayout *treeLayout = new QVBoxLayout();
    m_menuTree = new QTreeWidget(this);
    m_menuTree->setHeaderLabel("菜单结构");
    m_menuTree->setStyleSheet("QTreeWidget { border: 2px solid #555; }");
    m_menuTree->setIndentation(20);
    connect(m_menuTree, &QTreeWidget::itemClicked, this, &MenuPanelWidget::onMenuItemClicked);
    treeLayout->addWidget(m_menuTree);

    QHBoxLayout *treeBtnLayout = new QHBoxLayout();
    m_addTopBtn = new QPushButton("添加一级", this);
    m_addChildBtn = new QPushButton("添加二级", this);
    m_deleteBtn = new QPushButton("删除选中", this);

    m_updateMenuBtn = new QPushButton("更新菜单到机器人", this);



    treeBtnLayout->addWidget(m_addTopBtn);
    treeBtnLayout->addWidget(m_addChildBtn);
    treeBtnLayout->addWidget(m_deleteBtn);
     treeBtnLayout->addWidget(m_updateMenuBtn);
    treeBtnLayout->addStretch();
    treeLayout->addLayout(treeBtnLayout);
    connect(m_addTopBtn, &QPushButton::clicked, this, &MenuPanelWidget::onAddTopLevel);
    connect(m_addChildBtn, &QPushButton::clicked, this, &MenuPanelWidget::onAddChild);
    connect(m_deleteBtn, &QPushButton::clicked, this, &MenuPanelWidget::onDeleteSelected);
    connect(m_updateMenuBtn, &QPushButton::clicked, this, &MenuPanelWidget::onUpdateMenu);

    // 右：属性编辑

    QVBoxLayout *editLayout = new QVBoxLayout();
    QFormLayout *form = new QFormLayout();
    m_menuTitleEdit = new QLineEdit(this);
    form->addRow("标题:", m_menuTitleEdit);

    m_menuTypeCombo = new QComboBox(this);
    m_menuTypeCombo->addItems({"switch", "send_message", "link"});
    connect(m_menuTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int){ onMenuTypeChanged(m_menuTypeCombo->currentText()); });
    form->addRow("类型:", m_menuTypeCombo);

    m_menuDataEdit = new QLineEdit(this);
    form->addRow("数据:", m_menuDataEdit);

    m_menuSwitchDefault = new QCheckBox("默认开启", this);
    form->addRow(m_menuSwitchDefault);

    editLayout->addLayout(form);

    m_saveMenuBtn = new QPushButton("保存当前项", this);
    connect(m_saveMenuBtn, &QPushButton::clicked, this, &MenuPanelWidget::onSaveMenuNode);
    editLayout->addWidget(m_saveMenuBtn);

    menuLayout->addLayout(treeLayout, 1);
    menuLayout->addLayout(editLayout, 1);
    totalLayout->addLayout(menuLayout);



    // ============ 指令面板区域 ============

    QVBoxLayout *panelLayout = new QVBoxLayout( );


    // 表格
    m_panelTable = new QTableWidget(this);
    m_panelTable->setColumnCount(5);
    m_panelTable->setHorizontalHeaderLabels({"标题 7字", "声明(desc 15字)", "类型", "链接", "仅管理员"});
    m_panelTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    panelLayout->addWidget(m_panelTable);

    QHBoxLayout *tableBtnLayout = new QHBoxLayout();
    m_addRowBtn = new QPushButton("添加", this);
    m_removeRowBtn = new QPushButton("删除", this);
    m_updatePanelBtn = new QPushButton("更新面板到机器人", this);

    // 场景 + 作用范围


    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItems({"私聊", "群聊", "频道", "频道私聊"});
    m_targetTypeCombo = new QComboBox(this);
    m_targetTypeCombo->addItems({"all", "specific"});


    connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MenuPanelWidget::onScopeChanged);
    tableBtnLayout->addWidget(new QLabel("场景:"));
    tableBtnLayout->addWidget(m_scopeCombo);
    tableBtnLayout->addWidget(new QLabel("作用范围:"));
    tableBtnLayout->addWidget(m_targetTypeCombo);

    tableBtnLayout->addWidget(m_addRowBtn);
    tableBtnLayout->addWidget(m_removeRowBtn);
    tableBtnLayout->addWidget(m_updatePanelBtn);
    tableBtnLayout->addStretch();
    panelLayout->addLayout(tableBtnLayout);
    connect(m_addRowBtn, &QPushButton::clicked, this, &MenuPanelWidget::onAddTableRow);
    connect(m_removeRowBtn, &QPushButton::clicked, this, &MenuPanelWidget::onRemoveTableRow);
    connect(m_updatePanelBtn, &QPushButton::clicked, this, &MenuPanelWidget::onUpdatePanel);
    totalLayout->addLayout(panelLayout);

    // 状态栏
    m_statusBar = new QStatusBar(this);
    m_statusBar->showMessage("就绪");
    totalLayout->addWidget(m_statusBar);

    setLayout(totalLayout);


    onMenuTypeChanged("send_message");
}
void MenuPanelWidget::onScopeChanged()
{
    // 切换场景时自动加载该场景下的面板
    if (!m_botClient) {
        updateStatus("未选择Bot，无法加载面板", true);
        return;
    }
    QString scope = m_scopeCombo->currentText();
    updateStatus(QString("正在加载场景 [%1] 的面板...").arg(scope));
    if(scope=="私聊")
        scope="c2c";
    if(scope=="群聊")
        scope="group";
    if(scope=="频道")
        scope="channel";
    if(scope=="频道私聊")
        scope="dm";


    m_botClient->listPanels(scope, 1, "", [this](const QString &resp, QNetworkReply::NetworkError err) {
        QMetaObject::invokeMethod(this, [this, resp, err]() {
            if (err == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
                if (doc.isObject()) {
                    QJsonObject panels = doc.object() ;
                    if (!panels.isEmpty()) {
                        loadPanelData(panels);
                        updateStatus("面板已加载");
                    } else {
                        m_panelTable->setRowCount(0);
                        m_currentPanelId.clear();
                        updateStatus("无面板，可创建");
                    }
                }
            } else {
                updateStatus("加载面板失败", true);
            }
        });
    });
}
// ---------- 辅助 ----------
void MenuPanelWidget::updateStatus(const QString &msg, bool isError)
{
    if (m_statusBar) {
        m_statusBar->showMessage(msg);
        if (isError) qWarning() << "Error:" << msg;
    }
}

void MenuPanelWidget::onMenuTypeChanged(const QString &type)
{
    QString placeholder = (type == "switch") ? "switch_id" :
                              (type == "send_message") ? "发送消息" : "链接";
    m_menuDataEdit->setPlaceholderText(placeholder);
    m_menuSwitchDefault->setVisible(type == "switch");
}

// ---------- 菜单 ----------
void MenuPanelWidget::loadMenuItems(const QJsonArray &items)
{
    m_menuTree->clear();
    QTreeWidgetItem *root = m_menuTree->invisibleRootItem();
    for (const QJsonValue &val : items) {
        QJsonObject obj = val.toObject();
        QTreeWidgetItem *item = new QTreeWidgetItem(root);
        item->setText(0, obj["name"].toString());
        item->setData(0, Qt::UserRole, QJsonDocument(obj).toJson());
        if (obj.contains("sub_menu_items")) {
            const QJsonArray subs = obj["sub_menu_items"].toArray();
            for (const QJsonValue &sv : subs) {
                QJsonObject sub = sv.toObject();
                QTreeWidgetItem *subItem = new QTreeWidgetItem(item);
                subItem->setText(0, sub["name"].toString());
                subItem->setData(0, Qt::UserRole, QJsonDocument(sub).toJson());
            }
            item->setExpanded(true);
        }
    }
}
QJsonArray MenuPanelWidget::buildMenuJson()
{
    QJsonArray itemsArray;
    QTreeWidgetItem *root = m_menuTree->invisibleRootItem();

    for (int i = 0; i < root->childCount(); ++i) {
        QTreeWidgetItem *item = root->child(i);
        QByteArray data = item->data(0, Qt::UserRole).toByteArray();
        if (data.isEmpty()) continue;

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();

        // 检查是否有子菜单（二级节点）
        if (item->childCount() > 0) {
            // 设置类型为 menu
            obj["type"] = "menu";

            QJsonArray subItems;
            for (int j = 0; j < item->childCount(); ++j) {
                QTreeWidgetItem *sub = item->child(j);
                QByteArray subData = sub->data(0, Qt::UserRole).toByteArray();
                if (!subData.isEmpty()) {
                    QJsonDocument subDoc = QJsonDocument::fromJson(subData);
                    if (subDoc.isObject()) {
                        subItems.append(subDoc.object());
                    }
                }
            }
            if (!subItems.isEmpty()) {
                obj["sub_menu_items"] = subItems;
            }
        } else {
            // 无子菜单，保持原有 type（可能为 send_message / link / switch）
            // 但根据新规范，这些类型的字段名已明确，原数据应已包含
            // 移除可能残留的 sub_menu_items 字段（避免干扰）
            obj.remove("sub_menu_items");
        }

        itemsArray.append(obj);
    }


    return itemsArray;
}

void MenuPanelWidget::applyMenuNodeToUI(QTreeWidgetItem *item)
{
    if (!item) { clearMenuEdit(); return; }
    QByteArray data = item->data(0, Qt::UserRole).toByteArray();
    if (data.isEmpty()) { clearMenuEdit(); return; }
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) { clearMenuEdit(); return; }
    QJsonObject obj = doc.object();

    m_menuTitleEdit->setText(obj["name"].toString());
    QString type = obj["type"].toString();
    int idx = m_menuTypeCombo->findText(type);
    if (idx >= 0) m_menuTypeCombo->setCurrentIndex(idx);

    if (type == "switch") {
        QJsonObject sw = obj["switch"].toObject();
        m_menuDataEdit->setText(sw["switch_id"].toString());
        m_menuSwitchDefault->setChecked(sw["default"].toBool(false));
    } else if (type == "send_message") {
        m_menuDataEdit->setText(obj["send_message"].toString());
    } else if (type == "link") {
        m_menuDataEdit->setText(obj["link"].toString());
    }

}

void MenuPanelWidget::clearMenuEdit()
{
    m_menuTitleEdit->clear();
    m_menuTypeCombo->setCurrentIndex(0);
    m_menuDataEdit->clear();
    m_menuSwitchDefault->setChecked(false);

}

void MenuPanelWidget::onMenuItemClicked(QTreeWidgetItem *item, int col)
{
    Q_UNUSED(col);
    applyMenuNodeToUI(item);
}

void MenuPanelWidget::onAddTopLevel()
{
    QJsonObject newItem;
    newItem["name"] = "新按钮";
    newItem["type"] = "send_message";
    newItem["send_message"] = "";
    QTreeWidgetItem *item = new QTreeWidgetItem(m_menuTree);
    item->setText(0, "新按钮");
    item->setData(0, Qt::UserRole, QJsonDocument(newItem).toJson());
    m_menuTree->setCurrentItem(item);
    applyMenuNodeToUI(item);
}
void MenuPanelWidget::onAddChild()
{
    // 1. 获取当前选中项
    QTreeWidgetItem *cur = m_menuTree->currentItem();
    if (!cur) {
        // 若无选中，可以自动选中第一个一级节点，或提示用户
        if (m_menuTree->topLevelItemCount() == 0) {

            return;
        }
        cur = m_menuTree->topLevelItem(0);  // 默认选中第一个一级节点
    }

    // 2. 找到所属的一级节点（顶层项）
    QTreeWidgetItem *topLevel = cur;
    while (topLevel->parent() != nullptr) {
        topLevel = topLevel->parent();      // 向上追溯直到父级为 null
    }
    // 此时 topLevel 一定是顶层一级节点

    // 3. 在该一级节点下创建新的二级子项
    QJsonObject newSub;
    newSub["name"] = "子按钮";
    newSub["type"] = "send_message";
    newSub["send_message"] = "";

    QTreeWidgetItem *child = new QTreeWidgetItem(topLevel);
    child->setText(0, "子按钮");
    child->setData(0, Qt::UserRole, QJsonDocument(newSub).toJson());

    topLevel->setExpanded(true);          // 自动展开一级节点
    m_menuTree->setCurrentItem(child);    // 选中新建的子项
    applyMenuNodeToUI(child);             // 更新界面
}
void MenuPanelWidget::onDeleteSelected()
{
    QTreeWidgetItem *item = m_menuTree->currentItem();
    if (!item || item == m_menuTree->invisibleRootItem()) {

        return;
    }

    delete item;
    clearMenuEdit();

}

void MenuPanelWidget::onSaveMenuNode()
{
    QTreeWidgetItem *item = m_menuTree->currentItem();
    if (!item) {
        QMessageBox::information(this, "提示", "请先选中一个节点");
        return;
    }
    QJsonObject obj;
    obj["name"] = m_menuTitleEdit->text().trimmed();
    if (obj["name"].toString().isEmpty()) {
        QMessageBox::warning(this, "提示", "标题不能为空");
        return;
    }
    QString type = m_menuTypeCombo->currentText();
    obj["type"] = type;
    QString data = m_menuDataEdit->text().trimmed();
    if (type == "switch") {
        QJsonObject sw;
        sw["switch_id"] = data;
        sw["default"] = m_menuSwitchDefault->isChecked();
        obj["switch"] = sw;
    } else if (type == "send_message") {
        obj["send_message"] = data;
    } else if (type == "link") {
        obj["link"] = data;
    }
    item->setText(0, obj["name"].toString());
    item->setData(0, Qt::UserRole, QJsonDocument(obj).toJson());
    updateStatus("节点已保存");
}

void MenuPanelWidget::onUpdateMenu()
{
    if (!m_botClient) {
        switchBot();
        if (!m_botClient) {
            updateStatus("未选择Bot", true);
            return;
        }


    }
    if (m_menuTree->currentItem()) onSaveMenuNode();
    QJsonArray items = buildMenuJson();
    if (items.isEmpty()) {
        QMessageBox::warning(this, "提示", "菜单不能为空");
        return;
    }
    QJsonObject menuData;
    menuData["menu"] = QJsonObject{{"items", items}};




    updateStatus("正在更新菜单...");
    m_botClient->updateMenu(menuData, [this](const QString &resp, QNetworkReply::NetworkError err) {
        qDebug ( ) << resp;
        QMetaObject::invokeMethod(this, [this, err]() {

            updateStatus(err == QNetworkReply::NoError ? "菜单更新成功" : "菜单更新失败", err != QNetworkReply::NoError);
        });
    });
}

// ---------- 指令面板 ----------
void MenuPanelWidget::loadPanelData(const QJsonObject &responseObj)
{
    // 1. 解析 records 数组，取第一条记录（根据需求可调整）
    QJsonArray records = responseObj["records"].toArray();
    if (records.isEmpty()) {
        m_panelTable->setRowCount(0);

        return;
    }
    QJsonObject record = records[0].toObject();

    // 2. 提取面板基础信息
    m_currentPanelId = record["panel_id"].toString();
    m_targetTypeCombo->setCurrentText(record["target_type"].toString("all"));

    // 3. 清空表格，准备填充
    m_panelTable->setRowCount(0);

    // 4. 提取面板内的 items 数组
    QJsonObject panel = record["panel"].toObject();
    QJsonArray items = panel["items"].toArray();

    // 5. 遍历填充表格（与原逻辑完全相同，只是 items 来源变了）
    for (const QJsonValue &val : std::as_const(items)) {
        QJsonObject item = val.toObject();
        int row = m_panelTable->rowCount();
        m_panelTable->insertRow(row);

        // 名称 & 描述
        m_panelTable->setItem(row, 0, new QTableWidgetItem(item["name"].toString()));
        m_panelTable->setItem(row, 1, new QTableWidgetItem(item["desc"].toString()));

        // 类型下拉框
        QComboBox *typeCombo = new QComboBox();
        typeCombo->addItems({"command", "link"});
        QString type = item["type"].toString("command");
        int idx = typeCombo->findText(type);
        if (idx >= 0) typeCombo->setCurrentIndex(idx);
        m_panelTable->setCellWidget(row, 2, typeCombo);

        // 数据（command 或 link）
        QString data = (type == "command") ? item["command"].toString() : item["link"].toString();
        m_panelTable->setItem(row, 3, new QTableWidgetItem(data));

        // 仅管理员复选框
        QCheckBox *adminCheck = new QCheckBox();
        adminCheck->setChecked(item["only_admin"].toBool(false));
        m_panelTable->setCellWidget(row, 4, adminCheck);
    }
}
QJsonObject MenuPanelWidget::buildPanelJson()
{
    QJsonObject obj;

    QString scope= m_scopeCombo->currentText();
    if(scope=="私聊")
        scope="c2c";
    if(scope=="群聊")
        scope="group";
    if(scope=="频道")
        scope="channel";
    if(scope=="频道私聊")
        scope="dm";
    obj["scope"] =scope;
    obj["target_type"] = m_targetTypeCombo->currentText();

    QJsonArray items;
    for (int i = 0; i < m_panelTable->rowCount(); ++i) {
        QTableWidgetItem *nameItem = m_panelTable->item(i, 0);
        QTableWidgetItem *descItem = m_panelTable->item(i, 1);
        if (!nameItem) continue;

        QComboBox *typeCombo = qobject_cast<QComboBox*>(m_panelTable->cellWidget(i, 2));
        QString type = typeCombo ? typeCombo->currentText() : "command";

        QTableWidgetItem *dataItem = m_panelTable->item(i, 3);
        QString data = dataItem ? dataItem->text() : "";

        QCheckBox *adminCheck = qobject_cast<QCheckBox*>(m_panelTable->cellWidget(i, 4));
        bool onlyAdmin = adminCheck ? adminCheck->isChecked() : false;

        QJsonObject itemObj;
        itemObj["name"] = nameItem->text();
        if (descItem) itemObj["desc"] = descItem->text();
        itemObj["type"] = type;
        if (type == "command")
            itemObj["command"] = data;
        else if (type == "link")
            itemObj["link"] = data;
        itemObj["only_admin"] = onlyAdmin;
        items.append(itemObj);
    }
    QJsonObject panel;
    panel["items"] = items;
    obj["panel"] = panel;
    return obj;
}

void MenuPanelWidget::onAddTableRow()
{
    int row = m_panelTable->rowCount();
    m_panelTable->insertRow(row);
    m_panelTable->setItem(row, 0, new QTableWidgetItem("新指令"));
    m_panelTable->setItem(row, 1, new QTableWidgetItem(""));

    QComboBox *typeCombo = new QComboBox();
    typeCombo->addItems({"command", "link"});
    m_panelTable->setCellWidget(row, 2, typeCombo);

    m_panelTable->setItem(row, 3, new QTableWidgetItem(""));

    QCheckBox *adminCheck = new QCheckBox();
    adminCheck->setChecked(false);
    m_panelTable->setCellWidget(row, 4, adminCheck);
}

void MenuPanelWidget::onRemoveTableRow()
{
    int row = m_panelTable->currentRow();
    if (row < 0) {
        QMessageBox::information(this, "提示", "请选中一行");
        return;
    }
    m_panelTable->removeRow(row);
}

void MenuPanelWidget::onUpdatePanel()
{
    if (!m_botClient) {
        switchBot();
        if (!m_botClient) {
            updateStatus("未选择Bot", true);
            return;
        }


    }
    if (m_currentPanelId.isEmpty()) {
        QJsonObject panelData = buildPanelJson();

        updateStatus("正在创建面板...");
        m_botClient->createPanel(panelData, [this](const QString &resp, QNetworkReply::NetworkError err) {
            qDebug() <<resp;
            QMetaObject::invokeMethod(this, [this, err]() {
                updateStatus(err == QNetworkReply::NoError ? "面板创建成功" : "面板创建失败", err != QNetworkReply::NoError);
            });
        });


        return;
    }
    QJsonObject panelData = buildPanelJson();

    updateStatus("正在更新面板...");
    m_botClient->updatePanel(m_currentPanelId, panelData, [this](const QString &resp, QNetworkReply::NetworkError err) {
        qDebug() <<resp;
        QMetaObject::invokeMethod(this, [this, err]() {
            updateStatus(err == QNetworkReply::NoError ? "面板更新成功" : "面板更新失败", err != QNetworkReply::NoError);
        });
    });
}

// ---------- 切换Bot ----------
void MenuPanelWidget::switchBot()
{
    if (!m_botClients.contains(g_appid)) {
        updateStatus("未找到对应Bot", true);
        m_botClient = nullptr;
        return;
    }
    m_botClient = m_botClients[g_appid];
    updateStatus(QString("已切换至 %1，自动加载数据...").arg(g_appid));

    // 加载菜单
    m_botClient->getMenu([this](const QString &resp, QNetworkReply::NetworkError err) {
        QMetaObject::invokeMethod(this, [this, resp, err]() {
            if (err == QNetworkReply::NoError) {

                qDebug() <<resp;
                QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("menu")) {
                        QJsonObject menu = obj["menu"].toObject();
                        QJsonArray items = menu["items"].toArray();
                        loadMenuItems(items);
                    }
                }
                updateStatus("菜单已加载");
            } else {
                updateStatus("加载菜单失败", true);
            }
        });
    });

    // 加载面板（获取当前场景下的第一个面板）
    QString scope = m_scopeCombo->currentText();
    if(scope=="私聊")
        scope="c2c";
    if(scope=="群聊")
        scope="group";
    if(scope=="频道")
        scope="channel";
    if(scope=="频道私聊")
        scope="dm";

    m_botClient->listPanels(scope, 20, "", [this](const QString &resp, QNetworkReply::NetworkError err) {
        QMetaObject::invokeMethod(this, [this, resp, err]() {

            if (err == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
                if (doc.isObject()) {
                    QJsonObject panels = doc.object() ;
                    if (!panels.isEmpty()) {
                        loadPanelData(panels);
                        updateStatus("面板已加载");
                    } else {
                        m_panelTable->setRowCount(0);
                        m_currentPanelId.clear();
                        updateStatus("无面板，可创建");
                    }
                }
            } else {
                updateStatus("加载面板失败", true);
            }
        });
    });
}