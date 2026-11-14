/*
 * 纯白铃 - QQ 机器人管理平台
 * 抽奖（内置功能 → 抽奖）
 *
 * Copyright (C) 2026 两个月亮
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// ════════════════════════════════════════════════════════════════════════════
// 抽奖：数据、抽取逻辑、自动开奖，全在这一层。
//
// 存储 = 独立 SQLite 库 botdb/lottery.db（跟订阅库一个路子，不塞进 botdb 的 LMDB）：
//   lottery(appid, id, name, draw_at, status, data)
//     appid   机器人 appid —— **每个机器人各自一套抽奖**，界面切号就是换 appid
//     id      抽奖随机 id（8 位，用户发 #参与抽奖<id> 用），不是自增序号：
//             序号能遍历，谁都能把别人的抽奖挨个报一遍
//     data    整场抽奖的 JSON（奖品 / 参与者 / 中奖人）
//
// 三个用户指令（都在 core/qqbotclient.cpp 的指令区拦）：
//   #参与抽奖<id>     群里参与，回执带上开奖时间
//   #我的抽奖         列出自己在各个抽奖里的中奖记录（含领取 id）
//   #领取抽奖<抽奖ID>  私聊领取这一场的奖励（按 openid 认人，中了多个只领一条）
//
// 开奖：
//   · 到点自动（QTimer 每 30 秒扫一遍 status=0 且 draw_at<=now 的）
//   · 界面「立即开奖」提前开
//   · 界面「停止本场抽奖」作废（不开奖，参与名单清空，不产生中奖记录）
//
// 名额 = 奖品配置表里每个人数之和；发放文本（兑换码）用 ||| 切成多条，该奖品的第 n 个
// 中奖人取第 n 条。只写一条（没有 |||）= 这一档所有人领同一条；完全没写 = 未配置（领取时
// 提示「请联系管理员」，且不改「已发放」标记）；写了多条但不够份数 = 超出的人没有码，
// **不会**把同一条发给多个人、也不会绕回来。
//
// 启用开关：lottery_meta 表按 appid 存 one/off（默认关）。关掉时机器人侧完全不匹配
// 抽奖指令（连用法都不回），数据全部保留。
//
// 线程：消息处理可能在线程池里，界面在主线程 —— 所有 public 方法自己持 m_mutex，
// 内部一律调 *Locked 版本，**不要**在持锁时调自己另一个 public 方法（非递归锁会死锁）。
// ════════════════════════════════════════════════════════════════════════════

#ifndef LOTTERY_H
#define LOTTERY_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QJsonObject>
#include <QMutex>

struct sqlite3;
class QTimer;

// 抽奖状态
enum LotteryStatus {
    LotteryRunning = 0,   // 进行中（可参与）
    LotteryDrawn   = 1,   // 已开奖
    LotteryClosed  = 2    // 已关闭（作废，没开过奖）
};

// 一个奖品档位：「奖品名 * 人数（等级）」
struct LotteryPrize
{
    QString name;          // 奖品名
    int     count = 1;     // 中奖人数（这一档发几份）
    QString level;         // 等级 / 备注，如「一等奖」
    QString text;          // 中奖后发放内容（兑换码），一人一条用 ||| 分割

    // 该奖品第 idx 个中奖人应得的文本（0 起）。
    // · 完全没写 = 没配发放内容 → 返回空，领取时提示「未配置，请联系管理员」
    // · 只写一条（没有 |||）= 这一档**所有人领同一条**
    // · 写了多条但比份数少 → 超出的人返回空（绝不重复发同一个码）
    QString textFor(int idx) const;
    QJsonObject toJson() const;
    static LotteryPrize fromJson(const QJsonObject &o);
};

struct LotteryPlayer
{
    QString openid;
    QString nickname;
    QString safeNick;      // 安全昵称（平台审核过的），可能为空 → 显示时退回 at
    qint64  joinAt = 0;    // 参与时间（秒）
};

struct LotteryWinner
{
    QString wid;           // 领取凭证 = 抽奖 id（同一场所有中奖人相同，按 openid 认人）
    QString openid;
    QString nickname;
    QString safeNick;      // 安全昵称（开奖时从参与记录带过来），空则用 at 显示
    QString prizeName;
    QString level;
    QString text;          // 开奖时就定死的发放文本
    bool    delivered = false;
    qint64  deliveredAt = 0;

    QJsonObject toJson() const;
    static LotteryWinner fromJson(const QJsonObject &o);
};

struct LotteryRecord
{
    int     appid   = 0;
    QString id;            // 抽奖随机 id
    QString name;
    qint64  drawAt  = 0;   // 计划开奖时间（秒）
    int     status  = LotteryRunning;
    int     ctype   = 0;   // 公布会话类型：0 群 / 2 私聊（开奖公布用）
    QString convId;        // 公布会话 id（第一个参与者所在群）
    qint64  createdAt = 0;

    // 累计参与人数。开奖那一刻会把 players 整个清空（只留中奖的），
    // 所以人数必须单独存一份 —— 后台「参与 X 人」全靠它。
    int joined = 0;
    qint64 drawnAt = 0;    // 实际开奖时间（提前开奖时跟计划时间不一样）

    // 可用群白名单（**每场抽奖单独一份**）：一行一个群 openid 或群名。
    // 留空 = 不限制（所有群都能参与/查看）；填了 = 只有名单里的群能用。
    QString whitelist;

    // 满人开奖：参与人数到 N 人就立刻开（0 = 关，按开奖时间走）。
    // 最后一个报名的进来那一刻触发，开奖公告照常发到公布群。
    int autoDrawCount = 0;

    QList<LotteryPrize>  prizes;
    QList<LotteryPlayer> players;    // 只有「进行中」才非空；开奖后清掉
    QList<LotteryWinner> winners;

    int totalSlots() const;        // 奖品人数之和 = 中奖名额
    int joinedCount() const { return players.isEmpty() ? joined : players.size(); }
    QString statusText() const;    // 进行中 / 已开奖 / 已关闭
    bool isRunning() const { return status == LotteryRunning; }
    // 这个群能不能参与/查看本场（群 openid 和群名两个都认；白名单为空 = 允许）
    bool groupAllowed(const QString &groupId, const QString &groupName) const;

    QJsonObject toJson() const;    // 整场（appid 不进 JSON，列里有）
    static LotteryRecord fromJson(const QJsonObject &o);
};

class LotteryMgr : public QObject
{
    Q_OBJECT
public:
    explicit LotteryMgr(QObject *parent = nullptr);
    ~LotteryMgr();

    // dirPath：库所在目录（跟 SubDB 一样传目录），实际文件 <dir>/lottery.db
    bool open(const QString &dirPath);
    void close();
    bool isOpen() const;

    // ==================== 界面用 ====================
    QList<LotteryRecord> list(int appid);                    // 按开奖时间倒序
    bool get(int appid, const QString &id, LotteryRecord &out);
    bool save(LotteryRecord &rec);                           // upsert（id 为空 = 新建，回填 id）
    bool remove(int appid, const QString &id);

    // ==================== 业务 ====================
    // 0 = 成功；1 抽奖不存在；2 已开奖；3 已关闭；4 已经参与过；5 openid 为空；
    // 6 保存失败；7 这个群不在本场的白名单里
    int join(int appid, const QString &id, const QString &openid, const QString &nick,
             const QString &safeNick,
             int ctype, const QString &convId, const QString &groupId, const QString &groupName,
             QString *errMsg = nullptr);
    // 开奖（manual=true 是界面提前开）；announce=true 时把结果发到公布会话
    bool draw(int appid, const QString &id, bool manual, QString *errMsg = nullptr);
    bool closeLottery(int appid, const QString &id, QString *errMsg = nullptr);
    // #我的抽奖：该用户在所有抽奖里的中奖记录（文本）
    QString myWinnersText(int appid, const QString &openid);
    // #历史抽奖：抽奖列表（只列本群可用的，最近若干场，带抽奖ID和中奖人数）
    QString historyText(int appid, const QString &groupId, const QString &groupName);
    // #抽奖帮助：带可点按钮（[显示](指令) 会被转成内联命令）的操作指引。
    // openid 用来把「你还有 N 份没领」直接顶到最上面（拿不到就传空，只是少这一段）
    QString helpText(int appid, const QString &openid,
                     const QString &groupId, const QString &groupName);
    // #抽奖名单<抽奖ID>：某一场的中奖名单
    QString winnerListText(int appid, const QString &id,
                           const QString &groupId, const QString &groupName);
    // #抽奖奖品<抽奖ID>：某一场的奖品档位（奖品名 × 人数，共几个名额）
    QString prizeListText(int appid, const QString &id,
                          const QString &groupId, const QString &groupName);
    // 领取（#领取抽奖<抽奖ID>）：text 回填发放内容；errMsg 回填失败原因
    bool claim(int appid, const QString &lotteryId, const QString &openid,
               QString *text, QString *errMsg = nullptr);
    // 界面「标记已发放」（不真发内容，只改状态）；按 openid 定位中奖人
    bool markDelivered(int appid, const QString &lotteryId, const QString &openid);

    // ==================== 启用开关（每个机器人一份）====================
    // 关掉之后机器人侧**完全不匹配**抽奖指令（连"用法"都不回），数据都留着。
    // 默认关：不需要抽奖的用户就不会突然被抢走 # 开头的指令。
    bool isEnabled(int appid);                 // 带缓存，指令热路径可以放心调
    bool setEnabled(int appid, bool on);

    // 自动开奖：每 30 秒扫一次到点的（在 main.cpp 启动后调一次）
    void startAutoDraw();

signals:
    void changed(int appid);      // 数据变了（界面据此刷新，别在回调里递归保存）

private:
    bool openLocked();
    bool getLocked(int appid, const QString &id, LotteryRecord &out);
    bool saveLocked(const LotteryRecord &rec);
    QList<LotteryRecord> listLocked(int appid, int statusFilter = -1);
    bool removeLocked(int appid, const QString &id);
    QString newRandomId(int len) const;
    QString buildAnnounce(const LotteryRecord &rec) const;
    void postAnnounce(const LotteryRecord &rec);

private slots:
    void onTick();

private:
    QString  m_filePath;
    sqlite3 *m_db = nullptr;
    mutable QMutex m_mutex{QMutex::NonRecursive};
    QTimer  *m_timer = nullptr;
    QHash<int, int> m_enabledCache;   // appid → 0/1；只缓存查过的，setEnabled 时同步更新
};

// 全局单例：main.cpp 的 initDBs() 里创建并 open；打不开就留 nullptr，
// 界面和指令侧的调用点全部判空（抽奖只是附属功能，不能拖垮整个框架）
extern LotteryMgr *g_lottery;

#endif // LOTTERY_H
