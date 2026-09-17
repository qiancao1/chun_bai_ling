#include "addaccountdialog.h"
#include "pluginpage.h"
#include "qq_bind_login.h"
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
#include <QMessageBox>
#include <QEvent>
#include <qurlquery.h>
extern QList<PluginInfo> m_pluginList;

QString m_taskId_login;
QDialog *m_qrDialog =nullptr;
AddAccountDialog::AddAccountDialog(const AccountInfo &info, QWidget *parent)
    : QDialog(parent) {
    setupUI();

    // 填充数据
    setAccountInfo(info);
}

void AddAccountDialog::setAccountInfo(const AccountInfo &info) {
    if (!m_appidEdit) return;

    m_appidEdit->setText(info.appid);
    // Secret 失焦时只显示 ***，真实值单独存（见 refreshSecretDisplay）
    m_secretReal = info.secret;
    m_secretMasked = false;
    m_secretEdit->setText(m_secretReal);
    // 切换/回填账号时，若 Secret 还停在编辑态，先结束焦点让它回到 *** 显示
    if (m_secretEdit->hasFocus()) m_secretEdit->clearFocus();
    refreshSecretDisplay();


    if (info.type == 0)
        m_wsRadio->setChecked(true);
    else
        m_webhookRadio->setChecked(true);

    m_markdownCheckBox->setChecked(info.markdown);
    m_markdownCheckBox_pd->setChecked(info.markdown_pd);
    m_markdownCheckBox_pd_mb->setChecked(info.markdown_pd_mb);
    if (m_sandboxCheckBox)
        m_sandboxCheckBox->setChecked(info.sandbox);
    //m_welcomeEdit->setPlainText(info.welcomeMsg);
    //m_fallbackEdit->setPlainText(info.fallbackReply);

    setIntentsMask(info.wsIntents);
}

void AddAccountDialog::focusAppId() {
    if (m_appidEdit) {
        m_appidEdit->setFocus();
        m_appidEdit->selectAll();
    }
}

bool AddAccountDialog::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_secretEdit
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)) {
        refreshSecretDisplay();
    }
    return QDialog::eventFilter(watched, event);
}

void AddAccountDialog::refreshSecretDisplay() {
    if (!m_secretEdit) return;

    if (m_secretEdit->hasFocus()) {
        // 获得焦点：把明文还原出来
        if (m_secretMasked) {
            m_secretMasked = false;
            m_secretEdit->setText(m_secretReal);
        }
        return;
    }

    // 失去焦点：用 *** 盖住内容（本来就为空就保持空，不显示 *** 以免看起来像有值）
    if (m_secretMasked) return;

    const QString cur = m_secretEdit->text();
    m_secretReal = cur;
    if (cur.isEmpty()) return;

    m_secretMasked = true;
    m_secretEdit->setText(QStringLiteral("***"));
}

void AddAccountDialog::setupUI() {
    setWindowTitle("账号详细设置");
    resize(860, 540);
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
        QWidget#plainCard {
            background: #FFF9F2;
            border: 1px solid #EEDCCA;
            border-radius: 12px;
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

    // ---------- 基本信息（普通 QVBoxLayout 容器，不用分组框标题） ----------
    QWidget *basicBox = new QWidget;
    basicBox->setObjectName("plainCard");
    basicBox->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *basicLayout = new QVBoxLayout(basicBox);
    basicLayout->setContentsMargins(12, 10, 12, 10);
    basicLayout->setSpacing(6);

    // 一行一个字段：标签（固定宽右对齐）+ 输入框（自适应）+ 可选附加按钮
    const int labelWidth = 72;
    const int editHeight = 32;

    m_appidEdit = new QLineEdit;
    m_secretEdit = new QLineEdit;

    m_appidEdit->setFixedHeight(editHeight);
    m_secretEdit->setFixedHeight(editHeight);

    QPushButton *Btnlonin = new QPushButton("扫码登录");

    auto addFieldRow = [&](const QString &text, QWidget *field, QWidget *extra = nullptr) {
        QHBoxLayout *rowLayout = new QHBoxLayout;
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        QLabel *label = new QLabel(text);
        label->setFixedWidth(labelWidth);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rowLayout->addWidget(label);
        rowLayout->addWidget(field, 1);
        if (extra)
            rowLayout->addWidget(extra, 0);
        basicLayout->addLayout(rowLayout);
    };

    addFieldRow("AppID:", m_appidEdit, Btnlonin);
    addFieldRow("Secret:", m_secretEdit);

    // Secret：未聚焦时显示 ***，点进去（获得焦点）显示真实明文
    m_secretEdit->setToolTip("未聚焦时显示 ***，点击进入显示 / 编辑真实值");
    m_secretEdit->installEventFilter(this);
    connect(m_secretEdit, &QLineEdit::textEdited, this, [this](const QString &t) {
        // 只有用户实际编辑时才同步真实值；程序 setText（掩码/回填）不会触发 textEdited
        if (!m_secretMasked) m_secretReal = t;
    });



    connect(Btnlonin, &QPushButton::clicked, [this](){

        QQBindLogin::instance().start([this](bool ok, const QString& sid, const QString& qrUrl, const QString& err) {
            if (ok) {
                m_qrDialog = this;
                m_taskId_login = sid;
                QDialog * qrDialog = new QDialog(this);
                qrDialog->setWindowTitle("扫描二维码登录");
                qrDialog->setModal(false); // 可改为 true 为模态
                qrDialog->setAttribute(Qt::WA_DeleteOnClose); // 关闭时自动删除
                QVBoxLayout *layout = new QVBoxLayout(qrDialog);
                QLabel *qrLabel = new QLabel;
                qrLabel->setAlignment(Qt::AlignCenter);
                qrLabel->setFixedSize(300, 300); // 固定大小，也可自适应
                layout->addWidget(qrLabel);
                QUrl url("https://api.2dcode.biz/v1/create-qr-code");
                QUrlQuery query;
                query.addQueryItem("data", qrUrl);
                query.addQueryItem("size", "300x300");
                url.setQuery(query);

                QNetworkAccessManager *netManager = new QNetworkAccessManager(qrDialog);
                QNetworkReply *reply = netManager->get(QNetworkRequest(url));

                connect(reply, &QNetworkReply::finished, qrDialog, [qrLabel, reply, netManager]() {
                    if (reply->error() == QNetworkReply::NoError) {
                        QByteArray imageData = reply->readAll();
                        QPixmap pixmap;
                        if (pixmap.loadFromData(imageData)) {
                            // 显示二维码，自适应大小
                            qrLabel->setPixmap(pixmap.scaled(qrLabel->size(), Qt::KeepAspectRatio));
                        } else {
                            qrLabel->setText("二维码加载失败");
                        }
                    } else {
                        qrLabel->setText("网络错误: " + reply->errorString());
                    }
                    reply->deleteLater();
                    netManager->deleteLater(); // 网络管理器也回收
                });


                qrDialog->show();


            } else {
                QMessageBox::warning(this, "登录失败", "获取绑定任务失败: " + err);
            }
        });


    });

    // 连接设置：第一行 WebSocket | Webhook | 连接沙盒，第二行三个 Markdown 开关
    m_wsRadio = new QRadioButton("WebSocket");
    m_webhookRadio = new QRadioButton("Webhook");
    m_sandboxCheckBox = new QCheckBox("连接沙盒");
    m_sandboxCheckBox->setToolTip("勾选后走腾讯沙盒环境连接");

    m_markdownCheckBox = new QCheckBox("群Markdown");
    m_markdownCheckBox_pd = new QCheckBox("频道原生Markdown");
    m_markdownCheckBox_pd_mb = new QCheckBox("频道模板");

    m_wsRadio->setChecked(true);

    QWidget *typeWidget = new QWidget;
    QVBoxLayout *typeLayout = new QVBoxLayout(typeWidget);
    typeLayout->setContentsMargins(0, 0, 0, 0);
    typeLayout->setSpacing(4);

    QHBoxLayout *typeRow1 = new QHBoxLayout;
    typeRow1->setContentsMargins(0, 0, 0, 0);
    typeRow1->setSpacing(16);
    typeRow1->addWidget(m_wsRadio);
    typeRow1->addWidget(m_webhookRadio);
    typeRow1->addWidget(m_sandboxCheckBox);
    typeRow1->addStretch();

    QHBoxLayout *typeRow2 = new QHBoxLayout;
    typeRow2->setContentsMargins(0, 0, 0, 0);
    typeRow2->setSpacing(16);
    typeRow2->addWidget(m_markdownCheckBox);
    typeRow2->addWidget(m_markdownCheckBox_pd);
    typeRow2->addWidget(m_markdownCheckBox_pd_mb);
    typeRow2->addStretch();

    typeLayout->addLayout(typeRow1);
    typeLayout->addLayout(typeRow2);

    // 左侧留出与上面标签等宽的占位，让控件与输入框左对齐
    QHBoxLayout *typeRow = new QHBoxLayout;
    typeRow->setContentsMargins(0, 0, 0, 0);
    typeRow->setSpacing(8);
    QLabel *typeSpacer = new QLabel;
    typeSpacer->setFixedWidth(labelWidth);
    typeRow->addWidget(typeSpacer);
    typeRow->addWidget(typeWidget, 1);
    basicLayout->addLayout(typeRow);

    contentLayout->addWidget(basicBox);
    setupWsIntentsGroup();
    contentLayout->addWidget(m_wsIntentsGroup);
    formScroll->setWidget(contentWidget);
    outerLayout->addWidget(formScroll, 1);



}


void AddAccountDialog::setEmbeddedMode(bool embedded) {
    if (embedded) {
        setWindowFlags(Qt::Widget);
        setModal(false);
        setMinimumSize(0, 0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // 作为子控件时必须自己画背景，否则外层 #accountPage 的底色会盖不住
        setAttribute(Qt::WA_StyledBackground, true);
        // 嵌入时不需要 确定/取消 —— 由 AccountPage 的「保存当前账号配置」承担
        if (m_buttonBar) m_buttonBar->hide();
    }

}

void AddAccountDialog::setupWsIntentsGroup() {
    m_wsIntentsGroup = new QGroupBox("订阅事件 (WebSocket)");
    m_wsIntentsGroup->setContentsMargins(2, 2, 2, 2);
    QVBoxLayout *groupLayout = new QVBoxLayout(m_wsIntentsGroup);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    groupLayout->setSpacing(2);

    QScrollArea *scroll = new QScrollArea;

    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(150);

    QWidget *scrollWidget = new QWidget;
    scrollWidget->setObjectName("softScrollContent");
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollWidget);
    scrollLayout->setContentsMargins(2, 2, 2, 2);
    scrollLayout->setSpacing(2);

    struct IntentItem { QString name; int mask; };
    QList<IntentItem> intents = {
                                 {"GUILDS(频道事件)", 1<<0},
                                 {"GUILD_MEMBERS(成员加入)", 1<<1},
                                 {"GUILD_MESSAGES(*私域*无艾特)", 1<<9},
                                 {"GUILD_MESSAGE_REACTIONS(添加表情)", 1<<10},
                                 {"DIRECT_MESSAGE(频道私聊事件)", 1<<12},
                                 {"GROUP_MEMBER  (群成员添加退出 申请加群)", 1<<24},
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






void AddAccountDialog::getAccountInfo(AccountInfo *info) const {


    info->appid = m_appidEdit->text();
    info->appid_int =info->appid.toInt();
    // 掩码状态下输入框里是 ***，真实值要从 m_secretReal 取
    info->secret = m_secretMasked ? m_secretReal : m_secretEdit->text();

    if(info->nickname.isEmpty())
    {
        info->nickname = info->appid;
    }

    info->type = m_wsRadio->isChecked() ? 0 : 1;

    info->markdown = m_markdownCheckBox->isChecked();
    info->markdown_pd = m_markdownCheckBox_pd->isChecked();
    info->markdown_pd_mb = m_markdownCheckBox_pd_mb->isChecked();
    if (m_sandboxCheckBox)
        info->sandbox = m_sandboxCheckBox->isChecked();
    info->wsIntents = computeIntentsMask();
}