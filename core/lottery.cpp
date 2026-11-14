/*
 * 纯白铃 - QQ 机器人管理平台
 * 抽奖（内置功能 → 抽奖）—— 存储与业务实现
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

// 设计见 lottery.h 顶部。这里只强调实现约束：
//   1. 单连接 + 一把非递归锁：所有 public 方法自己持锁，内部只调 *Locked，
//      绝不互相调用 —— QMutex 非递归，自锁就是界面整个卡死（KvDB 踩过一次）。
//   2. emit / 发消息 / QMessageBox 一律在锁外：changed 的槽会回头读库（list），
//      send_msgAsync 也可能同步走一截回调，持锁调用就是死锁。
//   3. 开奖结果（每人发什么文本）在开奖那一刻定死写进 winners，之后改奖品配置
//      不会影响已经开出去的奖。

#include "lottery.h"

#include "global.h"
#include "qqbotclient.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTimer>
#include <QDateTime>
#include <QPair>
#include <QRegularExpression>

#include <sqlite3.h>

LotteryMgr *g_lottery = nullptr;

namespace {

const int LOTTERY_CACHE_KB = 2048;          // 页缓存硬顶 2MB（抽奖数据很小）
const int LOTTERY_BUSY_MS  = 3000;
const int LOTTERY_TICK_MS  = 30 * 1000;     // 30 秒扫一次到点的抽奖

// 随机 id 字符集：去掉 0/o/1/l/i 这些抄下来会看错的
const char *kIdChars = "23456789abcdefghjkmnpqrstuvwxyz";
const int   kIdCharsLen = 32;

bool bindText(sqlite3_stmt *st, int idx, const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    // SQLITE_TRANSIENT：让 sqlite 自己拷一份，utf8 出了作用域就没了
    return sqlite3_bind_text(st, idx, utf8.constData(), utf8.size(), SQLITE_TRANSIENT) == SQLITE_OK;
}

QString colText(sqlite3_stmt *st, int idx)
{
    const unsigned char *txt = sqlite3_column_text(st, idx);
    return txt ? QString::fromUtf8(reinterpret_cast<const char *>(txt)) : QString();
}

QString fmtTime(qint64 sec)
{
    if (sec <= 0) return QStringLiteral("—");
    return QDateTime::fromSecsSinceEpoch(sec).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

// 中奖名单显示谁：
//   ① 优先「安全昵称」（平台审核过的 nickname2）—— 有就直接显示，好看且不会因违规昵称被拦
//   ② 没有安全昵称 → 直接显示 openid 纯文本
//
// ⚠ 这里**绝不能**用任何 at 写法（用户 2026-11-13 明确要求「显示id就行了 不要at」）：
//   <@openid> / <@!openid> / <qqbot-at-user id="…" /> 全都会真的给中奖人弹「有人@我」，
//   每发一次公告就骚扰一遍。所以只打纯文本 openid，客户端不会做任何@处理。
QString openidText(const QString &openid)
{
    const QString id = openid.trimmed();
    if (id.isEmpty()) return QStringLiteral("（匿名）");
    return id;
}

// 中奖人展示名：安全昵称优先，没有就纯文本 openid。
// 顺手把换行压成空格（昵称里有换行会拆坏引用块）
QString winnerLabel(const LotteryWinner &w)
{
    QString safe = w.safeNick;
    safe.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
    safe = safe.trimmed();
    if (!safe.isEmpty()) return safe;
    return openidText(w.openid);
}

} // namespace

// ════════════════════════════════════════════════════════════════════════════
// 序列化
// ════════════════════════════════════════════════════════════════════════════

QString LotteryPrize::textFor(int idx) const
{
    const QString body = text.trimmed();
    // 完全没写 = 没配发放内容 → 返回空，领取时 claim() 回「还没有配置发放内容」
    if (body.isEmpty()) return QString();

    // 没有 ||| = 只有一条：**这一档所有中奖人都发这一条**（用户要的口径：
    // 3 份奖只写一行「恭喜你中了一等奖!」→ 3 个人领到的都是这句）。
    if (!body.contains(QStringLiteral("|||"))) return body;

    const QStringList parts = body.split(QStringLiteral("|||"), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return QString();
    if (idx < 0) idx = 0;
    // 写了多条但比份数少：超出的人**没有**（绝不取模循环，循环会把同一个兑换码发给两个人）
    if (idx >= parts.size()) return QString();
    // 每段自己 trim：兑换码两边多打的空格/换行别跟着发出去（用户抄码会连空格一起复制）
    return parts.at(idx).trimmed();
}

QJsonObject LotteryPrize::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("name")]  = name;
    o[QStringLiteral("count")] = count;
    o[QStringLiteral("level")] = level;
    o[QStringLiteral("text")]  = text;
    return o;
}

LotteryPrize LotteryPrize::fromJson(const QJsonObject &o)
{
    LotteryPrize p;
    p.name  = o.value(QStringLiteral("name")).toString();
    p.count = o.value(QStringLiteral("count")).toInt(1);
    if (p.count < 0) p.count = 0;
    p.level = o.value(QStringLiteral("level")).toString();
    p.text  = o.value(QStringLiteral("text")).toString();
    return p;
}

QJsonObject LotteryWinner::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("wid")]         = wid;
    o[QStringLiteral("openid")]      = openid;
    o[QStringLiteral("nickname")]    = nickname;
    o[QStringLiteral("safeNick")]    = safeNick;
    o[QStringLiteral("prizeName")]   = prizeName;
    o[QStringLiteral("level")]       = level;
    o[QStringLiteral("text")]        = text;
    o[QStringLiteral("delivered")]   = delivered;
    o[QStringLiteral("deliveredAt")] = deliveredAt;
    return o;
}

LotteryWinner LotteryWinner::fromJson(const QJsonObject &o)
{
    LotteryWinner w;
    w.wid         = o.value(QStringLiteral("wid")).toString();
    w.openid      = o.value(QStringLiteral("openid")).toString();
    w.nickname    = o.value(QStringLiteral("nickname")).toString();
    w.safeNick    = o.value(QStringLiteral("safeNick")).toString();
    w.prizeName   = o.value(QStringLiteral("prizeName")).toString();
    w.level       = o.value(QStringLiteral("level")).toString();
    w.text        = o.value(QStringLiteral("text")).toString();
    w.delivered   = o.value(QStringLiteral("delivered")).toBool(false);
    w.deliveredAt = static_cast<qint64>(o.value(QStringLiteral("deliveredAt")).toDouble(0));
    return w;
}

int LotteryRecord::totalSlots() const
{
    int n = 0;
    for (const LotteryPrize &p : prizes) n += qMax(0, p.count);
    return n;
}

QString LotteryRecord::statusText() const
{
    switch (status) {
    case LotteryRunning: return QStringLiteral("进行中");
    case LotteryDrawn:   return QStringLiteral("已开奖");
    case LotteryClosed:  return QStringLiteral("已关闭");
    default:             return QStringLiteral("未知");
    }
}

bool LotteryRecord::groupAllowed(const QString &groupId, const QString &groupName) const
{
    if (whitelist.trimmed().isEmpty()) return true;      // 没填 = 不限制

    // 纯「寻找文本」：整段白名单直接找，不按行/逗号拆开。
    // 只要这段文字里出现群 ID 或群名就算命中 → 换行、逗号、备注随便写都不会失效，
    // 例如 `3A1B2C3D4E5F 我的测试群` 或者几行挤在一行里都行。
    if (!groupId.isEmpty() && whitelist.contains(groupId)) return true;
    // 群名太短（1 个字）容易误命中别的备注，至少 2 个字才按群名找
    if (groupName.size() >= 2 && whitelist.contains(groupName)) return true;
    return false;
}

QJsonObject LotteryRecord::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")]        = id;
    o[QStringLiteral("name")]      = name;
    o[QStringLiteral("drawAt")]    = drawAt;
    o[QStringLiteral("status")]    = status;
    o[QStringLiteral("ctype")]     = ctype;
    o[QStringLiteral("convId")]    = convId;
    o[QStringLiteral("createdAt")] = createdAt;
    o[QStringLiteral("joined")]    = joined;
    o[QStringLiteral("drawnAt")]   = drawnAt;
    o[QStringLiteral("whitelist")] = whitelist;
    o[QStringLiteral("autoDrawCount")] = autoDrawCount;

    QJsonArray pa;
    for (const LotteryPrize &p : prizes) pa.append(p.toJson());
    o[QStringLiteral("prizes")] = pa;

    QJsonArray la;
    for (const LotteryPlayer &p : players) {
        QJsonObject po;
        po[QStringLiteral("openid")]   = p.openid;
        po[QStringLiteral("nickname")] = p.nickname;
        po[QStringLiteral("safeNick")] = p.safeNick;
        po[QStringLiteral("joinAt")]   = p.joinAt;
        la.append(po);
    }
    o[QStringLiteral("players")] = la;

    QJsonArray wa;
    for (const LotteryWinner &w : winners) wa.append(w.toJson());
    o[QStringLiteral("winners")] = wa;

    return o;
}

LotteryRecord LotteryRecord::fromJson(const QJsonObject &o)
{
    LotteryRecord r;
    r.id        = o.value(QStringLiteral("id")).toString();
    r.name      = o.value(QStringLiteral("name")).toString();
    r.drawAt    = static_cast<qint64>(o.value(QStringLiteral("drawAt")).toDouble(0));
    r.status    = o.value(QStringLiteral("status")).toInt(LotteryRunning);
    r.ctype     = o.value(QStringLiteral("ctype")).toInt(0);
    r.convId    = o.value(QStringLiteral("convId")).toString();
    r.createdAt = static_cast<qint64>(o.value(QStringLiteral("createdAt")).toDouble(0));
    r.joined    = o.value(QStringLiteral("joined")).toInt(0);
    r.drawnAt   = static_cast<qint64>(o.value(QStringLiteral("drawnAt")).toDouble(0));
    r.whitelist = o.value(QStringLiteral("whitelist")).toString();
    r.autoDrawCount = o.value(QStringLiteral("autoDrawCount")).toInt(0);

    const QJsonArray pa = o.value(QStringLiteral("prizes")).toArray();
    for (const QJsonValue &v : pa) r.prizes.append(LotteryPrize::fromJson(v.toObject()));

    const QJsonArray la = o.value(QStringLiteral("players")).toArray();
    for (const QJsonValue &v : la) {
        const QJsonObject po = v.toObject();
        LotteryPlayer p;
        p.openid   = po.value(QStringLiteral("openid")).toString();
        p.nickname = po.value(QStringLiteral("nickname")).toString();
        p.safeNick = po.value(QStringLiteral("safeNick")).toString();
        p.joinAt   = static_cast<qint64>(po.value(QStringLiteral("joinAt")).toDouble(0));
        if (!p.openid.isEmpty()) r.players.append(p);
    }

    const QJsonArray wa = o.value(QStringLiteral("winners")).toArray();
    for (const QJsonValue &v : wa) r.winners.append(LotteryWinner::fromJson(v.toObject()));

    // 状态兜底：已经有中奖名单就一定是「已开奖」。
    // 历史数据 / 写库时序异常时 status 可能还是 0（进行中），会出现「后台显示已开奖、
    // 指令却说进行中」这种两套口径 —— 统一以中奖名单为准。
    if (r.status == LotteryRunning && !r.winners.isEmpty()) r.status = LotteryDrawn;

    return r;
}

// ════════════════════════════════════════════════════════════════════════════
// LotteryMgr
// ════════════════════════════════════════════════════════════════════════════

LotteryMgr::LotteryMgr(QObject *parent)
    : QObject(parent)
{
}

LotteryMgr::~LotteryMgr()
{
    close();
}

bool LotteryMgr::isOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_db != nullptr;
}

bool LotteryMgr::open(const QString &dirPath)
{
    QMutexLocker locker(&m_mutex);
    m_filePath = dirPath + QStringLiteral("/lottery.db");
    return openLocked();
}

bool LotteryMgr::openLocked()
{
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }

    const QString dir = QFileInfo(m_filePath).absolutePath();
    if (!QDir().mkpath(dir)) {
        qCritical() << "Lottery: 无法创建目录" << dir;
        return false;
    }

    const QByteArray file = m_filePath.toUtf8();
    const int rc = sqlite3_open_v2(file.constData(), &m_db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        qCritical() << "Lottery: 打开失败" << m_filePath
                    << (m_db ? sqlite3_errmsg(m_db) : "sqlite3_open_v2 返回 NULL");
        if (m_db) { sqlite3_close_v2(m_db); m_db = nullptr; }
        return false;
    }
    sqlite3_busy_timeout(m_db, LOTTERY_BUSY_MS);

    const QByteArray cacheSql = "PRAGMA cache_size=-" + QByteArray::number(LOTTERY_CACHE_KB);
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, cacheSql.constData(), nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA mmap_size=0", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
    sqlite3_exec(m_db, "PRAGMA auto_vacuum=INCREMENTAL", nullptr, nullptr, nullptr);

    // draw_at / status 是冗余列：列表页只按这两列排序筛选，不必把整场 JSON 读出来
    const char *ddl =
        "CREATE TABLE IF NOT EXISTS lottery ("
        "  appid   INTEGER NOT NULL,"
        "  id      TEXT    NOT NULL,"
        "  name    TEXT    NOT NULL,"
        "  draw_at INTEGER NOT NULL,"
        "  status  INTEGER NOT NULL,"
        "  data    TEXT    NOT NULL,"
        "  PRIMARY KEY (appid, id)"
        ") WITHOUT ROWID";
    char *err = nullptr;
    if (sqlite3_exec(m_db, ddl, nullptr, nullptr, &err) != SQLITE_OK) {
        qCritical() << "Lottery: 建表失败" << (err ? err : "");
        if (err) sqlite3_free(err);
        sqlite3_close_v2(m_db);
        m_db = nullptr;
        return false;
    }
    sqlite3_exec(m_db, "CREATE INDEX IF NOT EXISTS idx_lottery_due ON lottery(status, draw_at)",
                 nullptr, nullptr, nullptr);

    // 每个机器人的启用开关（默认关）：关掉时机器人侧完全不匹配抽奖指令
    if (sqlite3_exec(m_db,
            "CREATE TABLE IF NOT EXISTS lottery_meta ("
            "  appid   INTEGER PRIMARY KEY,"
            "  enabled INTEGER NOT NULL DEFAULT 0"
            ") WITHOUT ROWID", nullptr, nullptr, nullptr) != SQLITE_OK) {
        qWarning() << "Lottery: 建 lottery_meta 失败" << (m_db ? sqlite3_errmsg(m_db) : "");
    }
    m_enabledCache.clear();          // 换库/重开：缓存全部作废

    qDebug().noquote() << QStringLiteral("Lottery 打开完成(SQLite) %1").arg(m_filePath);
    return true;
}

void LotteryMgr::close()
{
    QMutexLocker locker(&m_mutex);
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

// ---------- 启用开关 ----------

// 查一次、之后走缓存（消息热路径每条消息都会问，不能每次都进 SQLite）
bool LotteryMgr::isEnabled(int appid)
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_enabledCache.constFind(appid);
    if (it != m_enabledCache.constEnd()) return it.value() != 0;
    if (!m_db) return false;              // 库没开就先别缓存，等开起来再查真值

    int v = 0;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT enabled FROM lottery_meta WHERE appid=?1", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, appid);
        if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    // 没有这一行 = 默认**不启用**（用户明确要求：不要的机器人别乱匹配指令；
    // 想要的自己在后台勾一下「启用抽奖」）。
    m_enabledCache.insert(appid, v ? 1 : 0);
    return v != 0;
}

bool LotteryMgr::setEnabled(int appid, bool on)
{
    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    sqlite3_stmt *st = nullptr;
    const char *sql = "INSERT INTO lottery_meta(appid,enabled) VALUES(?1,?2) "
                      "ON CONFLICT(appid) DO UPDATE SET enabled=excluded.enabled";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "Lottery: 保存启用开关失败" << sqlite3_errmsg(m_db);
        return false;
    }
    sqlite3_bind_int(st, 1, appid);
    sqlite3_bind_int(st, 2, on ? 1 : 0);
    const bool ok = (sqlite3_step(st) == SQLITE_DONE);
    if (!ok) qWarning() << "Lottery: 写 lottery_meta 失败" << sqlite3_errmsg(m_db);
    sqlite3_finalize(st);
    if (ok) m_enabledCache.insert(appid, on ? 1 : 0);
    return ok;
}

// ---------- 读 ----------

bool LotteryMgr::getLocked(int appid, const QString &id, LotteryRecord &out)
{
    if (!m_db || id.isEmpty()) return false;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT data FROM lottery WHERE appid=?1 AND id=?2", -1, &st, nullptr) != SQLITE_OK)
        return false;

    sqlite3_bind_int(st, 1, appid);
    bindText(st, 2, id);

    bool ok = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const QString json = colText(st, 0);
        out = LotteryRecord::fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
        out.appid = appid;
        if (out.id.isEmpty()) out.id = id;
        ok = true;
    }
    sqlite3_finalize(st);
    return ok;
}

QList<LotteryRecord> LotteryMgr::listLocked(int appid, int statusFilter)
{
    QList<LotteryRecord> out;
    if (!m_db) return out;

    const char *sql = (statusFilter >= 0)
        ? "SELECT data FROM lottery WHERE appid=?1 AND status=?2 ORDER BY draw_at DESC"
        : "SELECT data FROM lottery WHERE appid=?1 ORDER BY draw_at DESC";

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK)
        return out;

    sqlite3_bind_int(st, 1, appid);
    if (statusFilter >= 0) sqlite3_bind_int(st, 2, statusFilter);

    while (sqlite3_step(st) == SQLITE_ROW) {
        LotteryRecord rec = LotteryRecord::fromJson(
            QJsonDocument::fromJson(colText(st, 0).toUtf8()).object());
        rec.appid = appid;
        out.append(rec);
    }
    sqlite3_finalize(st);
    return out;
}

QList<LotteryRecord> LotteryMgr::list(int appid)
{
    QMutexLocker locker(&m_mutex);
    return listLocked(appid, -1);
}

bool LotteryMgr::get(int appid, const QString &id, LotteryRecord &out)
{
    QMutexLocker locker(&m_mutex);
    return getLocked(appid, id, out);
}

// ---------- 写 ----------

bool LotteryMgr::saveLocked(const LotteryRecord &rec)
{
    if (!m_db || rec.id.isEmpty()) return false;

    const QString json = QString::fromUtf8(
        QJsonDocument(rec.toJson()).toJson(QJsonDocument::Compact));

    sqlite3_stmt *st = nullptr;
    // upsert：新建和修改走同一条语句，界面「保存」不用区分
    const char *sql =
        "INSERT INTO lottery(appid, id, name, draw_at, status, data) VALUES(?1,?2,?3,?4,?5,?6)"
        " ON CONFLICT(appid, id) DO UPDATE SET"
        "  name=excluded.name, draw_at=excluded.draw_at, status=excluded.status, data=excluded.data";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK) {
        qWarning() << "Lottery: save prepare 失败" << sqlite3_errmsg(m_db);
        return false;
    }

    sqlite3_bind_int(st, 1, rec.appid);
    bindText(st, 2, rec.id);
    bindText(st, 3, rec.name);
    sqlite3_bind_int64(st, 4, rec.drawAt);
    sqlite3_bind_int(st, 5, rec.status);
    bindText(st, 6, json);

    const bool ok = (sqlite3_step(st) == SQLITE_DONE);
    if (!ok) qWarning() << "Lottery: save 失败" << sqlite3_errmsg(m_db);
    sqlite3_finalize(st);
    return ok;
}

QString LotteryMgr::newRandomId(int len) const
{
    QString s;
    s.reserve(len);
    for (int i = 0; i < len; ++i) {
        s.append(QLatin1Char(kIdChars[QRandomGenerator::global()->bounded(kIdCharsLen)]));
    }
    return s;
}

bool LotteryMgr::save(LotteryRecord &rec)
{
    if (rec.appid == 0) return false;

    QMutexLocker locker(&m_mutex);
    if (!m_db) return false;

    // 新建：给一个库里没用过的随机 id（8 位，够用且不像序号那样能遍历）
    if (rec.id.isEmpty()) {
        for (int attempt = 0; attempt < 32; ++attempt) {
            const QString cand = newRandomId(8);
            LotteryRecord tmp;
            if (!getLocked(rec.appid, cand, tmp)) { rec.id = cand; break; }
        }
        if (rec.id.isEmpty()) return false;
    }
    if (rec.name.trimmed().isEmpty())
        rec.name = QStringLiteral("抽奖 ") + rec.id;
    if (rec.createdAt <= 0)
        rec.createdAt = QDateTime::currentSecsSinceEpoch();

    // ⚠ 界面「保存配置」传进来的是一份**旧快照**：它打开这一场时的 players 可能是空的，
    // 而机器人在那之后已经把参与名单写进库了。整份 JSON 覆盖会把名单冲掉（表现为
    // 「明明有人参与，参与人数却变 0，开奖 0 人中奖」）。
    // → 已存在的记录只接受「配置」几样（name / drawAt / prizes / whitelist / autoDrawCount），
    //   参与名单、中奖名单、公布会话、状态、时间戳一律以库里为准。
    if (!rec.id.isEmpty()) {
        LotteryRecord old;
        if (getLocked(rec.appid, rec.id, old)) {
            rec.players   = old.players;
            rec.winners   = old.winners;
            rec.joined    = old.joined;
            rec.convId    = old.convId;
            rec.ctype     = old.ctype;
            rec.status    = old.status;
            rec.drawnAt   = old.drawnAt;
            rec.createdAt = old.createdAt;
        }
    }

    return saveLocked(rec);
}

bool LotteryMgr::removeLocked(int appid, const QString &id)
{
    if (!m_db || id.isEmpty()) return false;

    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM lottery WHERE appid=?1 AND id=?2", -1, &st, nullptr) != SQLITE_OK)
        return false;
    sqlite3_bind_int(st, 1, appid);
    bindText(st, 2, id);
    const bool ok = (sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    return ok;
}

bool LotteryMgr::remove(int appid, const QString &id)
{
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        ok = removeLocked(appid, id);
    }
    if (ok) emit changed(appid);
    return ok;
}

// ---------- 参与 ----------

int LotteryMgr::join(int appid, const QString &id, const QString &openid, const QString &nick,
                     const QString &safeNick,
                     int ctype, const QString &convId, const QString &groupId, const QString &groupName,
                     QString *errMsg)
{
    if (openid.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("取不到你的用户 id，参与失败");
        return 5;
    }

    LotteryRecord rec;
    {
        QMutexLocker locker(&m_mutex);
        if (!getLocked(appid, id, rec)) {
            if (errMsg) *errMsg = QStringLiteral("没有找到这个抽奖，id 可能输错了");
            return 1;
        }
        if (rec.status == LotteryDrawn) {
            if (errMsg) *errMsg = QStringLiteral("这场抽奖已经开奖了，等下一场吧");
            return 2;
        }
        if (rec.status == LotteryClosed) {
            if (errMsg) *errMsg = QStringLiteral("这场抽奖已经被关闭了");
            return 3;
        }
        // 白名单是**这一场**自己的（不影响别的抽奖）；私聊参与不看群白名单
        if (ctype != 2 && !rec.groupAllowed(groupId, groupName)) {
            if (errMsg) *errMsg = QStringLiteral("这个群不在本场抽奖的白名单里");
            return 7;
        }
        for (const LotteryPlayer &p : rec.players) {
            if (p.openid == openid) {
                if (errMsg) *errMsg = QStringLiteral("你已经参与过了，当前 %1 人参与").arg(rec.players.size());
                return 4;
            }
        }

        LotteryPlayer p;
        p.openid   = openid;
        p.nickname = nick;
        p.safeNick = safeNick.trimmed();     // 平台审核过的昵称，展示优先用它
        p.joinAt   = QDateTime::currentSecsSinceEpoch();
        rec.players.append(p);
        rec.joined = rec.players.size();

        // 公布会话：第一次有人参与时记下（群里 = 群号，私聊 = 对方 openid）
        if (rec.convId.isEmpty()) {
            rec.convId = convId;
            rec.ctype  = (ctype == 2) ? 2 : 0;
        }
        if (!saveLocked(rec)) {
            if (errMsg) *errMsg = QStringLiteral("参与信息保存失败，请稍后再试");
            return 6;
        }
    }

    // 满人开奖：勾了「满 N 人开奖」且这一票正好凑够 → 立刻开
    // （draw 自己持锁、发公告、emit changed；锁外调用，别套着 m_mutex 进去）
    const bool full = (rec.autoDrawCount > 0 && rec.players.size() >= rec.autoDrawCount);

    emit changed(appid);

    if (full) {
        QString derr;
        if (!draw(appid, id, false, &derr)) {
            // 没配奖品之类：留在进行中，等开奖时间到了再走定时器
            qDebug().noquote() << QStringLiteral("Lottery: 满人开奖失败 %1/%2 —— %3")
                                      .arg(appid).arg(id, derr);
        }
    }
    return full ? 10 : 0;        // 10 = 参与成功，并且已经触发满人开奖
}

// ---------- 开奖 ----------

QString LotteryMgr::buildAnnounce(const LotteryRecord &rec) const
{
    QStringList lines;
    lines << QStringLiteral("【开奖】抽奖「%1」")
                 .arg(rec.name.isEmpty() ? rec.id : rec.name);
    lines << QStringLiteral("> 参与 %1 人 · 中奖 %2/%3")
                 .arg(rec.joinedCount()).arg(rec.winners.size()).arg(rec.totalSlots());

    if (rec.winners.isEmpty()) {
        lines << QStringLiteral("> 这次没有人中奖（参与人数不足或未配置奖品）");
        return lines.join(QLatin1Char('\n'));
    }

    // 按奖品分组列（同一档的人拼在一行，避免人多时刷屏）
    QString curLevel, curPrize;
    QStringList names;
    auto flush = [&]() {
        if (names.isEmpty()) return;
        const QString head = curLevel.isEmpty()
            ? curPrize
            : QStringLiteral("%1·%2").arg(curLevel, curPrize);
        // 一行几十个（openid 32 字符）会把单行撑太长，平台对单条长度有上限
        // → 每 20 个换一段，第一段带奖品名，后面只列人。
        const int kPerLine = 20;
        for (int i = 0; i < names.size(); i += kPerLine) {
            const QStringList part = names.mid(i, kPerLine);
            lines << (i == 0 ? QStringLiteral("> %1：%2").arg(head, part.join(QStringLiteral("、")))
                             : QStringLiteral("> %1").arg(part.join(QStringLiteral("、"))));
        }
        names.clear();
    };
    for (const LotteryWinner &w : rec.winners) {
        if (w.prizeName != curPrize || w.level != curLevel) {
            flush();
            curPrize = w.prizeName;
            curLevel = w.level;
        }
        names << winnerLabel(w);            // 安全昵称优先；没有就纯文本 openid（绝不打 at）
    }
    flush();

    lines << QStringLiteral("> 领奖：私聊机器人发 #领取抽奖%1").arg(rec.id);
    lines << QStringLiteral("> 忘了 ID：[我的中奖记录](#我的抽奖) 里每条都写着，也能直接点「点我领奖」");
    return lines.join(QLatin1Char('\n'));
}

void LotteryMgr::postAnnounce(const LotteryRecord &rec)
{
    if (rec.convId.isEmpty()) return;                 // 没有公布会话（纯界面抽的），只留记录
    QQBotClient *bot = m_botClients.value(rec.appid, nullptr);
    if (!bot) return;

    QString text = buildAnnounce(rec);
    const int type = (rec.ctype == 2) ? 2 : 0;
    bot->send_msgAsync(type, rec.convId, QStringLiteral("[抽奖]"), text, QString());
}

bool LotteryMgr::draw(int appid, const QString &id, bool manual, QString *errMsg)
{
    Q_UNUSED(manual)
    LotteryRecord rec;
    bool ok = false;

    {
        QMutexLocker locker(&m_mutex);
        if (!getLocked(appid, id, rec)) {
            if (errMsg) *errMsg = QStringLiteral("抽奖不存在或已被删除");
            return false;
        }
        if (rec.status == LotteryDrawn) {
            if (errMsg) *errMsg = QStringLiteral("这场抽奖已经开过奖了");
            return false;
        }
        if (rec.status == LotteryClosed) {
            if (errMsg) *errMsg = QStringLiteral("这场抽奖已被关闭，不能开奖");
            return false;
        }
        if (rec.prizes.isEmpty()) {
            if (errMsg) *errMsg = QStringLiteral("还没有配置奖品，先在奖品表格里加一行");
            return false;
        }
        // 没人参与就别开：开出来就是一张空名单，而且开完不能重抽。
        // 到点自动开奖遇到这种场次直接跳过（等有人报名再开，或者管理员手动停止作废）。
        if (rec.players.isEmpty()) {
            if (errMsg) *errMsg = QStringLiteral("还没有人参与，先别开");
            return false;
        }

        // 名额表：(奖品下标, 该奖品内第几个) —— 奖品顺序即开奖顺序，一等奖在前
        QList<QPair<int, int>> slotList;
        for (int pi = 0; pi < rec.prizes.size(); ++pi) {
            const int n = qMax(0, rec.prizes.at(pi).count);
            for (int k = 0; k < n; ++k) slotList.append(qMakePair(pi, k));
        }

        // 参与者洗牌（Fisher-Yates，QRandomGenerator 全局源）
        QList<LotteryPlayer> pool = rec.players;
        for (int i = pool.size() - 1; i > 0; --i) {
            const int j = QRandomGenerator::global()->bounded(i + 1);
            if (i != j) pool.swapItemsAt(i, j);
        }

        // 名额比人还多（例：10 份奖、只来 1 个人）：每人仍然只中一份，但中的是**随机哪个奖**，
        // 而不是固定发第一个（否则人少时永远只有一等奖发得出去）。
        // 人够的时候不洗，保持「一等奖先发」的顺序。
        if (pool.size() < slotList.size()) {
            for (int i = slotList.size() - 1; i > 0; --i) {
                const int j = QRandomGenerator::global()->bounded(i + 1);
                if (i != j) slotList.swapItemsAt(i, j);
            }
        }

        rec.winners.clear();
        const int take = qMin(pool.size(), slotList.size());
        for (int s = 0; s < take; ++s) {
            const LotteryPrize &prize = rec.prizes.at(slotList.at(s).first);
            LotteryWinner w;
            w.openid    = pool.at(s).openid;
            w.nickname  = pool.at(s).nickname;
            w.safeNick  = pool.at(s).safeNick;
            w.prizeName = prize.name.isEmpty() ? QStringLiteral("奖品") : prize.name;
            w.level     = prize.level;
            w.text      = prize.textFor(slotList.at(s).second);
            // 没配 / 条数不够 → w.text 留空：这一格在后台明确空着（一眼看出要手动发），
            // 中奖人领取时 claim() 会回「还没有配置发放内容，请联系管理员」。

            // 领取凭证直接用抽奖 ID：用户发 #领取抽奖<抽奖ID> 就能领
            // （同一个抽奖里所有中奖人共用这个 ID，进来按 openid 认人）
            w.wid = rec.id;

            rec.winners.append(w);
        }

        // 开奖后参与名单只留中奖的：人数先快照进 joined，再把 players 清空
        // （后台「参与 X 人」靠 joined，中奖人名单位置靠 winners，两边都不丢）
        rec.joined  = rec.players.size();
        rec.players.clear();
        rec.status  = LotteryDrawn;
        rec.drawnAt = QDateTime::currentSecsSinceEpoch();
        if (rec.drawAt <= 0) rec.drawAt = rec.drawnAt;

        ok = saveLocked(rec);
        if (!ok) {
            if (errMsg) *errMsg = QStringLiteral("开奖结果保存失败");
            return false;
        }
    }

    emit changed(appid);
    postAnnounce(rec);
    return true;
}

bool LotteryMgr::closeLottery(int appid, const QString &id, QString *errMsg)
{
    LotteryRecord rec;
    {
        QMutexLocker locker(&m_mutex);
        if (!getLocked(appid, id, rec)) {
            if (errMsg) *errMsg = QStringLiteral("抽奖不存在或已被删除");
            return false;
        }
        if (rec.status == LotteryDrawn) {
            if (errMsg) *errMsg = QStringLiteral("这场抽奖已经开过奖了，不能再关闭");
            return false;
        }
        if (rec.status == LotteryClosed) {
            if (errMsg) *errMsg = QStringLiteral("这场抽奖已经是关闭状态");
            return false;
        }
        rec.status = LotteryClosed;
        // 停止就作废：参与名单没必要留着，人数快照进 joined 给后台看
        if (!rec.players.isEmpty()) {
            rec.joined = rec.players.size();
            rec.players.clear();
        }
        if (!saveLocked(rec)) {
            if (errMsg) *errMsg = QStringLiteral("关闭失败");
            return false;
        }
    }
    emit changed(appid);
    return true;
}

// ---------- 中奖 / 领取 ----------

QString LotteryMgr::myWinnersText(int appid, const QString &openid)
{
    if (openid.isEmpty()) return QStringLiteral("取不到你的用户 id，查不了中奖记录");

    // 只有「已开奖」的场次才可能有中奖名单：
    // 进行中的没抽、已关闭的作废 —— 直接在 SQL 层筛掉，不用把整库拉出来再逐个判断。
    QList<LotteryRecord> all;
    {
        QMutexLocker locker(&m_mutex);
        all = listLocked(appid, LotteryDrawn);
    }

    QStringList lines;
    int n = 0;
    for (const LotteryRecord &rec : all) {
        for (const LotteryWinner &w : rec.winners) {
            if (w.openid != openid) continue;
            ++n;
            const QString prize = w.level.isEmpty()
                ? w.prizeName
                : QStringLiteral("%1·%2").arg(w.level, w.prizeName);
            lines << QStringLiteral("> %1. 「%2」%3 %4")
                         .arg(n)
                         .arg(rec.name.isEmpty() ? rec.id : rec.name)
                         .arg(prize)
                         .arg(w.delivered ? QStringLiteral("· 已领取") : QStringLiteral("· 未领取"));
            // ID 明写 + 可点领奖：群消息里链接会被剥成纯文字，用户还得能照着 ID 手打
            if (!w.delivered)
                lines << QStringLiteral(">    ID %1 · [点我领奖](#领取抽奖%1)（私聊机器人发）").arg(rec.id);
            else
                lines << QStringLiteral(">    ID %1").arg(rec.id);
        }
    }

    if (n == 0)
        return QStringLiteral("你还没有中奖记录\n"
                              "> [看全部场次](#历史抽奖)：里面有进行中的抽奖，点「点我报名」就能参加\n"
                              "> 参与要在这个抽奖支持的群里发指令，私聊发不算");

    QStringList out;
    out << QStringLiteral("你的中奖记录（共 %1 条）").arg(n);
    out << lines;
    out << QString();
    out << QStringLiteral("> 领奖要私聊机器人 · 更多指令看 [抽奖帮助](#抽奖帮助)");
    return out.join(QLatin1Char('\n'));
}

QString LotteryMgr::historyText(int appid, const QString &groupId, const QString &groupName)
{
    const QList<LotteryRecord> all = list(appid);

    // 每场抽奖有自己的白名单：别的群的抽奖不在这里露出来
    QList<LotteryRecord> running, finished;
    for (const LotteryRecord &r : all) {
        if (!r.groupAllowed(groupId, groupName)) continue;
        if (r.status == LotteryRunning) running.append(r);
        else                            finished.append(r);   // 已开奖 + 已关闭（旧的也能翻）
    }
    if (running.isEmpty() && finished.isEmpty())
        return QStringLiteral("本群还没有可参与的抽奖，等管理员开一场吧");

    const int kMax = 8;                  // 每段最多列 8 场，群里刷屏没意义

    QStringList lines;
    lines << QStringLiteral("抽奖列表（共 %1 场）").arg(running.size() + finished.size());

    // 每场压成 2 行（> 引用块内），比原来 4 行省一半地方。
    // [显示](指令) 会被 api.cpp 转成可点的内联命令；抽奖名做成链接 → 点开看奖品档位。
    auto pushOne = [&](const LotteryRecord &r) {
        lines << QStringLiteral("> [%1](#抽奖奖品%2) · %3 · %4 · 中奖 %5/%6")
                     .arg(r.name.isEmpty() ? r.id : r.name, r.id, r.statusText(),
                          fmtTime(r.drawAt))
                     .arg(r.joinedCount())
                     .arg(r.winners.size()).arg(r.totalSlots());
        QStringList acts;
        acts << QStringLiteral("ID %1").arg(r.id);      // 链接被剥掉时用户还得能手打
        if (r.status == LotteryRunning)
            acts << QStringLiteral("[点我报名](#参与抽奖%1)").arg(r.id);
        if (r.status == LotteryDrawn && !r.winners.isEmpty())
            acts << QStringLiteral("[看中奖名单](#抽奖名单%1)").arg(r.id);
        acts << QStringLiteral("[看奖品](#抽奖奖品%1)").arg(r.id);
        lines << QStringLiteral("> ") + acts.join(QStringLiteral(" · "));
    };

    if (!running.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("进行中 %1 场").arg(running.size());
        const int shown = qMin(running.size(), kMax);
        for (int i = 0; i < shown; ++i) pushOne(running.at(i));
        if (running.size() > shown)
            lines << QStringLiteral("> …还有 %1 场没列出").arg(running.size() - shown);
    }

    if (!finished.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("往期 %1 场（已开奖 / 已关闭）").arg(finished.size());
        const int shown = qMin(finished.size(), kMax);
        for (int i = 0; i < shown; ++i) pushOne(finished.at(i));   // 旧记录也能点开看中奖人
        if (finished.size() > shown)
            lines << QStringLiteral("> …还有 %1 场没列出").arg(finished.size() - shown);
    }

    lines << QString();
    lines << QStringLiteral("[我的中奖记录](#我的抽奖) · [抽奖帮助](#抽奖帮助)");
    return lines.join(QLatin1Char('\n'));
}

QString LotteryMgr::helpText(int appid, const QString &openid,
                             const QString &groupId, const QString &groupName)
{
    const QList<LotteryRecord> all = list(appid);

    // 白名单照样过滤：别的群的抽奖不在这个群露出来
    QList<LotteryRecord> running, drawn;
    for (const LotteryRecord &r : all) {
        if (!r.groupAllowed(groupId, groupName)) continue;
        if (r.status == LotteryRunning)      running.append(r);
        else if (r.status == LotteryDrawn)   drawn.append(r);
    }

    // 这个人还有哪几场没领 —— 直接把「去领奖」摆到最上面，省得他自己翻列表找 ID
    QStringList pendingIds;
    if (!openid.isEmpty()) {
        for (const LotteryRecord &r : drawn) {
            for (const LotteryWinner &w : r.winners) {
                if (w.openid == openid && !w.delivered) { pendingIds.append(r.id); break; }
            }
        }
    }

    // ⚠ 排版用 > 引用块：QQ 里会渲染成左侧竖条的小字块，比一堆独立行省地方；
    //   顺带解决了「行首 # 被当成标题吃掉」——行首是 >，命令里的 # 安全。
    // 所有完整指令都写成 [文字](#指令)：频道/支持内联命令的客户端里是**可点按钮**，
    // 群消息被 convertMdLinksKeepHttp 剥成纯文字时也还剩「文字」，配合旁边写出来的 ID 仍可手打。
    QStringList lines;
    lines << QStringLiteral("抽奖帮助");

    // ① 中奖了去哪看（用户问得最多的就是这个）
    lines << QStringLiteral("① 我中了什么");
    lines << QStringLiteral("> [我的中奖记录](#我的抽奖)：列出你中过的每一场 —— 抽奖名 / 奖品 / 领没领");
    lines << QStringLiteral("> 没领的那条下面有「点我领奖」，点一下就是领奖指令；也能看每场的 ID");
    if (!pendingIds.isEmpty()) {
        QStringList links;
        const int kMaxLink = 3;
        for (int i = 0; i < pendingIds.size() && i < kMaxLink; ++i)
            links << QStringLiteral("[点我领奖](#领取抽奖%1)").arg(pendingIds.at(i));
        lines << QStringLiteral("> ⚠ 你还有 %1 份奖励没领：%2")
                     .arg(pendingIds.size()).arg(links.join(QStringLiteral(" · ")));
        if (pendingIds.size() > kMaxLink)
            lines << QStringLiteral("> 另外 %1 份看 [我的中奖记录](#我的抽奖)")
                         .arg(pendingIds.size() - kMaxLink);
    }

    // ② 领奖方式（最容易搞错的一步：必须私聊）
    lines << QString();
    lines << QStringLiteral("② 怎么领");
    lines << QStringLiteral("> 必须【私聊机器人】发 #领取抽奖 抽奖ID —— 群里发只提示你去私聊");
    lines << QStringLiteral("> 兑换码只私聊回给你一个人，不会出现在群里");
    lines << QStringLiteral("> 抽奖ID = 8 位字母数字，在「我的抽奖」每条的 ID 行，或下面场次里");

    // ③ 本群进行中
    lines << QString();
    if (running.isEmpty()) {
        lines << QStringLiteral("③ 本群进行中：暂无");
        lines << QStringLiteral("> 等管理员开一场吧 · [看往期记录](#历史抽奖)");
    } else {
        const int kMaxRun = 5;              // 按钮多了反而看不清，只列最近 5 场
        const int shown = qMin(running.size(), kMaxRun);
        lines << QStringLiteral("③ 本群进行中（%1 场）").arg(running.size());
        for (int i = 0; i < shown; ++i) {
            const LotteryRecord &r = running.at(i);
            // 抽奖名点一下 = 看这一场的奖品 / 名额（[显示](指令) 会被转成可点的内联命令）
            lines << QStringLiteral("> %1. [%2](#抽奖奖品%3) · %4 开奖 · %5 人已参与")
                         .arg(i + 1)
                         .arg(r.name.isEmpty() ? r.id : r.name, r.id, fmtTime(r.drawAt))
                         .arg(r.joinedCount());
            // ID 明写出来：群消息里链接会被剥掉，用户还得能手打
            QString tail = QStringLiteral(">    ID %1 · [点我报名](#参与抽奖%1)").arg(r.id);
            if (r.autoDrawCount > 0)
                tail += QStringLiteral(" · 满 %1 人开奖（还差 %2 人）")
                            .arg(r.autoDrawCount)
                            .arg(qMax(0, r.autoDrawCount - r.joinedCount()));
            lines << tail;
        }
        if (running.size() > shown)
            lines << QStringLiteral("> …还有 %1 场（只显示最近 %2 场）").arg(running.size() - shown).arg(shown);
    }

    // ④ 最近开奖
    if (!drawn.isEmpty()) {
        const LotteryRecord &r = drawn.first();
        lines << QString();
        lines << QStringLiteral("④ 最近开奖");
        lines << QStringLiteral("> 「%1」· ID %2")
                     .arg(r.name.isEmpty() ? r.id : r.name, r.id);
        lines << QStringLiteral("> [看中奖名单](#抽奖名单%1) · [看我中没中](#我的抽奖)").arg(r.id);
    }

    // ⑤ 其他 + 管理员
    lines << QString();
    lines << QStringLiteral("⑤ 其他");
    lines << QStringLiteral("> [全部场次 / 往期记录](#历史抽奖) · [我的中奖记录](#我的抽奖) · [本帮助](#抽奖帮助)");
    lines << QStringLiteral("> 管理员：#开奖 抽奖ID · #停止抽奖 抽奖ID（把 ID 换成上面那串）");

    return lines.join(QLatin1Char('\n'));
}

QString LotteryMgr::prizeListText(int appid, const QString &id,
                                  const QString &groupId, const QString &groupName)
{
    if (id.isEmpty())
        return QStringLiteral("用法：#抽奖奖品<抽奖ID>（发 #历史抽奖 可以看到 ID）");

    LotteryRecord rec;
    if (!get(appid, id, rec))
        return QStringLiteral("没有找到这个抽奖，发 #历史抽奖 看看列表");
    if (!rec.groupAllowed(groupId, groupName))
        return QStringLiteral("这场抽奖不在本群的白名单里");

    QStringList lines;
    lines << QStringLiteral("「%1」%2 · ID %3")
                 .arg(rec.name.isEmpty() ? rec.id : rec.name, rec.statusText(), rec.id);
    lines << QStringLiteral("> %1 开奖 · 参与 %2 人 · 中奖 %3/%4")
                 .arg(fmtTime(rec.drawAt)).arg(rec.joinedCount())
                 .arg(rec.winners.size()).arg(rec.totalSlots());

    if (rec.prizes.isEmpty()) {
        lines << QStringLiteral("> 还没配置奖品");
    } else {
        int n = 0;
        for (const LotteryPrize &p : rec.prizes) {
            ++n;
            // 条的算法要和 textFor 一致：没写 = 0 条（未配置）；只写一行 = 1 条（全员同发）
            const QString body = p.text.trimmed();
            const int codes = body.isEmpty() ? 0
                                             : body.split(QStringLiteral("|||"), Qt::SkipEmptyParts).size();
            const QString prize = p.level.isEmpty()
                ? (p.name.isEmpty() ? QStringLiteral("奖品") : p.name)
                : QStringLiteral("%1·%2").arg(p.level, p.name.isEmpty() ? QStringLiteral("奖品") : p.name);
            QString how;
            if (codes <= 0)      how = QStringLiteral("未配发放内容（需手动发）");
            else if (codes == 1) how = QStringLiteral("1 条 · 全员同发");
            else                 how = QStringLiteral("%1 条 · 一人一条").arg(codes);
            lines << QStringLiteral("> %1. %2 ×%3 —— %4")
                         .arg(n).arg(prize).arg(qMax(0, p.count)).arg(how);
        }
        lines << QStringLiteral("（名额共 %1 份 = 各档人数之和）").arg(rec.totalSlots());
    }

    if (rec.status == LotteryRunning)
        lines << QStringLiteral("[点我报名](#参与抽奖%1)").arg(rec.id);
    else if (rec.status == LotteryDrawn && !rec.winners.isEmpty())
        lines << QStringLiteral("[看中奖名单](#抽奖名单%1) · [看我中没中](#我的抽奖)").arg(rec.id);

    return lines.join(QLatin1Char('\n'));
}

QString LotteryMgr::winnerListText(int appid, const QString &id,
                                   const QString &groupId, const QString &groupName)
{
    if (id.isEmpty())
        return QStringLiteral("用法：#抽奖名单<抽奖ID>（发 #历史抽奖 可以看到 ID）");

    LotteryRecord rec;
    if (!get(appid, id, rec))
        return QStringLiteral("没有找到这个抽奖，发 #历史抽奖 看看列表");
    if (!rec.groupAllowed(groupId, groupName))
        return QStringLiteral("这场抽奖不在本群的白名单里");

    QStringList lines;
    lines << QStringLiteral("「%1」%2 · ID %3")
                 .arg(rec.name.isEmpty() ? rec.id : rec.name, rec.statusText(), rec.id);
    lines << QStringLiteral("> 参与 %1 人 · 中奖 %2/%3")
                 .arg(rec.joinedCount()).arg(rec.winners.size()).arg(rec.totalSlots());

    if (rec.status == LotteryRunning) {
        lines << QStringLiteral("> 还没开奖 · %1 开奖").arg(fmtTime(rec.drawAt));
        lines << QStringLiteral("> [点我报名](#参与抽奖%1)").arg(rec.id);
        return lines.join(QLatin1Char('\n'));
    }
    if (rec.status == LotteryClosed) {
        lines << QStringLiteral("> 这场抽奖已关闭，没有开奖");
        return lines.join(QLatin1Char('\n'));
    }
    if (rec.winners.isEmpty()) {
        lines << QStringLiteral("> 这场没有人中奖");
        return lines.join(QLatin1Char('\n'));
    }

    int n = 0;
    for (const LotteryWinner &w : rec.winners) {
        ++n;
        const QString prize = w.level.isEmpty()
            ? w.prizeName
            : QStringLiteral("%1·%2").arg(w.level, w.prizeName);
        lines << QStringLiteral("> %1. %2 · %3")
                     .arg(n).arg(winnerLabel(w), prize);   // 安全昵称优先，没有就纯文本 openid
    }
    lines << QStringLiteral("> 你中没中看 [我的中奖记录](#我的抽奖)，中了在那条点「点我领奖」");
    return lines.join(QLatin1Char('\n'));
}

bool LotteryMgr::claim(int appid, const QString &lotteryId, const QString &openid,
                       QString *text, QString *errMsg)
{
    if (errMsg) errMsg->clear();
    if (lotteryId.isEmpty()) {
        if (errMsg) *errMsg = QStringLiteral("缺少抽奖ID，例：#领取抽奖hv4udba5（发 #我的抽奖 能看到）");
        return false;
    }

    QString got;
    QString fail;
    bool claimed = false;

    {
        QMutexLocker locker(&m_mutex);

        auto deliver = [&](LotteryRecord &r, LotteryWinner &w) {
            const QString body = w.text.trimmed();
            // 没配发放内容（没写 ||| / 条数不够）就不算领过：不标记已发放，
            // 让管理员在后台手动发完再点「标记已发放」。
            if (body.isEmpty()) {
                fail = QStringLiteral("这个奖品还没有配置发放内容，请联系管理员（开发者）手动发放");
                return false;
            }
            w.delivered   = true;
            w.deliveredAt = QDateTime::currentSecsSinceEpoch();
            if (!saveLocked(r)) {
                fail = QStringLiteral("领取记录保存失败，请稍后再试");
                return false;
            }
            got = body;
            claimed = true;
            return true;
        };

        // 领取凭证 = 抽奖 ID：先按场次找，再在这个场次里按 openid 认人
        LotteryRecord rec;
        const bool found = getLocked(appid, lotteryId, rec);
        if (found && rec.status != LotteryDrawn) {
            // 已关闭 / 还没开奖的场次直接告诉他，别白跑一趟
            fail = (rec.status == LotteryClosed)
                ? QStringLiteral("这场抽奖已经关闭了，没有奖励可领")
                : QStringLiteral("这场抽奖还没开奖，等开奖后再来领");
        } else if (found) {
            bool mine = false;      // 这场的名单里有这个人
            bool done = false;      // 有记录但都领过了
            for (LotteryWinner &w : rec.winners) {
                if (!openid.isEmpty() && w.openid != openid) continue;
                mine = true;
                if (w.delivered) { done = true; continue; }   // 可能中了多个，继续翻没领的那条
                deliver(rec, w);
                break;
            }
            if (!claimed && fail.isEmpty()) {
                if (!mine)     fail = QStringLiteral("这场抽奖没有你的中奖记录，发 #我的抽奖 看看");
                else if (done) fail = QStringLiteral("这场抽奖的奖励你已经领过了");
                else           fail = QStringLiteral("这场抽奖没有可领取的奖励");
            }
        } else {
            // 旧数据兜底：早期版本给每条中奖记录发过随机「领取ID」。
            // 只翻「已开奖」的场次 —— 进行中的没有名单、已关闭的作废，不白遍历。
            // ⚠ 不走 SQL 的 status 过滤：历史数据里 status 列可能是 0（进行中）但名单已经存在，
            //   以「有中奖名单」为准，否则这些人会领不到。
            QList<LotteryRecord> all = listLocked(appid, -1);
            for (LotteryRecord &r : all) {
                if (r.status != LotteryDrawn && r.winners.isEmpty()) continue;
                bool hit = false;
                for (LotteryWinner &w : r.winners) {
                    if (w.wid != lotteryId) continue;
                    hit = true;
                    if (!openid.isEmpty() && w.openid != openid) {
                        fail = QStringLiteral("这条中奖记录不是你的哦");
                    } else if (w.delivered) {
                        fail = QStringLiteral("这条奖励已经领取过了");
                    } else {
                        deliver(r, w);
                    }
                    break;
                }
                if (hit) break;      // 命中了就别再翻别的场次
            }
            if (!claimed && fail.isEmpty())
                fail = QStringLiteral("没有找到这个抽奖，发 #我的抽奖 看看你的中奖记录");
        }
    }

    if (claimed) {
        emit changed(appid);
        if (text) *text = got;
        return true;
    }
    if (errMsg) *errMsg = fail.isEmpty() ? QStringLiteral("领取失败，请稍后再试") : fail;
    return false;
}

bool LotteryMgr::markDelivered(int appid, const QString &lotteryId, const QString &openid)
{
    if (openid.isEmpty()) return false;
    bool ok = false;
    {
        QMutexLocker locker(&m_mutex);
        LotteryRecord rec;
        if (getLocked(appid, lotteryId, rec)) {
            for (LotteryWinner &w : rec.winners) {
                if (w.openid != openid) continue;
                if (w.delivered) return true;
                w.delivered   = true;
                w.deliveredAt = QDateTime::currentSecsSinceEpoch();
                ok = saveLocked(rec);
                break;
            }
        }
    }
    if (ok) emit changed(appid);
    return ok;
}

// ---------- 自动开奖 ----------

void LotteryMgr::startAutoDraw()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &LotteryMgr::onTick);
    }
    if (!m_timer->isActive()) m_timer->start(LOTTERY_TICK_MS);
}

void LotteryMgr::onTick()
{
    QList<QPair<int, QString>> due;
    {
        QMutexLocker locker(&m_mutex);
        if (!m_db) return;

        sqlite3_stmt *st = nullptr;
        if (sqlite3_prepare_v2(m_db,
                "SELECT appid, id FROM lottery WHERE status=0 AND draw_at>0 AND draw_at<=?1",
                -1, &st, nullptr) != SQLITE_OK)
            return;
        sqlite3_bind_int64(st, 1, QDateTime::currentSecsSinceEpoch());
        while (sqlite3_step(st) == SQLITE_ROW) {
            due.append(qMakePair(sqlite3_column_int(st, 0), colText(st, 1)));
        }
        sqlite3_finalize(st);
    }

    // 锁外开奖（draw 自己会持锁、会发消息、会 emit）
    for (const auto &d : due) {
        // 这个机器人关了抽奖：别自动开奖、也别往群里发公告（重新打开后照旧会开）
        if (!isEnabled(d.first)) continue;
        QString err;
        if (!draw(d.first, d.second, false, &err)) {
            // 「没有人参与」不算错误：等有人报名下一轮再开，不然每 30 秒刷一条
            if (!err.contains(QStringLiteral("还没有人参与"))) {
                // 没配奖品之类：留在进行中，下一轮再试（qDebug 免得每 30 秒刷一条警告）
                qDebug().noquote() << QStringLiteral("Lottery: 自动开奖跳过 %1/%2 —— %3")
                                          .arg(d.first).arg(d.second, err);
            }
        }
    }
}
