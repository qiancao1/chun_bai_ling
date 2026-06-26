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
        info->shuap=ui->checkBox->isChecked();
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
        ui->checkBox->setChecked(info->shuap);
        ui->checkBox_2->setChecked(info->pbbot);
    }
}
void qunguan::on_checkBox_2_checkStateChanged(const Qt::CheckState &arg1)
{
    if (g_appid!=0) {
        int index=accinfo(g_appid);
        if(index==-1){
            QMessageBox::warning(this,"失败","保存失败 保存的指定机器人 好像不在于账号列表 请重新选择 机器人");
            return;
        }
        auto &info = m_accounts [index];
        info->pbbot=ui->checkBox_2->isChecked();
        accountPage->saveAccounts(info.get());
    }
}

