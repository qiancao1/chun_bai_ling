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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QPointer>
#include <QRegularExpression>
#include <functional>
#include <vector>       // scanWorkingSetPages() 的 QueryWorkingSetEx 批量缓冲
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

// ── 工作集页级归因 ────────────────────────────────────────────────────────────
// 任务管理器只给一个总数，于是所有人都在猜「到底是数据库还是缓存」。这里按页扫一遍
// 当前工作集（QueryWorkingSetEx），把每个驻留页分成四类：
//   · 私有页     —— 堆/栈/QString/QTextLayout 等进程自己的内存（「提交大小」里真正驻留的那部分）
//   · 映像页     —— 本程序和系统 DLL 的代码段（每个进程都有，省不掉）
//   · 文件映射页 —— mmap 出来的 data.mdb、QPixmap 背后的 DIBSection 位图等（可回收）
//   · 其它共享   —— 共享内存段之类
// 只有这个拆分能回答「日志库占多少、缓存占多少、底座占多少」。
// ⚠ 只在「重载数据库」按钮里跑一次（十几万页，约 0.1~0.5 秒），不进主流程。
struct WsPages {
    qint64 privKB   = 0;
    qint64 imageKB  = 0;
    qint64 mappedKB = 0;
    qint64 otherKB  = 0;
    qint64 pages    = 0;
    bool   ok       = false;
};

WsPages scanWorkingSetPages()
{
    WsPages r;
#ifdef Q_OS_WIN
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const quintptr pageSize = si.dwPageSize ? si.dwPageSize : 4096;
    const qint64 pageKB = static_cast<qint64>(pageSize) / 1024;
    const HANDLE hProc = GetCurrentProcess();

    const int kBatch = 2048;
    std::vector<PSAPI_WORKING_SET_EX_INFORMATION> batch(kBatch);

    quintptr addr = reinterpret_cast<quintptr>(si.lpMinimumApplicationAddress);
    const quintptr maxAddr = reinterpret_cast<quintptr>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi;
    while (addr < maxAddr) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
            break;
        const quintptr regionBase = reinterpret_cast<quintptr>(mbi.BaseAddress);
        const quintptr regionSize = static_cast<quintptr>(mbi.RegionSize);
        if (regionSize == 0) break;

        if (mbi.State == MEM_COMMIT) {
            const quintptr end = regionBase + regionSize;
            quintptr p = regionBase;
            while (p < end) {
                int n = 0;
                for (; n < kBatch && (p + static_cast<quintptr>(n) * pageSize) < end; ++n)
                    batch[n].VirtualAddress = reinterpret_cast<PVOID>(p + static_cast<quintptr>(n) * pageSize);
                if (n <= 0) break;
                if (!QueryWorkingSetEx(hProc, batch.data(),
                                       static_cast<DWORD>(n * sizeof(batch[0]))))
                    break;
                for (int i = 0; i < n; ++i) {
                    const auto &a = batch[i].VirtualAttributes;
                    if (!a.Valid) continue;          // 不在工作集（页被换出）
                    ++r.pages;
                    if (a.Shared) {
                        if (mbi.Type == MEM_IMAGE)       r.imageKB  += pageKB;
                        else if (mbi.Type == MEM_MAPPED) r.mappedKB += pageKB;
                        else                             r.otherKB  += pageKB;
                    } else {
                        r.privKB += pageKB;
                    }
                }
                p += static_cast<quintptr>(n) * pageSize;
            }
            r.ok = true;
        }
        addr = regionBase + regionSize;
    }
#endif
    return r;
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
    m_pubIpBtn = new QPushButton(tr("获取公网IP"), this);
    m_pubIpBtn->setCursor(Qt::PointingHandCursor);
    m_pubIpBtn->setToolTip(tr("问第三方回显服务（myip.ipip.net / api.ipify.org / ip.3322.net）\n"
                              "「你看到我是谁」，把查到的公网 IPv4 填进「对外链接」。\n"
                              "拿到的只是出口 IP：家里宽带还要在路由器上把端口映射到本机。"));
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
        grid->addWidget(fieldUnit(m_addrEdit, {m_pubIpBtn, m_startStopBtn}), 0, 1);
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

        // 测试 webhook 端口是否真的能访问（本机 + 公网两跳，见 testWebhookReach()）
        if (!m_testWebhookBtn)
            m_testWebhookBtn = new QPushButton(tr("测试能否访问"), this);
        m_testWebhookBtn->setCursor(Qt::PointingHandCursor);
        m_testWebhookBtn->setToolTip(tr("先用 127.0.0.1 测本机监听，再用「对外链接」测公网地址能不能连上\n"
                                       "（只看能不能建立 HTTP 响应，404 也算通）"));
        switchRow->addWidget(m_testWebhookBtn);

        grid->addLayout(switchRow, 2, 0, 1, 4);

        connect(m_testWebhookBtn, &QPushButton::clicked, this, [this]() { testWebhookReach(); });
        connect(m_pubIpBtn, &QPushButton::clicked, this, [this]() { fetchPublicIp(); });

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
            "日志库现在是 SQLite：删旧记录后会自动回收空闲页（auto_vacuum=INCREMENTAL），\n"
            "不需要再手工放 compact.force，也不必等 3 天冷却。");
    });

    // 内存诊断：卸载并重新挂载全部日志库 + 清用户/群/图片缓存，前后对比弹窗
    connect(btReloadDb, &QPushButton::clicked, [this]() {
        if (QMessageBox::question(this, "重载数据库",
                "会把所有日志库的数据库连接关掉再重新打开，并清空：\n"
                "· 用户缓存 / 群缓存（BotDB，下次访问自动从库里重读）\n"
                "· 头像 / 图片 / 排版缓存、Qt 全局图片缓存\n\n"
                "日志、配置、库内容一条都不动；当前会话里的图片可能要重新加载一次。\n\n"
                "继续吗？",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;

        // ⚠ 全程 reloadAfterClean(false)：不让它压工作集。
        //   上一版在这里埋了 SetProcessWorkingSetSize，第一步就把私有页也挪进 standby，
        //   于是「工作集掉了 953 MB」被误读成「日志库映射页」—— 而 data.mdb 一共才 223 MB，
        //   物理上不可能是映射页。现在每一步的贡献才是真的。
        const MemSnap before = readProcMem();
        const WsPages pagesBefore = scanWorkingSetPages();   // 未 trim 时的真实构成

        // 1. 日志库：flush 缓存 → 关连接（扔掉 SQLite 的页缓存和临时缓冲）→ 重新打开。
        //    2026-11-11 起日志库是 SQLite（cache_size 硬顶 8MB、不开 mmap），所以这一步
        //    省下的内存很少；留在这里是为了让诊断按钮能如实反映「库占多少」。内存大头
        //    现在是私有堆（Qt / Python / 插件）和图片位图，那部分只有下面的 trim 才动得了。
        int okCount = 0, total = 0;
        QStringList failed;
        for (int i = 0; i < static_cast<int>(g_logdb.size()); ++i) {
            LogDB *db = g_logdb[i].get();
            if (!db) continue;
            ++total;
            if (db->reloadAfterClean(false)) ++okCount;
            else failed << QStringLiteral("logdb_%1").arg(i);
        }
        if (g_errlogdb) {
            ++total;
            if (g_errlogdb->reloadAfterClean(false)) ++okCount;
            else failed << QStringLiteral("logdb_err");
        }
        const MemSnap afterDb = readProcMem();

        // 2. 用户 / 群缓存（BotDB 里那两份 QHash，本来只在 4h / 24h 定时清）
        int botCount = 0;
        for (BotDB *db : g_botdb) {
            if (!db) continue;
            db->clearAllCaches();
            ++botCount;
        }
        const MemSnap afterBot = readProcMem();

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
        QTimer::singleShot(400, this, [this, before, afterDb, afterBot, afterWork,
                                      okCount, total, failed, botCount, pagesBefore]() {
            const MemSnap afterTrim = readProcMem();

            auto diffText = [](qint64 a, qint64 b) -> QString {
                if (a < 0 || b < 0) return QString();
                const double d = (b - a) / 1024.0;
                return QStringLiteral("（%1%2 MB）")
                    .arg(d <= 0 ? QStringLiteral("-") : QStringLiteral("+"))
                    .arg(QString::number(qAbs(d), 'f', 1));
            };

            QStringList L;
            L << QStringLiteral("日志库重载：%1/%2 成功%3")
                 .arg(okCount).arg(total)
                 .arg(failed.isEmpty() ? QString()
                                       : QStringLiteral("（失败：") + failed.join(QStringLiteral("、")) + QStringLiteral("）"));
            L << QStringLiteral("已清空用户/群缓存：%1 个账号\n").arg(botCount);

            L << QStringLiteral("工作集（任务管理器「内存」列）—— 每步都不压工作集，是真实卸载");
            L << QStringLiteral("    起始：              %1").arg(mbText(before.wsKB));
            L << QStringLiteral("    日志库卸载重载后：  %1 %2")
                 .arg(mbText(afterDb.wsKB), diffText(before.wsKB, afterDb.wsKB));
            L << QStringLiteral("    清用户/群缓存后：   %1 %2")
                 .arg(mbText(afterBot.wsKB), diffText(afterDb.wsKB, afterBot.wsKB));
            L << QStringLiteral("    清界面缓存后：      %1 %2")
                 .arg(mbText(afterWork.wsKB), diffText(afterBot.wsKB, afterWork.wsKB));
            L << QStringLiteral("    压工作集后（400ms）：%1  ← 只是把冷页挪进 standby，"
                                "不代表真省了这么多").arg(mbText(afterTrim.wsKB));
            L << QString();

            // 起始工作集构成 —— 这一段才是「到底占在哪」的答案
            if (pagesBefore.ok) {
                L << QStringLiteral("起始工作集构成（按页扫描 %1 页）").arg(pagesBefore.pages);
                L << QStringLiteral("    私有页（堆 / QString / 排版）      %1").arg(mbText(pagesBefore.privKB));
                L << QStringLiteral("    程序与系统 DLL（代码段）          %1").arg(mbText(pagesBefore.imageKB));
                L << QStringLiteral("    文件映射页（图片位图 / 字体等）     %1").arg(mbText(pagesBefore.mappedKB));
                if (pagesBefore.otherKB > 0)
                    L << QStringLiteral("    其它共享                          %1").arg(mbText(pagesBefore.otherKB));
                L << QString();
            }

            L << QStringLiteral("提交大小（私有提交）：%1 → %2 %3")
                 .arg(mbText(before.commitKB), mbText(afterWork.commitKB),
                      diffText(before.commitKB, afterWork.commitKB));
            L << QString();

            // 结论按数据算，不写死
            if (before.wsKB > 0 && afterWork.wsKB > 0) {
                const double dWs = (before.wsKB - afterWork.wsKB) / 1024.0;
                const double dCm = (before.commitKB - afterWork.commitKB) / 1024.0;
                if (dWs >= 5.0) {
                    L << QStringLiteral("· 卸载日志库 + 清缓存，工作集掉了 %1 MB。")
                         .arg(QString::number(dWs, 'f', 1));
                }
                if (qAbs(dCm) < 5.0) {
                    L << QStringLiteral("· 提交大小几乎没变 → 掉的都是映射页（可回收），"
                                        "私有内存本来就没被释放。");
                }
            }

            if (pagesBefore.ok) {
                const qint64 mapMB  = pagesBefore.mappedKB / 1024;
                const qint64 privMB = pagesBefore.privKB / 1024;
                const qint64 imgMB  = pagesBefore.imageKB / 1024;
                if (mapMB > privMB) {
                    L << QStringLiteral("· 起始工作集里最大的是「文件映射页」%1 MB →"
                                        "主要是图片位图（QPixmap 在 Windows 上是 DIBSection，也算映射页）"
                                        "和字体/资源文件。这部分是全进程最「可回收」的："
                                        "淘汰即可，不是泄漏。")
                         .arg(mapMB);
                } else {
                    L << QStringLiteral("· 起始工作集里最大的是「私有页」%1 MB → 占用在进程自己的堆上"
                                        "（QString / 排版 / 插件 / Python），跟日志库和图片缓存无关。")
                         .arg(privMB);
                }
                L << QStringLiteral("· DLL 代码段 %1 MB，是每个进程都有的底座，动不了。").arg(imgMB);
            }

            QMessageBox::information(this, "重载完成", L.join(QStringLiteral("\n")));
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




// ===========================================================================
// 「获取公网IP」
//
// 「公网 IP」不是本机能算出来的：本机只知道自己的内网地址（192.168.x.x），
// 出口 IP 在你路由器 / 运营商那侧。通用的做法是**问一个回显服务**——
// 它把「这次请求的来源地址」原样回给你，等于告诉你「外面看到你是谁」。
// 这类服务都免 key、只要 GET，按「国内优先」排三条，前一条失败自动换下一条：
//   ① https://myip.ipip.net              纯文本「当前 IP：1.2.3.4  来自于：中国 广东 深圳 电信」
//   ② https://api.ipify.org?format=json  {"ip":"1.2.3.4"}
//   ③ http://ip.3322.net/                纯文本 IP（老牌国内 DDNS 回显，只在 https 都挂时兜底）
// ⚠ 拿到的是「出口 IP」：家里 / 公司宽带大多在 NAT 后面，这个 IP 在路由器上，
//   要让平台推事件进来还得做端口映射（云服务器则是放行安全组），所以填完会提示一句。
// ===========================================================================
namespace {

// 从任意文本里挑第一个合法 IPv4：ipip 是中文句子、ipify 是 JSON、3322 是裸 IP，
// 统一用正则捞（顺便校验每段 0-255，防「2025.11.25」这种日期被误认成 IP）。
QString pickIPv4(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("(\\d{1,3}(?:\\.\\d{1,3}){3})"));
    auto it = re.globalMatch(text.left(600));
    while (it.hasNext()) {
        const QString ip = it.next().captured(1);
        const QStringList parts = ip.split(QLatin1Char('.'));
        if (parts.size() != 4)
            continue;
        bool ok = true;
        for (const QString &p : parts) {
            const int v = p.toInt(&ok);
            if (!ok || v < 0 || v > 255) { ok = false; break; }
        }
        if (ok)
            return ip;
    }
    return QString();
}

// 内网 / 保留 / 运营商级 NAT：这种地址填进「对外链接」外面肯定进不来
bool isPrivateIPv4(const QString &ip)
{
    const QStringList p = ip.split(QLatin1Char('.'));
    if (p.size() != 4)
        return false;
    const int a = p.at(0).toInt();
    const int b = p.at(1).toInt();
    if (a == 10 || a == 127 || a == 0)          return true;
    if (a == 192 && b == 168)                   return true;
    if (a == 172 && b >= 16 && b <= 31)         return true;
    if (a == 169 && b == 254)                   return true;   // 链路本地（网卡没拿到 DHCP）
    if (a == 100 && b >= 64 && b <= 127)        return true;   // CGNAT：运营商级 NAT，一样进不来
    return false;
}

// 探测链：串行问几个回显服务，第一个给出合法 IPv4 的就用。
// 做成挂在 set 下面的 QObject，结束时 deleteLater() 自删 —— 不用 shared_ptr 自引用
// （lambda 里存 shared_ptr<function> 会形成环，点一次泄漏一次）。
class PublicIpProbe : public QObject
{
public:
    struct Src { const char *url; const char *name; };
    // done(成功?, IP, 归属地(ipip 才有), 失败明细/命中的服务名)
    using DoneCb = std::function<void(bool, const QString &, const QString &, const QString &)>;

    PublicIpProbe(QObject *parent, DoneCb done)
        : QObject(parent), m_done(std::move(done))
    {
        m_nam = new QNetworkAccessManager(this);
    }

    void start() { query(0); }

private:
    void query(int idx)
    {
        static const Src kSrcs[] = {
            { "https://myip.ipip.net",             "myip.ipip.net" },
            { "https://api.ipify.org?format=json", "api.ipify.org" },
            { "http://ip.3322.net/",               "ip.3322.net" },
        };
        const int n = int(sizeof(kSrcs) / sizeof(kSrcs[0]));

        if (idx >= n) {                       // 三条全挂
            m_done(false, QString(), QString(), m_errs.join(QLatin1Char('\n')));
            deleteLater();
            return;
        }

        const QString name = QString::fromLatin1(kSrcs[idx].name);
        QNetworkRequest req{QUrl(QString::fromLatin1(kSrcs[idx].url))};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        // 有的回显站按 UA 判人机，不带就 403 —— 给个普通浏览器串
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64)"));

        QNetworkReply *r = m_nam->get(req);
        QTimer::singleShot(6000, r, [r]() { if (r->isRunning()) r->abort(); });

        connect(r, &QNetworkReply::finished, this, [this, r, idx, name]() {
            const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = r->readAll();
            const auto err = r->error();
            const QString errStr = r->errorString();     // deleteLater 前先取
            r->deleteLater();

            if (status >= 200 && status < 300) {
                const QString text = QString::fromUtf8(data.left(600));
                const QString ip = pickIPv4(text);
                if (!ip.isEmpty()) {
                    // ipip 的原文带归属地，捞出来当提示（其余两个没有）
                    QString loc;
                    const int p = text.indexOf(QStringLiteral("来自于"));
                    if (p >= 0)
                        loc = text.mid(p + 3).simplified().trimmed();
                    m_done(true, ip, loc, name);
                    deleteLater();
                    return;
                }
            }

            QString why;
            if (status > 0)
                why = QStringLiteral("HTTP %1，返回里没有 IP").arg(status);
            else if (err == QNetworkReply::OperationCanceledError || err == QNetworkReply::TimeoutError)
                why = QStringLiteral("6 秒没响应（超时）");
            else
                why = errStr.isEmpty() ? QStringLiteral("连不上") : errStr;
            m_errs << QStringLiteral("· %1：%2").arg(name, why);
            query(idx + 1);     // 换下一个
        });
    }

    QNetworkAccessManager *m_nam = nullptr;
    DoneCb m_done;
    QStringList m_errs;
};

} // namespace

void set::fetchPublicIp()
{
    if (!m_pubIpBtn)
        return;

    m_pubIpBtn->setEnabled(false);
    m_pubIpBtn->setText(tr("获取中…"));

    auto *probe = new PublicIpProbe(this,
        [this](bool ok, const QString &ip, const QString &loc, const QString &detail) {
            if (m_pubIpBtn) {
                m_pubIpBtn->setEnabled(true);
                m_pubIpBtn->setText(tr("获取公网IP"));
            }

            if (!ok) {
                QMessageBox::warning(this, tr("获取公网IP失败"),
                    QStringLiteral("三个回显服务都没问到：\n\n%1\n\n"
                                   "· 本机断网，或者走了代理但代理不通？\n"
                                   "· 防火墙 / 安全软件拦了程序的出站请求？\n"
                                   "· 也可以自己在浏览器打开 https://myip.ipip.net 手抄一个填进来。")
                        .arg(detail));
                return;
            }

            // 当前填的是域名（含字母）就别默默覆盖，先问一句
            const QString cur = m_addrEdit->text().trimmed();
            bool looksDomain = false;
            for (const QChar &c : cur) {
                if (c.isLetter()) { looksDomain = true; break; }
            }
            if (looksDomain && cur != ip
                && QMessageBox::question(this, tr("要替换吗"),
                       QStringLiteral("「对外链接」现在填的是：\n%1\n\n"
                                      "要换成刚查到的公网 IP %2 吗？").arg(cur, ip))
                       != QMessageBox::Yes) {
                return;
            }

            m_addrEdit->setText(ip);

            QString msg = QStringLiteral("已填入：%1").arg(ip);
            if (!loc.isEmpty())
                msg += QStringLiteral("\n归属地：%1").arg(loc);
            msg += QStringLiteral("\n（来自 %1）\n\n").arg(detail);

            if (isPrivateIPv4(ip))
                msg += QStringLiteral("⚠ 这是内网 / 运营商级 NAT 地址，外面进不来：\n"
                                      "需要在路由器上做端口映射，或者用内网穿透。");
            else
                msg += QStringLiteral("这是本机对外的出口 IP。还要做两件事才收得到推送：\n"
                                      "· 点「保存」把它写进配置；\n"
                                      "· 本机在路由器后面 → 路由器上把这个端口映射到这台机器；\n"
                                      "  云服务器 → 安全组放行 webhook 端口。\n"
                                      "填好后可以点「测试能否访问」验一下。");

            QMessageBox::information(this, tr("获取公网IP成功"), msg);
        });
    probe->start();
}

// ===========================================================================
// 「测试能否访问」：先打 127.0.0.1 看监听起没起，再按「对外链接」打公网地址
//
// 判定口径：**只要连上并收到 HTTP 响应就算通**（404 也算通 —— 说明服务在，
// 只是没这个路径）。连不上 / 解析不了 / 超时 / 握手失败才算不通。
// 探测不走 NetManager（它拿不到状态码），这里自带 QNAM；https 不校验证书，
// 因为自签证书也要能测通，这条只用来判断「能不能连上」。
// ===========================================================================
void set::testWebhookReach()
{
    const int port = g_config["webhook_p"].toInt();
    if (port <= 0 || port > 0xffff) {
        QMessageBox::warning(this, "端口不对",
            "webhook 端口没配置（或不在 1-65535）。先填端口并点确认。");
        return;
    }
    if (!Ewebhook->isChecked()) {
        QMessageBox::warning(this, "webhook 没启用",
            QStringLiteral("「启动用webhook」没勾上，端口 %1 上没有任何服务在监听。\n\n"
                           "勾上之后点确认，再回来测。").arg(port));
        return;
    }

    const bool ssl = ESSL->isChecked();

    // 对外链接允许三种写法：host / host:port / http(s)://host[:port][/path]
    auto makeUrl = [ssl, port](const QString &base) -> QUrl {
        QString b = base.trimmed();
        if (b.isEmpty()) return QUrl();
        if (!b.contains(QStringLiteral("://")))
            b.prepend(ssl ? QStringLiteral("https://") : QStringLiteral("http://"));
        QUrl u(b);
        if (u.port() <= 0) u.setPort(port);
        u.setPath(QStringLiteral("/"));
        u.setQuery(QString());
        return u;
    };

    const QUrl localUrl  = makeUrl(QStringLiteral("127.0.0.1"));
    const QUrl publicUrl = makeUrl(m_addrEdit->text());

    if (m_testWebhookBtn) m_testWebhookBtn->setEnabled(false);

    // 单次探测：done(是否通, 一句话结果)
    auto probe = [this](const QString &label, const QUrl &url,
                        std::function<void(bool, QString)> done) {
        auto *nam = new QNetworkAccessManager(this);
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QSslConfiguration cfg = QSslConfiguration::defaultConfiguration();
        cfg.setPeerVerifyMode(QSslSocket::VerifyNone);   // 自签证书也算通
        req.setSslConfiguration(cfg);

        QNetworkReply *r = nam->get(req);
        // 自签 / 过期证书：直接忽略（这条只测连通性）
        connect(r, &QNetworkReply::sslErrors, r, [r](const QList<QSslError> &) { r->ignoreSslErrors(); });

        QPointer<QNetworkReply> guard(r);
        QTimer::singleShot(8000, r, [guard]() {
            if (guard && guard->isRunning()) guard->abort();
        });

        connect(r, &QNetworkReply::finished, this,
            [r, nam, label, url, done]() {
                const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                const auto err = r->error();
                const QString errStr = r->errorString();   // deleteLater 前先取
                r->deleteLater();
                nam->deleteLater();

                if (status > 0) {         // 收到 HTTP 响应就是通
                    done(true, QStringLiteral("%1  %2 → 通（HTTP %3）")
                                   .arg(label, url.toString()).arg(status));
                    return;
                }

                QString why;
                switch (err) {
                case QNetworkReply::ConnectionRefusedError:
                    why = QStringLiteral("连接被拒（端口没在监听 / 防火墙拦了）"); break;
                case QNetworkReply::HostNotFoundError:
                    why = QStringLiteral("域名解析失败（地址写错了？）"); break;
                case QNetworkReply::TimeoutError:
                case QNetworkReply::OperationCanceledError:
                    why = QStringLiteral("超时（8 秒没响应）"); break;
                case QNetworkReply::SslHandshakeFailedError:
                    why = QStringLiteral("SSL 握手失败"); break;
                default:
                    why = errStr.isEmpty()
                              ? QStringLiteral("连不上（错误码 %1）").arg(int(err))
                              : errStr;
                    break;
                }
                done(false, QStringLiteral("%1  %2 → 不通：%3")
                               .arg(label, url.toString(), why));
            });
    };

    // 先本机，后公网（串行，避免两条都超时干等 16 秒分不清哪个慢）
    probe(QStringLiteral("【本机】"), localUrl,
          [this, publicUrl, probe](bool localOk, QString localText) {
              if (publicUrl.isEmpty()) {
                  if (m_testWebhookBtn) m_testWebhookBtn->setEnabled(true);
                  QMessageBox::information(this,
                      localOk ? "本机能访问" : "本机也不通",
                      localText + "\n\n"
                      + (localOk
                            ? "本机监听是正常的。\n「对外链接」是空的，所以没测公网地址 —— "
                              "要测公网就把对外链接填上（域名或 IP，点旁边的保存）。"
                            : "本机都连不上，说明服务没真的起来：\n"
                              "· 「启动用webhook」勾了吗？端口点了确认吗？\n"
                              "· 端口是不是被别的程序占了（日志里搜「启动服务器启动失败」）。"));
                  return;
              }

              probe(QStringLiteral("【对外】"), publicUrl,
                    [this, localOk, localText](bool pubOk, QString pubText) {
                        if (m_testWebhookBtn) m_testWebhookBtn->setEnabled(true);

                        QString tail;
                        if (localOk && pubOk)
                            tail = "两条都通 —— 平台能推到这个地址。记得去开放平台订阅事件。";
                        else if (localOk && !pubOk)
                            tail = "本机是通的，问题在「外面进不来」：\n"
                                   "· 云服务器安全组 / 防火墙放行这个端口了吗？\n"
                                   "· 路由器端口映射（内网穿透）对吗？对外 IP 是公网 IP 吗？\n"
                                   "· 用域名的话，DNS 解析到这个 IP 了吗？\n"
                                   "· 「对外链接」里不该带 http:// 以外的路径。";
                        else if (!localOk && pubOk)
                            tail = "公网能通但本机 127.0.0.1 不通，比较怪：\n"
                                   "可能服务监听在指定网卡上，或对外地址其实指向了另一台机器（反代）。";
                        else
                            tail = "两条都不通：先把服务起起来（勾「启动用webhook」+ 点端口确认），"
                                   "再看防火墙/端口映射。";

                        QMessageBox::information(this,
                            (localOk && pubOk) ? "可以访问" : "有不通的",
                            localText + "\n" + pubText + "\n\n" + tail);
                    });
          });
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



