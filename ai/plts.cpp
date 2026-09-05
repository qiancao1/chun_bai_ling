#include "plts.h"
#include "global.h"
#include "ui_plts.h"
#include <QMessageBox>
#include <QThreadPool>

int 成功数量g=0;
QList<int> ts_m_friendStatus;
QList<QString> ts_m_groupStatus;
QString ts_m_text= QString();
bool ts_m_stopPush = false;

                    // 应用ID，用于文件名
plts::plts(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::plts)
{

    ui->setupUi(this);

    m_saveTimer = new QTimer(this);
    connect(m_saveTimer, &QTimer::timeout, [this](){
        保存();
        if(!ts_m_stopPush) m_saveTimer->stop();
    });
}

plts::~plts()
{
    delete ui;
}

void plts::extracted(QList<int> &pendingFriends) {
    for (int seq : std::as_const(ts_m_friendStatus)) {
        if (seq != 0)
            pendingFriends.append(seq);
    }
}
void plts::extracted(QList<QString> &pendingGroups) {
    for (const QString &gid : std::as_const(ts_m_groupStatus)) {
        if (!gid.isEmpty())
            pendingGroups.append(gid);
    }
}
bool plts::保存() {
    // 好友列表：只保存未完成的（seq != 0）
    QList<int> pendingFriends;
    extracted(pendingFriends);

    QFile fFile(QString("data/%1_f.bin").arg(g_appid));
    if (!fFile.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&fFile);
    out << pendingFriends; // 只写未完成的
    fFile.close();

    // 群列表：只保存未完成的（非空）
    QList<QString> pendingGroups;
    extracted(pendingGroups);

    QFile gFile(QString("data/%1_g.bin").arg(g_appid));
    if (!gFile.open(QIODevice::WriteOnly))
        return false;
    QDataStream out2(&gFile);
    out2 << pendingGroups;
    gFile.close();
    if (pendingGroups.size() == 0 && pendingFriends.size() == 0)
        ts_m_stopPush = false;

    int g_len = ts_m_groupStatus.size();
    int f_len = ts_m_friendStatus.size();
    ui->ts_ztbq->setText(QString("群(正在推送)：%1 / %2 好友： %3 / %4").arg(g_len-pendingGroups.size()).arg(g_len).arg(f_len-pendingFriends.size()).arg(f_len));
    return true;
}
void plts::加载()
{
    ts_m_friendStatus.clear();
    ts_m_groupStatus.clear();

    QFile fFile(QString("data/%1_f.bin").arg(g_appid));
    if (fFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&fFile);
        in >> ts_m_friendStatus;
        fFile.close();
    }
    QFile gFile(QString("data/%1_g.bin").arg(g_appid));
    if (gFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&gFile);
        in >> ts_m_groupStatus;
        gFile.close();
    }
}
void plts::on_sctswj_2_clicked()
{
    if(ts_m_stopPush)
    {
        QMessageBox::warning(this,"已经在运行","已经在群发中 点击这个按钮无效");
        return;
    }
    if(!g_botdb.contains(g_appid))
    {
        QMessageBox::warning(this,"生成失败","请登录账号 自动打开数据库后再试试");
        return;
    }
    if(ui->checkBox_tsq->checkState() == false && ui->checkBox_tsq->checkState() == ui->checkBox_tssl->checkState()) //懒得优化就这样子吧
    {
        QMessageBox::warning(this,"生成失败","推送群 或推送好友必须打勾一个");
        return ;
    }
    auto *db = g_botdb[g_appid];
    ts_m_friendStatus.clear();
    ts_m_groupStatus.clear();
    if(ui->checkBox_tsq->checkState())
    {
        for (auto it = chatPage->全量群.begin(); it != chatPage->全量群.end(); ++it) {
            int appid =it.value();
            if (g_appid != appid) continue;
            ts_m_groupStatus.append(it.key());
        }
    }
    if(ui->checkBox_tssl->checkState())
        ts_m_friendStatus = db->getFriendList();
    if(ts_m_groupStatus.size()==0 && ts_m_friendStatus.size()==0)
    {
        QMessageBox::warning(this,"失败","要推送的群或好友为0 生成失败");
        return ;
    }

    if(保存()) QMessageBox::warning(this,"生成完成","点击开始推送进行推送吧 注意 群 只选了 全量群 其他群没主动能力也不行");
    else QMessageBox::warning(this,"生成失败","写出到文件失败");
}
void plts::on_sctswj_clicked(bool checked)
{
    if(ts_m_stopPush)
    {
        QMessageBox::warning(this,"已经在运行","已经在群发中 点击这个按钮无效");
        return;
    }

    QFile::remove(QString("data/%1_f.bin").arg(g_appid));
    QFile::remove(QString("data/%1_g.bin").arg(g_appid));

}
void plts::on_tzts_clicked(bool checked)
{
    ts_m_stopPush = false;
    m_saveTimer->stop();
    保存();

}



class ___tsnr_f : public QRunnable {
public:
    // 通过构造函数把需要的数据传进来（如果有的话）
    ___tsnr_f() {}

    void run() override {
        auto *db = g_botdb[g_appid];
        QQBotClient *bot = m_botClients[g_appid];

        QString pname = "[批量推送]";
        int len = ts_m_friendStatus.size();
        const int maxPerSec = 8;
        const int minIntervalMs = 1000 / maxPerSec;  // 125ms

        QElapsedTimer timer;
        timer.start();  // 用于计算每次调用的时间点

        for (int i = 0; i < len; ++i) {
            if (i >= len) return;
            if (!ts_m_stopPush) return;
            if (ts_m_friendStatus[i] == 0) continue;

            QString user;
            db->getOpenIdBySeqId(ts_m_friendStatus[i], user);

            // 实际发送消息
            bot->send_msgAsync(2, user, pname, ts_m_text, QString());
            ts_m_friendStatus[i] = 0;

            qint64 elapsed = timer.elapsed();
            if (elapsed < minIntervalMs) {
                QThread::msleep(minIntervalMs - elapsed);
            }
            timer.restart();  // 重置计时器，准备下一次间隔计算
        }
    }

private:

};
class ___tsnr_g : public QRunnable {
public:
    // 通过构造函数把需要的数据传进来（如果有的话）
    ___tsnr_g() {}

    void run() override {
        QQBotClient *bot = m_botClients[g_appid];
        QString pname="[批量推送]";
        int len = ts_m_groupStatus.size();
        for(int i=0;i<len;i++)
        {

            if(!ts_m_stopPush) return;
            if(i>=len) return;
            if(ts_m_groupStatus[i].isEmpty()) continue;

            QString res = bot->send_messages(0,ts_m_groupStatus[i],pname,ts_m_text);
            ts_m_groupStatus[i]=QString();
            if(res.contains("ROBOT")) 成功数量g++;
        }
    }

private:

};

void plts::on_ksts_clicked(bool checked)
{
    if(ts_m_stopPush)
    {
        QMessageBox::warning(this,"已经在运行","还点启动呢 都快发完了");
        return;
    }
    if(!m_botClients.contains(g_appid))
    {
        QMessageBox::warning(this,"开始失败","指定appid 机器人没登录");
        return;
    }
    if(!m_botClients[g_appid]->isOnline())
    {
        QMessageBox::warning(this,"开始失败","指定appid 机器人没登录");
        return;
    }
    ts_m_text = ui->textEdit->toPlainText();
    if(ts_m_text.isEmpty())
    {
        QMessageBox::warning(this,"开始失败","要推送内容为空");
        return;
    }
    ts_m_stopPush = true;
    加载();

    auto *task = new ___tsnr_f();
    QThreadPool::globalInstance()->start(task);

    auto *task2 = new ___tsnr_g();
    QThreadPool::globalInstance()->start(task2);

    m_saveTimer->setInterval(3000); // 3秒
    m_saveTimer->start();
}




