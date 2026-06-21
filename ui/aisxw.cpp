#include "aisxw.h"
#include "global.h"
#include "ui_aisxw.h"

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
        ui->listWidget->addItem(openid);
    }
}


void aisxw::on_pushButton_3_clicked()
{
    ui->listWidget->clear();
    for(const auto &sess : std::as_const(ai_ui->m_sessions))
    {
        ui->listWidget->addItem(sess.openid);
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
    if(ai_ui->m_sessions.contains(m_openid))
    {
        auto &sess = ai_ui->m_sessions[m_openid];
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
    m_openid = item->text();
    QString content = aidb->get(m_openid);

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError) {

        content = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }

    ui->textEdit->setPlainText(content);
}
