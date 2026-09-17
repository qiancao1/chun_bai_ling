/*
 * 纯白铃 - AI 长期日志（write_log 工具的后端）
 *
 * AI 主动调用 write_log 把重要内容记下来，每次请求时这份日志会被拼进系统提示词，
 * 让 AI 跨会话记住（称呼、偏好、约定、群里发生过的事……）。
 *
 * 三条规则：
 *   1. 每个账号一份，存在 LMDB 键值库 aidb 里：key = "ailog_<appid>"，值是整份日志文本。
 *      （分隔符是下划线不是冒号，原因见 keyOf() 上方 —— 用冒号会被 AI 记忆管理页
 *      当成会话列出来。它天然会被"清空上下文 / 清除记忆"之类操作排除在外，只有
 *      `#清空日志` 能删它。）
 *   2. 长度有上限（默认 1w 字符，见 maxChars()），因为它是要塞进上下文的。
 *   3. 追加时若超限，从**最早的一条**开始丢（FIFO），永远不会把最后写进去的挤掉。
 *      单条还有长度上限（entryLimit），防止一条超长内容把整个日志清空。
 *
 * 为什么存 aidb 而不是单开文件：项目里 AI 的状态（会话上下文、sxw、记忆）本来就都在
 * aidb，放一起才统一；而且 LmdbKV 的每个操作自带互斥量，不用自己管文件句柄和目录。
 *
 * 为什么还是存"整份文本"而不是一条一个键：LmdbKV 没有前缀扫描 / 有序游标接口，
 * 一条一个键就得每次追加都 getAllKeys() 全库扫一遍来找下一个序号。整份读写反而最快，
 * 也正好匹配注入场景（一次 get 拿全文）——上限才 1w 字符，整份也就 10KB。
 *
 * 纯头文件 + 全 inline + namespace，不需要改 CMakeLists。
 *
 * 线程说明：工具调用是在线程池线程里执行的（handleAiResponse），
 * 而系统提示词拼装在主线程（buildBaseContext），所以本地这把互斥量还是必须留着 ——
 * LmdbKV 只保证单次 put/get 原子，不保证"读-改-写"这个复合序列不被打断，
 * 两个线程同时 append 会各读同一份旧内容、后写的覆盖前一个。长度上限用 atomic 缓存，
 * 避免每次读 g_config。
 */
#ifndef AILOG_H
#define AILOG_H

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <atomic>
#include <utility>   // std::as_const

#include "lmdbkv.h"   // LmdbKV；它只依赖 QObject/QString/QByteArray/QMutex/lmdb.h，不拉 UI 头

// g_config 定义在 main.cpp，aidb 也是（global.h 里各有一份同样声明）。
// 这里自己写这两句是为了不把整个 global.h（会拽进一堆 UI 头）包进来。
extern QJsonObject g_config;
extern LmdbKV *aidb;

namespace AiLog {

// 默认上限（字符）：1w，纯粹是上下文消耗的考虑
inline int defaultMaxChars() { return 10000; }

// 把配置值夹到合理区间
inline int clampMax(int v)
{
    if (v <= 0) return defaultMaxChars();
    if (v < 200) return 200;              // 再小日志就没意义了
    if (v > 1000000) return 1000000;      // 兜底，别把上下文撑爆
    return v;
}

// 长度上限的缓存。第一次调用时从 g_config["ai_log_max"] 读一次，之后只读这个原子量。
// 这样工具调用（线程池线程）不会反复去读 g_config —— 它可能正被主线程上的
// #日志长度 指令改写，QJsonObject 边读边写是数据竞争。
inline std::atomic<int> &maxCache()
{
    static std::atomic<int> v{0};
    return v;
}

// 当前上限（字符）
inline int maxChars()
{
    int v = maxCache().load(std::memory_order_relaxed);
    if (v > 0) return v;

    v = clampMax(g_config.value(QStringLiteral("ai_log_max")).toInt(0));
    maxCache().store(v, std::memory_order_relaxed);
    return v;
}

// 运行时改上限（#日志长度 指令用）：同时更新缓存和 g_config，并返回生效值
inline int setMaxChars(int v)
{
    const int n = clampMax(v);
    g_config[QStringLiteral("ai_log_max")] = n;
    maxCache().store(n, std::memory_order_relaxed);
    return n;
}

// 单条内容的上限：总上限的 1/4，至少 200 字符。
// 不留这个限制的话，一条超长内容会把整份日志挤成它自己。
inline int entryLimit(int limit)
{
    const int v = limit / 4;
    return v < 200 ? 200 : v;
}

// 每个账号一份日志。
//
// ⚠️ key 里**不能有冒号**，这跟 aidb 里会话键的格式 `<appid>:<openid>` 有关：
// AI 记忆管理页（ai/aisxw.cpp:23）用 `key.split(":")` 后判断 `size() < 2` 来
// 过滤掉非会话键 —— 分隔符若也用冒号，日志就会混进那个列表变成一条叫
// "ailog:123" 的条目，点进去还能被"保存"按钮覆盖掉（等于把日志清了）。
// 改下划线后 split 只得到 1 段，天然被过滤，不会污染那里。
//
// appid 用 AccountInfo::appid_int —— 它等于 appid.toInt()（accountinfo.cpp:115），
// 而写入侧的 ev.appid 也是同一个值（qqbotclient.cpp:912 `ev.appid = m_info->appid_int`），
// 所以"写"和"注入时读"用的是同一把钥匙，不要改成别的字段。
inline QString keyOf(int appid)
{
    return QStringLiteral("ailog_") + QString::number(appid);
}

// 保护"读-改-写"的互斥量（C++11 起局部 static 的初始化是线程安全的）
inline QMutex &mutex()
{
    static QMutex m;
    return m;
}

// 把一条内容压成单行：换行/制表折叠成空格，多余空格合并，去首尾空白。
// 一行一条，裁剪和注入都简单，也不会因为换行把格式搞乱。
inline QString oneLine(const QString &s)
{
    QString t = s;
    t.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t]+")), QStringLiteral(" "));
    t.replace(QRegularExpression(QStringLiteral(" {2,}")), QStringLiteral(" "));
    return t.trimmed();
}

// 读全文（没有记录返回空串）
inline QString readAll(int appid)
{
    QMutexLocker locker(&mutex());
    if (!aidb) return QString();
    return aidb->get(keyOf(appid));
}

// 清空（删掉这个账号的记录）
inline bool clear(int appid, QString *err = nullptr)
{
    QMutexLocker locker(&mutex());
    if (!aidb) {
        if (err) *err = QStringLiteral("AI 日志库未就绪");
        return false;
    }
    return aidb->remove(keyOf(appid));
}

// 追加一条后的结果
struct AppendResult {
    bool ok = false;
    int chars = 0;     // 写入后日志总字符数
    int count = 0;     // 写入后条数
    int dropped = 0;   // 本次因超限丢弃的条数
    QString err;
};

// 追加一条。limit <= 0 时用当前配置的上限。
inline AppendResult append(int appid, const QString &content, int limit = 0)
{
    AppendResult r;
    if (limit <= 0) limit = maxChars();

    QString line = oneLine(content);
    if (line.isEmpty()) {
        r.err = QStringLiteral("内容为空，什么都没记");
        return r;
    }

    // 单条截断
    const int el = entryLimit(limit);
    if (line.size() > el)
        line = line.left(el - 1) + QStringLiteral("…");

    line = QStringLiteral("[%1] %2")
               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), line);

    QMutexLocker locker(&mutex());
    if (!aidb) {
        r.err = QStringLiteral("AI 日志库未就绪");
        return r;
    }

    QStringList lines;
    const QString old = aidb->get(keyOf(appid));
    if (!old.trimmed().isEmpty())
        lines = old.split(QStringLiteral("\n"), Qt::SkipEmptyParts);

    lines.append(line);

    // 总长（含换行）超过上限就从最早的一条开始丢
    auto totalChars = [&lines]() {
        int n = 0;
        for (const QString &l : std::as_const(lines)) n += l.size() + 1;   // +1 是换行
        return n;
    };
    while (lines.size() > 1 && totalChars() > limit) {
        lines.removeFirst();
        ++r.dropped;
    }

    const QString out = lines.join(QStringLiteral("\n")) + QStringLiteral("\n");

    if (!aidb->put(keyOf(appid), out)) {
        r.err = QStringLiteral("写入失败（AI 日志库拒绝写入）");
        return r;
    }

    r.ok     = true;
    r.chars  = out.size();
    r.count  = lines.size();
    return r;
}

} // namespace AiLog

#endif // AILOG_H
