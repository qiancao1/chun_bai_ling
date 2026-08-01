#include "set.h"
#include "global.h"
#include <QSettings>
#include <QMessageBox>
#include <QHeaderView>
#include <QColorDialog>
#include <QTimer>
#include <qclipboard.h>
#include "websocketserver.h"
void stopImageServer();
bool startImageServer(quint16 port,const QString &certPath = "",const QString &keyPath = "",const QString &ssl_pem ="");


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


void set::setupUI()
{
    QVBoxLayout *mainVLayout = new QVBoxLayout(this);
    mainVLayout->setContentsMargins(4, 4, 4, 4);
    mainVLayout->setSpacing(8);
    mainVLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // 模式行
    QHBoxLayout *remoteLayout1 = new QHBoxLayout;
    remoteLayout1->setAlignment(Qt::AlignLeft);
    QLabel *urlLabel1 = new QLabel(tr("ffmpeg路径："), this);
    m_ffmpegpath = new QLineEdit(this);
    m_ffmpegpath->setPlaceholderText("ffmpeg/");
    m_ffmpegpath->setMinimumWidth(250);

    QPushButton *bt = new QPushButton(tr("确认"), this);
    QLabel *urlLabel3 = new QLabel(tr("日志数量"), this);
    m_日志数量 = new QLineEdit(this);
    m_日志数量->setPlaceholderText("默认10w条 看你电脑配置来 是永久缓存 这样子可用存更多聊天信息");
    m_日志数量->setText(QString::number(g_config["logs"].toInt(100000)));
    m_日志数量->setMinimumWidth(100);
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


    remoteLayout1->addWidget(urlLabel1);
    remoteLayout1->addWidget(m_ffmpegpath);
    remoteLayout1->addWidget(bt);

    remoteLayout1->addWidget(urlLabel3);
    remoteLayout1->addWidget(m_日志数量);
    remoteLayout1->addWidget(bt2);

    remoteLayout1->addWidget(urlLabel4);
    remoteLayout1->addWidget(colorPreview);


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

    remoteLayout1->addWidget(urlLabel5);
    remoteLayout1->addWidget(colorPreview2);

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

    QHBoxLayout *remoteLayout2 = new QHBoxLayout;
    remoteLayout2->setAlignment(Qt::AlignLeft);

    remoteLayout2->addWidget(new QLabel("对外链接："));
    remoteLayout2->addWidget(m_addrEdit);
    remoteLayout2->addWidget(m_startStopBtn);


    remoteLayout2->addWidget(new QLabel("webhook端口："));
    remoteLayout2->addWidget(webhook);
    remoteLayout2->addWidget(webhook_but);

    remoteLayout2->addWidget(new QLabel("聊天室端口："));
    remoteLayout2->addWidget(webws_port);
    remoteLayout2->addWidget(web_qr);

    remoteLayout2->addWidget(lts_but);


    QHBoxLayout *localLayout = new QHBoxLayout;
    localLayout->setAlignment(Qt::AlignLeft);


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

    localLayout->addWidget(Ewebhook);
    localLayout->addWidget(Ews);
    localLayout->addWidget(ESSL);
    localLayout->addWidget(Eimg);
    localLayout->addWidget(loadimg);
    localLayout->addWidget(m_loadport);
    localLayout->addWidget(m_loadbut);
    mainVLayout->addLayout(remoteLayout1);


    mainVLayout->addLayout(remoteLayout2);
    mainVLayout->addLayout(localLayout);

    m_admid_deit = new QTextEdit;
    m_admid_deit->setPlainText("空格分割 允许触发 webui 全局禁用启用插件 启用ws");
    m_admid_deit->setText(g_admin);
    m_admin_qr = new QPushButton("保存全局管理");
    mainVLayout->addWidget(new QLabel("超级管理员：空格分割"));
    mainVLayout->addWidget(m_admid_deit);
    mainVLayout->addWidget(m_admin_qr);


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



