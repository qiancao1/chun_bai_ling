#include "botset.h"
#include "global.h"
#include "ui/ui_botset.h"


botset::botset(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::botset)
{
    ui->setupUi(this);
}

botset::~botset()
{
    delete ui;
}
int accinfo(int appid);
void botset::列表行被单击()
{
    if (g_appid!=0) {
        int index=accinfo(g_appid);
        if(index==-1) return;

        auto &info = m_accounts [index];
        ui->wclhf->setPlainText(info->fallbackReply);
        ui->textEdit->setPlainText(info->welcomeMsg);
        ui->textEdit_3->setPlainText(info->rqhy);
        ui->lineEdit->setText(QString::number(info->fasjg));
    }
}
void botset::on_pushButton_clicked()
{
    if(g_appid<=0)
    {
        QMessageBox::warning(this,"还没选中机器人","请选中一个机器人再点击保存");
        return;
    }
    int index=accinfo(g_appid);
    if(index==-1){
        QMessageBox::warning(this,"失败","保存失败 保存的指定机器人 好像不在于账号列表 请重新选择 机器人");
        return;
    }
    auto &info = m_accounts [index];
    info->fallbackReply = ui->wclhf->toPlainText();
    info->welcomeMsg= ui->textEdit->toPlainText();
    info->rqhy = ui->textEdit_3->toPlainText();

    info->fasjg = ui->lineEdit->text().toInt();
    accountPage->saveAccounts(info.get());
}

