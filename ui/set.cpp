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
#include <QApplication>
#ifdef Q_OS_WIN
#ifndef _WINDOWS_
#include <windows.h>
#endif
#include <Psapi.h>      // K32GetProcessMemoryInfo（kernel32 导出，不用额外加 psapi.lib）
#endif
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

// 「重载数据库」按钮用的内存读数（KB）。工作集 = 任务管理器「内存」那一列；
// PagefileUsage = 「提交大小」；PrivateUsage = 私有提交（不含共享页）。
// ⚠ 只做前后对比用，不追求跟任务管理器逐字节一致。
namespace {
struct MemSnap {
    qint64 wsKB     = -1;   // -1 = 没取到
    qint64 commitKB = -1;
    qint64 privKB   = -1;
};

MemSnap readProcMem()
{
    MemSnap s;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(),
                                reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                                sizeof(pmc))) {
        s.wsKB     = static_cast<qint64>(pmc.WorkingSetSize) / 1024;
        s.commitKB = static_cast<qint64>(pmc.PagefileUsage)  / 1024;
        s.privKB   = static_cast<qint64>(pmc.PrivateUsage)   / 1024;
    }
#endif
    return s;
}

QString mbText(qint64 kb)
{
    if (kb < 0) return QStringLiteral("取不到");
    return QString::number(kb / 1024.0, 'f', 1) + QStringLiteral(" MB");
}
} // namespace
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
#include <QFile>
#include <QCoreApplication>
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
    m_日志数量->setPlaceholderText("100000");
    m_日志数量->setToolTip("日志保留条数。\n"
                           "日志库是文件映射（mmap）：条数多不代表全读进内存，只按访问过的页占；\n"
                           "但清理/压缩时要全表遍历一遍，那一趟会把整库页都摸进来。");
    m_日志数量->setText(QString::number(g_config["logs"].toInt(100000)));
    m_日志数量->setMaximumWidth(120);
    QPushButton *bt2 = new QPushButton(tr("确认"), this);

    // 内存诊断：把所有日志库卸载再挂上（munmap → mmap），顺带清用户/群/图片缓存，
    // 弹窗给出前后对比 —— 用来判断「内存到底是不是数据库/缓存占的」。
    QPushButton *btReloadDb = new QPushButton(tr("重载数据库"), this);
    btReloadDb->setToolTip("把所有日志库卸载再重新挂载（munmap → mmap），并清空：\n"
                           "· 用户缓存 / 群缓存（BotDB，下次访问自动从库里重读）\n"
                           "· 头像 / 图片 / 排版缓存、Qt 全局图片缓存\n\n"
                           "日志、配置、库内容一条都不动；当前会话里的图片可能要重新加载一下。\n"
                           "点完弹窗会给出「工作集 / 提交大小」的前后对比。");

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

        // 内存诊断按钮：单独一行，右边跟一句灰字说明
        grid->addWidget(makeLabel(tr("内存"), "setFieldLabel"), 2, 0);
        grid->addWidget(btReloadDb, 2, 1);
        grid->addWidget(makeLabel(tr("卸载并重新挂载全部日志库 + 清空用户/群/图片缓存，弹窗给出前后对比"),
                                  "setCardHint"), 2, 2, 1, 2);

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

        // 这里只给**格式说明**，不放输入框 —— 这条地址是填在 QQ 开放平台「回调配置」里的，
        // 平台会往它 POST 事件。框架只接 POST 且 path 必须以 /webhook/ 开头（image-server.cpp:872），
        // 然后按 split('/') 拆：parts[0]=webhook / parts[1]=appid / parts[2]=secret。
        auto *hint = makeLabel(
            tr("QQ 开放平台「回调配置」填的地址格式：  https://域名[:端口]/webhook/appid/secret\n"
               "· 必须是 HTTPS 公网地址，端口只允许 80 / 443 / 8080 / 8443；\n"
               "· 路径前缀固定是 /webhook/，后面紧跟 appid —— 少一段框架就不接这条回调；\n"
               "· secret 可选（16 位以上）：只有「该 appid 还没添加进框架」时才用它做首次验证，"
               "账号已在框架里就不用带；\n"
               "· 用 nginx / 内网穿透反代时，按反代给出的对外域名和端口填这条地址；\n"
               "· 勾「启用SSL」时证书要放程序目录：ssl.crt（证书）、ssl.key（私钥）、"
               "ssl.pem（可选，中间证书链），缺文件时 http/ws 都会启动失败"),
            "setCardHint");
        hint->setWordWrap(true);
        grid->addWidget(hint, 3, 0, 1, 4);

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
        QMessageBox::warning(this,"修改日志数量",
            "修改完成。下次启动后生效（启动 30 秒会自动清理一次）。\n\n"
            "注意：LMDB 删条目不缩文件，所以要真正省内存，还要让库压一次 ——\n"
            "在 botdb/logdb_0 ~ logdb_4、botdb/logdb_err 里各放一个空文件 compact.force，再重启。");
    });

    // 内存诊断：卸载并重新挂载全部日志库 + 清用户/群/图片缓存，前后对比弹窗
    connect(btReloadDb, &QPushButton::clicked, [this]() {
        if (QMessageBox::question(this, "重载数据库",
                "会把所有日志库卸载再重新挂载（munmap → mmap），并清空：\n"
                "· 用户缓存 / 群缓存（BotDB，下次访问自动从库里重读）\n"
                "· 头像 / 图片 / 排版缓存、Qt 全局图片缓存\n\n"
                "日志、配置、库内容一条都不动；当前会话里的图片可能要重新加载一次。\n\n"
                "继续吗？",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;

        const MemSnap before = readProcMem();

        // 1. 日志库：flush 缓存 → mdb_env_close（munmap，把被清理/查询摸过的文件页直接还给系统）→ 重新 mmap
        int okCount = 0, total = 0;
        QStringList failed;
        for (int i = 0; i < static_cast<int>(g_logdb.size()); ++i) {
            LogDB *db = g_logdb[i].get();
            if (!db) continue;
            ++total;
            if (db->reloadAfterClean()) ++okCount;
            else failed << QStringLiteral("logdb_%1").arg(i);
        }
        if (g_errlogdb) {
            ++total;
            if (g_errlogdb->reloadAfterClean()) ++okCount;
            else failed << QStringLiteral("logdb_err");
        }

        // 2. 用户 / 群缓存（BotDB 里那两份 QHash，本来只在 4h / 24h 定时清）
        int botCount = 0;
        for (BotDB *db : g_botdb) {
            if (!db) continue;
            db->clearAllCaches();
            ++botCount;
        }

        // 3. 界面缓存（头像 / 图片 / 排版 / Qt 全局图片缓存）
        if (chatPage) chatPage->resetRuntimeCaches();

        // 4. 事件队列里排着的 deleteLater / 重排先跑掉（它们可能还攥着图片和 QString）
        qApp->processEvents();

        // ⚠ 必须在「压工作集」之前取这一次：SetProcessWorkingSetSize(-1,-1) 之后立刻读，
        //   工作集会读到 0.1 MB 这种假数（页全被挪进 standby，还没缺页回来）。
        const MemSnap afterWork = readProcMem();

#ifdef Q_OS_WIN
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
#endif

        // 等 400ms 再读一次：给缺页回来的时间，这才是「压过工作集之后」的真实水位
        QTimer::singleShot(400, this, [this, before, afterWork, okCount, total, failed, botCount]() {
            const MemSnap afterTrim = readProcMem();

            auto diffText = [](qint64 a, qint64 b) -> QString {
                if (a < 0 || b < 0) return QString();
                const double d = (b - a) / 1024.0;
                return QStringLiteral("（%1%2 MB）")
                    .arg(d <= 0 ? QStringLiteral("-") : QStringLiteral("+"))
                    .arg(QString::number(qAbs(d), 'f', 1));
            };

            QString text = QStringLiteral("日志库重载：%1/%2 成功%3\n"
                                          "已清空用户/群缓存：%4 个账号\n\n"
                                          "工作集（任务管理器「内存」列）\n"
                                          "    重载前：%5\n"
                                          "    清完缓存（未压工作集）：%6 %7\n"
                                          "    压工作集后（等 400ms）：%8\n\n"
                                          "提交大小（私有提交，不随映射页变动）\n"
                                          "    重载前：%9\n"
                                          "    重载后：%10 %11\n\n")
                .arg(okCount)
                .arg(total)
                .arg(failed.isEmpty() ? QString()
                                      : QStringLiteral("（失败：") + failed.join(QStringLiteral("、")) + QStringLiteral("）"))
                .arg(botCount)
                .arg(mbText(before.wsKB))
                .arg(mbText(afterWork.wsKB), diffText(before.wsKB, afterWork.wsKB))
                .arg(mbText(afterTrim.wsKB))
                .arg(mbText(before.commitKB))
                .arg(mbText(afterWork.commitKB), diffText(before.commitKB, afterWork.commitKB));

            // 结论按数据算，不写死
            if (before.wsKB > 0 && afterWork.wsKB > 0) {
                const double dWs = (before.wsKB - afterWork.wsKB) / 1024.0;
                const double dCm = (before.commitKB - afterWork.commitKB) / 1024.0;

                if (dWs >= 30.0) {
                    text += QStringLiteral("· 工作集掉了 %1 MB → 这部分就是日志库 mmap 出来的文件页"
                                           "（清理/查询时摸过的那批）。它们是可回收的，系统内存紧张时"
                                           "本来也会被收回，不是泄漏。\n")
                                .arg(QString::number(dWs, 'f', 1));
                } else if (dWs < 5.0) {
                    text += QStringLiteral("· 工作集基本没动 → 日志库映射页不是大头。\n");
                } else {
                    text += QStringLiteral("· 工作集掉了 %1 MB（日志库映射页为主）。\n")
                                .arg(QString::number(dWs, 'f', 1));
                }

                if (qAbs(dCm) < 5.0) {
                    text += QStringLiteral("· 提交大小几乎没变 → 进程自身（Qt / Python / 插件）就占这么多，"
                                           "内存问题不在库和缓存上。");
                } else if (dCm > 0) {
                    text += QStringLiteral("· 提交大小降了 %1 MB → 这部分是我们自己缓存的 Message / 图片副本"
                                           "（真正的私有内存）。")
                                .arg(QString::number(dCm, 'f', 1));
                } else {
                    text += QStringLiteral("· 提交大小反而涨了 %1 MB → 重载时新分配的临时缓冲，"
                                           "过一会儿会被回收，不用管。")
                                .arg(QString::number(-dCm, 'f', 1));
                }
            }

            QMessageBox::information(this, "重载完成", text);
        });
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
        // 证书没放好就开 SSL → http/ws 两个服务都起不来（日志里只有一句「启动失败」，
        // 用户根本不知道是缺证书）。这里先查文件，缺了就提示并**不真的启用**。
        if (ssl) {
            const QString dir = QCoreApplication::applicationDirPath();
            QStringList miss;
            if (!QFile::exists(dir + QStringLiteral("/ssl.crt"))) miss << QStringLiteral("ssl.crt");
            if (!QFile::exists(dir + QStringLiteral("/ssl.key"))) miss << QStringLiteral("ssl.key");
            if (!miss.isEmpty()) {
                QMessageBox::information(this, QStringLiteral("启用 SSL 前先放证书"),
                    QStringLiteral("程序目录里缺少：%1\n\n"
                                   "请放成 PEM 格式：\n"
                                   "  ssl.crt —— 服务器证书\n"
                                   "  ssl.key —— 私钥（不要带密码）\n"
                                   "  ssl.pem —— 可选，中间证书链\n\n"
                                   "目录：%2\n\n放好后再勾一次「启用SSL」。")
                        .arg(miss.join(QStringLiteral("、")), dir));
                ESSL->setChecked(false);         // 只触发 toggled，不会再次进这里
                g_config["SSL"] = false;
                saveConfig();
                return;
            }
        }
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
    // Linux 上无论配置如何都启动 WS —— 和上面的 HTTP 保持一致。
    // 原因：headless 没有界面，用户点不到「启用 WS」那个勾选框，webws_run 一旦是 false
    // 就永远是 false，于是 WebUI 页面能打开、却一直卡在「尚未连接到后端 / not connected」。
#ifdef Q_OS_LINUX
    const bool wsAutoStart = true;
#else
    const bool wsAutoStart = g_config["webws_run"].toBool();
#endif
    if(wsAutoStart)
    {
        int wsPort = g_config["webws_p"].toInt();
        if(wsPort!=0){

            if(ESSL->isChecked())

                Ews->setChecked(ws_server->open(wsPort,"ssl.crt","ssl.key","ssl.pem"));  // 监听 8080 端口
            else
                Ews->setChecked(ws_server->open(wsPort));
            // 让内存里的状态和实际监听结果一致（端口被占时是 false），别处读 webws_run 才准
            g_config["webws_run"] = Ews->isChecked();
        }
    }

#ifdef Q_OS_LINUX
    // Linux 上无论配置如何都尝试启动 WebUI
    // ⚠ 必须自己一层花括号：上面 244 行已经有一个 setupUI() 作用域的 `int port`，
    //   Windows 分支是靠 `if(...){ }` 把变量圈住的，Linux 分支原来没圈 → 重定义（Linux 独有编译错）。
    {
        int port = g_config["webhook_p"].toInt();
        if (port == 0) port = 8080;  // 默认端口
        Ewebhook->setChecked(true);
        Ewebhook->setChecked(onStartStopClicked(port));
    }
#else
    if(g_config["webhook_run"].toBool())
    {
        int port = g_config["webhook_p"].toInt();
        if(port!=0){
            Ewebhook->setChecked(true);
            Ewebhook->setChecked(onStartStopClicked(port));
        }
    }
#endif

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



