#include "set.h"
#include "global.h"
#include <QSettings>
#include <QMessageBox>
#include <QHeaderView>
#include <QColorDialog>
#include <QScrollArea>
#include <QGridLayout>
#include <QTimer>
#include <qclipboard.h>
#include <qpainter.h>
#include <qthreadpool.h>
#include "websocketserver.h"
#include "machinekey.h"
#include "themecolorwidget.h"
#include "themecolors.h"
void stopImageServer();
bool startImageServer(quint16 port,const QString &certPath = "",const QString &keyPath = "",const QString &ssl_pem ="");
QString uploadFileSync_cos(const QString &localPath);
QString uploadFileSync(const QString &filePath);
QString uploadFileSync_test(const QString &filePath);
void DelFileSync_Cnb();
QString ffmpegdiv;
extern QString g_ip;
extern bool e_img;
WebSocketServer *ws_server=nullptr;
set::set(QWidget *parent) : QWidget(parent)
{
    setupUI();
    loadConfig();           // 加载配置到 UI
}

set::~set()
{

}


#include <QImage>
#include <QPainter>
#include <QDir>
#include <QRandomGenerator>
#include <QDateTime>
QString uploadImageByPath(const QString &serverUrl, const QString &localPath, int timeoutMs, QString *errorMsg);
bool generateUniqueTestImage() {
    // 用当前毫秒 + 随机数作为文件名
    quint64 ts = QDateTime::currentMSecsSinceEpoch();

    QString filename = "test_64_64.png";
    QString savePath = QDir::currentPath() + "/" + filename;

    QImage img(64, 64, QImage::Format_RGB32);
    // 1. 随机背景色
    QColor bgColor(QRandomGenerator::global()->bounded(256),
                   QRandomGenerator::global()->bounded(256),
                   QRandomGenerator::global()->bounded(256));
    img.fill(bgColor);

    QPainter painter(&img);
    // 2. 绘制随机噪点或图案（增加差异化）
    for (int i = 0; i < 20; ++i) {
        int x = QRandomGenerator::global()->bounded(64);
        int y = QRandomGenerator::global()->bounded(64);
        QColor dotColor(QRandomGenerator::global()->bounded(256),
                        QRandomGenerator::global()->bounded(256),
                        QRandomGenerator::global()->bounded(256));
        painter.setPen(dotColor);
        painter.drawPoint(x, y);
    }

    // 3. 绘制当前时间戳文本（确保肉眼可见差异）
    painter.setPen(Qt::white);
    painter.drawText(img.rect(), Qt::AlignCenter, QString::number(ts % 100000));

    if (!img.save(savePath)) {
        qDebug() << "Failed to save image to" << savePath;
        return false;
    }
    qDebug() << "Generated unique image:" << savePath;
    return true;
}
void set::setupUI()
{
    // 整页结构：可滚动的容器 → 若干张白卡（运行参数 / 网络与服务 / 图床 / 界面配色 / 超级管理员）。
    // 控件创建和信号连接保持原样，只是不再一行行往顶层布局里硬塞。
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setObjectName("setScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    QVBoxLayout *pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(scroll);

    QWidget *content = new QWidget;
    content->setObjectName("setContent");
    content->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *mainVLayout = new QVBoxLayout(content);
    mainVLayout->setContentsMargins(8, 8, 8, 8);
    mainVLayout->setSpacing(10);
    scroll->setWidget(content);

    // ---- 小工具：卡片 / 字段行 ----
    auto makeLabel = [](const QString &text, const char *objectName) {
        auto *label = new QLabel(text);
        label->setObjectName(objectName);
        return label;
    };
    // 卡片：白底圆角 + 标题行（标题 + 灰色说明），返回卡片内部的纵向布局
    auto addCard = [&](const QString &title, const QString &hint) -> QVBoxLayout * {
        auto *card = new QFrame;
        card->setObjectName("setCard");
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(12);

        auto *head = new QHBoxLayout;
        head->setSpacing(8);
        head->addWidget(makeLabel(title, "setCardTitle"));
        if (!hint.isEmpty())
            head->addWidget(makeLabel(hint, "setCardHint"));
        head->addStretch();
        cardLayout->addLayout(head);

        mainVLayout->addWidget(card);
        return cardLayout;
    };
    // 字段单元：输入框 + 右侧按钮（按钮跟着输入框走，不会被拉伸跑远）
    auto fieldUnit = [](QWidget *input, const QList<QWidget *> &buttons) -> QWidget * {
        auto *unit = new QWidget;
        auto *h = new QHBoxLayout(unit);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);
        h->addWidget(input, 1);
        for (QWidget *button : buttons) {
            if (button) h->addWidget(button);
        }
        return unit;
    };

    QLabel *urlLabel1 = new QLabel(tr("ffmpeg路径"), this);
    m_ffmpegpath = new QLineEdit(this);
    m_ffmpegpath->setPlaceholderText("ffmpeg/");
    //m_ffmpegpath->setMinimumWidth(150);

    QPushButton *bt = new QPushButton(tr("确认"), this);
    int xc = QThread::idealThreadCount();
    m_xcs = new QLineEdit(this);
    m_xcs->setPlaceholderText(QString::number(xc));
    int xcs= g_config["xc_s"].toInt(xc);
    if(xcs<=0) xcs=1;
    if(xcs>500) xcs=500;
    m_xcs->setText(QString::number(xcs));
    QThreadPool *pool = QThreadPool::globalInstance();
    pool->setMaxThreadCount(xcs);
    m_xcs->setMaximumWidth(70);


    QLabel *urlLabel3 = new QLabel(tr("日志数"), this);
    m_日志数量 = new QLineEdit(this);
    m_日志数量->setPlaceholderText("200000");
    m_日志数量->setToolTip("日志保留条数，默认 20 万条。看电脑配置来，是永久缓存（落数据库，不占内存）");
    m_日志数量->setText(QString::number(g_config["logs"].toInt(200000)));
    m_日志数量->setMaximumWidth(120);
    QPushButton *bt2 = new QPushButton(tr("确认"), this);

    Color_0 = g_config["log_Color0"].toInt(0);
    Color_1 = g_config["log_Color1"].toInt(0);


    QLabel *urlLabel4 = new QLabel(tr("日志颜色"), this);
    QPushButton *colorPreview = new QPushButton(this);
    colorPreview->setFixedSize(30, 30);
    colorPreview->setStyleSheet(QString("background-color: %1; border: 1px solid gray;").arg(QColor(Color_0).name()));
    colorPreview->setCursor(Qt::PointingHandCursor);  // 手型光标

    // 点击预览按钮 → 打开颜色选择器
    connect(colorPreview, &QPushButton::clicked, this, [=]() {
        QColor oldColor = QColor::fromRgb(Color_0);
        QColor newColor = QColorDialog::getColor(oldColor, this, tr("选择日志颜色"));
        if (newColor.isValid()) {
            Color_0 = newColor.rgb();
            colorPreview->setStyleSheet(QString("background-color: %1; border: 1px solid gray;")
                                            .arg(newColor.name()));
            g_config["log_Color0"] = Color_0;
            saveConfig();
        }
    });
    QPushButton *btn_xc = new QPushButton("确认", this);
    connect(btn_xc, &QPushButton::clicked, this, [=]() {

        int xcs= m_xcs->text().toInt();
        if(xcs<=0) xcs=1;
        if(xcs>500) xcs=500;
        m_xcs->setText(QString::number(xcs));
        g_config["xc_s"] = xcs;
        QThreadPool *pool = QThreadPool::globalInstance();
        pool->setMaxThreadCount(xcs); // 增加到10
        saveConfig();
        QMessageBox::warning(this,"修改完成","修改线程数量完成，注意每条线程将会占用1m内存\n"
                                               "线程多不代表执行效率高\n减少线程数量(推荐cpu线程数 你的是线程数："+QString::number(QThread::idealThreadCount())+")\n"
                                                "降低线程数上下文切换频率\n"
                                                "在什么时候要增加线程数量，插件在执行堵塞型http请求多时\n"
                                                "需要提高线程数量，但是一般开发者都知道异步\n"
                                               "但是本线程池是不提供事件循环的，\n\n"
                                               "如果你只是用户，你可以在日志显示有延迟时(延迟个几秒) 代表你可能要提高线程数量 可能是那个插件的史山发力了");
    });

    QLabel *urlLabel5 = new QLabel(tr("未处理颜色"), this);
    QPushButton *colorPreview2 = new QPushButton(this);
    colorPreview2->setFixedSize(30, 30);
    colorPreview2->setStyleSheet(QString("background-color: %1; border: 1px solid gray;").arg(QColor(Color_1).name()));
    colorPreview2->setCursor(Qt::PointingHandCursor);  // 手型光标

    // 点击预览按钮 → 打开颜色选择器
    connect(colorPreview2, &QPushButton::clicked, this, [=]() {
        QColor oldColor = QColor::fromRgb(Color_1);
        QColor newColor = QColorDialog::getColor(oldColor, this, tr("选择日志颜色"));
        if (newColor.isValid()) {
            Color_1 = newColor.rgb();
            colorPreview2->setStyleSheet(QString("background-color: %1; border: 1px solid gray;")
                                            .arg(newColor.name()));
            g_config["log_Color1"] = Color_1;
            saveConfig();
        }
    });

    webhook = new QLineEdit(this);
    webhook_but = new QPushButton("确认");


    //webhook_ssl->setPlaceholderText("可选，证书放置 运行目录/ssl.key,ssl.crt,ssl.pem");

    webws_port = new QLineEdit(this);
    web_qr = new QPushButton("确认");



    int port = g_config["webhook_p"].toInt();
    int port2 = g_config["webws_p"].toInt();
    if(port==0)
    {
        port=8080;
        g_config["webhook_p"]=8080;
    }
    if(port2==0)
    {
        port2=8081;
        g_config["webws_p"]=8081;
    }


    webhook->setText(QString::number(port));
    webhook->setPlaceholderText("8080");
    webhook->setMaximumWidth(70);



    webws_port->setText(QString::number(port2));
    webws_port->setMaximumWidth(70);
    QUuid uuid= QUuid::createUuid();
    ws_token = uuid.toString(QUuid::WithoutBraces);


    m_addrEdit = new QLineEdit(this);
    m_addrEdit->setPlaceholderText(tr("如 abcd.com 或 22.33.44.55"));
    m_addrEdit->setMinimumWidth(150);
    m_startStopBtn = new QPushButton("保存", this);
    lts_but = new QPushButton("复制聊天室链接", this);

    Ewebhook = new QCheckBox;
    Ews = new QCheckBox;
    ESSL = new QCheckBox;
    Eimg = new QCheckBox;
    loadimg = new QCheckBox;
    Ewebhook->setText("启动用webhook");
    Ews->setText("启用web管理");
    ESSL->setText("启用SSL");
    Eimg->setText("启用图床");
    loadimg->setText("启用其他本地图床");
    远程服务器 = g_config["y_img"].toBool();
    loadimg->setChecked(远程服务器);

    Eimg->setChecked(e_img);

    m_loadport = new QLineEdit(this);
    m_loadport->setPlaceholderText(tr("8080"));
    m_loadport->setMinimumWidth(50);
    远程端口 = g_config["y_port"].toString();
    m_loadport->setText(远程端口);
    m_loadbut = new QPushButton("确认", this);
    m_test_api = new QPushButton("测试接口");
    connect(m_test_api, &QPushButton::clicked, [this](){
        if (!generateUniqueTestImage()) {
            QMessageBox::warning(this, "错误", "生成测试图片失败");
            return;
        }
        QElapsedTimer t;
        t.start();
        QString err;
        QString url = uploadImageByPath("http://127.0.0.1:"+setA->远程端口+"/",QCoreApplication::applicationDirPath()+"/test_64_64.png",30000,&err);

        QMessageBox::warning(this,"测试本地其他图床","测试返回url(如果是空代表失败):"+url+"\n\n耗时："+QString::number(t.elapsed())+"ms");

    });

    m_cnb = new QCheckBox("启用 CNB 图床");
    m_cnb_repo = new QLineEdit;
    m_cnb_repo->setPlaceholderText("nya/image");
    m_cnb_token= new QLineEdit;
    m_cnb_qr = new QPushButton("保存");
    m_cnb_cs = new QPushButton("测试接口");
    g_cnb.repo=g_config["cnb_repo"].toString();
    QByteArray key1 = MachineKey::generateKey(g_cnb.repo);
    g_cnb.key=g_config["cnb_key"].toString();
    g_cnb.key=MachineKey::decrypt(g_cnb.key, key1);
    g_cnb.e=g_config["cnb_e"].toBool();
    m_cnb_repo->setText(g_cnb.repo);
    m_cnb_token->setText(g_cnb.key);
    m_cnb->setChecked(g_cnb.e);

    connect(m_cnb_qr, &QPushButton::clicked, [this](){
        g_cnb.repo = m_cnb_repo->text();
        g_cnb.key = m_cnb_token->text();
        g_cnb.e = m_cnb->isChecked();

        QByteArray key = MachineKey::generateKey(g_cnb.repo);
        g_config["cnb_repo"]= g_cnb.repo;
        g_config["cnb_key"]= MachineKey::encrypt(g_cnb.key, key);
        g_config["cnb_e"]= g_cnb.e;
        saveConfig();
    });
    connect(m_cnb_cs, &QPushButton::clicked, [this](){
        if (!generateUniqueTestImage()) {
            QMessageBox::warning(this, "错误", "生成测试图片失败");
            return;
        }
        QElapsedTimer t;
        t.start();
        QString url = uploadFileSync_test("test_64_64.png");
        QMessageBox::warning(this,"测试结果CNB","测试返回url(如果是空代表失败):"+url+"\n\n耗时："+QString::number(t.elapsed())+"ms");

    });

    m_cos = new QCheckBox("启用 COS 图床");
    m_cos_secretId = new QLineEdit;
    m_cos_secretKey= new QLineEdit;
    m_cos_host= new QLineEdit;
    m_cos_secretId->setPlaceholderText("从腾讯云创建");
    m_cos_secretKey->setPlaceholderText("从腾讯云创建");
    m_cos_host->setPlaceholderText("bot-1250000000.cos.ap-guangzhou.myqcloud.com");
    m_cos_qr = new QPushButton("保存");
    m_cos_cs = new QPushButton("测试接口");
    g_cos.secretId=g_config["cos_id"].toString();
    QByteArray key = MachineKey::generateKey(g_cos.secretId);
    g_cos.secretKey=g_config["cos_key"].toString();
    g_cos.secretKey=MachineKey::decrypt(g_cos.secretKey, key);
    g_cos.host=g_config["cos_host"].toString();
    g_cos.host=MachineKey::decrypt(g_cos.host, key);
    g_cos.e=g_config["cos_e"].toBool();
    if(g_cos.host.startsWith("https://"))
    {
        g_cos.host.remove("https://");
        g_cos.baseUrl = g_cos.host;
    }else
    {
        g_cos.baseUrl = "https://" + g_cos.host;
    }
    m_cos->setChecked(g_cos.e);
    m_cos_secretId->setText(g_cos.secretId);
    m_cos_secretKey->setText(g_cos.secretKey);
    m_cos_host->setText(g_cos.host);

    connect(m_cos_qr, &QPushButton::clicked, [this](){
        g_cos.secretId = m_cos_secretId->text();
        g_cos.secretKey = m_cos_secretKey->text();
        g_cos.host = m_cos_host->text();
        g_cos.e = m_cos->isChecked();

        if(g_cos.host.startsWith("https://"))
        {
            g_cos.host.remove("https://");
            g_cos.baseUrl = g_cos.host;
        }else
        {
            g_cos.baseUrl = "https://" + g_cos.host;
        }
        QByteArray key = MachineKey::generateKey(g_cos.secretId);
        g_config["cos_id"]= g_cos.secretId;
        g_config["cos_key"]= MachineKey::encrypt(g_cos.secretKey, key);
        g_config["cos_host"]= MachineKey::encrypt(g_cos.host, key);
        g_config["cos_e"]= g_cos.e;
        saveConfig();
        QMessageBox msgBox(this);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowTitle("设置自动删除");
        msgBox.setTextFormat(Qt::RichText); // 关键：启用富文本，让链接可点击[reference:2]
        msgBox.setText("框架里面没有自动删除 上传的文件\n\n"
                       "请在 <a href=\"https://console.cloud.tencent.com/cos/bucket\">腾讯云COS控制台</a> 设置自动删除规则\n\n"
                       "否则 容量超限制 可能会造成额外费用\n"
                       "框架使用优先级 1.cnb 2.cos 3.内部其他");
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.exec();
    });
    connect(m_cos_cs, &QPushButton::clicked, [this](){
        if (!generateUniqueTestImage()) {
            QMessageBox::warning(this, "错误", "生成测试图片失败");
            return;
        }
        QElapsedTimer t;
        t.start();

        QString url = uploadFileSync_cos("test_64_64.png");
        qDebug() << url;
        QMessageBox::warning(this,"测试结果COS","测试返回url(如果是空代表失败):"+url+"\n\n耗时："+QString::number(t.elapsed())+"ms");

    });

    // ================== 卡片式布局 ==================
    auto addDivider = [](QVBoxLayout *parent) {
        auto *line = new QFrame;
        line->setObjectName("setDivider");
        line->setFixedHeight(1);
        parent->addWidget(line);
    };
    auto makeGrid = []() {
        auto *grid = new QGridLayout;
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(8);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        return grid;
    };

    // ---- 运行参数 ----
    {
        QVBoxLayout *card = addCard(QStringLiteral("运行参数"),
                                    QStringLiteral("改完点右侧的确认才会生效"));
        urlLabel1->setObjectName("setFieldLabel");
        urlLabel3->setObjectName("setFieldLabel");
        urlLabel4->setObjectName("setFieldLabel");
        urlLabel5->setObjectName("setFieldLabel");

        auto *grid = makeGrid();
        grid->addWidget(urlLabel1, 0, 0);
        grid->addWidget(fieldUnit(m_ffmpegpath, {bt}), 0, 1);
        grid->addWidget(makeLabel(tr("线程数"), "setFieldLabel"), 0, 2);
        grid->addWidget(fieldUnit(m_xcs, {btn_xc}), 0, 3);

        grid->addWidget(urlLabel3, 1, 0);
        grid->addWidget(fieldUnit(m_日志数量, {bt2}), 1, 1);
        grid->addWidget(urlLabel4, 1, 2);

        // 两个颜色块 + 各自的说明，放在同一个格子里
        auto *colorUnit = new QWidget;
        auto *colorRow = new QHBoxLayout(colorUnit);
        colorRow->setContentsMargins(0, 0, 0, 0);
        colorRow->setSpacing(8);
        colorRow->addWidget(colorPreview);
        colorRow->addWidget(urlLabel5);
        colorRow->addWidget(colorPreview2);
        colorRow->addStretch();
        grid->addWidget(colorUnit, 1, 3);

        card->addLayout(grid);
    }

    // ---- 网络与服务 ----
    {
        QVBoxLayout *card = addCard(QStringLiteral("网络与服务"),
                                    QStringLiteral("端口改完要点确认，会重启对应监听"));
        auto *grid = makeGrid();
        grid->addWidget(makeLabel(tr("对外链接"), "setFieldLabel"), 0, 0);
        grid->addWidget(fieldUnit(m_addrEdit, {m_startStopBtn}), 0, 1);
        grid->addWidget(makeLabel(tr("聊天室链接"), "setFieldLabel"), 0, 2);
        grid->addWidget(lts_but, 0, 3, Qt::AlignLeft);

        grid->addWidget(makeLabel(tr("webhook 端口"), "setFieldLabel"), 1, 0);
        grid->addWidget(fieldUnit(webhook, {webhook_but}), 1, 1);
        grid->addWidget(makeLabel(tr("聊天室端口"), "setFieldLabel"), 1, 2);
        grid->addWidget(fieldUnit(webws_port, {web_qr}), 1, 3);

        auto *switchRow = new QHBoxLayout;
        switchRow->setSpacing(18);
        switchRow->addWidget(Ewebhook);
        switchRow->addWidget(Ews);
        switchRow->addWidget(ESSL);
        switchRow->addStretch();
        grid->addLayout(switchRow, 2, 0, 1, 4);

        card->addLayout(grid);
    }

    // ---- 图床 ----
    {
        QVBoxLayout *card = addCard(QStringLiteral("图床"),
                                    QStringLiteral("上传优先级：CNB → COS → 本地图床"));

        auto *localTop = new QHBoxLayout;
        localTop->setSpacing(10);
        localTop->addWidget(Eimg);
        localTop->addWidget(loadimg);
        localTop->addWidget(makeLabel(tr("端口"), "setFieldLabel"));
        m_loadport->setMaximumWidth(90);
        localTop->addWidget(m_loadport);
        localTop->addWidget(m_loadbut);
        localTop->addWidget(m_test_api);
        localTop->addStretch();
        card->addLayout(localTop);

        addDivider(card);

        auto *cnbHead = new QHBoxLayout;
        cnbHead->setSpacing(8);
        cnbHead->addWidget(m_cnb);
        cnbHead->addStretch();
        cnbHead->addWidget(m_cnb_qr);
        cnbHead->addWidget(m_cnb_cs);
        card->addLayout(cnbHead);

        auto *cnbGrid = makeGrid();
        cnbGrid->addWidget(makeLabel(tr("组织 / 仓库"), "setFieldLabel"), 0, 0);
        cnbGrid->addWidget(m_cnb_repo, 0, 1);
        cnbGrid->addWidget(makeLabel(tr("访问令牌"), "setFieldLabel"), 0, 2);
        cnbGrid->addWidget(m_cnb_token, 0, 3);
        card->addLayout(cnbGrid);

        addDivider(card);

        auto *cosHead = new QHBoxLayout;
        cosHead->setSpacing(8);
        cosHead->addWidget(m_cos);
        cosHead->addStretch();
        cosHead->addWidget(m_cos_qr);
        cosHead->addWidget(m_cos_cs);
        card->addLayout(cosHead);

        auto *cosGrid = makeGrid();
        cosGrid->addWidget(makeLabel(tr("SecretId"), "setFieldLabel"), 0, 0);
        cosGrid->addWidget(m_cos_secretId, 0, 1);
        cosGrid->addWidget(makeLabel(tr("SecretKey"), "setFieldLabel"), 0, 2);
        cosGrid->addWidget(m_cos_secretKey, 0, 3);
        cosGrid->addWidget(makeLabel(tr("Host"), "setFieldLabel"), 1, 0);
        cosGrid->addWidget(m_cos_host, 1, 1, 1, 3);
        card->addLayout(cosGrid);
    }

    // ---- 界面配色（控件自带标题行，卡片不再重复写标题） ----
    {
        auto *card = new QFrame;
        card->setObjectName("setCard");
        card->setAttribute(Qt::WA_StyledBackground, true);
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 14);
        cardLayout->setSpacing(8);
        cardLayout->addWidget(new ThemeColorWidget(card));
        mainVLayout->addWidget(card);
    }

    // ---- 超级管理员 ----
    {
        QVBoxLayout *card = addCard(QStringLiteral("超级管理员"),
                                    QStringLiteral("空格分隔；可触发 webui 全局禁用/启用插件、启用 ws"));
        m_admid_deit = new QTextEdit;
        m_admid_deit->setPlaceholderText(QStringLiteral("空格分割 允许触发 webui 全局禁用启用插件 启用ws"));
        m_admid_deit->setPlainText(g_admin);
        m_admid_deit->setFixedHeight(96);
        card->addWidget(m_admid_deit);

        m_admin_qr = new QPushButton("保存全局管理");
        m_admin_qr->setObjectName("setPrimary");
        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch();
        btnRow->addWidget(m_admin_qr);
        card->addLayout(btnRow);
    }

    mainVLayout->addStretch();

    ThemeColors::applyTo(this, QString::fromUtf8(R"(
        QScrollArea#setScroll { background: transparent; border: none; }
        QScrollArea#setScroll > QWidget > QWidget { background: transparent; }
        QWidget#setContent { background: #F7EFE5; }
        QFrame#setCard {
            background: #FFFFFF;
            border: 1px solid #E6D4C0;
            border-radius: 12px;
        }
        QFrame#setDivider { background: #F8EFE6; border: none; }
        QLabel#setCardTitle { color: #263241; font-size: 14px; font-weight: 700; background: transparent; }
        QLabel#setCardHint { color: #8A94A6; font-size: 12px; background: transparent; }
        QLabel#setFieldLabel { color: #596579; font-size: 12px; background: transparent; }
        QPushButton#setPrimary {
            background: #FF7F32;
            color: #FFFFFF;
            padding: 6px 18px;
        }
        QPushButton#setPrimary:hover { background: #F06E22; }
        /* 预设按钮：普通态走全局按钮样式（浅橙底 + 描边 + 深字），
           当前生效的那套由 ThemeColorWidget 打上 active 属性变实心橙 */
        QPushButton#presetChip { padding: 4px 12px; }
        QPushButton#presetChip[active="true"] {
            background: #FF7F32;
            color: #FFFFFF;
            border: 1px solid #FF7F32;
        }
        QPushButton#presetChip[active="true"]:hover {
            background: #FF914D;
            color: #FFFFFF;
            border: 1px solid #FF914D;
        }
    )"));



    connect(m_admin_qr, &QPushButton::clicked, [this](){
        g_admin = m_admid_deit->toPlainText();
        g_config["admin"]= g_admin;
        saveConfig();
    });
    // 信号槽连接（新增）
    connect(bt, &QPushButton::clicked, [this](){
        ffmpegdiv = m_ffmpegpath->text();
        g_config["ffmpeg"]= ffmpegdiv;
        saveConfig();
    });
    connect(m_loadbut, &QPushButton::clicked, [this](){
        远程端口 = m_loadport->text();
        g_config["y_port"]= 远程端口;
        saveConfig();
    });

    connect(bt2, &QPushButton::clicked, [this](){
        int configCapacity= m_日志数量->text().toInt();
        if(configCapacity<1000 && configCapacity>0)
        {
            m_日志数量->setText("1000");
            configCapacity=1000;
        }
        g_config["logs"]= configCapacity;
        saveConfig();
        if(configCapacity<=0) configCapacity=1000;
        QMessageBox::warning(this,"修改日志数量","修改完成 这里是修改数据库日志保留数据 日志不保存在内存");
    });

    connect(m_startStopBtn, &QPushButton::clicked, [this](){
        g_ip=m_addrEdit->text().trimmed();
        g_config["local_server_ip"]= g_ip;
        saveConfig();
    });

    connect(lts_but, &QPushButton::clicked, [this](){
        QClipboard *clipboard = QApplication::clipboard();
        int port=g_config["webhook_p"].toInt();
        if(ESSL->isChecked())
        {
            clipboard->setText(QString("https://%1:%2/webui/index.html?token=%3").arg(g_ip).arg(port).arg(ws_token));

        }else{
            clipboard->setText(QString("http://%1:%2/webui/index.html?token=%3").arg(g_ip).arg(port).arg(ws_token));
        }

    });

    connect(web_qr, &QPushButton::clicked, [this](){
        int prot = webws_port->text().toInt();
        if(prot<=10 || prot>0xffff)
        {
            QMessageBox::warning(this,"端口错误","端口不在 1-65535 之间");
            return ;
        }
        g_config["webws_p"] = prot;
        if(Ews->isChecked())
        {
            int port = g_config["webws_p"].toInt();

            ws_server->close();
            if(ESSL->isChecked())

            Ews->setChecked(ws_server->open(port,"ssl.crt","ssl.key","ssl.pem"));  // 监听 8080 端口
            else
                Ews->setChecked(ws_server->open(port));

        }
        saveConfig();

    });
    connect(webhook_but, &QPushButton::clicked, [this](){
        int prot = webhook->text().toInt();
        if(prot<=10 || prot>0xffff)
        {
            QMessageBox::warning(this,"端口错误","端口不在 1-65535 之间");
            return ;
        }
        g_config["webhook_p"] = prot;
        if(Ewebhook->isChecked())
        {
            int port = g_config["webhook_p"].toInt();
            Ewebhook->setChecked(false);
            onStartStopClicked(port); //停止
            Ewebhook->setChecked(true);  //不会触发信号
            Ewebhook->setChecked(onStartStopClicked(port)); //重新开

        }
        saveConfig();

    });
    //启用禁用webhook



    connect(Eimg, &QCheckBox::clicked, [this](){
        e_img= Eimg->isChecked();
        g_config["e_img"]=e_img;
        saveConfig();

    });
    connect(loadimg, &QCheckBox::clicked, [this](){
        远程服务器= Eimg->isChecked();
        g_config["y_img"]=远程服务器;
        saveConfig();

    });
    if( g_config["SSL"].toBool()) //要在 webhook之前
    {
        ESSL->setChecked(true);
    }

    connect(ESSL, &QCheckBox::clicked, [this](){
        bool ssl = ESSL->isChecked();
        g_config["SSL"]=ssl;
        if(Ewebhook->isChecked())
        {
            int port = g_config["webhook_p"].toInt();
            Ewebhook->setChecked(false);
            onStartStopClicked(port); //停止
            Ewebhook->setChecked(true);  //不会触发信号
            Ewebhook->setChecked(onStartStopClicked(port)); //重新开

        }

        if(Ews->isChecked())
        {
            int port = g_config["webws_p"].toInt();

            ws_server->close();
            if(ESSL->isChecked())

                Ews->setChecked(ws_server->open(port,"ssl.crt","ssl.key","ssl.pem"));  // 监听 8080 端口
            else
                Ews->setChecked(ws_server->open(port));

        }
        saveConfig();

    });

    ws_server = new WebSocketServer;
    if(g_config["webws_run"].toBool())
    {
        int port = g_config["webws_p"].toInt();
        if(port!=0){

            if(ESSL->isChecked())

                Ews->setChecked(ws_server->open(port,"ssl.crt","ssl.key","ssl.pem"));  // 监听 8080 端口
            else
                Ews->setChecked(ws_server->open(port));
        }
    }

    if(g_config["webhook_run"].toBool())
    {
        int port = g_config["webhook_p"].toInt();
        if(port!=0){
            Ewebhook->setChecked(true);
            Ewebhook->setChecked(onStartStopClicked(port));  //不会触发信号
        }
    }

    connect(Ewebhook, &QCheckBox::clicked, [this](){
        int port = g_config["webhook_p"].toInt();
        if(port!=0){
            Ewebhook->setChecked(onStartStopClicked(port));  //不会触发信号
        }
    });

    //启用禁用ws聊天室
    connect(Ews, &QCheckBox::clicked, [this](){

        int port = g_config["webws_p"].toInt();
        if(port==0)
        {
            Ews->setChecked(false);
            QMessageBox::warning(this,"启动失败","为设置端口 请设置端口后再试试 如果框里面有值点一下旁边的确认");
            return ;
        }
        if(Ews->isChecked())
        {
            if(ESSL->isChecked())

                ws_server->open(port,"ssl.crt","ssl.key","ssl.pem");  // 监听 8080 端口
            else
                ws_server->open(port);  // 监听 8080 端口
            g_config["webws_run"]=true;
            saveConfig();
        }
        else
        {
            ws_server->close();
            g_config["webws_run"]=false;
            saveConfig();
        }
    });
}
void set::set_webui(bool value)
{
    QMetaObject::invokeMethod(qApp, [=]() {
        if(value)
        {
            int port = g_config["webws_p"].toInt();
            if(port==0) return ;

            if(ESSL->isChecked())

                ws_server->open(port,"ssl.crt","ssl.key","ssl.pem");  // 监听 8080 端口
            else
                ws_server->open(port);  // 监听 8080 端口
            Ews->setChecked(true);
            g_config["webws_run"]=true;
            saveConfig();
            return;
        }
        if(ws_server){
            ws_server->close();
            g_config["webws_run"]=false;
            Ews->setChecked(false);
            saveConfig();
        }
    }, Qt::QueuedConnection); // 使用 QueuedConnection 确保异步投递到主线程

}




void set::loadConfig()
{
    ffmpegdiv = g_config["ffmpeg"].toString();
    if(ffmpegdiv.isEmpty())
        ffmpegdiv="ffmpeg/";
    m_ffmpegpath->setText(ffmpegdiv);

    g_ip = g_config["local_server_ip"].toString();
    m_addrEdit->setText(g_ip);
}



bool set::onStartStopClicked(int port)
{
    g_config["webhook_run"]=Ewebhook->isChecked();
    saveConfig();
    bool ok=false;
    if (Ewebhook->isChecked()) {


        if(ESSL->isChecked())
            ok = startImageServer(port,"ssl.crt","ssl.key","ssl.pem");
        else
            ok = startImageServer(port);
        if (!ok) {
            Ewebhook->setChecked(false);
            AppendEventLog("启动服务器启动失败，端口是否可用。下次程序启动将自动重试。" ,0xff);

        }
    } else {
        stopImageServer();
    }
    return ok;
}



