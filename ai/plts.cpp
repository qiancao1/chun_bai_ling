#include "plts.h"
#include "global.h"
#include "ui_plts.h"
#include <QMessageBox>
#include <QThreadPool>
#include <QDateTime>

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
        QHash<QString,int> fullGroups;
        {
            // 先拷快照：避免遍历期间长期持锁
            QMutexLocker locker(&chatPage->m_GMutex);
            fullGroups = chatPage->全量群;
        }
        for (auto it = fullGroups.begin(); it != fullGroups.end(); ++it) {
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
        // 一次扫库拿到所有好友可用的被动回复 msgid（窗口 5 分钟），整批推送都不用再查
        QHash<QString, QStringList> replyIds;
        if (LogDB *ldb = g_logdb[mapTypeToTabIndex(2)].get())
            replyIds = ldb->getRecentMsgIdMap(QString::number(g_appid), false);

        QString pname = "[批量推送]";
        int len = ts_m_friendStatus.size();

        // 好友**没有**分钟限制（「30 条/分钟」只是「未认证」机器人才有的，这里都是有认证的）→
        // 只做秒级限速：被动回复走全局 60 条/秒；主动发送维持原来的 8 条/秒。
        const int maxPerSec = 8;
        const int minIntervalMs = 1000 / maxPerSec;   // 125ms
        QElapsedTimer timer;
        timer.start();

        for (int i = 0; i < len; ++i) {
            if (i >= len) return;
            if (!ts_m_stopPush) return;
            if (ts_m_friendStatus[i] == 0) continue;

            QString user;
            db->getOpenIdBySeqId(ts_m_friendStatus[i], user);

            // 混合发送：有 msgid 就走被动回复（好友窗口 1 小时），没有就走主动发送
            QString msgid = replyIds.value(user).value(0);

            // 实际发送消息
            bot->send_msgAsync(2, user, pname, ts_m_text, msgid);
            ts_m_friendStatus[i] = 0;

            if (!msgid.isEmpty()) {
                paceMsgIdReply(60);   // 被动回复：全局 60 条/秒
            } else {
                qint64 elapsed = timer.elapsed();
                if (elapsed < minIntervalMs) {
                    QThread::msleep(minIntervalMs - elapsed);
                }
                timer.restart();      // 重置计时器，准备下一次间隔计算
            }
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
        // 群日志库（type=0 → tabIndex=1）
        LogDB *ldb = g_logdb[mapTypeToTabIndex(0)].get();
        // 群的被动回复窗口只有 5 分钟，整批推送可能跑很久 → 先扫一次，之后每 60 秒重扫
        QHash<QString, QStringList> replyIds;
        if (ldb) replyIds = ldb->getRecentMsgIdMap(QString::number(g_appid), true);
        QElapsedTimer replyIdsTimer;
        replyIdsTimer.start();

        QString pname="[批量推送]";
        int len = ts_m_groupStatus.size();

        // 群硬限：只有「主动发送」（不带 msgid）才吃 50 条/分钟（官方 60，留 10 条保险）；
        // 被动回复不占这个额度（只受 paceMsgIdReply(60) 的全局 60 条/秒限制）。
        // 主动额度满 50 条就睡到下一个整分钟再继续，期间支持中途停止（ts_m_stopPush）。
        const int GROUP_MIN_LIMIT = 50;
        int sentThisMinute = 0;                      // 本分钟内「主动发送」条数
        qint64 minuteStart = QDateTime::currentMSecsSinceEpoch();

        for(int i=0;i<len;i++)
        {

            if(!ts_m_stopPush) return;
            if(i>=len) return;
            if(ts_m_groupStatus[i].isEmpty()) continue;

            if (ldb && replyIdsTimer.elapsed() > 60 * 1000) {
                replyIds = ldb->getRecentMsgIdMap(QString::number(g_appid), true);
                replyIdsTimer.restart();
            }

            // 混合发送：有 msgid 就走被动回复（群窗口 5 分钟），没有就走主动发送
            QString msgid = replyIds.value(ts_m_groupStatus[i]).value(0);

            if (msgid.isEmpty()) {
                // 主动发送才受 50 条/分钟限制；满了就等到下一个整分钟
                qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (nowMs - minuteStart >= 60000) { sentThisMinute = 0; minuteStart = nowMs; }
                if (sentThisMinute >= GROUP_MIN_LIMIT) {
                    qint64 waitMs = 60000 - (nowMs - minuteStart);
                    while (waitMs > 0) {
                        if (!ts_m_stopPush) return;
                        QThread::msleep(qMin<qint64>(waitMs, 200));
                        waitMs = 60000 - (QDateTime::currentMSecsSinceEpoch() - minuteStart);
                    }
                    sentThisMinute = 0;
                    minuteStart = QDateTime::currentMSecsSinceEpoch();
                }
            }

            QString res = bot->send_messages(0,ts_m_groupStatus[i],pname,ts_m_text,msgid);
            ts_m_groupStatus[i]=QString();
            if(res.contains("ROBOT")) 成功数量g++;

            // 带上 msgid 就是被动回复，可以走全局 60 条/秒；主动发送才吃 50 条/分钟
            if(!msgid.isEmpty()) paceMsgIdReply(60);
            else ++sentThisMinute;
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




