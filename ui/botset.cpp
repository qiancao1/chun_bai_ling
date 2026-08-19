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
        ui->textEdit_3->setPlaceholderText("用户加群时 #python开头为py代码\npy代码有个全局数组 UserList 这个是用户id\n非py代码时 变量 {ID} {艾特} {数量} {头像} {混合}\n当前开启延迟时 推荐 {混合} 标签");
        ui->lineEdit->setText(QString::number(info->fasjg));
        ui->lineEdit_4->setText(QString::number(info->rq_ychf));

        ui->textEdit_2->setText(info->tqhy);
        ui->textEdit_3->setPlaceholderText("用户加群时 #python开头为py代码\npy代码有个全局数组 UserList 这个是用户id\n非py代码时 有变量 {ID} {艾特} {数量} {头像} {混合}\n当前开启延迟时 推荐 {混合} 标签 艾特可能无效");
        ui->lineEdit_2->setText(QString::number(info->tq_ychf));
        ui->lineEdit_3->setText(QString::number(info->tq_lq));
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


    info->tqhy= ui->textEdit_2->toPlainText();

    info->tq_ychf = ui->lineEdit_2->text().toInt();
    info->tq_lq = ui->lineEdit_3->text().toInt();
    info->rq_ychf = ui->lineEdit_4->text().toInt();
    info->rq_lq = ui->lineEdit_4->text().toInt();
    accountPage->saveAccounts(info.get());
}

