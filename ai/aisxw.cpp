#include "aisxw.h"
#include "global.h"
#include "ui_aisxw.h"
#include <QMessageBox>
aisxw::aisxw(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::aisxw)
{
    ui->setupUi(this);
}

aisxw::~aisxw()
{
    delete ui;
}

void aisxw::on_pushButton_2_clicked()
{
    QStringList list = aidb->getAllKeys();
    ui->listWidget->clear();
    for(const auto &openid : std::as_const(list))
    {
        QStringList list = openid.split(":");
        if(list.size()<2) continue;
        int appid = list[0].toInt();
        QString name;
        if(m_botClients.contains(appid)){
            MessageEvent ev;
            ev.appid = appid;
            ev.type = 0;

            ev.groupId =list[1];
            ev.user =list[1];
            if(g_botdb.contains(appid))
                g_botdb[appid]->getOrUpdateUser(m_botClients[appid],ev,true);
            name = ev.nickname;
            if(name.isEmpty()) name = ev.groupname;

        }
        if(name.isEmpty()) name = openid;
        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole,openid);
        ui->listWidget->addItem(item);
    }
}


void aisxw::on_pushButton_3_clicked()
{
    ui->listWidget->clear();
    for(const auto &sess : std::as_const(ai_ui->m_sessions))
    {
        QString name;
        if(sess.type == 0 || sess.type ==2){
            if(m_botClients.contains(sess.appid)){
                MessageEvent ev;
                ev.appid = sess.appid;
                ev.type = sess.type;

                ev.groupId =sess.groupId;
                ev.user = sess.openid;
                if(g_botdb.contains(sess.appid))
                    g_botdb[sess.appid]->getOrUpdateUser(m_botClients[sess.appid],ev,true);
                name = ev.nickname;
                if(name.isEmpty())
                    name=ev.groupname;

            }
        }
        if(name.isEmpty()) name = sess.openid;
        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole,QString("%1:%2").arg( sess.appid).arg(sess.openid));
        ui->listWidget->addItem(item);
    }
}


void aisxw::on_pushButton_clicked()
{
    if(m_openid.isEmpty())
    {
        QMessageBox::warning(this,"保存失败","请点击列表任意成员 打开原上下文");
        return ;
    }
    QString text = ui->textEdit->toPlainText();
    if(text.isEmpty()) text = "{}";
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
    {
        QMessageBox::warning(this,"保存失败","保存失败 保存的内容无法被json解析 请确保json完整");
        return ;
    }
    QString openid = m_openid.section(':', -1);
    if(ai_ui->m_sessions.contains(openid))
    {
        auto &sess = ai_ui->m_sessions[openid];
        if(sess.isProcessing)
        {
            QMessageBox::warning(this,"保存失败","被修改上下文对象正在与ai对话中");
            return;
        }
    }
    aidb->put(m_openid, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}


void aisxw::on_listWidget_itemClicked(QListWidgetItem *item)
{
    m_openid = item->data(Qt::UserRole).toString();
    QString content = aidb->get(m_openid);

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError) {

        content = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    ui->textEdit->setPlainText(content);
}
