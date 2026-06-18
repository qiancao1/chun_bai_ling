#include "addaccountdialog.h"
#include "PluginPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QTextEdit>
#include <QGroupBox>
#include <QStackedWidget>
#include <QScrollArea>
#include <QRadioButton>

extern QList<PluginInfo> m_pluginList;

AddAccountDialog::AddAccountDialog(const AccountInfo &info, QWidget *parent)
    : QDialog(parent) {
    setupUI();

    // 填充数据
    m_appidEdit->setText(info.appid);
    m_secretEdit->setText(info.secret);
    m_botqqEdit->setText(info.botqq);
    m_wsAddressEdit->setText(info.wsAddress);

    m_botsettextEdit->setText(info.botsettext);

    if (info.type == 0)
        m_wsRadio->setChecked(true);
    else
        m_webhookRadio->setChecked(true);

    m_arkCheckBox->setChecked(info.ark);
    m_markdownCheckBox->setChecked(info.markdown);
    m_welcomeEdit->setPlainText(info.welcomeMsg);
    m_fallbackEdit->setPlainText(info.fallbackReply);


    setIntentsMask(info.wsIntents);


}

void AddAccountDialog::setupUI() {
    setWindowTitle("账号详细设置");
    resize(760, 738);
    setModal(true);


    setStyleSheet(R"(
        AddAccountDialog {
            background: #F7EFE5;
        }
        QWidget#formContent {
            background: #F7EFE5;
        }
        QScrollArea#accountFormScroll {
            background: #F7EFE5;
            border: none;
        }
        QWidget#dialogButtonBar {
            background: #F7EFE5;
        }
        QGroupBox {
            background: #FFF9F2;
            border: 1px solid #EEDCCA;
            border-radius: 12px;
            margin-top: 12px;
            padding: 14px 12px 12px 12px;
            color: #17202A;
            font-weight: 700;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 14px;
            padding: 0 8px;
            background: #FFF9F2;
            color: #17202A;
        }
        QLabel {
            background: transparent;
            color: #344054;
            font-size: 13px;
        }
        QLineEdit, QTextEdit {
            background: #FFFCF8;
            border: 1px solid #E6D4C0;
            border-radius: 8px;
            padding: 7px 9px;
            color: #17202A;
            selection-background-color: #7CB7FF;
        }
        QLineEdit:focus, QTextEdit:focus {
            border: 1px solid #FFB066;
            background: #FFFFFF;
        }
        QCheckBox, QRadioButton {
            background: transparent;
            color: #344054;
            spacing: 8px;
            min-height: 24px;
        }
        QScrollArea {
            border: none;
            background: #F7EFE5;
        }
        QScrollArea > QWidget > QWidget {
            background: #F7EFE5;
        }
        QStackedWidget {
            background: transparent;
            border: none;
        }
        QWidget#softScrollContent {
            background: #FFF9F2;
        }
        QPushButton {
            background: #FFF0DE;
            color: #FF7F32;
            border: none;
            border-radius: 10px;
            padding: 7px 20px;
            font-weight: 700;
        }
        QPushButton:hover {
            background: #FFE5C8;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 2px;
        }
        QScrollBar::handle:vertical {
            background: #C9D5E3;
            border-radius: 5px;
            min-height: 36px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QRadioButton::indicator {
            width: 14px;
            height: 14px;
            border-radius: 7px;
            border: 1px solid #C0B5A6;
            background: #FFFFFF;
        }
        QRadioButton::indicator:checked {
            background: #EAB2B6;      /* 选中时的圆点颜色（橙色） */
            border: 1px solid #FF7F32;
        }
    )");
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QScrollArea *formScroll = new QScrollArea;
    formScroll->setObjectName("accountFormScroll");
    formScroll->setWidgetResizable(true);
    formScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    formScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget *contentWidget = new QWidget;
    contentWidget->setObjectName("formContent");
    QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(4, 4, 4, 4);
    contentLayout->setSpacing(4);

    // ---------- 基本信息 Group ----------
    QGroupBox *basicGroup = new QGroupBox("基本信息");
    QGridLayout *basicLayout = new QGridLayout(basicGroup);
    basicLayout->setContentsMargins(4, 4, 4, 4);
    basicLayout->setHorizontalSpacing(4);
    basicLayout->setVerticalSpacing(4);

    // 定义列宽：列0=标签列(固定118px)，列1=第1个输入框，列2=间隔，列3=第2个输入框，列4=间隔，列5=第3个输入框
    // 让列1、3、5的宽度比例相同，且与下面单行的输入框对齐（下面单行的输入框会跨越多列）
    basicLayout->setColumnMinimumWidth(0, 118);
    basicLayout->setColumnStretch(1, 1);   // 第1个输入框可拉伸
    basicLayout->setColumnMinimumWidth(2, 8);   // 固定间距
    basicLayout->setColumnStretch(3, 1);   // 第2个输入框
    basicLayout->setColumnMinimumWidth(4, 8);
    basicLayout->setColumnStretch(5, 1);   // 第3个输入框

    int row = 0;

    // 辅助函数：创建带标签和输入框的一对（用于下面单行）
    auto addSingleRow = [&](const QString &labelText, QLineEdit *edit) {
        QLabel *label = new QLabel(labelText);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setFixedHeight(32);   // 与输入框同高
        edit->setFixedHeight(32);
        basicLayout->addWidget(label, row, 0);
        basicLayout->addWidget(edit, row, 1, 1, 5);  // 输入框跨第1~5列
        row++;
    };

    // 第一行：三个标签+三个输入框
    m_appidEdit = new QLineEdit;
    m_secretEdit = new QLineEdit;
    m_botqqEdit = new QLineEdit;
    const int editHeight = 32;
    m_appidEdit->setFixedHeight(editHeight);
    m_secretEdit->setFixedHeight(editHeight);
    m_botqqEdit->setFixedHeight(editHeight);
    m_appidEdit->setMaximumWidth(100);
    m_botqqEdit->setMaximumWidth(100);
    QLabel *labelAppId = new QLabel("AppID:");
    labelAppId->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    labelAppId->setFixedHeight(editHeight);
    QLabel *labelSecret = new QLabel("Secret:");
    labelSecret->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    labelSecret->setFixedHeight(editHeight);
    QLabel *labelBotQQ = new QLabel("Bot QQ:");
    labelBotQQ->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    labelBotQQ->setFixedHeight(editHeight);

    basicLayout->addWidget(labelAppId, row, 0);
    basicLayout->addWidget(m_appidEdit, row, 1);

    basicLayout->addWidget(labelBotQQ, row, 2);
    basicLayout->addWidget(m_botqqEdit, row, 3);
    basicLayout->addWidget(labelSecret, row, 4);
    basicLayout->addWidget(m_secretEdit, row, 5);
    row++;

    // 后续单行使用 addSingleRow
    m_wsAddressEdit = new QLineEdit;
    m_wsAddressEdit->setPlaceholderText("留空则使用腾讯官方地址");
    addSingleRow("WS 地址:", m_wsAddressEdit);

    m_botsettextEdit = new QLineEdit;
    addSingleRow("QQ 回调文本:", m_botsettextEdit);

    // 连接设置（单选按钮，需要单独处理）
    QWidget *typeWidget = new QWidget;
    QGridLayout *typeLayout = new QGridLayout(typeWidget);
    typeLayout->setContentsMargins(0, 0, 0, 0);
    typeLayout->setHorizontalSpacing(16);
    m_wsRadio = new QRadioButton("WebSocket");
    m_webhookRadio = new QRadioButton("Webhook");
    m_arkCheckBox = new QCheckBox("启用Ark");
    m_markdownCheckBox = new QCheckBox("启用Markdown");
    m_wsRadio->setChecked(true);
    typeLayout->addWidget(m_wsRadio, 0, 0);
    typeLayout->addWidget(m_webhookRadio, 0, 1);
    typeLayout->addWidget(m_arkCheckBox, 0, 2);
    typeLayout->addWidget(m_markdownCheckBox, 0, 3);
    // typeWidget 需要占满第1~5列
    basicLayout->addWidget(new QLabel("连接设置:"), row, 0);
    basicLayout->addWidget(typeWidget, row, 1, 1, 5);
    row++;


    contentLayout->addWidget(basicGroup);

    m_wsConfigWidget = new QWidget;
    setupWsIntentsGroup();
    contentLayout->addWidget(m_wsIntentsGroup);
    contentLayout->addWidget(m_wsConfigWidget);


    QGroupBox *replyGroup = new QGroupBox("回复设置");
    QGridLayout *replyLayout = new QGridLayout(replyGroup);
    replyLayout->setContentsMargins(4, 4, 4, 4);
    replyLayout->setHorizontalSpacing(14);
    replyLayout->setVerticalSpacing(10);
    QLabel *welcomeLabel = new QLabel("被添加时欢迎词:");
    m_welcomeEdit = new QTextEdit;
    m_welcomeEdit->setMinimumHeight(84);
    m_welcomeEdit->setMaximumHeight(120);
    QLabel *fallbackLabel = new QLabel("指令未处理回应:");
    m_fallbackEdit = new QTextEdit;
    m_fallbackEdit->setMinimumHeight(84);
    m_fallbackEdit->setMaximumHeight(120);
    replyLayout->addWidget(welcomeLabel, 0, 0);
    replyLayout->addWidget(m_welcomeEdit, 1, 0);
    replyLayout->addWidget(fallbackLabel, 0, 1);
    replyLayout->addWidget(m_fallbackEdit, 1, 1);
    replyLayout->setColumnStretch(0, 1);
    replyLayout->setColumnStretch(1, 1);
    contentLayout->addWidget(replyGroup);
    contentLayout->addStretch();

    formScroll->setWidget(contentWidget);
    outerLayout->addWidget(formScroll, 1);

    QWidget *buttonBar = new QWidget;
    buttonBar->setObjectName("dialogButtonBar");
    QHBoxLayout *btnLayout = new QHBoxLayout(buttonBar);
    btnLayout->setContentsMargins(4, 4, 4, 4);
    QPushButton *okBtn = new QPushButton("确定");
    QPushButton *cancelBtn = new QPushButton("取消");
    btnLayout->addStretch();
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    outerLayout->addWidget(buttonBar);

    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}


void AddAccountDialog::setEmbeddedMode(bool embedded) {
    if (embedded) {
        setWindowFlags(Qt::Widget);
        setModal(false);
        setMinimumSize(0, 0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

}

void AddAccountDialog::setupWsIntentsGroup() {
    m_wsIntentsGroup = new QGroupBox("订阅事件 (WebSocket)");
    m_wsIntentsGroup->setContentsMargins(4, 4, 4, 4);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_wsIntentsGroup);
    groupLayout->setContentsMargins(4, 4, 4, 4);
    QScrollArea *scroll = new QScrollArea;

    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(150);
    scroll->setMaximumHeight(220);
    QWidget *scrollWidget = new QWidget;
    scrollWidget->setObjectName("softScrollContent");
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollWidget);
    scrollLayout->setContentsMargins(4, 4, 4, 4);
    scrollLayout->setSpacing(6);

    struct IntentItem { QString name; int mask; };
    QList<IntentItem> intents = {
                                 {"GUILDS(频道事件)", 1<<0},
                                 {"GUILD_MEMBERS(成员加入)", 1<<1},
                                 {"GUILD_MESSAGES(**私域**无艾特)", 1<<9},
                                 {"GUILD_MESSAGE_REACTIONS(添加表情)", 1<<10},
                                 {"DIRECT_MESSAGE(频道私聊事件)", 1<<12},
                                 {"GROUP_MEMBER  (群成员添加退出)", 1<<24},
                                 {"GROUP_AND_C2C_EVENT  (私聊和群聊事件)", 1<<25},
                                 {"INTERACTION(互动事件)", 1<<26},
                                 {"MESSAGE_AUDIT (审核事件)", 1<<27},
                                 {"FORUMS_EVENT(**私域**论坛事件)", 1<<28},
                                 {"AUDIO_ACTION(频道直播间相关)", 1<<29},
                                 {"PUBLIC_GUILD_MESSAGES(频道艾特事件)", 1<<30},
                                 };
    for (const auto &item : intents) {
        QCheckBox *chk = new QCheckBox(item.name);
        chk->setProperty("mask", item.mask);
        scrollLayout->addWidget(chk);
        m_intentCheckboxes.append(chk);
    }
    scrollLayout->addStretch();
    scroll->setWidget(scrollWidget);
    groupLayout->addWidget(scroll);
}

int AddAccountDialog::computeIntentsMask() const {
    int mask = 0;
    for (auto chk : m_intentCheckboxes) {
        if (chk->isChecked())
            mask |= chk->property("mask").toInt();
    }
    return mask;
}

void AddAccountDialog::setIntentsMask(int mask) {
    if(mask==0)
        mask = 1174409216;
    for (auto chk : std::as_const(m_intentCheckboxes)) {
        int m = chk->property("mask").toInt();
        chk->setChecked((mask & m) != 0);
    }
}






AccountInfo AddAccountDialog::getAccountInfo() const {
    AccountInfo info;

    info.appid = m_appidEdit->text();
    info.appid_int =info.appid.toInt();
    info.secret = m_secretEdit->text();
    info.botqq = m_botqqEdit->text();
    info.wsAddress = m_wsAddressEdit->text();
    info.botsettext = m_botsettextEdit->text();

    info.type = m_wsRadio->isChecked() ? 0 : 1;
    info.ark = m_arkCheckBox->isChecked();
    info.markdown = m_markdownCheckBox->isChecked();


    info.welcomeMsg = m_welcomeEdit->toPlainText();
    info.fallbackReply = m_fallbackEdit->toPlainText();
    info.wsIntents = computeIntentsMask();


    return info;
}