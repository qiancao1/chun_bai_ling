#include "ButtonEditor.h"
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonObject>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>

// -------------------------- ButtonData --------------------------
ButtonData::ButtonData() : id(-1), style(0), actionType(0), permissionType(2),
    reply(false), enter(false), anchor(0), subscribeTemplateId(0) {}

bool ButtonData::fromJson(const QJsonObject &obj) {
    buttonId = obj["id"].toString();
    QJsonObject renderData = obj["render_data"].toObject();
    label = renderData["label"].toString();
    visitedLabel = renderData["visited_label"].toString();
    style = renderData["style"].toInt(0);
    QJsonObject action = obj["action"].toObject();
    actionType = action["type"].toInt(0);
    actionData = action["data"].toString();
    unsupportTips = action["unsupport_tips"].toString();
    reply = action["reply"].toBool(false);
    enter = action["enter"].toBool(false);
    anchor = action["anchor"].toInt(0);
    QJsonObject permission = action["permission"].toObject();
    permissionType = permission["type"].toInt(2);
    specifyUserIds.clear();
    for (const QJsonValue &v : permission["specify_user_ids"].toArray())
        specifyUserIds.append(v.toString());
    specifyRoleIds.clear();
    for (const QJsonValue &v : permission["specify_role_ids"].toArray())
        specifyRoleIds.append(v.toString());

    // 解析 modal
    QJsonObject modal = action["modal"].toObject();
    modalContent = modal["content"].toString();
    modalConfirmText = modal["confirm_text"].toString();
    modalCancelText = modal["cancel_text"].toString();

    // 解析 subscribe_data
    QJsonObject subscribe = action["subscribe_data"].toObject();
    QJsonArray templates = subscribe["template_ids"].toArray();
    if (!templates.isEmpty()) {
        QJsonObject firstTemplate = templates[0].toObject();
        subscribeTemplateId = firstTemplate["template_id"].toInt();
        subscribeCustomId = firstTemplate["custom_template_id"].toString();
    }
    return true;
}

QJsonObject ButtonData::toJson() const {
    QJsonObject result;
    if (!buttonId.isEmpty()) result["id"] = buttonId;
    QJsonObject renderData;
    renderData["label"] = label;
    renderData["visited_label"] = visitedLabel;
    if(style==9999)
    {
        QJsonObject renderData2;
        renderData2["font_size"] = "small";
        renderData["style"] = renderData2;
    }else{
        renderData["style"] = style;
    }

    result["render_data"] = renderData;
    QJsonObject action;
    action["type"] = actionType;
    action["data"] = actionData;
    action["unsupport_tips"] = unsupportTips;
    if (reply) action["reply"] = reply;
    if (enter) action["enter"] = enter;
    if (anchor > 0) action["anchor"] = anchor;
    QJsonObject permission;
    permission["type"] = permissionType;
    if (!specifyUserIds.isEmpty()) {
        QJsonArray arr;
        for (const QString &uid : specifyUserIds) arr.append(uid);
        permission["specify_user_ids"] = arr;
    }
    if (!specifyRoleIds.isEmpty()) {
        QJsonArray arr;
        for (const QString &rid : specifyRoleIds) arr.append(rid);
        permission["specify_role_ids"] = arr;
    }
    action["permission"] = permission;

    // 模态框
    if (!modalContent.isEmpty()) {
        QJsonObject modal;
        modal["content"] = modalContent;
        if (!modalConfirmText.isEmpty()) modal["confirm_text"] = modalConfirmText;
        if (!modalCancelText.isEmpty()) modal["cancel_text"] = modalCancelText;
        action["modal"] = modal;
    }
    // 订阅数据
    if (subscribeTemplateId != 0 || !subscribeCustomId.isEmpty()) {
        QJsonObject subscribe;
        QJsonArray templateIds;
        QJsonObject tpl;
        if (subscribeTemplateId != 0) tpl["template_id"] = subscribeTemplateId;
        if (!subscribeCustomId.isEmpty()) tpl["custom_template_id"] = subscribeCustomId;
        templateIds.append(tpl);
        subscribe["template_ids"] = templateIds;
        action["subscribe_data"] = subscribe;
    }
    result["action"] = action;
    return result;
}

// -------------------------- DragButton --------------------------
DragButton::DragButton(int id, const QString &text, QWidget *parent)
    : QPushButton(text, parent), m_id(id) {
    setMinimumSize(80, 40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAcceptDrops(true);
}

void DragButton::setButtonData(const ButtonData &data) {
    m_data = data;
    setText(data.label);

    if (data.style == 0) {
        // 灰色线框样式
        setStyleSheet(
            "QPushButton {"
            "    background-color: #f0f0f0;"
            "    border: 1px solid #ccc;"
            "    border-radius: 4px;"
            "}"
            "QPushButton:hover {"
            "    background-color: #e0e0e0;"
            "    border: 1px solid #999;"
            "}"
            "QPushButton:focus {"
            "    outline: none;"
            "    border: 2px solid #1976d2;"
            "    background-color: #e8f0fe;"
            "}"
            );
    } else {
        // 蓝色线框样式
        setStyleSheet(
            "QPushButton {"
            "    background-color: #e8f0fe;"
            "    border: 1px solid #1976d2;"
            "    border-radius: 4px;"
            "    color: #1976d2;"
            "}"
            "QPushButton:hover {"
            "    background-color: #d0e0fc;"
            "    border: 1px solid #0d5b9e;"
            "}"
            "QPushButton:focus {"
            "    outline: none;"
            "    border: 2px solid #0d5b9e;"
            "    background-color: #c0d4f0;"
            "}"
            );
    }
}

void DragButton::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        m_dragStartPos = event->pos();
    QPushButton::mousePressEvent(event);
}

void DragButton::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPos).manhattanLength() < QApplication::startDragDistance())
        return;
    QDrag *drag = new QDrag(this);
    QMimeData *mimeData = new QMimeData;
    mimeData->setText(text());
    mimeData->setData("application/x-button-id", QByteArray::number(m_id));
    drag->setMimeData(mimeData);
    drag->exec(Qt::MoveAction);
}

// -------------------------- ButtonPropertyPanel --------------------------


ButtonPropertyPanel::ButtonPropertyPanel(QWidget *parent)
    : QWidget(parent), m_currentButton(nullptr)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);

    // 使用 QTabWidget 分组，更美观
    QTabWidget *tabWidget = new QTabWidget(this);

    // ========== 基础属性页 ==========
    QWidget *basicPage = new QWidget;
    QFormLayout *basicLayout = new QFormLayout(basicPage);
    basicLayout->setSpacing(4);

    m_buttonIdEdit = new QLineEdit;
    basicLayout->addRow("按钮ID (id):", m_buttonIdEdit);

    m_labelEdit = new QLineEdit;
    basicLayout->addRow("文字 (label):", m_labelEdit);
    m_dataEdit = new QLineEdit;
    basicLayout->addRow("点击后文字 (action.data):", m_dataEdit);
    m_visitedLabelEdit = new QLineEdit;
    basicLayout->addRow("其他标签 (visited_label):", m_visitedLabelEdit);

    m_styleCombo = new QComboBox;
    m_styleCombo->addItem("灰色线框 (0)", 0);
    m_styleCombo->addItem("蓝色线框 (1)", 1);
    m_styleCombo->addItem("图标 (2)", 2);
    m_styleCombo->addItem("灰线红文本 (3)", 3);
    m_styleCombo->addItem("蓝低白文本 (4)", 4);
    m_styleCombo->addItem("保留 (5)", 5);
    m_styleCombo->addItem("小按钮 (n)", 9999);
    basicLayout->addRow("按钮样式 (style):", m_styleCombo);

    m_actionTypeCombo = new QComboBox;
    m_actionTypeCombo->addItem("跳转按钮 (0)", 0);
    m_actionTypeCombo->addItem("回调按钮 (1)", 1);
    m_actionTypeCombo->addItem("指令按钮 (2)", 2);
    m_actionTypeCombo->addItem("mqqapi (3)", 3);
    m_actionTypeCombo->addItem("订阅 (4)", 4);
    basicLayout->addRow("操作类型 (action.type):", m_actionTypeCombo);



    m_unsupportTipsEdit = new QLineEdit;
    basicLayout->addRow("不支持提示 (unsupport_tips):", m_unsupportTipsEdit);

    m_replyCheck = new QCheckBox("引用回复 (reply)");
    m_enterCheck = new QCheckBox("自动发送 (enter)");
    m_anchorCheck = new QCheckBox("唤起选图器 (anchor)");
    basicLayout->addRow(m_replyCheck,m_enterCheck);
    basicLayout->addRow(m_anchorCheck);

    tabWidget->addTab(basicPage, "基础");

    // ========== 权限页 ==========
    QWidget *permPage = new QWidget;
    QFormLayout *permLayout = new QFormLayout(permPage);

    m_permissionTypeCombo = new QComboBox;
    m_permissionTypeCombo->addItem("指定用户 (0)", 0);
    m_permissionTypeCombo->addItem("仅管理者 (1)", 1);
    m_permissionTypeCombo->addItem("所有人 (2)", 2);
    m_permissionTypeCombo->addItem("指定身份组 (3)", 3);
    permLayout->addRow("权限类型:", m_permissionTypeCombo);

    m_userIdListEdit = new QTextEdit;
    m_userIdListEdit->setMaximumHeight(150);
    m_userIdListEdit->setPlaceholderText("每行一个用户ID");
    permLayout->addRow("用户ID列表:", m_userIdListEdit);

    m_roleIdListEdit = new QTextEdit;
    m_roleIdListEdit->setMaximumHeight(150);
    m_roleIdListEdit->setPlaceholderText("每行一个身份组ID");
    permLayout->addRow("身份组列表:", m_roleIdListEdit);

    tabWidget->addTab(permPage, "权限");

    // ========== 模态框页 ==========
    QWidget *modalPage = new QWidget;
    QFormLayout *modalLayout = new QFormLayout(modalPage);

    m_modalContentEdit = new QLineEdit;
    m_modalContentEdit->setPlaceholderText("弹窗内容，最多40字符");
    modalLayout->addRow("弹窗内容 (content):", m_modalContentEdit);

    m_modalConfirmEdit = new QLineEdit;
    m_modalConfirmEdit->setPlaceholderText("确认按钮文本，最多4字");
    modalLayout->addRow("确认按钮文本:", m_modalConfirmEdit);

    m_modalCancelEdit = new QLineEdit;
    m_modalCancelEdit->setPlaceholderText("取消按钮文本，最多4字");
    modalLayout->addRow("取消按钮文本:", m_modalCancelEdit);


    m_subscribeTemplateIdEdit = new QLineEdit;
    m_subscribeTemplateIdEdit->setPlaceholderText("模板ID");
    modalLayout->addRow("模板ID (template_id):", m_subscribeTemplateIdEdit);

    m_subscribeCustomIdEdit = new QLineEdit;
    m_subscribeCustomIdEdit->setPlaceholderText("自定义订阅ID");
    modalLayout->addRow("自定义订阅ID:", m_subscribeCustomIdEdit);
    tabWidget->addTab(modalPage, "模态框");

    mainLayout->addWidget(tabWidget);

    // 连接信号（每个控件修改后都调用 emitDataChanged）
    auto connectField = [this](QObject *widget) {
        if (auto *lineEdit = qobject_cast<QLineEdit*>(widget)) {
            connect(lineEdit, &QLineEdit::textChanged, this, &ButtonPropertyPanel::emitDataChanged);
        } else if (auto *combo = qobject_cast<QComboBox*>(widget)) {
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ButtonPropertyPanel::emitDataChanged);
        } else if (auto *checkBox = qobject_cast<QCheckBox*>(widget)) {
            connect(checkBox, &QCheckBox::stateChanged, this, &ButtonPropertyPanel::emitDataChanged);
        } else if (auto *textEdit = qobject_cast<QTextEdit*>(widget)) {
            connect(textEdit, &QTextEdit::textChanged, this, &ButtonPropertyPanel::emitDataChanged);
        }
    };

    connectField(m_buttonIdEdit);
    connectField(m_labelEdit);
    connectField(m_visitedLabelEdit);
    connectField(m_styleCombo);
    connectField(m_actionTypeCombo);
    connectField(m_dataEdit);
    connectField(m_unsupportTipsEdit);
    connectField(m_replyCheck);
    connectField(m_enterCheck);
    connectField(m_anchorCheck);
    connectField(m_permissionTypeCombo);
    connectField(m_userIdListEdit);
    connectField(m_roleIdListEdit);
    connectField(m_modalContentEdit);
    connectField(m_modalConfirmEdit);
    connectField(m_modalCancelEdit);
    connectField(m_subscribeTemplateIdEdit);
    connectField(m_subscribeCustomIdEdit);
}

void ButtonPropertyPanel::setCurrentButton(DragButton *button) {
    m_currentButton = button;
    updateUiFromCurrentButton();
    setEnabled(true);
}

void ButtonPropertyPanel::clearSelection() {
    m_currentButton = nullptr;
    setEnabled(false);
    // 清空所有输入
    m_buttonIdEdit->clear();
    m_labelEdit->clear();
    m_visitedLabelEdit->clear();
    m_dataEdit->clear();
    m_unsupportTipsEdit->clear();
    m_replyCheck->setChecked(false);
    m_enterCheck->setChecked(false);
    m_anchorCheck->setChecked(false);
    m_userIdListEdit->clear();
    m_roleIdListEdit->clear();
    m_modalContentEdit->clear();
    m_modalConfirmEdit->clear();
    m_modalCancelEdit->clear();
    m_subscribeTemplateIdEdit->clear();
    m_subscribeCustomIdEdit->clear();
    m_permissionTypeCombo->setCurrentIndex(2); // 默认所有人
}

void ButtonPropertyPanel::updateUiFromCurrentButton() {
    if (!m_currentButton) return;
    const ButtonData &data = m_currentButton->getButtonData();

    m_buttonIdEdit->setText(data.buttonId);
    m_labelEdit->setText(data.label);
    m_visitedLabelEdit->setText(data.visitedLabel);
    m_dataEdit->setText(data.actionData);
    m_unsupportTipsEdit->setText(data.unsupportTips);
    m_replyCheck->setChecked(data.reply);
    m_enterCheck->setChecked(data.enter);
    m_anchorCheck->setChecked(data.anchor > 0);

    int styleIdx = m_styleCombo->findData(data.style);
    if (styleIdx >= 0) m_styleCombo->setCurrentIndex(styleIdx);
    int actionIdx = m_actionTypeCombo->findData(data.actionType);
    if (actionIdx >= 0) m_actionTypeCombo->setCurrentIndex(actionIdx);
    int permIdx = m_permissionTypeCombo->findData(data.permissionType);
    if (permIdx >= 0) m_permissionTypeCombo->setCurrentIndex(permIdx);

    m_userIdListEdit->setPlainText(data.specifyUserIds.join("\n"));
    m_roleIdListEdit->setPlainText(data.specifyRoleIds.join("\n"));

    // 模态框
    m_modalContentEdit->setText(data.modalContent);
    m_modalConfirmEdit->setText(data.modalConfirmText);
    m_modalCancelEdit->setText(data.modalCancelText);

    // 订阅数据
    if (data.subscribeTemplateId != 0)
        m_subscribeTemplateIdEdit->setText(QString::number(data.subscribeTemplateId));
    else
        m_subscribeTemplateIdEdit->clear();
    m_subscribeCustomIdEdit->setText(data.subscribeCustomId);
}

void ButtonPropertyPanel::emitDataChanged() {
    if (!m_currentButton) return;

    ButtonData data = m_currentButton->getButtonData();

    // 基础
    data.buttonId = m_buttonIdEdit->text();
    data.label = m_labelEdit->text();
    data.visitedLabel = m_visitedLabelEdit->text();
    data.style = m_styleCombo->currentData().toInt();
    data.actionType = m_actionTypeCombo->currentData().toInt();
    data.actionData = m_dataEdit->text();
    data.unsupportTips = m_unsupportTipsEdit->text();
    data.reply = m_replyCheck->isChecked();
    data.enter = m_enterCheck->isChecked();
    data.anchor = m_anchorCheck->isChecked() ? 1 : 0;

    // 权限
    data.permissionType = m_permissionTypeCombo->currentData().toInt();
    data.specifyUserIds = m_userIdListEdit->toPlainText().split("\n", Qt::SkipEmptyParts);
    data.specifyRoleIds = m_roleIdListEdit->toPlainText().split("\n", Qt::SkipEmptyParts);

    // 模态框
    data.modalContent = m_modalContentEdit->text();
    data.modalConfirmText = m_modalConfirmEdit->text();
    data.modalCancelText = m_modalCancelEdit->text();

    // 订阅数据
    bool ok;
    int tid = m_subscribeTemplateIdEdit->text().toInt(&ok);
    data.subscribeTemplateId = ok ? tid : 0;
    data.subscribeCustomId = m_subscribeCustomIdEdit->text();

    emit buttonDataChanged(m_currentButton->getId(), data);
}

// -------------------------- ButtonEditor --------------------------
ButtonEditor::ButtonEditor(QWidget *parent) : QWidget(parent), m_nextButtonId(1), m_currentSelectedButton(nullptr) {
    initUI();
    setAcceptDrops(true);   // 接受拖拽
    addNewRow();  // 初始有一行并带一个默认按钮
}

ButtonEditor::~ButtonEditor() {}

void ButtonEditor::initUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(10);

    // 主水平布局（左右分割）
    QHBoxLayout *mainHorizontal = new QHBoxLayout();
    mainLayout->addLayout(mainHorizontal, 1);

    // ========== 左侧区域（垂直布局） ==========
    QWidget *leftSide = new QWidget(this);
    leftSide->setMaximumWidth(400);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftSide);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    // 左侧上部：按钮行（水平布局）
    QHBoxLayout *toolBarLayout = new QHBoxLayout();
    toolBarLayout->setContentsMargins(0, 0, 0, 0);
    toolBarLayout->setSpacing(2);

    QPushButton *addRowBtn = new QPushButton("📄 新增一行", this);
    QPushButton *delBtn = new QPushButton("❌ 删除选中按钮", this);
    QPushButton *clearBtn = new QPushButton("🗑️ 清空所有", this);
    toolBarLayout->addWidget(addRowBtn);
    toolBarLayout->addWidget(delBtn);
    toolBarLayout->addWidget(clearBtn);
    toolBarLayout->addStretch();  // 靠左对齐
    leftLayout->addLayout(toolBarLayout);

    // 左侧下部：按钮网格滚动区域
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setStyleSheet("QScrollArea { border: 1px solid #cccccc; border-radius: 4px; }");
    m_scrollContent = new QWidget();
    m_rowsLayout = new QVBoxLayout(m_scrollContent);
    m_rowsLayout->setContentsMargins(2, 2, 2, 2);
    m_rowsLayout->setSpacing(2);
    m_rowsLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_scrollContent->setLayout(m_rowsLayout);
    m_scrollArea->setWidget(m_scrollContent);
    leftLayout->addWidget(m_scrollArea, 1);  // 占用剩余空间

    // 将左侧面板加入主水平布局
    mainHorizontal->addWidget(leftSide, 3);   // 左侧占3份

    // ========== 右侧属性面板 ==========
    m_propertyPanel = new ButtonPropertyPanel(this);
    m_propertyPanel->setMinimumWidth(400);
    // 不限制最大宽度，让它随窗口拉伸
    mainHorizontal->addWidget(m_propertyPanel, 2);   // 右侧占2份

    // ========== 底部 JSON 区域（跨左右，放在主垂直布局底部） ==========
    QWidget *bottomWidget = new QWidget(this);
    QVBoxLayout *bottomLayout = new QVBoxLayout(bottomWidget);
    QHBoxLayout *jsonBtnLayout = new QHBoxLayout();
    QPushButton *genBtn = new QPushButton("📋 生成JSON", this);
    QPushButton *loadBtn = new QPushButton("📂 从JSON加载", this);
    QPushButton *copyBtn = new QPushButton("📄 复制", this);
    QPushButton *saveBtn = new QPushButton("💾 保存", this);
    jsonBtnLayout->addWidget(genBtn);
    jsonBtnLayout->addWidget(loadBtn);
    jsonBtnLayout->addWidget(copyBtn);
    jsonBtnLayout->addWidget(saveBtn);
    jsonBtnLayout->addStretch();
    m_jsonPreview = new QTextEdit(this);
    m_jsonPreview->setFont(QFont("Monospace", 10));
    m_jsonPreview->setMinimumHeight(150);
    bottomLayout->addLayout(jsonBtnLayout);
    bottomLayout->addWidget(m_jsonPreview);
    mainLayout->addWidget(bottomWidget, 0);

    // 连接信号
    connect(addRowBtn, &QPushButton::clicked, this, &ButtonEditor::addNewRow);
    connect(delBtn, &QPushButton::clicked, this, &ButtonEditor::deleteSelectedButton);
    connect(clearBtn, &QPushButton::clicked, this, &ButtonEditor::clearAllButtons);
    connect(genBtn, &QPushButton::clicked, this, &ButtonEditor::generateJson);
    connect(loadBtn, &QPushButton::clicked, this, &ButtonEditor::loadFromJson);
    connect(copyBtn, &QPushButton::clicked, this, &ButtonEditor::copyToClipboard);
    connect(saveBtn, &QPushButton::clicked, this, &ButtonEditor::saveToFile);
    connect(m_propertyPanel, &ButtonPropertyPanel::buttonDataChanged, this, &ButtonEditor::onButtonDataChanged);
}

void ButtonEditor::onButtonClicked(DragButton *button) {
    m_currentSelectedButton = button;
    m_propertyPanel->setCurrentButton(button);
}

void ButtonEditor::rebuildAllRows() {
    // 清空所有行 Widget
    QLayoutItem *item;
    while ((item = m_rowsLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_rowWidgets.clear();

    for (int row = 0; row < m_buttonRows.size(); ++row) {
        QWidget *rowWidget = createRowWidget(row);
        m_rowsLayout->addWidget(rowWidget);
        m_rowWidgets.append(rowWidget);
    }
    m_rowsLayout->addStretch();  // 弹簧占位，使内容靠上
}

QWidget* ButtonEditor::createRowWidget(int rowIndex) {
    QWidget *rowWidget = new QWidget();
    QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
    rowLayout->setContentsMargins(0,0,0,0);
    rowLayout->setSpacing(10);
    rowLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const QList<DragButton*> &buttons = m_buttonRows[rowIndex];
    for (DragButton *btn : buttons) {
        btn->setFixedWidth(100);   // 固定宽度100px
        rowLayout->addWidget(btn);
    }

    QPushButton *addBtn = new QPushButton("➕");
    addBtn->setFixedSize(40, 26);
    addBtn->setToolTip("在本行末尾添加按钮");
    connect(addBtn, &QPushButton::clicked, [this, rowIndex]() {
        addButtonToRow(rowIndex);
    });
    rowLayout->addWidget(addBtn);
    rowLayout->addStretch();
    return rowWidget;
}

void ButtonEditor::addButtonToRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= m_buttonRows.size()) return;
    QList<DragButton*> &row = m_buttonRows[rowIndex];
    if (row.size() >= MAX_COLUMNS) {
        QMessageBox::warning(this, "提示", QString("每行最多 %1 个按钮").arg(MAX_COLUMNS));
        return;
    }
    int newId = m_nextButtonId++;
    ButtonData data = getDefaultButtonData(newId);
    data.id = newId;
    DragButton *btn = new DragButton(newId, data.label, this);
    btn->setButtonData(data);
    //btn->setMinimumWidth(350);
    connect(btn, &QPushButton::clicked, [this, btn]() { onButtonClicked(btn); });

    row.append(btn);
    m_buttonMap[newId] = btn;

    refreshRow(rowIndex);
    updateButtonWidths();
}

void ButtonEditor::refreshRow(int rowIndex) {
    if (rowIndex < 0 || rowIndex >= m_rowWidgets.size()) return;
    QWidget *oldWidget = m_rowWidgets[rowIndex];
    QWidget *newWidget = createRowWidget(rowIndex);
    m_rowsLayout->insertWidget(rowIndex, newWidget);
    m_rowWidgets[rowIndex] = newWidget;
    oldWidget->deleteLater();
}

void ButtonEditor::addNewRow() {
    if (m_buttonRows.size() >= MAX_ROWS) {
        QMessageBox::warning(this, "提示", QString("最多 %1 行").arg(MAX_ROWS));
        return;
    }
    m_buttonRows.append(QList<DragButton*>());
    // 在新行末尾添加一个默认按钮
    addButtonToRow(m_buttonRows.size() - 1);
    // 强制重建布局以保证新行显示（因为新增了一行，需要重新构建整个垂直布局）
    rebuildAllRows();
    updateButtonWidths();
}

void ButtonEditor::deleteSelectedButton() {
    if (!m_currentSelectedButton) {
        QMessageBox::information(this, "提示", "请先点击选中要删除的按钮");
        return;
    }
    int id = m_currentSelectedButton->getId();
    for (int r = 0; r < m_buttonRows.size(); ++r) {
        int c = m_buttonRows[r].indexOf(m_currentSelectedButton);
        if (c >= 0) {
            m_buttonRows[r].removeAt(c);
            m_buttonMap.remove(id);
            m_currentSelectedButton->deleteLater();
            m_currentSelectedButton = nullptr;
            if (m_buttonRows[r].isEmpty() && m_buttonRows.size() > 1) {
                // 删除整行
                m_buttonRows.removeAt(r);
                rebuildAllRows();
            } else {
                refreshRow(r);
            }
            m_propertyPanel->clearSelection();
            if (m_buttonRows.isEmpty()) {
                addNewRow();
            }
            updateButtonWidths();
            return;
        }
    }
}

void ButtonEditor::clearAllButtons() {
    if (QMessageBox::question(this, "确认", "确定清空所有按钮？") != QMessageBox::Yes)
        return;
    for (DragButton *btn : std::as_const(m_buttonMap)) btn->deleteLater();
    m_buttonRows.clear();
    m_buttonMap.clear();
    m_nextButtonId = 1;
    m_currentSelectedButton = nullptr;
    m_propertyPanel->clearSelection();
    addNewRow();  // 重新添加一行带一个按钮
    rebuildAllRows();
    updateButtonWidths();
}

void ButtonEditor::updateButtonWidths() {

    if (m_scrollArea->viewport()->width() <= 0) return;
    int viewportWidth = m_scrollArea->viewport()->width();

    const int spacing = 6;           // 按钮之间的间距
    const int addButtonWidth = 40;    // 每行末尾的“➕”按钮宽度
    const int leftRightMargin = 0;    // 行布局的左右边距（已在 createRowWidget 中设置为0）

    // 遍历每一行
    for (int r = 0; r < m_buttonRows.size(); ++r) {
        int btnCount = m_buttonRows[r].size();
        if (btnCount == 0) continue;

        // 该行可用总宽度 = 视口宽度 - (按钮数-1)*间距 - 加号按钮宽度 - 左右边距
        int available = viewportWidth - (btnCount - 1) * spacing - addButtonWidth - leftRightMargin * 2;
        int btnWidth = available / btnCount;
        btnWidth = qBound(10, btnWidth, 352);   // 限制最小60px，最大200px

        // 设置该行每个按钮的宽高
        for (DragButton *btn : m_buttonRows[r]) {
            btn->setMinimumWidth(btnWidth);
            btn->setMaximumWidth(btnWidth);
        }
    }
    m_scrollContent->setMinimumWidth(viewportWidth);
    m_scrollContent->resize(viewportWidth, m_scrollContent->height());
}

void ButtonEditor::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateButtonWidths();
}

// ===================== 拖拽交换支持 =====================
void ButtonEditor::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-button-id"))
        event->acceptProposedAction();
}

void ButtonEditor::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->hasFormat("application/x-button-id")) return;

    int draggedId = QString::fromUtf8(event->mimeData()->data("application/x-button-id")).toInt();
    DragButton *draggedBtn = m_buttonMap.value(draggedId, nullptr);
    if (!draggedBtn) return;

    // 获取鼠标位置下的目标按钮
    QPoint pos = event->pos();
    DragButton *targetBtn = nullptr;
    QWidget *child = childAt(pos);
    while (child && !targetBtn) {
        targetBtn = qobject_cast<DragButton*>(child);
        child = child->parentWidget();
    }
    if (!targetBtn || targetBtn == draggedBtn) return;

    // 查找拖拽按钮和目标按钮的位置 (行, 列)
    int srcRow = -1, srcCol = -1, dstRow = -1, dstCol = -1;
    for (int r = 0; r < m_buttonRows.size(); ++r) {
        for (int c = 0; c < m_buttonRows[r].size(); ++c) {
            if (m_buttonRows[r][c] == draggedBtn) { srcRow = r; srcCol = c; }
            if (m_buttonRows[r][c] == targetBtn) { dstRow = r; dstCol = c; }
        }
    }
    if (srcRow < 0 || dstRow < 0) return;

    // 交换模型中的数据
    m_buttonRows[srcRow][srcCol] = targetBtn;
    m_buttonRows[dstRow][dstCol] = draggedBtn;

    // 更新选中状态（如果选中的是被拖拽或目标按钮，更新指针）
    if (m_currentSelectedButton == draggedBtn)
        m_currentSelectedButton = targetBtn;
    else if (m_currentSelectedButton == targetBtn)
        m_currentSelectedButton = draggedBtn;

    // 刷新涉及的两行（如果同行则只刷新一行）
    if (srcRow == dstRow) {
        refreshRow(srcRow);
    } else {
        refreshRow(srcRow);
        refreshRow(dstRow);
    }

    updateButtonWidths();
    event->acceptProposedAction();
}
// ===================== 拖拽交换结束 =====================

ButtonData ButtonEditor::getDefaultButtonData(int id) const {
    ButtonData data;
    data.id = id;
    data.buttonId = QString::number(id);
    data.label = QString("按钮%1").arg(id);
    data.visitedLabel = data.label;
    data.style = 1;
    data.actionType = 2;
    data.permissionType = 2;
    data.actionData = QString("data_%1").arg(id);
    data.unsupportTips = "不支持";
    return data;
}

void ButtonEditor::generateJson() {
    QJsonObject root, keyboard, content;
    QJsonArray rowsArray;
    for (const QList<DragButton*> &row : std::as_const(m_buttonRows)) {
        QJsonObject rowObj;
        QJsonArray btnsArray;
        for (DragButton *btn : row)
            btnsArray.append(btn->getButtonData().toJson());
        rowObj["buttons"] = btnsArray;
        rowsArray.append(rowObj);
    }
    content["rows"] = rowsArray;
    keyboard["content"] = content;
    root["keyboard"] = keyboard;
    m_jsonPreview->setPlainText(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void ButtonEditor::loadFromJson() {
    QString txt = m_jsonPreview->toPlainText();
    if (txt.isEmpty()) {
        QMessageBox::warning(this, "错误", "JSON预览区为空");
        return;
    }
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(txt.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, "JSON解析错误", err.errorString());
        return;
    }
    QJsonObject root = doc.object();
    QJsonObject keyboard = root["keyboard"].toObject();
    QJsonObject content = keyboard["content"].toObject();
    QJsonArray rows = content["rows"].toArray();

    // 清空现有
    for (DragButton *btn : std::as_const(m_buttonMap)) btn->deleteLater();
    m_buttonRows.clear();
    m_buttonMap.clear();
    m_nextButtonId = 1;

    int newId = 1;
    for (const QJsonValue &rowVal : std::as_const(rows)) {
        QJsonObject obj = rowVal.toObject();
        const QJsonArray btns = obj["buttons"].toArray();
        QList<DragButton*> newRow;
        for (const QJsonValue &btnVal : btns) {
            if (newId > MAX_COLUMNS * MAX_ROWS) break;
            ButtonData data;
            data.fromJson(btnVal.toObject());
            data.id = newId;
            DragButton *btn = new DragButton(newId, data.label, this);
            btn->setButtonData(data);
            connect(btn, &QPushButton::clicked, [this, btn]() { onButtonClicked(btn); });
            newRow.append(btn);
            m_buttonMap[newId] = btn;
            newId++;
        }
        if (!newRow.isEmpty())
            m_buttonRows.append(newRow);
        if (newId > MAX_COLUMNS * MAX_ROWS || m_buttonRows.size() >= MAX_ROWS) break;
    }
    if (m_buttonRows.isEmpty()) {
        addNewRow();
    } else {
        rebuildAllRows();
        updateButtonWidths();
    }
    m_nextButtonId = newId;
    m_currentSelectedButton = nullptr;
    m_propertyPanel->clearSelection();
    QMessageBox::information(this, "成功", QString("加载了 %1 个按钮").arg(m_buttonMap.size()));
}

void ButtonEditor::copyToClipboard() {
    if (m_jsonPreview->toPlainText().isEmpty()) generateJson();
    QApplication::clipboard()->setText(m_jsonPreview->toPlainText());
    QMessageBox::information(this, "提示", "JSON已复制到剪贴板");
}

void ButtonEditor::saveToFile() {
    QString path = QFileDialog::getSaveFileName(this, "保存JSON", "", "JSON (*.json)");
    if (path.isEmpty()) return;
    if (m_jsonPreview->toPlainText().isEmpty()) generateJson();
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(m_jsonPreview->toPlainText().toUtf8());
        file.close();
        QMessageBox::information(this, "成功", "已保存");
    } else {
        QMessageBox::warning(this, "错误", "无法保存文件");
    }
}

void ButtonEditor::onButtonDataChanged(int buttonId, const ButtonData &newData) {
    if (m_buttonMap.contains(buttonId)) {
        m_buttonMap[buttonId]->setButtonData(newData);
        if (m_currentSelectedButton && m_currentSelectedButton->getId() == buttonId) {
            m_currentSelectedButton->setText(newData.label);
        }
    }
}