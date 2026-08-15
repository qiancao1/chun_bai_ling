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
#include <qbuttongroup.h>



MenuPanelWidget::MenuPanelWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
}

void MenuPanelWidget::setupUI()
{
    // 主布局：顶部是 TabWidget，底部是状态栏
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QTabWidget *tabWidget = new QTabWidget(this);

    // ==================== 第一个 Tab：菜单（保留原样） ====================
    QWidget *menuTab = new QWidget;
    QHBoxLayout *menuLayout = new QHBoxLayout(menuTab);

    // ---- 左侧树形菜单 ----
    QVBoxLayout *treeLayout = new QVBoxLayout;
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

    // ---- 右侧属性编辑 ----
    QVBoxLayout *editLayout = new QVBoxLayout;
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
    editLayout->addStretch();

    menuLayout->addLayout(treeLayout, 1);
    menuLayout->addLayout(editLayout, 1);
    menuTab->setLayout(menuLayout);

    // ==================== 第二个 Tab：指令（完全重构） ====================
    QWidget *commandTab = new QWidget;
    QVBoxLayout *commandTabLayout = new QVBoxLayout(commandTab);
    commandTabLayout->setContentsMargins(0, 0, 0, 0);

    // ---------- 顶部控制行（作用范围 + 场景单选框 + 编辑按钮） ----------
    QHBoxLayout *topControlLayout = new QHBoxLayout;

    // 作用范围
    QLabel *targetLabel = new QLabel("作用范围：");
    m_targetTypeCombo = new QComboBox(this);
    m_targetTypeCombo->addItems({"all", "specific"});
    topControlLayout->addWidget(targetLabel);
    topControlLayout->addWidget(m_targetTypeCombo);

    // 场景单选框
    QLabel *scopeLabel = new QLabel("场景：");
    m_scopeC2C = new QRadioButton("私聊", this);
    m_scopeGroupChat = new QRadioButton("群聊", this);
    m_scopeChannel = new QRadioButton("频道", this);
    m_scopeDM = new QRadioButton("频道私聊", this);
    m_scopeGroupChat->setChecked(true);   // 默认群聊

    m_scopeGroup = new QButtonGroup(this);
    m_scopeGroup->addButton(m_scopeC2C, 0);
    m_scopeGroup->addButton(m_scopeGroupChat, 1);
    m_scopeGroup->addButton(m_scopeChannel, 2);
    m_scopeGroup->addButton(m_scopeDM, 3);

    topControlLayout->addWidget(scopeLabel);
    topControlLayout->addWidget(m_scopeC2C);
    topControlLayout->addWidget(m_scopeGroupChat);
    topControlLayout->addWidget(m_scopeChannel);
    topControlLayout->addWidget(m_scopeDM);

    // 编辑群ID / 好友ID 按钮
    m_editGroupsBtn = new QPushButton("编辑群ID", this);
    m_editFriendsBtn = new QPushButton("编辑好友ID", this);
    topControlLayout->addWidget(m_editGroupsBtn);
    topControlLayout->addWidget(m_editFriendsBtn);
    topControlLayout->addStretch();

    // ---------- 主区域（左右分割） ----------
    QSplitter *mainSplitter = new QSplitter(Qt::Horizontal);

    // 左侧：面板列表（只显示备注）
    QWidget *leftWidget = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_panelListTable = new QTableWidget(this);
    m_panelListTable->setColumnCount(1);
    m_panelListTable->setHorizontalHeaderLabels({"面板备注"});
    m_panelListTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_panelListTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    connect(m_panelListTable, &QTableWidget::itemClicked, this, &MenuPanelWidget::onPanelListItemClicked);

    // 面板管理按钮（创建 / 删除）
    QHBoxLayout *panelBtnLayout = new QHBoxLayout;
    m_createPanelBtn = new QPushButton("创建新面板", this);
    m_deletePanelBtn = new QPushButton("删除选中面板", this);
    panelBtnLayout->addWidget(m_createPanelBtn);
    panelBtnLayout->addWidget(m_deletePanelBtn);
    panelBtnLayout->addStretch();

    leftLayout->addWidget(m_panelListTable, 1);
    leftLayout->addLayout(panelBtnLayout);

    // 右侧：面板内容表格 + 操作按钮
    QWidget *rightWidget = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_panelTable = new QTableWidget(this);
    m_panelTable->setColumnCount(5);
    m_panelTable->setHorizontalHeaderLabels({"标题 7字", "声明(desc 15字)", "类型", "链接", "仅管理员"});
    m_panelTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rightLayout->addWidget(m_panelTable);

    QHBoxLayout *tableBtnLayout = new QHBoxLayout;
    m_addRowBtn = new QPushButton("添加行", this);
    m_removeRowBtn = new QPushButton("删除行", this);
    m_updatePanelBtn = new QPushButton("更新面板到机器人", this);
    tableBtnLayout->addWidget(m_addRowBtn);
    tableBtnLayout->addWidget(m_removeRowBtn);
    tableBtnLayout->addWidget(m_updatePanelBtn);
    tableBtnLayout->addStretch();
    rightLayout->addLayout(tableBtnLayout);

    // 左右组合
    mainSplitter->addWidget(leftWidget);
    mainSplitter->addWidget(rightWidget);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 2);

    commandTabLayout->addLayout(topControlLayout);
    commandTabLayout->addWidget(mainSplitter, 1);
    commandTab->setLayout(commandTabLayout);

    // ==================== 将两个 Tab 加入 TabWidget ====================
    tabWidget->addTab(menuTab, "菜单");
    tabWidget->addTab(commandTab, "指令");

    mainLayout->addWidget(tabWidget);

    // 状态栏（全局）
    m_statusBar = new QStatusBar(this);
    m_statusBar->showMessage("就绪");
    mainLayout->addWidget(m_statusBar);

    setLayout(mainLayout);


    connect(m_scopeGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &MenuPanelWidget::onScopeRadioClicked);
    connect(m_createPanelBtn, &QPushButton::clicked, this, &MenuPanelWidget::onCreatePanel);
    connect(m_deletePanelBtn, &QPushButton::clicked, this, &MenuPanelWidget::onDeleteSelectedPanel);
    connect(m_editGroupsBtn, &QPushButton::clicked, this, &MenuPanelWidget::onEditGroups);
    connect(m_editFriendsBtn, &QPushButton::clicked, this, &MenuPanelWidget::onEditFriends);
    connect(m_addRowBtn, &QPushButton::clicked, this, &MenuPanelWidget::onAddTableRow);
    connect(m_removeRowBtn, &QPushButton::clicked, this, &MenuPanelWidget::onRemoveTableRow);
    connect(m_updatePanelBtn, &QPushButton::clicked, this, &MenuPanelWidget::onUpdatePanel);
    connect(m_panelListTable, &QTableWidget::itemChanged, this, &MenuPanelWidget::onPanelRemarkChanged);
    onMenuTypeChanged("send_message");


}
void MenuPanelWidget::onScopeRadioClicked(int id)
{
    Q_UNUSED(id);  // id 参数可以不直接使用，因为 currentScope() 会从按钮组获取状态

    // 1. 获取当前选中的场景字符串
    QString scope = currentScope();

    // 2. 根据场景启用/禁用编辑按钮（可选）
    // 群聊场景允许编辑群ID，私聊/频道私聊允许编辑好友ID
    m_editGroupsBtn->setEnabled(scope == "group");
    m_editFriendsBtn->setEnabled(scope == "c2c" || scope == "dm");

    // 3. 加载该场景下的面板列表（刷新左侧列表和缓存）
    loadPanelsForCurrentScope();

    // 4. 状态栏提示
    updateStatus(QString("已切换到场景: %1").arg(scope));
}
#include <QInputDialog>
void MenuPanelWidget::onEditGroups()
{
    bool ok;
    QString text = QInputDialog::getMultiLineText(this, "编辑绑定群列表",
                                                  "请输入群ID，每行一个或用逗号分隔：",
                                                  m_bindGroups.join(","), &ok);
    if (ok) {
        // 解析为字符串列表
        QStringList ids = text.split(QRegExp("[,;\\s]+"), Qt::SkipEmptyParts);
        m_bindGroups = ids;
        updateStatus(QString("已设置 %1 个群ID").arg(ids.size()));
    }
}

void MenuPanelWidget::onEditFriends()
{
    bool ok;
    QString text = QInputDialog::getMultiLineText(this, "编辑绑定好友列表",
                                                  "请输入用户ID，每行一个或用逗号分隔：",
                                                  m_bindFriends.join(","), &ok);
    if (ok) {
        QStringList ids = text.split(QRegExp("[,;\\s]+"), Qt::SkipEmptyParts);
        m_bindFriends = ids;
        updateStatus(QString("已设置 %1 个好友ID").arg(ids.size()));
    }
}
void MenuPanelWidget::onPanelListItemClicked(QTableWidgetItem *item)
{
    if (!item) return;
    QString panelId = item->data(Qt::UserRole).toString();
    if (panelId.isEmpty()) return;

    if (panelId == "__new__") {
        // 临时项：清空表格，设置当前ID为空
        m_panelTable->setRowCount(0);
        m_currentPanelId.clear();
        updateStatus("正在编辑新面板");
        return;
    }

    if (!m_panelCache.contains(panelId)) {
        updateStatus("错误：面板数据未缓存");
        return;
    }
    loadPanelData(m_panelCache[panelId]);  // 传入单个 PanelRecord
    updateStatus("已加载面板: " + panelId);
}

void MenuPanelWidget::onPanelRemarkChanged(QTableWidgetItem *item)
{
    if (item->column() != 0) return;  // 只处理备注列
    QString panelId = item->data(Qt::UserRole).toString();
    if (panelId.isEmpty() || panelId == "__new__") return;  // 临时项不保存

    if (m_panelCache.contains(panelId)) {
        QJsonObject record = m_panelCache[panelId];
        QJsonObject panel = record["panel"].toObject();
        panel["remark"] = item->text();
        record["panel"] = panel;
        m_panelCache[panelId] = record;
    }
}
void MenuPanelWidget::onCreatePanel()
{
    // 清空右侧表格
    m_panelTable->setRowCount(0);
    m_currentPanelId.clear();  // 标记为新建

    // 在左侧列表添加一个临时项（panel_id 为 "__new__"）
    int row = m_panelListTable->rowCount();
    m_panelListTable->insertRow(row);
    QTableWidgetItem *item = new QTableWidgetItem("新面板");
    item->setData(Qt::UserRole, "__new__");   // 特殊标记
    m_panelListTable->setItem(row, 0, item);
    m_panelListTable->selectRow(row);

    updateStatus("已创建新面板，编辑内容后点击「更新面板到机器人」提交");
}
void MenuPanelWidget::onDeleteSelectedPanel()
{
    int row = m_panelListTable->currentRow();
    if (row < 0) {
        updateStatus("请先选择一个面板");
        return;
    }
    QString panelId = m_panelListTable->item(row, 0)->data(Qt::UserRole).toString();
    if (panelId.isEmpty()) return;

    // 如果是临时项，直接删除行，不调用接口
    if (panelId == "__new__") {
        m_panelListTable->removeRow(row);
        if (m_currentPanelId.isEmpty()) {
            m_panelTable->setRowCount(0);
        }
        updateStatus("已取消新建面板");
        return;
    }

    // 确认删除
    if (QMessageBox::question(this, "确认删除", "确定要删除面板 " + panelId + " 吗？",
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
        return;

    if (m_botClient) {
        m_botClient->deletePanel(panelId, [this, panelId, row](const QString &resp, QNetworkReply::NetworkError err) {
            QMetaObject::invokeMethod(this, [this, panelId, row, err,resp]() {
                if (err == QNetworkReply::NoError) {
                    m_panelCache.remove(panelId);
                    m_panelListTable->removeRow(row);
                    if (m_currentPanelId == panelId) {
                        m_panelTable->setRowCount(0);
                        m_currentPanelId.clear();
                    }
                    updateStatus("面板删除成功");
                } else {
                    updateStatus("删除失败: " + resp, true);
                }
            });
        });
    } else {
        updateStatus("未选择Bot", true);
    }
}
void MenuPanelWidget::loadPanelsForCurrentScope()
{
    if (!m_botClient) {
        updateStatus("未选择Bot，无法加载面板", true);
        return;
    }
    QString scope = currentScope();
    updateStatus(QString("正在加载场景 [%1] 的面板...").arg(scope));

    m_botClient->listPanels(scope, 20, "", [this](const QString &resp, QNetworkReply::NetworkError err) {
        QMetaObject::invokeMethod(this, [this, resp, err]() {
            qDebug()<<resp;
            if (err == QNetworkReply::NoError) {
                QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    loadPanelList(obj);  // 填充左侧表格和缓存
                }
            } else {
                updateStatus("加载面板失败: " + resp, true);
            }
        });
    });

}
void MenuPanelWidget::loadPanelList(const QJsonObject &responseObj)
{
    QJsonArray records = responseObj["records"].toArray();
    m_panelListTable->setRowCount(0);
    m_panelCache.clear();

    if (records.isEmpty()) {
        updateStatus("该场景下无面板");
        m_panelTable->setRowCount(0);
        m_currentPanelId.clear();
        return;
    }

    for (const QJsonValue &val : records) {
        QJsonObject record = val.toObject();
        QString panelId = record["panel_id"].toString();
        QString remark = record["panel"].toObject()["remark"].toString();
        QString displayText = remark.isEmpty() ? panelId : remark;

        int row = m_panelListTable->rowCount();
        m_panelListTable->insertRow(row);
        QTableWidgetItem *item = new QTableWidgetItem(displayText);
        item->setData(Qt::UserRole, panelId);
        m_panelListTable->setItem(row, 0, item);

        m_panelCache[panelId] = record;   // 缓存完整记录
    }

    // 自动选中第一个
    if (m_panelListTable->rowCount() > 0) {
        m_panelListTable->selectRow(0);
        onPanelListItemClicked(m_panelListTable->item(0, 0));
    }

    updateStatus(QString("已加载 %1 个面板").arg(records.size()));
}

QString MenuPanelWidget::currentScope() const
{
    int id = m_scopeGroup->checkedId();
    switch (id) {
    case 0: return "c2c";
    case 1: return "group";
    case 2: return "channel";
    case 3: return "dm";
    default: return "group";  // 默认
    }
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

    QJsonObject menuData;
    menuData["menu"] = QJsonObject{{"items", items}};




    updateStatus("正在更新菜单...");
    m_botClient->updateMenu(menuData, [this](const QString &resp, QNetworkReply::NetworkError err) {

        QMetaObject::invokeMethod(this, [this,resp, err]() {

            updateStatus(err == QNetworkReply::NoError ? "菜单更新成功" : "菜单更新失败"+resp, err != QNetworkReply::NoError);
        });
    });
}

// ---------- 指令面板 ----------
void MenuPanelWidget::loadPanelData(const QJsonObject &record)
{
    // 1. 提取面板基础信息（单个记录）
    m_currentPanelId = record["panel_id"].toString();
    m_targetTypeCombo->setCurrentText(record["target_type"].toString("all"));

    // 2. 清空表格
    m_panelTable->setRowCount(0);

    // 3. 提取面板内的 items 数组
    QJsonObject panel = record["panel"].toObject();
    QJsonArray items = panel["items"].toArray();

    // 4. 遍历填充表格
    for (const QJsonValue &val : std::as_const(items)) {
        QJsonObject item = val.toObject();
        int row = m_panelTable->rowCount();
        m_panelTable->insertRow(row);

        m_panelTable->setItem(row, 0, new QTableWidgetItem(item["name"].toString()));
        m_panelTable->setItem(row, 1, new QTableWidgetItem(item["desc"].toString()));

        QComboBox *typeCombo = new QComboBox();
        typeCombo->addItems({"command", "link"});
        QString type = item["type"].toString("command");
        int idx = typeCombo->findText(type);
        if (idx >= 0) typeCombo->setCurrentIndex(idx);
        m_panelTable->setCellWidget(row, 2, typeCombo);

        QString data = (type == "command") ? item["command"].toString() : item["link"].toString();
        m_panelTable->setItem(row, 3, new QTableWidgetItem(data));

        QCheckBox *adminCheck = new QCheckBox();
        adminCheck->setChecked(item["only_admin"].toBool(false));
        m_panelTable->setCellWidget(row, 4, adminCheck);
    }
}
QJsonObject MenuPanelWidget::buildPanelJson()
{
    QJsonObject obj;

    obj["scope"] = currentScope();
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
        if (type == "link")
            itemObj["link"] = data;
        itemObj["only_admin"] = onlyAdmin;
        items.append(itemObj);
    }

    QString remark;
    if (!m_currentPanelId.isEmpty() && m_panelCache.contains(m_currentPanelId)) {
        remark = m_panelCache[m_currentPanelId]["panel"].toObject()["remark"].toString();
    } else {
        // 新建面板：从左侧列表当前选中项获取备注（如果存在）
        int row = m_panelListTable->currentRow();
        if (row >= 0) {
            QTableWidgetItem *item = m_panelListTable->item(row, 0);
            if (item) remark = item->text();
        }
        if (remark.isEmpty()) remark = "新面板"; // 默认
    }
    QJsonObject panel;
    panel["items"] = items;
    panel["remark"] = remark;
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

    QJsonObject panelData = buildPanelJson();

    if (m_currentPanelId.isEmpty()) {

        updateStatus("正在创建面板...");
        m_botClient->createPanel(panelData, [this](const QString &resp, QNetworkReply::NetworkError err) {
            QMetaObject::invokeMethod(this, [this, resp, err]() {
                if (err == QNetworkReply::NoError) {
                    QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
                    if (doc.isObject()) {
                        QString newId = doc.object()["panel_id"].toString();
                        if (!newId.isEmpty()) {

                            loadPanelsForCurrentScope();
                            updateStatus("面板创建成功，ID: " + newId);
                            return;
                        }
                    }
                    updateStatus("面板创建成功，但未获取到ID", true);
                } else {
                    updateStatus("面板创建失败: " + resp, true);
                }
            });
        });
    } else {
        // 更新已有面板
        updateStatus("正在更新面板...");
        m_botClient->updatePanel(m_currentPanelId, panelData, [this](const QString &resp, QNetworkReply::NetworkError err) {
            QMetaObject::invokeMethod(this, [this, err, resp]() {
                if (err == QNetworkReply::NoError) {
                    loadPanelsForCurrentScope();
                    updateStatus("面板更新成功");
                } else {
                    updateStatus("面板更新失败: " + resp, true);
                }
            });
        });
    }
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
                updateStatus("加载菜单失败.."+resp, true);
            }
        });
    });

    loadPanelsForCurrentScope();

}
