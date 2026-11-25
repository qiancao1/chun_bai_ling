#include "plts.h"
#include "global.h"
#include "ui_plts.h"
#include <QMessageBox>
#include <QThreadPool>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QPushButton>
#include <QStandardPaths>
#include <QSet>
#include <atomic>

// 群批量推送的成功计数：改成异步发送后由回调线程累加，必须原子
// （多个请求同时在飞 → 回调可能并发，普通 int 的 ++ 会丢计数）
std::atomic<int> 成功数量g{0};
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

// ==================== 导入好友（CSV/TXT 的 union_openid 列）====================
namespace {

// 拆一行 CSV：支持 "..." 包裹（字段内可含逗号）、"" 转义
QStringList splitCsvLine(const QString &line)
{
    QStringList out;
    QString cur;
    bool inQuote = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuote) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) { cur.append(QLatin1Char('"')); ++i; }
                else inQuote = false;
            } else {
                cur.append(c);
            }
        } else {
            if (c == QLatin1Char('"'))      inQuote = true;
            else if (c == QLatin1Char(',')) { out.append(cur); cur.clear(); }
            else                            cur.append(c);
        }
    }
    out.append(cur);
    return out;
}

// openid 只认 32 位十六进制（= 16 字节），与 BotDB 里的 key 口径一致
bool isOpenidHex(const QString &s)
{
    if (s.size() != 32) return false;
    for (const QChar &c : s) {
        const ushort u = c.unicode();
        const bool hex = (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
        if (!hex) return false;
    }
    return true;
}

} // namespace

void plts::on_drhy_clicked()
{
    if(ts_m_stopPush)
    {
        QMessageBox::warning(this,"已经在运行","正在群发中，先停止推送再导入");
        return;
    }
    if(!g_botdb.contains(g_appid))
    {
        QMessageBox::warning(this,"导入失败","请登录账号 自动打开数据库后再试试");
        return;
    }

    // ---- 当前账号是谁（必须让用户看清楚：导错账号不好删）----
    QString accName = QString("AppID %1").arg(g_appid);
    QString accDetail;
    for (const auto &acc : std::as_const(m_accounts)) {
        if (!acc) continue;
        if (acc->appid_int != g_appid) continue;
        accName = acc->nickname.isEmpty() ? acc->appid : acc->nickname;
        accDetail = QString("AppID：%1").arg(acc->appid);
        if (!acc->botqq.isEmpty()) accDetail += QString("    机器人QQ：%1").arg(acc->botqq);
        break;
    }

    // ---- 选文件 ----
    QString defDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (defDir.isEmpty()) defDir = QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(this, "选择要导入的好友文件（CSV/TXT）",
                                                      defDir,
                                                      "文本 / CSV (*.txt *.csv);;所有文件 (*.*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,"导入失败","文件打不开：\n" + path);
        return;
    }
    QString content = QString::fromUtf8(f.readAll());
    f.close();
    if (content.isEmpty()) {
        QMessageBox::warning(this,"导入失败","文件是空的");
        return;
    }

    // ---- 切行（吃掉 BOM / CR）----
    QStringList lines = content.split(QLatin1Char('\n'));
    for (QString &l : lines) {
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);
    }
    if (!lines.isEmpty() && lines.first().startsWith(QChar(0xFEFF)))
        lines[0].remove(0, 1);

    // ---- 找 union_openid 列：先按表头找，没有表头就从右往左猜一个十六进制列 ----
    int col = -1;
    int startLine = 0;
    for (int i = 0; i < qMin(lines.size(), 5) && col < 0; ++i) {
        const QStringList fields = splitCsvLine(lines.at(i));
        for (int c = 0; c < fields.size(); ++c) {
            if (fields.at(c).trimmed().toLower() == "union_openid") {
                col = c;
                startLine = i + 1;   // 表头下面才是数据
                break;
            }
        }
    }
    if (col < 0) {
        // 没有表头：取第一个非空行里**最靠右**的十六进制列
        // （openid,union_openid 两列内容一样时优先拿 union_openid 那一列）
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).trimmed().isEmpty()) continue;
            const QStringList fields = splitCsvLine(lines.at(i));
            for (int c = fields.size() - 1; c >= 0; --c) {
                if (isOpenidHex(fields.at(c).trimmed())) { col = c; break; }
            }
            if (col >= 0) { startLine = i; break; }
        }
    }
    if (col < 0) {
        QMessageBox::warning(this,"导入失败","没找到 union_openid 列（文件里没有任何 32 位十六进制的 ID）");
        return;
    }

    // ---- 收集 openid（去重、跳过无效行）----
    QStringList ids;
    QStringList invalidSamples;
    QSet<QString> seen;
    int dataRows = 0;
    int dupRows = 0;
    for (int i = startLine; i < lines.size(); ++i) {
        if (lines.at(i).trimmed().isEmpty()) continue;
        ++dataRows;
        const QStringList fields = splitCsvLine(lines.at(i));
        const QString id = (col < fields.size()) ? fields.at(col).trimmed().toUpper() : QString();
        if (!isOpenidHex(id)) {
            if (invalidSamples.size() < 5)
                invalidSamples.append(QString("第 %1 行：%2").arg(i + 1).arg(lines.at(i).trimmed().left(60)));
            continue;
        }
        if (seen.contains(id)) { ++dupRows; continue; }
        seen.insert(id);
        ids.append(id);
    }

    if (ids.isEmpty()) {
        QMessageBox::warning(this,"导入失败",
            QString("没有一条有效 union_openid\n（共 %1 行数据，全部无效或为空）").arg(dataRows));
        return;
    }

    // ---- 二次确认：账号 + 数量 + 不可撤销提示，默认按钮是「取消」----
    QString info = QString("%1\n%2\n\n"
                           "文件：%3\n"
                           "有效 union_openid：%4 条（原始 %5 行，重复 %6 行）")
                       .arg(accName, accDetail.isEmpty() ? QString() : accDetail)
                       .arg(QDir::toNativeSeparators(path))
                       .arg(ids.size()).arg(dataRows).arg(dupRows);
    if (!invalidSamples.isEmpty()) {
        info += "\n\n以下行没有导入（不是 32 位十六进制）：\n" + invalidSamples.join('\n');
        if (dataRows > ids.size() + dupRows)
            info += "\n……共 " + QString::number(dataRows - ids.size() - dupRows) + " 行无效";
    }
    info += "\n\n⚠ 导入后会直接写进这个账号的好友库，**不好单独删掉**。\n"
            "请再核对一次上面的 AppID / 账号是否就是要导入的那个。";

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("确认导入好友");
    box.setText("即将导入好友到：" + accName);
    box.setInformativeText(info);
    QPushButton *okBtn = box.addButton("确认导入到该账号", QMessageBox::AcceptRole);
    QPushButton *noBtn = box.addButton("取消", QMessageBox::RejectRole);
    box.setDefaultButton(noBtn);      // 默认落在「取消」，防手滑回车
    box.exec();
    if (box.clickedButton() != okBtn) return;

    // ---- 真正写库（整批一个事务：要么全进，要么全不进）----
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int newCount = 0, alreadyCount = 0;
    QStringList invalid;
    const int ret = g_botdb[g_appid]->batchImportFriends(ids, BotDB::nowMinutes(),
                                                         &newCount, &alreadyCount, &invalid);
    QApplication::restoreOverrideCursor();

    if (ret < 0) {
        QMessageBox::critical(this,"导入失败",
            "写数据库失败了（整批已回滚，一条都没进去）。\n请确认账号已登录后重试。");
        return;
    }

    QString done = QString("导入完成\n\n新增好友：%1\n已经是好友（跳过）：%2").arg(newCount).arg(alreadyCount);
    if (!invalid.isEmpty()) done += QString("\n无效 openid：%1").arg(invalid.size());
    done += "\n\n下一步：勾选「推送私聊」→ 点「生成推送文件」→ 再点「开始推送」。";
    QMessageBox::information(this, "导入完成", done);
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
        // 一次扫库拿到所有好友可用的被动回复 msgid（窗口 1 小时），下面每 60 秒再补扫一次
        // （好友多、队列长时跑过 1 小时，老 msgid 就作废了 → 必须换新的，不能一直用开局那份）
        LogDB *ldbF = g_logdb[mapTypeToTabIndex(2)].get();
        QHash<QString, QStringList> replyIds;
        if (ldbF) replyIds = ldbF->getRecentMsgIdMap(QString::number(g_appid), false);
        QElapsedTimer replyIdsTimerF;
        replyIdsTimerF.start();

        QString pname = "[批量推送]";
        int len = ts_m_friendStatus.size();

        // 好友**没有**分钟限制（「30 条/分钟」只是「未认证」机器人才有的，这里都是有认证的）→
        // 只做秒级限速：被动回复走**本号** 60 条/秒（paceMsgIdReply 按 appid 分桶）；
        // 主动发送维持原来的 8 条/秒。
        const int maxPerSec = 8;
        const int minIntervalMs = 1000 / maxPerSec;   // 125ms
        QElapsedTimer timer;
        timer.start();

        for (int i = 0; i < len; ++i) {
            if (i >= len) return;
            if (!ts_m_stopPush) return;
            if (ts_m_friendStatus[i] == 0) continue;

            // 每 60 秒换一份 msgid：老的可能已经超过 1 小时窗口，再拿去发会被平台拒（消息就丢了）
            if (ldbF && replyIdsTimerF.elapsed() > 60 * 1000) {
                replyIds = ldbF->getRecentMsgIdMap(QString::number(g_appid), false);
                replyIdsTimerF.restart();
            }

            QString user;
            db->getOpenIdBySeqId(ts_m_friendStatus[i], user);

            // 混合发送：有 msgid 就走被动回复（好友窗口 1 小时），没有就走主动发送
            QString msgid = replyIds.value(user).value(0);

            // 实际发送消息
            bot->send_msgAsync(2, user, pname, ts_m_text, msgid);
            ts_m_friendStatus[i] = 0;

            if (!msgid.isEmpty()) {
                paceMsgIdReply(60, QString::number(g_appid));   // 被动回复：本号 60 条/秒
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
        // 群的被动回复窗口 1 小时（旧口径 5 分钟），整批推送可能跑很久 → 先扫一次，之后每 60 秒重扫
        // （既换掉快过期的，也补上这一分钟内新收到的）
        QHash<QString, QStringList> replyIds;
        if (ldb) replyIds = ldb->getRecentMsgIdMap(QString::number(g_appid), true);
        QElapsedTimer replyIdsTimer;
        replyIdsTimer.start();

        QString pname="[批量推送]";
        int len = ts_m_groupStatus.size();

        // 群硬限：只有「主动发送」（不带 msgid）才吃 50 条/分钟（官方 60，留 10 条保险）；
        // 被动回复不占这个额度（只受 paceMsgIdReply 的**本号** 60 条/秒限制）。
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

            // 混合发送：有 msgid 就走被动回复（群窗口 1 小时），没有就走主动发送
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

            // 异步发送：立刻返回，不再把调用线程钉在 RTT 上。
            //   原来走 send_messages（同步版）→ 每条都要等 30~100ms 的回包，8 个号的线程全堵在网络上，
            //   实际吞吐被「线程数 ÷ RTT」卡死（8/0.05 ≈ 160 条/秒），限速器提到多少都没用。
            //   改成带回调版后，线程只负责投递 + 睡 16ms 的时间片，真正的等待由网络线程管。
            // 成功计数放进回调：ctx.cb 收到的是**真实回包**（api2.cpp 里 ctx.cb(resp, err)），
            //   和同步版返回值同源，所以 contains("ROBOT") 的判定不变。
            // ⚠ text 必须传**局部副本**：send_msgAsync 会原地改写正文（摘 #^/#$ 前缀、改插件指令名），
            //   直接传成员 ts_m_text 会把上一轮的改写结果带进下一轮。
            const QString gid = ts_m_groupStatus[i];
            QString text = ts_m_text;
            bot->send_msgAsync(0, gid, pname, text, msgid, QString(), false, 0, false,
                               [](const QString &resp, QNetworkReply::NetworkError) {
                                   if (resp.contains("ROBOT"))
                                       成功数量g.fetch_add(1, std::memory_order_relaxed);
                               });
            ts_m_groupStatus[i]=QString();

            // 带上 msgid 就是被动回复，可以走本号 60 条/秒；主动发送才吃 50 条/分钟
            if(!msgid.isEmpty()) paceMsgIdReply(60, QString::number(g_appid));
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




