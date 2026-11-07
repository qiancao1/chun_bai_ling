// helppage.cpp
#include "helppage.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

HelpPage::HelpPage(QWidget *parent) : QWidget(parent)
{
    setupUI();
    loadFromFile();
}

// ── 文本存哪 ──
// 固定用「程序目录」，不用工作目录：从快捷方式 / 别的地方启动时 CWD 不一定等于 exe 所在目录，
// 相对路径会飘到别处 —— 表现就是「自己写的引导文本不见了，还在别的文件夹里新建了一个 data/help.md」。
QString HelpPage::helpFilePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/data/help.md");
}

QString HelpPage::defaultHelpText()
{
    return QString::fromUtf8(R"(纯白铃 · 使用引导

一、先添加一个机器人
  1. 顶部导航进入「账号」页，点「添加」，填写 AppID / AppSecret / Token。
  2. 保存后，标题栏左上角的机器人选择器就是唯一的切号入口；切换即切换「当前操作的账号」。
  3. 账号卡片上的「登录」用来启动长连接，「删除」会连同该账号的本地数据一起清掉，请确认后再点。

二、配置项在哪里
  顶部「基础 / 按钮 / 内置 / 违禁词」是全局配置，对所有机器人生效。
  顶部「账号」页里选中某个机器人后，左侧那一列是「只对这个机器人生效」的配置：
    Ai            模型与接口（模型列表、接口地址、密钥）
    菜单/指令/素材 QQ 端发送「菜单」等指令时的回复内容
    基础          群管、签到等基础开关
    问答          关键词自动回复
    按钮          菜单里的按钮生成与编辑
    黑名单        指定群/用户的拦截
    昵称审核      入群昵称审核
    批量推送      一次给多个会话发消息
    文本替换      发送前对文本做替换
    订阅|定时     定时任务与订阅推送
    关键词撤回    命中违禁词后的撤回处理
  没有选中机器人时点「保存」，会弹出「批量保存」窗口，勾选要应用到哪些机器人即可。

三、发送前缀（框架内部生效，不会发给对方）
  #^ 开头  → 按普通文本发送
  #$ 开头  → 按 Markdown 发送
  不加前缀  → 按原样发送

四、常见问题
  1. 消息发不出去
     先看「日志」页的「发送错误」，里面有平台返回的原文。
     返回内容里带「频率」两个字的，通常是发得太快被限流，等一会儿再发即可。
  2. 改了配置没生效
     绝大部分配置保存即生效；插件相关的改动，到「插件」页重新加载同一个插件就行（重复加载 = 热重载）。
  3. 界面卡住、连接异常
     用标题栏右侧的「快速重启」重启框架；重启过程会写进「日志」页，卡在哪一步一看就知道。
  4. 端口被占用 / 想换端口
     「基础 → 网络与服务」里改，改完需要重启框架才生效。
  5. 插件从哪来
     「插件」页可以浏览插件市场，按平台（Windows / Linux）自动匹配安装；已安装的插件在列表里可以重载和卸载。

五、其他
    跳到名片   mqqapi://card/show_pslcard?src_type=internal&version=1&uin=
    跳到群名片 mqqapi://card/show_pslcard?src_type=internal&version=1&card_type=group&uin=
    伪艾特全体 mqqapi://markdown/mention?at_type=everyone

)");
}

void HelpPage::setupUI()
{
    setObjectName(QStringLiteral("helpPage"));
    setAttribute(Qt::WA_StyledBackground, true);   // 裸 QWidget 要靠这个才会画 QSS 背景

    // 每个选择器都带 objectName，避免无选择器的块当成 *{} 传给子孙控件
    setStyleSheet(QStringLiteral(
        "QWidget#helpPage { background: #F7EFE5; }"
        "QFrame#helpCard { background: #FFFFFF; border: 1px solid #EFE2D2; border-radius: 10px; }"
        "QLabel#helpTitle { font-size: 15px; font-weight: 600; color: #2F3542; }"
        "QLabel#helpTip { font-size: 12px; color: #9A9384; }"
        "QLabel#helpStatus { font-size: 12px; color: #B08A6A; }"
        "QPlainTextEdit#helpEditor { background: #FFFFFF; border: none; font-size: 13px; color: #3A4048;"
        "                           selection-background-color: #FFD9B8; selection-color: #2F3542; }"
        "QPushButton#helpBtn { background: #FFF0DE; border: 1px solid #E6D4C0; color: #344054;"
        "                      border-radius: 6px; padding: 6px 12px; }"
        "QPushButton#helpBtn:hover { background: #FFE6CC; }"
        "QPushButton#helpBtn:pressed { background: #FFDCB8; }"
    ));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    QFrame *card = new QFrame(this);
    card->setObjectName(QStringLiteral("helpCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    root->addWidget(card, 1);

    QVBoxLayout *v = new QVBoxLayout(card);
    v->setContentsMargins(16, 12, 16, 12);
    v->setSpacing(8);

    QHBoxLayout *head = new QHBoxLayout;
    head->setSpacing(8);

    QLabel *title = new QLabel(QStringLiteral("使用引导"), card);
    title->setObjectName(QStringLiteral("helpTitle"));

    QLabel *tip = new QLabel(QStringLiteral("这个页面显示的就是下面的文字，直接编辑即可；停手 1 秒自动保存到 data/help.md"), card);
    tip->setObjectName(QStringLiteral("helpTip"));
    tip->setWordWrap(true);

    m_status = new QLabel(card);
    m_status->setObjectName(QStringLiteral("helpStatus"));

    QPushButton *resetBtn = new QPushButton(QStringLiteral("恢复默认"), card);
    resetBtn->setObjectName(QStringLiteral("helpBtn"));
    resetBtn->setCursor(Qt::PointingHandCursor);
    resetBtn->setFocusPolicy(Qt::NoFocus);

    QPushButton *saveBtn = new QPushButton(QStringLiteral("保存"), card);
    saveBtn->setObjectName(QStringLiteral("helpBtn"));
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setFocusPolicy(Qt::NoFocus);

    head->addWidget(title);
    head->addSpacing(10);
    head->addWidget(tip, 1);
    head->addWidget(m_status);
    head->addWidget(resetBtn);
    head->addWidget(saveBtn);
    v->addLayout(head);

    m_editor = new QPlainTextEdit(card);
    m_editor->setObjectName(QStringLiteral("helpEditor"));
    m_editor->setPlaceholderText(QStringLiteral("在这里写引导文本…"));
    m_editor->setFrameShape(QFrame::NoFrame);
    v->addWidget(m_editor, 1);

    connect(saveBtn,  &QPushButton::clicked, this, &HelpPage::onSaveClicked);
    connect(resetBtn, &QPushButton::clicked, this, &HelpPage::onResetClicked);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1000);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() { saveToFile(); });

    connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
        if (m_loading) return;           // 载入 / 恢复默认时不要当成用户编辑
        setStatus(QStringLiteral("编辑中…"));
        m_saveTimer->start();
    });
}

void HelpPage::setStatus(const QString &text)
{
    if (m_status) m_status->setText(text);
}

void HelpPage::loadFromFile()
{
    const QString path = helpFilePath();
    const bool exists = QFileInfo::exists(path);

    QString text;
    if (exists) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            in.setCodec("UTF-8");        // Qt5 默认跟 locale 走，不指定会变 GBK
            text = in.readAll();
            f.close();
        }
    }

    m_loading = true;
    if (!exists) {
        text = defaultHelpText();        // 首次运行：落一份初稿，用户可以照着自己的说法改
        m_editor->setPlainText(text);
        m_loading = false;
        saveToFile();
        setStatus(QStringLiteral("已生成默认引导"));
        return;
    }

    m_editor->setPlainText(text);        // 文件存在就照原样显示（哪怕是空的，也不要擅自填回去）
    m_loading = false;
    setStatus(QStringLiteral("已加载"));
}

void HelpPage::saveToFile()
{
    const QString path = helpFilePath();
    const QString dir = QFileInfo(path).absolutePath();
    if (!dir.isEmpty()) QDir().mkpath(dir);   // data 目录不在就建

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setStatus(QStringLiteral("保存失败：写不了 ") + path);
        return;
    }
    QTextStream out(&f);
    out.setCodec("UTF-8");
    out << m_editor->toPlainText();
    out.flush();
    f.close();

    setStatus(QStringLiteral("已保存 ") + QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
}

void HelpPage::onSaveClicked()
{
    if (m_saveTimer) m_saveTimer->stop();
    saveToFile();
}

void HelpPage::onResetClicked()
{
    const auto ret = QMessageBox::question(this, QStringLiteral("恢复默认"),
                                           QStringLiteral("当前文本会被内置的示例引导覆盖，确定吗？"),
                                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (m_saveTimer) m_saveTimer->stop();
    m_loading = true;
    m_editor->setPlainText(defaultHelpText());
    m_loading = false;
    saveToFile();
    setStatus(QStringLiteral("已恢复默认引导"));
}
