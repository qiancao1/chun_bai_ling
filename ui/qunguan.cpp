#include "qunguan.h"
#include "global.h"
#include "ui_qunguan.h"
#include <QMessageBox>
int accinfo(int appid);
qunguan::qunguan(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::qunguan)
{
    ui->setupUi(this);
}

qunguan::~qunguan()
{
    delete ui;
}

void qunguan::on_pushButton_clicked()
{
    if (g_appid!=0) {
        int index=accinfo(g_appid);
        if(index==-1){
            QMessageBox::warning(this,"失败","保存失败 保存的指定机器人 好像不在于账号列表 请重新选择 机器人");
            return;
        }
        auto &info = m_accounts [index];
        info->times =ui->time_Edit->text().toInt();
        info->tiaoshu=ui->tiao_Edit->text().toInt();

        info->pbbot=ui->checkBox_2->isChecked();
        info->autoht=ui->zdht->isChecked();
        info->admin =ui->textEdit->toPlainText();

        info->help =ui->textEdit_4->toPlainText();
        info->caidan =ui->textEdit_2->toPlainText();
        info->at =ui->textEdit_3->toPlainText();

        accountPage->saveAccounts(info.get());
    }
}

void qunguan::列表行被单击()
{
    if (g_appid!=0) {
        int index=accinfo(g_appid);
        if(index==-1) return;
        auto &info = m_accounts [index];
        ui->time_Edit->setText(QString::number(info->times));
        ui->tiao_Edit->setText(QString::number(info->tiaoshu));

        ui->checkBox_2->setChecked(info->pbbot);
        ui->textEdit->setText(info->admin);
        ui->zdht->setChecked(info->autoht);
        ui->textEdit_2->setText(info->caidan);
        ui->textEdit_3->setText(info->at);
        ui->textEdit_4->setText(info->help);
    }
}







