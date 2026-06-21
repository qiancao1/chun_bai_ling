#include "plts.h"
#include "global.h"
#include "ui_plts.h"
int 成功数量f=0;
int 成功数量g=0;
QList<int> ts_m_friendStatus;
QList<QString> ts_m_groupStatus;
QString ts_m_text= QString();
bool ts_m_stopPush = false;

int ts_m_appid=0;                      // 应用ID，用于文件名
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
bool plts::保存() {
    // 好友列表：只保存未完成的（seq != 0）
    QList<int> pendingFriends;
    extracted(pendingFriends);

    QFile fFile(QString("data/%1_f.bin").arg(ts_m_appid));
    if (!fFile.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&fFile);
    out << pendingFriends; // 只写未完成的
    fFile.close();

    // 群列表：只保存未完成的（非空）
    QList<QString> pendingGroups;
    for (const QString &gid : ts_m_groupStatus) {
        if (!gid.isEmpty())
            pendingGroups.append(gid);
    }

    QFile gFile(QString("data/%1_g.bin").arg(ts_m_appid));
    if (!gFile.open(QIODevice::WriteOnly))
        return false;
    QDataStream out2(&gFile);
    out2 << pendingGroups;
    gFile.close();
    if (pendingGroups.size() == 0 && pendingFriends.size() == 0)
        ts_m_stopPush = false;
    return true;
}
void plts::加载()
{
    ts_m_friendStatus.clear();
    ts_m_groupStatus.clear();

    QFile fFile(QString("data/%1_f.bin").arg(ts_m_appid));
    if (fFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&fFile);
        in >> ts_m_friendStatus;
        fFile.close();
    }
    QFile gFile(QString("data/%1_g.bin").arg(ts_m_appid));
    if (gFile.open(QIODevice::ReadOnly)) {
        QDataStream in(&gFile);
        in >> ts_m_groupStatus;
        gFile.close();
    }
}
void plts::on_sctswj_2_clicked(bool checked)
{
    if(ts_m_stopPush)
    {
        QMessageBox::warning(this,"已经在运行","已经在群发中 点击这个按钮无效");
        return;
    }
    if(!g_botdb.contains(ts_m_appid))
    {
        QMessageBox::warning(this,"生成失败","请登录账号 自动打开数据库后再试试");
        return;
    }
    auto *db = g_botdb[ts_m_appid];
    ts_m_friendStatus.clear();
    ts_m_groupStatus.clear();
    if(ui->checkBox_tsq->checkState())
        ts_m_groupStatus = db->getAllGroupIds();
    if(ui->checkBox_tssl->checkState())
        ts_m_friendStatus = db->getFriendList();
    if(ts_m_groupStatus.size()==0 && ts_m_friendStatus.size()==0)
        QMessageBox::warning(this,"失败","要推送的群或好友为0 生成失败");

    if(保存()) QMessageBox::warning(this,"生成完成","点击开始推送进行推送吧");
    else QMessageBox::warning(this,"生成失败","写出到文件失败");
}
void plts::on_sctswj_clicked(bool checked)
{
    if(ts_m_stopPush)
    {
        QMessageBox::warning(this,"已经在运行","已经在群发中 点击这个按钮无效");
        return;
    }

    QFile::remove(QString("data/%1_f.bin").arg(ts_m_appid));
    QFile::remove(QString("data/%1_g.bin").arg(ts_m_appid));

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
    ___tsnr_f(int index):m_index(index) {}

    void run() override {
        auto *db = g_botdb[ts_m_appid];
        QQBotClient *bot = m_botClients[ts_m_appid];
        int i2=0;
        QString pname="[批量推送]";
        int len = ts_m_friendStatus.size();
        for(int i=m_index;i2<50;i++)
        {
            if(i>=len) return;
            if(!ts_m_stopPush) return;
            if(ts_m_friendStatus[i]==0) continue;
            QString user;
            db->getOpenIdBySeqId(ts_m_friendStatus[i],user);
            i2++;
            QString res = bot->send_messages(0,user,pname,ts_m_text,QString(),true);
            ts_m_friendStatus[i]=0;
            if(res.contains("ROBOT")) 成功数量f++; //谁没事解析json啊
        }
    }

private:
    int m_index;
};
class ___tsnr_g : public QRunnable {
public:
    // 通过构造函数把需要的数据传进来（如果有的话）
    ___tsnr_g(int index): m_index(index) {}

    void run() override {

        QQBotClient *bot = m_botClients[ts_m_appid];
        int i2=0;
        QString pname="[批量推送]";
         int len = ts_m_groupStatus.size();
        for(int i=m_index;i2<50;i++)
        {

            if(!ts_m_stopPush) return;
            if(i>=len) return;
            if(ts_m_groupStatus[i].isEmpty()) continue;
            i2++;
            QString res = bot->send_messages(0,ts_m_groupStatus[i],pname,ts_m_text);
            ts_m_groupStatus[i]=QString();
            if(res.contains("ROBOT")) 成功数量g++;
        }
    }

private:
    int m_index;
};

void plts::on_ksts_clicked(bool checked)
{
    if(ts_m_stopPush)
    {
        QMessageBox::warning(this,"已经在运行","还点启动呢 都快发完了");
        return;
    }
    if(!m_botClients.contains(ts_m_appid))
    {
        QMessageBox::warning(this,"开始失败","指定appid 机器人没登录");
        return;
    }
    if(!m_botClients[ts_m_appid]->isOnline())
    {
        QMessageBox::warning(this,"开始失败","指定appid 机器人没登录");
        return;
    }
    ts_m_stopPush = true;
    加载();
    ts_m_text = ui->textEdit->toPlainText();
    int threadCount = (ts_m_friendStatus.size() + 49) / 50; // 向上取整
    for(int i=0; i<threadCount; ++i)
    {
        int start = i * 50;
        auto *task = new ___tsnr_f(start);
        QThreadPool::globalInstance()->start(task);
    }
    threadCount = (ts_m_groupStatus.size() + 49) / 50; // 向上取整
    for(int i=0;i<threadCount;++i)
    {
        int start = i * 50;
        auto *task = new ___tsnr_g(start);
        QThreadPool::globalInstance()->start(task);
    }


    m_saveTimer->setInterval(3000); // 3秒
    m_saveTimer->start();
}