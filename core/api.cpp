/*
 * 纯白铃 - QQ 机器人管理平台 - DLL 插件 SDK
 * [当前文件的简短功能描述]
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


#include "apiprocessor.h"
#include "netmanager.h"
#include "qqbotclient.h"
#include "qcffmpeg.h"     // 自建 ffmpeg.dll 的动态加载封装（用到才加载，缺了就补包）
#include <QRandomGenerator>
#include <qwaitcondition.h>
#include <string>
#include <cstring>
#include "global.h"
#include <QFile>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtMath>
#include <QNetworkReply>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QMutex>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QThread>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <thread>
#include <memory>
#include <functional>

const int OUTLOG = 1; //输出日志
const int API_ID_SEND_MESSAGES    = 2; //发送消息
const int API_ID_SEND_MESSAGES_ARK = 3; //发送卡片
const int API_ID_DELETE_MESSAGES  = 4; //撤回消息
const int API_ID_GENERATE_SHARE_LINK = 5; //获取邀请链接
const int API_ID_RESPOND_INTERACTION = 6; //响应回调
const int API_ID_BOT_LIST = 7; //获取机器人列表
const int API_ID_GET_OPENID = 8; //获取用户openid
const int API_ID_GET_USER_NAME=9; //获取用户昵称
const int API_ID_PYTHON_HTTP=10; //弃用
const int API_ID_GET_USER_ID=11;

const int API_ID_HTMLIMG1=12; //html制图
const int API_ID_HTMLIMG2=13;
const int API_ID_DS=14; //添加定时
const int API_ID_AI=15; //调用内部AI
const int API_ID_GET_MEMBER=16;//获取群成员消息
const int API_ID_GET_MEMBER_LIST=17; //获取群成员列表
const int API_ID_GET_groups_info=18;//获取群消息
const int API_ID_GET_groups_bot_state=19; //获取机器人群内状态

const int API_ID_SET_JOIN_REQUEST=20; //处理加群请求
const int API_ID_GET_JOIN_REQUEST_LIST=21 ;//获取加群列表
const int API_ID_SET_MUTE_G=22; //禁言某人
const int API_ID_GET_MUTE_LIST_G=23; //获取禁言列表

const int API_ID_REMOV_MEMBER =24;//批量移除成员
const int API_ID_GET_GROUP_BLCKLIST=25; //获取群黑名单列表
const int API_ID_GROUP_BLCKLIST=26; //修改黑名单列表



void DelFileSync_Cnb();
QString renderInThread(const QString &htmlContent,int width = 400) ;
inline QString toQString(const char* s) {
    return s ? QString::fromUtf8(s) : QString();
}

inline int toInt(const char* s) {
    return s ? std::atoi(s) : 0;
}

inline bool toBool(const char* s) {
    if (!s) return false;
    QString str = QString::fromUtf8(s).trimmed().toLower();
    return str == "1" || str == "true";
}

inline QJsonArray toJsonArray(const char* s) {
    if (!s) return QJsonArray();
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(s));
    if (doc.isArray()) return doc.array();

    return QJsonArray();
}

inline QJsonObject toJsonObject(const char* s) {
    if (!s) return QJsonObject();
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(s));
    if (doc.isObject()) return doc.object();

    return QJsonObject();
}

inline QByteArray toByteArray(const char* s) {
    if (!s) return QByteArray();
    return QByteArray(s, std::strlen(s));
}

QString formatDuration(qint64 seconds) {
    const qint64 DAY_SECS = 86400;
    qint64 days = seconds / DAY_SECS;
    qint64 remainder = seconds % DAY_SECS;
    qint64 hours = remainder / 3600;
    qint64 minutes = (remainder % 3600) / 60;
    qint64 secs = remainder % 60;

    return QString("%1天%2时%3分%4秒")
        .arg(days)
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}

QString convertMdLinksKeepHttp(const QString &input)
{
    static QRegularExpression re(R"((?<!!)\[([^\]]*?)\]\(([^\)]*?)\))");
    QRegularExpressionMatchIterator it = re.globalMatch(input);

    QString output;
    int lastIndex = 0;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int start = match.capturedStart();
        int end = match.capturedEnd();

        // 添加匹配之前的普通文本
        output.append(QStringView(input).mid(lastIndex, start - lastIndex));

        QString text = match.captured(1);   // 方括号内文字
        QString url = match.captured(2);    // 圆括号内地址

        // 检查 url 是否以 http:// 或 https:// 开头（不区分大小写）
        QString lowerUrl = url.toLower();
        bool isHttpLink = lowerUrl.startsWith("http://") || lowerUrl.startsWith("https://");

        if (isHttpLink) {
            // 保持原样
            output.append(match.captured(0));
        } else {
            // 只保留方括号内的文字
            output.append(text);
        }

        lastIndex = end;
    }

    // 添加剩余文本
    output.append(QStringView(input).mid(lastIndex));

    return output;
}

QString convertMarkdownLinksToXml(const QString &input)
{
    // 修改正则：前面不能有感叹号（排除图片格式）
    QRegularExpression re(R"((?<!!)\[([^\]]*?)\]\(([^\)]*?)\))");
    QRegularExpressionMatchIterator it = re.globalMatch(input);

    QString output;
    int lastIndex = 0;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int start = match.capturedStart();
        int end = match.capturedEnd();

        output.append(QStringView(input).mid(lastIndex, start - lastIndex));

        QString showText = match.captured(1);
        QString url = match.captured(2);

        QString lowerUrl = url.toLower();
        bool shouldConvert = !(lowerUrl.startsWith("http://") ||
                               lowerUrl.startsWith("https://") ||
                               lowerUrl.startsWith("mqqapi://") ||
                               lowerUrl.startsWith("qagent://")) ;

        if (shouldConvert) {
            QString encodedUrl = QString::fromUtf8(QUrl::toPercentEncoding(url));
            showText.replace("%", "%25");

            encodedUrl.replace("\\","\\\\");
            encodedUrl.replace("\"","\\\"");
            if(encodedUrl.isEmpty())
                encodedUrl=showText;
            if(url.size()>95)
            {
                QString xmlTag = QString("[%1](qagent://aio/inlinecmd?command=%2)")
                .arg(showText,encodedUrl);
                //qDebug() <<xmlTag;
                output.append(xmlTag);
            }else{


                QString xmlTag = QString("<qqbot-cmd-input text=\"%1\" show=\"%2\" reference=\"false\" />")
                                 .arg(encodedUrl, showText);
                output.append(xmlTag);
            }

        } else {
            output.append(match.captured(0));
        }

        lastIndex = end;
    }

    output.append(QStringView(input).mid(lastIndex));
    return output;
}
QString botlist()
{
    qint64 now = QDateTime::currentSecsSinceEpoch();
    QDateTime dt = QDateTime::fromSecsSinceEpoch(now);
    int day = dt.date().day();

    QJsonArray array;
    for(auto &info : m_accounts)
    {
        if (!info) continue;
        if(info->appid_int==0) continue;
        QJsonObject obj;
        obj["appid"] = info->appid_int;
        obj["name"] = info->nickname;
        obj["qq"]=info->botqq;
        obj["avatarPath"] = info->avatarPath;
        obj["total_received"] = info->message_received;//累计
        obj["total_sent"]=info->message_sent;
        obj["received"] = info->received;//当前运行
        obj["sent"]=info->sent;
        obj["online"] = info->online;
        obj["id"] = info->pduid; //频道id
        obj["union_openid"]=info->unid;   //QQid
        if(info->online)
            obj["time"] = formatDuration(now-info->startup_time);
        else
            obj["time"] = "未在线";
        obj["admin"] = info->admin;
        if(g_botdb.contains(info->appid_int)){
            auto *db = g_botdb[info->appid_int];

            obj["dau"] = db->m_userDailyMsg.size();
            obj["group_dau"] = db->m_groupDailyMsg.size();
        }else{
            obj["dau"] = 0;
            obj["group_dau"] = 0;
        }

        if(info->日计时变量!=day)
        {
            info->今日加群数量 = 0;
            info->今日退群数量 = 0;
            info->今日好友数量 = 0;
            info->今日删除好友数量 = 0;
            info->日计时变量 = day;
            info->今日频道数量=0;
            info->今日退出频道数量=0;
        }
        obj["today_join_count"] = info->今日加群数量;       // 今日加群
        obj["today_leave_count"] = info->今日退群数量;       // 今日退群
        obj["today_friend_count"] = info->今日好友数量;      // 今日新增好友
        obj["today_del_friend_count"] = info->今日删除好友数量; // 今日删除好友
        obj["today_channel_join_count"] = info->今日频道数量;    // 今日加入频道
        obj["today_channel_leave_count"] = info->今日退出频道数量; // 今日退出频道
        array.append(obj);

    }
    return QJsonDocument(array).toJson();
}


#include <QImageReader>
#include <QImage>
#include <QBuffer>
#include <QMutex>
#include <QHash>

// ---------------------------------------------------------------------------
// 文件指纹（上传缓存键）= 「绝对路径 | 文件大小 | mtime」**原样拼接，不读内容、不做哈希**
//
//   · 只 1 次 QFileInfo（= 一次 stat）就三样全有，IO/CPU 都可忽略
//   · 为什么连哈希都不做：XXH64 有 11 GB/s，压这几十字节只要 0.02μs，
//     而一次 stat 本身就要 1~3μs —— 省它毫无意义，直接拼字符串更简单也更省事
//   · 为什么不用内容哈希：**读文件才是瓶颈**（页缓存 ~3 GB/s、机械盘 0.1 GB/s，比哈希慢一个数量级），
//     而「路径 + 大小 + mtime」已足够判断「是不是同一个文件、有没有被改过」
//     （make / rsync / 各类构建系统判文件变更用的就是这三样）。图片同理：
//     换一张图 → 内容变了 → 大小或 mtime 必然变
//
// 现在唯一还要真打开文件的只剩宽高解析（QImageReader），所以用内存表按
//   (绝对路径, 大小, mtime) 缓存 w/h：命中就直接返回，连图片头都不解析。
//   反复发的固定素材 = 全程只有 1 次 stat。
//
// 已知取舍（别再当 bug 查）：
//   · 同一份内容放两个不同路径 → 两个键 → 各上传一次（缓存不跨路径共享）
//   · 被 cp -p 之类覆盖（大小、mtime 都被保留）→ 可能误命中
//   · 内容没变、只被 touch → 键变 → 白重传一次
//   · key 比 16 字节 hex 长（中文路径 100+ 字节）→ 库体积、B-tree 分支因子都比哈希版差，
//     靠「双库按日轮换」把单库压到一天的量来抵消
// ⚠ 函数名沿用历史的 "MD5"（5 个调用点，语义都是「取缓存键」）。
// ---------------------------------------------------------------------------
static const int kFpCacheMax = 1024;             // 宽高内存表上限，超了整表清空

struct FileFpEntry {
    qint64 size    = 0;
    qint64 mtimeMs = 0;
    int    w       = -1;   // -1 = 没解析过宽高；0 = 非图片（音频/视频/文件）
    int    h       = -1;
};
static QMutex                      s_fpMutex;
static QHash<QString, FileFpEntry> s_fpCache;    // key = 绝对路径

bool calculateFileMD5AndSize(const QString &filePath, QString &md5, int &width, int &height)
{
    if (filePath.isEmpty()) return false;

    const QFileInfo fi(filePath);
    const qint64 fsize = fi.size();
    if (fsize <= 0) {
        //qWarning() << "文件为空或读取失败:" << filePath;
        return false;
    }
    const qint64  fmtime  = fi.lastModified().toMSecsSinceEpoch();
    const QString absPath = fi.absoluteFilePath();
    const bool    wantSize = (width == 0 && height == 0);   // 调用方没给宽高才需要解析

    // ① 指纹 = 绝对路径 + '|' + 大小 + '|' + mtime，**原样拼接，不做任何哈希**
    //   · Windows 文件名不允许 '|'，分隔无歧义；纯可打印字符，写日志/JSON 也干净
    //   · 代价：key 比 16 字节的哈希长（英文路径约 60 字节，中文路径 100+ 字节）——
    //     库按天整库删除，单库最多一天的量，所以这个膨胀是可控的
    md5 = absPath + QLatin1Char('|') + QString::number(fsize)
                + QLatin1Char('|') + QString::number(fmtime);

    if (!wantSize) return true;

    // ② 宽高：内存表命中就直接返回 —— 这是整条链上唯一还要真开文件的地方
    {
        QMutexLocker lock(&s_fpMutex);
        auto it = s_fpCache.constFind(absPath);
        if (it != s_fpCache.constEnd() && it->size == fsize && it->mtimeMs == fmtime
                && it->w >= 0) {
            width  = it->w;
            height = it->h;
            return true;
        }
    }

    auto remember = [&](int w, int h) {
        QMutexLocker lock(&s_fpMutex);
        if (s_fpCache.size() >= kFpCacheMax) s_fpCache.clear();
        FileFpEntry e; e.size = fsize; e.mtimeMs = fmtime; e.w = w; e.h = h;
        s_fpCache.insert(absPath, e);
    };

    // ③ 只有没缓存过才真解析图片头（QImageReader 自己只拉头部那几 KB）
    QImageReader reader(filePath);
    if (!reader.canRead()) {
        //qWarning() << "无法识别图片格式:" << filePath;
        // 非图片（音频/视频/文件）→ 宽高留 0，并记住，免得每次都再来撞一次
        remember(0, 0);
        return true;
    }
    const QSize size = reader.size();
    if (size.isEmpty()) {
        //qWarning() << "无法获取图片尺寸:" << filePath;
        return false;
    }
    width  = size.width();
    height = size.height();
    remember(width, height);

    return true;
}

// ---------------------------------------------------------------------------
// 读上传缓存（**统一入口**）
//   本库缓存值统一是 "<过期秒>||||<内容>"，图片是 <url>、媒体是裸 file_info 列表；
//   早期媒体缓存还可能是 "[<类型>,path=…,md5=…,Time=<过期秒>]"。
//
//   ★ 只读不删（2026-10-20）：
//     缓存库现在是**双库按日轮换**（main.cpp 的 ensureCacheDb），每天换库并把旧库整目录删掉，
//     过期条目活不过当天 24 点，所以这里**不需要**再 remove —— 读缓存因此永远是「一次纯读」：
//        · 一次 mdb_get（微秒级，B-tree 查找，跟库里有多少条无关）
//        · 不拿写事务、不写 WAL、不触发 map 扩容、不阻塞别的线程的写
//     命中但已过期 → 直接当未命中返回空，让调用方重新上传；重传成功后 put 覆盖同一 key，
//     不会堆垃圾条目。
//   早期为了兜住「LMDB 没有 TTL、过期条目永不消失」才顺手删，那个问题已由换库解决。
// ---------------------------------------------------------------------------
static QString cachedValueIfFresh(const QString &key)
{
    if (!cache_db || key.isEmpty()) return {};
    const QString v = cache_db->get(key);      // ← 整个读缓存链路就这一次库访问
    if (v.isEmpty()) return {};

    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // ① 现行格式：过期秒在最前面
    const int sep = v.indexOf(QStringLiteral("||||"));
    if (sep > 0) {
        bool ok = false;
        const qint64 expire = v.left(sep).trimmed().toLongLong(&ok);
        if (ok && now >= expire) return {};    // 已过期：当未命中，不删
        return v;
    }

    // ② 旧格式（早期媒体缓存）：过期秒藏在 ",Time=" 里，整串末尾还有个 ']'
    if (v.startsWith(QLatin1Char('['))) {
        const int ti = v.lastIndexOf(QStringLiteral(",Time="));
        if (ti != -1) {
            QString s = v.mid(ti + 6);
            const int rb = s.indexOf(QLatin1Char(']'));
            if (rb >= 0) s.truncate(rb);
            bool ok = false;
            const qint64 expire = s.trimmed().toLongLong(&ok);
            if (ok && now >= expire) return {};   // 同上
        }
    }
    return v;
}


// 异步发送的「回执投递」：把框架完成回调里的响应，送回发起它的 Python 插件协程。
//
// 调用现场是 QThreadPool 的 worker（既不是网络线程也不是主线程，**没有事件循环**），
// 所以这里只做一件事：拿到 GIL，把结果交给 qiancao_sdk._deliver ——
// 由它用 loop.call_soon_threadsafe 把结果排进事件循环（跨线程改 Future 的唯一安全姿势）。
static void deliverPluginAsyncResult(const QString &reqId, const QString &resp)
{
    if (reqId.isEmpty()) return;
    if (!Py_IsInitialized()) return;      // 解释器已在收尾 → 直接丢，别在关停阶段崩
    py::gil_scoped_acquire gil;
    try {
        py::module_ m = py::module_::import("qiancao_sdk");
        if (py::hasattr(m, "_deliver"))
            m.attr("_deliver")(reqId.toStdString(), resp.toStdString());
    } catch (const py::error_already_set &e) {
        qWarning() << "[qiancao] 异步回执投递失败 reqId=" << reqId << ":" << e.what();
    }
}


// 把插件 API 的第 7 参（`_7`）直接变成底层要的 Callback —— 各 case 通用。
//   `_7` 非空 = Python SDK 的 `xxx_async()` 调用 → 底层（Get/Post/Delete/put2）见到 Callback 就走
//            PostAsync/GetAsync 等异步分支，完成后由 deliverPluginAsyncResult 唤醒等待中的协程；
//   `_7` 为空（JS / DLL 插件、老的同步调用）→ 返回**空 Callback**，底层原样走同步，行为一字不变。
// ⚠ 回调只按值捕获 reqId，**绝不捕获 this** —— 请求飞行途中客户端被摘表销毁也不会悬空。
static Callback pluginAsyncCb(const char *_7)
{
    if (_7 == nullptr) return Callback();
    const QString reqId = toQString(_7);
    if (reqId.isEmpty()) return Callback();
    return [reqId](const QString &resp, QNetworkReply::NetworkError) {
        deliverPluginAsyncResult(reqId, resp);
    };
}


// Python 入口（pybind11 绑的就是它）。
//   `release` 是局部 RAII：myCallback 先跑完 → release 析构、**重新拿回 GIL** → 然后 pybind11
//   才 decode 成 str。顺序不能变，否则就是无 GIL 调 Python C API。
//   返回值直接透传 myCallback 的 TLS 指针：pybind 会立刻拷成 Python str，拷完就不再用。
const char* myCallbackA(const char* uuid, int apiId, int appid, const char* _1, const char* _2,
                       const char* _3, const char* _4, const char* _5,
                       const char* _6, const char* _7, const char* _8)
{
    py::gil_scoped_release release;
    return myCallback(uuid,apiId,appid,_1,_2,_3,_4,_5,_6,_7,_8);
}
// 真正的实现：内部用 thread_local 缓冲拼结果，返回的是 **TLS 裸指针**（框架持有、只读）。
// 本函数不直接对外 —— 交给插件的是下面的 myCallback，它只做两件事：透传指针、
// 给「同步就返回」的结果补一次异步回执投递（理由见那里）。
static const char* myCallbackImpl(const char* uuid, int apiId, int appid, const char* _1, const char* _2,
                       const char* _3, const char* _4, const char* _5,
                       const char* _6, const char* _7, const char* _8) {
    // ⚠⚠ 必须是 thread_local，不能用进程级 static —— 本函数是**真并发**入口：
    //   · JS 插件：node_plugin_manager 用 QThreadPool::globalInstance()->start() 在 worker 线程里调；
    //   · Python：myCallbackA 先 py::gil_scoped_release，多个 Python 线程可以真并发进来；
    //   · 上传 / 异步回执：也都在 worker 线程。
    //   进程级 static 的后果不只是"内容被覆盖"：A 线程执行 result = 大 JSON 触发 realloc 时，
    //   旧缓冲被释放，B 线程手里刚拿到的 c_str() 会**立刻悬空**（读到已释放内存）。
    //   thread_local 之后每线程各一份，谁也不会覆盖谁；返回的指针就是交给调用方的那个（只读）。
    //   契约：**框架持有，调用方只读、不释放**；有效期 = 本线程下一次调用，线程退出即销毁。
    //   所有调用方都是"拿到就拷"（pybind / QString::fromUtf8 / std::string / 各语言 SDK），
    //   所以这个有效期从来不是问题；也正因为不分配，旧插件不可能泄漏。
    //   ⚠ 别被写法骗了：`static thread_local` 里的 static **不是**「全局一份」，它只表示存储期；
    //     真正起作用的是 thread_local —— 每个线程**各自**一个独立的 std::string，互不覆盖、互不 realloc。
    //     （这里必须带 static：下面 11 个出口都是 `return result.c_str()`，局部变量在 return 时析构，
    //       返回的指针会立刻悬空，所以它的生命周期必须比函数调用长。）
    static thread_local std::string result="{}";
    result="{}"; //初始化（只清内容，capacity 保留）
     //qDebug() << "apiid:"<< apiId << " appid:"<< appid << " _1:" << _1 << "_2" <<_2 << "_3"<<_3 << "_4"<<_4 << "_5"<<_6 << "_7"<<_7 ;
    if (apiId == 10000) {
        miaomiao32 = 0;
        return result.c_str();
    }
    if (apiId == 10001) {//32位异常
        QString text = toQString(_1);
        if (_2 == nullptr || strlen(_2) == 0) {
            AppendEventLog(text);
           return result.c_str();
        }

        AppendEventLog(text,toInt(_2));
        return result.c_str();
    }
    if(apiId==10002)
    {
        botnomsg(appid,toInt(_1),toQString(_2),toQString(_3));
        return result.c_str();
    }

    QString pname;
    int pluginindex=0;
    if(strcmp(uuid, g_keyuuid2) != 0)
    {
        for(int i=0;i<m_pluginList.size();i++)
        {
            if(m_pluginList[i].uuid!=uuid) continue;
            pluginindex=i;
            if(apiId==OUTLOG)
                pname = "["+m_pluginList[i].name+"]";
            else if(apiId ==API_ID_SEND_MESSAGES || apiId == API_ID_SEND_MESSAGES_ARK)
                pname = "["+m_pluginList[i].name+"|%1ms]";
            else
                pname = "[]";
            break;
        }
        if(apiId==API_ID_AI)
        {
            result = "无权限调用内置Ai 请等待授权添加？";
            return result.c_str();//这里
        }
    }else{
        pname = "[私有指令|%1ms]";
    }
    if(pname.isEmpty()) return result.c_str();

    // ── 插件测试（插件页「测试插件」按钮）：正在测的那个插件发的「文本 / 卡片」一律扣下 ──
    //   插件想说出去的话就是判断「有没有复读用户输入」的证据，这里存进测试收集表后
    //   直接当发送成功返回 —— 不 POST、不写日志、一个字都不出这台机器。
    //   ⚠ 必须放在下面的「bot 在线检查」**之前**：测试不该要求机器人已登录，
    //     否则用户为了查一句复读还得先掉线重连一次。
    //   ⚠ 归属：优先按 msgid 里的 "|T<序号>|" 认（见 pluginTestMsgId），
    //     插件没把 msgid 回传时由收集器退化成「当前正在测的那条」。
    if ((apiId == API_ID_SEND_MESSAGES || apiId == API_ID_SEND_MESSAGES_ARK) && pluginTestActive()) {
        if (pluginTestIntercept(toQString(uuid), toQString(_4), toQString(_3))) {
            // Python SDK 的 *_async 在等回执：给一个和真发送成功一样的 "{}"，别让协程挂到超时。
            const QString testReqId = toQString(_7);
            if (!testReqId.isEmpty())
                deliverPluginAsyncResult(testReqId, QStringLiteral("{}"));
            result = "{}";
            return result.c_str();
        }
    }

    QQBotClient *client=nullptr;
    if(apiId!=OUTLOG && apiId!=API_ID_BOT_LIST && apiId!=API_ID_PYTHON_HTTP && apiId!=API_ID_HTMLIMG1 && apiId!=API_ID_HTMLIMG2 && apiId!=API_ID_AI)
    {
        bool ok=false;
        for(int i=0;i<m_accounts.size();i++)
        {
            if(m_accounts[i]->appid_int!=appid) continue;
            if(!m_accounts[i]->online)
            {
                result = "{\"msg\":\"bot不在线\"}";
                AppendEventLog(QString("插件：%1 发送消息：%2 时bot不在线 无法发送消息 目标appid:%3").arg(pname,toQString(_3),QString::number(appid)));
                return result.c_str();
            }
            ok = true;
            if(m_botClients.contains(appid))
            {
                client = m_botClients[appid];
                break;
            }
            result = "{\"msg\":\"client没找到 代表机器人未登录 一般来说online 是 false 这里不会执行\"}";
            return result.c_str();
        }
        if(!ok)
        {
            AppendEventLog(QString("插件：%1 发送消息：%2 时指定appid不可用 无法发送消息 目标appid:%3").arg(pname,toQString(_3),QString::number(appid)));
            return result.c_str();
        }
    }


    switch (apiId) {
    case OUTLOG: {
        QString text = pname+toQString(_1);
        if (_2 == nullptr || strlen(_2) == 0) {
            AppendEventLog(text);
            break;
        }

        AppendEventLog(text,toInt(_2));
        break;
    }
    case API_ID_SEND_MESSAGES: {
        m_pluginList[pluginindex].SendQuantity++;
        int type = toInt(_1);
        QString openid = toQString(_2);
        QString text =toQString(_3);

        QString msgid = toQString(_4);
        bool is_wakeup = toBool(_5);
        QString ret;
        // _7 = 异步回执标识，只有 Python SDK 的 *_async 会传。
        // 传了它 → 走 PostAsync 回调路径，发送完成后把响应送回插件协程；
        // 不传（JS / DLL / 老的同步调用）→ 行为与以前完全一致。
        const QString reqId = (_7 == nullptr) ? QString() : toQString(_7);
        // 插件按**注册原名**输出按钮（如 [ping]()），用户改名成 pong 后点它发出去的还是 ping →
        // 按钮失效。这里把**发起方 uuid** 直接当 pname 传进去，由发送管线按该插件**自己的**规则表
        // 把 [原指令]() 重定向成生效指令；addmsglog 再按 uuid 还原成 "[插件名|%1ms]" 显示。
        // 关键词匹配没有插件（uuid 不在插件表），照旧传展示标签，正文一字不动。
        const QString pn = (strcmp(uuid, g_keyuuid2) != 0) ? toQString(uuid) : pname;

        // ── AI 插件工具调用：插件要「说」的话在这里被扣下，交给 AI 决定发不发 ──
        // 调插件前框架把 msgid 打成了 "|-<token>|<真实msgid>"（见 core/global.h）；
        // 这里认出 token 就直接当发送成功返回 —— 不 POST、不写日志，一个字都不出这台机器。
        // token 查不到（插件延迟发送、工具调用早结束了）→ 照常发出去，不吞消息。
        {
            const int gateTok = toolGateTokenFromMsgId(msgid);
            if (gateTok > 0 && toolGateIntercept(gateTok, text)) {
                // ⚠ Python SDK 的 *_async 版在等回执：不回调的话协程会一直挂到超时。
                //   这里给一个和真发送成功一样的 "{}"，让它以为发出去了。
                if (!reqId.isEmpty())
                    deliverPluginAsyncResult(reqId, QStringLiteral("{}"));
                result = "{}";
                break;
            }
        }

        if(toBool(_6))
        {
            if(reqId.isEmpty())
                ret = client->send_msgAsync(type, openid, pn, text, msgid, is_wakeup);
            else
                ret = client->send_msgAsync(type, openid, pn, text, msgid, is_wakeup,
                                            false, 0, false,
                                            [reqId](const QString &resp, QNetworkReply::NetworkError) {
                                                deliverPluginAsyncResult(reqId, resp);
                                            });
        }
        else
            ret = client->send_messages(type, openid, pn, text, msgid, is_wakeup);
        result = ret.toStdString();
        break;
    }
    case API_ID_SEND_MESSAGES_ARK: {
        m_pluginList[pluginindex].SendQuantity++;
        int type = toInt(_1);
        QString openid = toQString(_2);
        QJsonObject ark = toJsonObject(_3);
        QString msgid = toQString(_4);
        bool is_wakeup = toBool(_5);
        // _7 = 异步回执标识（Python SDK 的 send_ark_async 才传）。
        // 非空 → 传一个带 cb 且 openid 非空的 ctx，send_messages_ark 便走 PostAsync 分支。
        MessageLogContext ctx;
        if (Callback cb = pluginAsyncCb(_7)) { ctx.cb = cb; ctx.openid = openid; }
        QString ret = client->send_messages_ark(type, openid,pname, ark, msgid, is_wakeup, 0, ctx);
        result = ret.toStdString();
        break;
    }
    case API_ID_DELETE_MESSAGES: {
        int type = toInt(_1);
        QString openid = toQString(_2);
        QString msgid = toQString(_3);

        QString ret = client->delete_messages(type, openid, msgid, pluginAsyncCb(_7));
        result = ret.toStdString();
        break;
    }
    case API_ID_GENERATE_SHARE_LINK: {
        QString callback_data = toQString(_1);
        QString ret = client->generate_share_link(callback_data, pluginAsyncCb(_7));
        result = ret.toStdString();
        break;
    }
    case API_ID_RESPOND_INTERACTION: {
        QString interaction_id = toQString(_1);
        int code = toInt(_2);
        QString data = toQString(_3);
        QString ret = client->respond_interaction(interaction_id, code, data, pluginAsyncCb(_7));
        result = ret.toStdString();
        break;
    }
    case API_ID_BOT_LIST:{
        QString ret = botlist();
        result = ret.toStdString();
        break;
    }
    case API_ID_GET_OPENID: {
        if(!g_botdb.contains(appid))
        {
            result = "";
            break;
        }
        if(!g_botdb.contains(appid)) break ;
        BotDB *db = g_botdb[appid];
        QString user;
        db->getOpenIdBySeqId(toInt(_1),user);
        result =user.toStdString();
        break;
    }
    case API_ID_GET_USER_NAME: {

        if(!g_botdb.contains(appid))
        {
            result = "";
            break;
        }

        QString text = toQString(_1);
        // _2 = 是否取「默认昵称」：true / 不传 → 原始 nickname（老行为）；
        //      false → 审核后的正式昵称 name（可能为空，没审核过就是空串）
        bool use_default = (_2 == nullptr || strlen(_2) == 0) ? true : toBool(_2);

        BotDB *db = g_botdb[appid];

        if(text.size()==32)
        {
            // 长 id：一次调用同时拿到原始昵称和审核后的昵称（后者可能为空）
            QString name, review_name;
            db->getOrUpdateUser(text, name, review_name);
            result = (use_default ? name : review_name).toStdString();
            break;
        }

        UserRecord userShort{};
        if(!db->getUserBySeqId((uint32_t)text.toInt(), userShort))
        {
            result = "";
            break;
        }

        result = use_default ? std::string(userShort.nickname) : std::string(userShort.name);
        break;
    }
    case API_ID_PYTHON_HTTP: {

        result = "弃用..";
        break;
    }
    case API_ID_GET_USER_ID: {
        if(!g_botdb.contains(appid))
        {
            result = "";
            break;
        }
        BotDB *db = g_botdb[appid];
        QString name;
        uint32_t id = db->getOrUpdateUser(toQString(_1),name);
        result = id;
        break;
    }
    case API_ID_HTMLIMG1: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="HTMLIMG1参数1为空";
            break;
        }
        result = renderInThread(text,toInt((_2))).toStdString();
        break;
    }
    case API_ID_HTMLIMG2: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="HTMLIMG2参数1为空";
            break;
        }
        result = ScreenA->captureHtmlSync(text,toInt(_2),toInt(_3),toInt(_4)).toStdString();
        break;
    }
    case API_ID_DS: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="添加定时 参数1 备注为空";
            break;
        }
        QString text2 = toQString(_2);
        if(text2.isEmpty())
        {
            result ="添加定时 参数3 定时时间为空";
            break;
        }
        QString text3 = toQString(_4);
        if(text3.isEmpty())
        {
            result ="添加定时 参数3 python代码为空";
            break;
        }
        result = schedule->add_byAi(text,appid,text2,toInt(_3),text3).toStdString();
        break;
    }
    case API_ID_AI: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="添加定时 参数1 模型不能是空";
            break;
        }
        QString text2 = toQString(_2);
        if(text2.isEmpty())
        {
            result ="添加定时 参数2 提交AI 内容不能是空";
            break;
        }
        result = ai_ui->Ai_post(text,text2,toInt(_3)).toStdString();
        break;
    }
    case API_ID_GET_MEMBER: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取用户信息 参数1 群id不能是空";
            break;
        }
        QString text2 = toQString(_2);
        if(text2.isEmpty())
        {
            result ="获取用户信息 参数2 用户id 内容不能是空";
            break;
        }
        result = client->get_groups_members(text,text2, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_GET_groups_info: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取用户信息 参数1 群id不能是空";
            break;
        }

        result = client->get_groups_info(text, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_GET_groups_bot_state: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取机器人状态 参数1 群id不能是空";
            break;
        }

        result = client->get_groups_bot_state(text, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_GET_MEMBER_LIST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取群成员列表 参数1 群id不能是空";
            break;
        }
        QString cursor = toQString(_2);

        result = client->get_members_list(text, cursor, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_SET_JOIN_REQUEST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="处理加群 参数1 群id不能是空";
            break;
        }
        QString user = toQString(_2);
        if(user.isEmpty())
        {
            result ="处理加群 参数2 用户ID不能是空";
            break;
        }
        bool op = toBool(_3);
        QString id = toQString(_4);
        QString reject = toQString(_5);
        bool bilack = toBool(_6);
        result = client->approveGroupJoinRequest(text,user,op,id,reject,bilack, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_SET_MUTE_G: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="设置禁言 参数1 群id不能是空";
            break;
        }
        QString user = toQString(_2);
        if(user.isEmpty())
        {
            result ="设置禁言 参数2 JSON不能是空";
            break;
        }
        QJsonDocument doc = QJsonDocument::fromJson(user.toUtf8());
        if (doc.isNull() || !doc.isArray()) {

            result ="设置禁言 参数2 json无法解析";
            break  ;
        }
        QJsonArray membersArray = doc.array();
        result = client->setGroupRestrictChatSetting(text,membersArray, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_GET_MUTE_LIST_G: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取禁言列表 参数1 群id不能是空";
            break;
        }
        result = client->getGroupRestrictChatSetting(text, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_GET_JOIN_REQUEST_LIST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取加群列表 参数1 群id不能是空";
            break;
        }
        result = client->getjoin_request_list(text, 20, QString(), pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_GET_GROUP_BLCKLIST: {

        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="获取群黑名单列表 参数1 群id不能是空";
            break;
        }
        QString cursor = toQString(_1);

        result = client->get_member_blacklist(text,cursor, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_GROUP_BLCKLIST: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="设置群黑名单列表 参数1 群id不能是空";
            break;
        }
        QString user_list = toQString(_1);
        bool op = toBool(_3);
        result = client->member_blacklist(text,user_list,op, pluginAsyncCb(_7)).toStdString();
        break;
    }
    case API_ID_REMOV_MEMBER: {
        QString text = toQString(_1);
        if(text.isEmpty())
        {
            result ="移除群成员 参数1 群id不能是空";
            break;
        }
        QString user_list = toQString(_1);
        if(user_list.isEmpty())
        {
            result ="移除群成员 参数2 设置的用户不能是空";
            break;
        }
        bool add_blacklist = toBool(_3);
        result = client->del_members(text,user_list,add_blacklist, pluginAsyncCb(_7)).toStdString();
        break;
    }

    default:
        result = R"({"error":"Unknown apiId"})";
        break;
    }

    return result.c_str();
}

// 对外入口（get_plugin_info 交给插件的那个函数指针就是它）。
//   直接透传 impl 的指针：**框架持有、调用方只读、不释放**（契约见文件开头的说明）。
const char* myCallback(const char* uuid, int apiId, int appid, const char* _1, const char* _2,
                       const char* _3, const char* _4, const char* _5,
                       const char* _6, const char* _7, const char* _8) {
    const char* r = myCallbackImpl(uuid, apiId, appid, _1, _2, _3, _4, _5, _6, _7, _8);
    // Python SDK 的 *_async 把 req_id 塞在 _7 等回执。**凡是「同步就返回」的结果必须当场投递一次**，
    // 否则协程会一直等到 60 秒超时才拿到一个假的 timeout —— 而真正的错误（bot不在线 / client没找到 /
    // 发送类型错误 / pname 为空 …）全都在这条早退路径上，永远不会回调。
    // 真异步路径返回的是 "{}"（sendMessagesAsyncCore 末尾），回执稍后由发送完成回调投递 → 跳过，不重复。
    if (_7 != nullptr && _7[0] != '\0' && r != nullptr && std::strcmp(r, "{}") != 0)
        deliverPluginAsyncResult(toQString(_7), toQString(r));
    return r;
}






QString get_url(int type,const QString &openid,const QString &text = QString(),const QString &text2 = QString())
{
    QString url;
    if(type==0) url = "https://api.bot.qq.com/v2/groups/" + openid;
    else if(type==1) url = "https://api.bot.qq.com/channels/" + openid;
    else if(type==2) url = "https://api.bot.qq.com/v2/users/" + openid;
    else url = "https://api.bot.qq.com/dms/" + openid;
    if(!text.isEmpty()) url +="/" + text;
    if(!text2.isEmpty()) url +="/" + text2;
    return url;
}




static bool extractParamValue(QStringView params, const QString &key, QString &value) {
    int pos = 0;
    const int len = params.size();
    while (pos < len) {
        // 跳过空格
        while (pos < len && params[pos].isSpace()) ++pos;
        if (pos >= len) break;

        // 检查 key 是否匹配
        bool keyMatch = true;
        for (int i = 0; i < key.size(); ++i) {
            if (pos + i >= len || params[pos + i].toLower() != key[i].toLower()) {
                keyMatch = false;
                break;
            }
        }
        if (!keyMatch) {
            // 不匹配，跳至下一个逗号
            while (pos < len && params[pos] != ',') ++pos;
            if (pos < len && params[pos] == ',') ++pos;
            continue;
        }

        // key 匹配，跳到等号
        pos += key.size();
        while (pos < len && params[pos].isSpace()) ++pos;
        if (pos >= len || params[pos] != '=') {
            // 格式错误，跳过
            while (pos < len && params[pos] != ',') ++pos;
            if (pos < len && params[pos] == ',') ++pos;
            continue;
        }
        ++pos; // 跳过 '='

        // 跳过等号后的空格
        while (pos < len && params[pos].isSpace()) ++pos;
        if (pos >= len) break;

        // 提取 value，直到逗号或结尾
        int valueStart = pos;
        while (pos < len && params[pos] != ',') ++pos;
        int valueLen = pos - valueStart;
        // 去除 value 尾部的空格
        while (valueLen > 0 && params[valueStart + valueLen - 1].isSpace()) --valueLen;

        if (valueLen > 0) {
            value = params.mid(valueStart, valueLen).toString();
        } else {
            value.clear();
        }
        return true;
    }
    return false;
}

struct ImageInfo {
    QString urlOrPath;
    int x = 0;
    int y = 0;
};

static ImageInfo parseImageTagContent(QStringView tagContent) {
    ImageInfo info;
    // 去掉开头的 "image" 和可能的逗号、空格
    int start = 0;
    while (start < tagContent.size() && tagContent[start].isSpace()) ++start;
    if (start < tagContent.size() && tagContent[start].toLower() == 'i') {
        // 跳过 "image" 单词
        if (tagContent.size() >= start + 5 &&
            tagContent.mid(start, 5).compare(QLatin1String("image"), Qt::CaseInsensitive) == 0) {
            start += 5;
        }
    }
    // 跳过后面的空白和逗号
    while (start < tagContent.size() && (tagContent[start].isSpace() || tagContent[start] == ',')) ++start;
    if (start >= tagContent.size()) return info;

    QStringView params = tagContent.mid(start);
    // 提取 url 或 path
    if (!extractParamValue(params, QStringLiteral("url"), info.urlOrPath)) {
        extractParamValue(params, QStringLiteral("path"), info.urlOrPath);
    }
    QString xStr, yStr;
    if (extractParamValue(params, QStringLiteral("x"), xStr)) info.x = xStr.toInt();
    if (extractParamValue(params, QStringLiteral("y"), yStr)) info.y = yStr.toInt();

    return info;
}



void get_ref(QString &text,QString &message_reference)
{

    int refStart = text.indexOf(QLatin1String("[ref,"), 0, Qt::CaseInsensitive);
    if (refStart != -1) {
        int refEnd = text.indexOf(']', refStart);
        if (refEnd != -1) {
            QStringView tagContent = QStringView(text).mid(refStart + 5, refEnd - refStart - 5);
            int idxPos = tagContent.indexOf(QLatin1String("msg_idx="), 0, Qt::CaseInsensitive);
            if (idxPos != -1) {
                int valStart = idxPos + 8;
                int valEnd = tagContent.size();
                int commaPos = tagContent.indexOf(',', valStart);
                if (commaPos != -1) valEnd = commaPos;
                while (valEnd > valStart && tagContent[valEnd - 1].isSpace()) --valEnd;
                if (valEnd > valStart) {
                    message_reference = tagContent.mid(valStart, valEnd - valStart).toString();
                } else {
                    message_reference.clear();
                }
            } else {
                message_reference.clear();
            }
            text.remove(refStart, refEnd - refStart + 1);
            // 「引用」必须带正文，只剩引用平台直接报错。正文被这个标记吃空（AI 单发了 <seq:N>、
            // 插件只拼了 [ref,msg_idx=N]）时就不带引用了，让上层按「内容为空」正常处理。
            if (text.trimmed().isEmpty()) {
                message_reference.clear();
                text.clear();
            }
        }
    }

}
std::future<QString> uploadimg(const QString &filePath);
void uploadimgCb(const QString &filePath, std::function<void(QString)> onDone);

// 原样放行、**不做任何上传**的图片地址：
//   · http(s) 外链 —— 平台自己去取；
//   · qqbot-media://image/v1/<token> —— 「素材」页上传后拿到的引用链接，平台发送时自动转存，
//     本地上传反而会失败（它根本不是本地文件）。
// 统一走这里，免得每个分支各写一遍 startsWith。
static bool isPassThroughImageUrl(const QString &url)
{
    return url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
        || url.startsWith(QLatin1String("qqbot-media://"), Qt::CaseInsensitive);
}

// Markdown 图片 ![alt](url) 的匹配串。**不需要处理的地址直接在匹配阶段排掉**，
// 免得先抠出来占一个 allTags/replacements 槽位再 continue 掉：
//   · mdPass  = markdown 消息（type==1）用：http(s) 和 qqbot-media:// 都原样发出去，
//               平台自己去取 / 自己转存，框架这边一个字都不用改；
//   · mdMedia = 纯文本 / ark（type 0/2）用：图片要被抠出来当媒体发（info = url），
//               所以 http 仍要匹配，**只排 qqbot-media** —— 它不是能下载的地址，
//               塞进 info 只会上传失败，留着原文反而无害。
static const QRegularExpression& mdImageRegexPassThrough()
{
    static const QRegularExpression re(R"(!\[([^\]]*)\]\(((?!\s*(?:https?:|qqbot-media:))[^)]*)\))");
    return re;
}

static const QRegularExpression& mdImageRegexMedia()
{
    static const QRegularExpression re(R"(!\[([^\]]*)\]\(((?!\s*qqbot-media:)[^)]*)\))");
    return re;
}

// 给「按 type 选正则」用：type==1 → markdown（http 也放行）
static const QRegularExpression& mdImageRegexFor(int type)
{
    return type == 1 ? mdImageRegexPassThrough() : mdImageRegexMedia();
}

// 给「放行」的图片补尺寸标记（与原来 needPadding 的规则完全一致）：
//   无尺寸       → 补 #1000px #0px（宽 1000、高自适应）
//   只有宽度     → 补 #0px
//   已有宽高     → 原样
// 平台对没写 #宽px #高px 的图会按原始尺寸渲染，AI 十次有八次忘写，所以这步不能省。
static QString padImageAltForMarkdown(const QString &alt)
{
    static const QRegularExpression sizeRe(R"(#(\d+)px)");
    QList<int> sizes;
    QRegularExpressionMatchIterator sit = sizeRe.globalMatch(alt);
    while (sit.hasNext()) sizes.append(sit.next().captured(1).toInt());

    if (sizes.size() >= 2) return alt;                       // 已经有宽高：一个字都不动
    if (sizes.size() == 1)                                   // 只有宽度：补高度 0（自适应）
        return alt.trimmed() + QLatin1String(" #0px");

    QString core = alt;                                      // 无尺寸：去掉残留标记后补 1000 宽
    core.remove(sizeRe);
    core = core.trimmed();
    return core.isEmpty() ? QStringLiteral("#1000px #0px")
                          : core + QLatin1String(" #1000px #0px");
}

// markdown 里 http(s) / qqbot-media 的图片**不参与上传**（原样发出去），
// 但尺寸标记还得补 —— 平台对没写 #宽px #高px 的图按原始尺寸渲染（AI 十次有八次忘写），
// qqbot-media 的素材图同样会撑爆。所以两类都补，规则一致：
//   无尺寸 → #1000px #0px；只有宽度 → 补 #0px；已有宽高 → 不动。
// 纯正则一趟 + 原地改写：不建 allTags、不算 md5、不读文件、不发请求。
// 放在真正扫描本地图片之前跑，避免两套下标互相打架。
static void normalizePassThroughImageSizes(QString &text)
{
    if (!text.contains(QLatin1String("]("))) return;          // 快筛：没有 markdown 链接

    static const QRegularExpression re(R"(!\[([^\]]*)\]\((\s*(?:https?:|qqbot-media:)[^)]*)\))",
                                       QRegularExpression::CaseInsensitiveOption);
    QList<QRegularExpressionMatch> hits;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) hits.append(it.next());

    // 从后往前改，前面的下标才不会失效
    for (int i = hits.size() - 1; i >= 0; --i) {
        const QRegularExpressionMatch &m = hits.at(i);
        const QString alt = m.captured(1);
        const QString newAlt = padImageAltForMarkdown(alt);
        if (newAlt == alt) continue;
        text.replace(m.capturedStart(), m.capturedLength(),
                     QStringLiteral("![%1](%2)").arg(newAlt, m.captured(2)));
    }
}

// 本地图片路径不存在时的默认图：运行目录下的「图片不存在.webp」（程序目录优先，其次工作目录）。
// 没找到返回空 —— 调用方保持原行为（跳过该标签）。
static QString missingImagePlaceholderPath()
{
    static const QStringList s_bases = {
        QCoreApplication::applicationDirPath(),
        QDir::currentPath()
    };
    for (const QString &base : s_bases) {
        if (base.isEmpty()) continue;
        const QString p = QDir(base).filePath(QStringLiteral("图片不存在.webp"));
        if (QFileInfo(p).isFile()) return p;
    }
    return QString();
}

QString QQBotClient::processImageTags(QString &text, int type, QString &info,
                                      int targetType, const QString &openid,
                                      QString &message_reference)
{
    get_ref(text, message_reference);
    // md 消息里放行的图（http / qqbot-media）先补齐尺寸；纯文本 / ark 不涉及 markdown，跳过
    if (type == 1) normalizePassThroughImageSizes(text);
    const QRegularExpression &mdImgRe = mdImageRegexFor(type);
    static const QRegularExpression sizeRe(R"(#(\d+)px)");
    // ---------- 1. 定义统一的图片标签结构 ----------
    struct ImgTag {
        int start;          // 起始位置
        int length;         // 原始长度

        // 用于最终替换的宽高（优先使用用户指定，否则使用文件读取）
        int width;
        int height;

        // 扩展字段（仅对 Markdown 图片有效）
        bool isMdImg = false;          // 是否来自 ![]()
        bool needPadding = false;      // 是否需要补尺寸（用户未指定任何尺寸）
        QString alt;                   // 修正后的完整 alt（已补全或保持原样）
        QString coreText;              // 去除所有尺寸标记后的纯文本（用于 needPadding=true 时拼接）
        int userWidth = 0;             // 用户指定的宽度（若有）
        int userHeight = 0;            // 用户指定的高度（若有）
        bool hasUserSize = false;      // 用户是否指定了至少一个尺寸

        QString url;                   // 图片路径或 URL
    };
    QList<ImgTag> allTags;

    // ---------- 2. 解析旧标签 [image] ----------
    int searchFrom = 0;
    while (true) {
        int imgStart = text.indexOf(QLatin1String("[image"), searchFrom, Qt::CaseInsensitive);
        if (imgStart == -1) break;

        int imgEnd = imgStart + 1;
        int bracketDepth = 1;
        while (imgEnd < text.size() && bracketDepth > 0) {
            if (text[imgEnd] == '[') bracketDepth++;
            else if (text[imgEnd] == ']') bracketDepth--;
            ++imgEnd;
        }
        if (bracketDepth != 0) break;

        int tagLen = imgEnd - imgStart;
        int contentStart = imgStart + 6; // "[image"
        while (contentStart < imgEnd - 1 && (text[contentStart].isSpace() || text[contentStart] == ','))
            ++contentStart;
        int contentLen = tagLen - (contentStart - imgStart) - 1;
        if (contentLen < 0) contentLen = 0;
        QStringView tagContentView = QStringView(text).mid(contentStart, contentLen);

        ImageInfo imgInfo = parseImageTagContent(tagContentView);
        if (!imgInfo.urlOrPath.isEmpty()) {
            ImgTag tag;
            tag.start = imgStart;
            tag.length = tagLen;
            tag.url = imgInfo.urlOrPath;
            tag.width = imgInfo.x;
            tag.height = imgInfo.y;
            tag.isMdImg = false;            // 旧标签
            allTags.append(tag);
        }
        searchFrom = imgEnd;
    }

    // ---------- 3. 解析 Markdown 图片标签 ![]() ----------

    QRegularExpressionMatchIterator it = mdImgRe.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString alt = match.captured(1).trimmed();
        QString url = match.captured(2).trimmed();
        if (url.isEmpty()) continue;
        // 正则里已经排过了，这里再兜一层大小写变体（HTTP:// 之类）：
        //   type==1 → http(s) / qqbot-media 都放行；type 0/2 → 只有 qqbot-media 放行
        if (type == 1 ? isPassThroughImageUrl(url)
                      : url.startsWith(QLatin1String("qqbot-media:"), Qt::CaseInsensitive))
            continue;

        // 提取所有尺寸标记 #数字px

        QRegularExpressionMatchIterator sizeIt = sizeRe.globalMatch(alt);
        QList<int> sizes;
        while (sizeIt.hasNext()) {
            QRegularExpressionMatch sizeMatch = sizeIt.next();
            sizes.append(sizeMatch.captured(1).toInt());
        }
        int count = sizes.size();

        // 提取核心文本（去除所有尺寸标记）
        QString coreText = alt;
        coreText.remove(sizeRe);

        // 确定 needPadding、修正后的 alt、用户尺寸
        bool needPadding = false;
        QString modifiedAlt = alt;
        int userWidth = 0, userHeight = 0;
        bool hasUserSize = false;

        if (count == 0) {
            needPadding = true;            // 无尺寸 → 需要补
            hasUserSize = false;
            // modifiedAlt 保持原样（无尺寸）
        } else if (count == 1) {
            // 只有宽度 → 立即补高度 #0px
            modifiedAlt = alt.trimmed() + " #0px";
            needPadding = false;
            userWidth = sizes[0];
            userHeight = 0;
            hasUserSize = true;
        } else { // count >= 2
            // 已有完整尺寸，不变
            needPadding = false;
            userWidth = sizes[0];
            userHeight = sizes[1];
            hasUserSize = true;
            // modifiedAlt 保持原样
        }

        ImgTag tag;
        tag.start = match.capturedStart();
        tag.length = match.capturedLength();
        tag.url = url;
        tag.isMdImg = true;
        tag.alt = modifiedAlt;
        tag.coreText = coreText;
        tag.needPadding = needPadding;
        tag.userWidth = userWidth;
        tag.userHeight = userHeight;
        tag.hasUserSize = hasUserSize;
        // 当前宽高先设为用户指定值（后续可能被文件读取覆盖，但会恢复）
        tag.width = userWidth;
        tag.height = userHeight;

        allTags.append(tag);
    }

    // ---------- 4. 若没有任何图片标签，处理其他 Markdown 链接后返回 ----------
    if (allTags.isEmpty()) {
        if (type == 0 || type == 2)
            text = convertMdLinksKeepHttp(text);
        else
            text = convertMarkdownLinksToXml(text);
        return text;
    }

    // ---------- 5. 按起始位置从后往前排序 ----------
    std::sort(allTags.begin(), allTags.end(),
              [](const ImgTag &a, const ImgTag &b) { return a.start > b.start; });



    // ========== 定义 ReplaceInfo 结构体（放在循环外） ==========
    struct ReplaceInfo {
        int start;
        int length;
        QString newUrl;
        bool isMdImg;
        QString coreText;
        QString alt;
        int width;
        int height;
        bool needPadding;
        QString fileMd5;          // 用于缓存写入
        QString originalUrl;      // 原始路径（上传失败时回退）
    };

    // ========== 如果 type == 1，使用并发上传 ==========
    if (type == 1) {
        // ---------- 替换信息结构体 ----------
        struct ReplaceInfo {
            int start;
            int length;
            QString newUrl;
            bool isMdImg;
            QString coreText;
            QString alt;
            int width;
            int height;
            bool needPadding;
            QString fileMd5;
            QString originalUrl;
            QByteArray fileData; // 为了备用上传保存数据
        };

        QList<ReplaceInfo> replacements;
        std::vector<std::pair<int, std::future<QString>>> uploadFutures;

        // ---------- 遍历所有标签 ----------
        for (int idx = 0; idx < allTags.size(); ++idx) {
            ImgTag &tag = allTags[idx];
            QString newUrl = tag.url;
            // http(s) / qqbot-media:// 都算「外部地址」：不读文件、不上传，原样发出去
            const bool isPassThrough = isPassThroughImageUrl(newUrl);

            if (!isPassThrough && !newUrl.isEmpty()) {
                QString fileMd5;
                // 本地文件不存在（或不是文件）→ 用运行目录下的默认图「图片不存在.webp」顶上，
                // 否则下面的 calculateFileMD5AndSize 会失败、这个标签就原样残留在正文里
                if (!QFileInfo(newUrl).isFile()) {
                    const QString placeholder = missingImagePlaceholderPath();
                    if (placeholder.isEmpty())
                        continue;                       // 连默认图都没有 → 保持原行为
                    newUrl = placeholder;
                    tag.url = placeholder;              // 回退路径也指向默认图
                }
                if (!calculateFileMD5AndSize(newUrl, fileMd5, tag.width, tag.height))
                    continue;

                if (tag.isMdImg && tag.hasUserSize) {
                    tag.width = tag.userWidth;
                    tag.height = tag.userHeight;
                }

                // 缓存检查（纯读一次；过期就当未命中，不删 —— 见 cachedValueIfFresh）
                QString cacheKey = m_info->appid + ":imageB_" + fileMd5;
                bool cacheValid = false;
                QString cachedUrl;
                if (cache_db && !fileMd5.isEmpty()) {
                    QString cached = cachedValueIfFresh(cacheKey);
                    if (!cached.isEmpty()) {
                        int sepIdx = cached.lastIndexOf("||||");
                        if (sepIdx != -1) {
                            qint64 expireTime = cached.left(sepIdx).toLongLong();
                            cachedUrl = cached.mid(sepIdx + 4);
                            if (QDateTime::currentSecsSinceEpoch() < expireTime)
                                cacheValid = true;
                        }
                    }
                }

                // 读取文件数据（用于备用上传）
                QByteArray fileData;
                if (!cacheValid) {
                    QFile file(newUrl);
                    if (file.open(QIODevice::ReadOnly)) {
                        fileData = file.readAll();
                        file.close();
                    }
                }

                int replaceIdx = replacements.size();
                replacements.append({
                    tag.start,
                    tag.length,
                    cacheValid ? cachedUrl : QString(),
                    tag.isMdImg,
                    tag.coreText,
                    tag.alt,
                    tag.width,
                    tag.height,
                    tag.needPadding,
                    fileMd5,
                    newUrl,
                    fileData // 保存数据
                });

                if (!cacheValid) {
                    // 需要上传：调用 uploadimg 并保存 future
                    std::future<QString> future = uploadimg(newUrl); // 假设 openid 为空或由其他逻辑提供
                    uploadFutures.emplace_back(replaceIdx, std::move(future));

                }
            } else {
                // HTTP 链接或空路径
                replacements.append({
                    tag.start,
                    tag.length,
                    newUrl,
                    tag.isMdImg,
                    tag.coreText,
                    tag.alt,
                    tag.width,
                    tag.height,
                    tag.needPadding,
                    QString(),
                    newUrl,
                    QByteArray()
                });
            }
        }

        // ---------- 等待所有上传任务完成并获取结果 ----------
        for (auto &pair : uploadFutures) {
            int replaceIdx = pair.first;
            std::future<QString> &future = pair.second;
            QString uploadedUrl;
            try {
                uploadedUrl = future.get();
            } catch (const std::exception &e) {
                //qWarning() << "uploadimg exception:" << e.what();
                uploadedUrl = QString();
            }

            // 如果 uploadimg 返回空，则尝试备用富媒体上传（堵塞）
            if (uploadedUrl.isEmpty() && !replacements[replaceIdx].fileData.isEmpty()) {

                qint64 expireTime = 0;
                QString md5;
                bool ok = false;
                uploadRichMediaPool(targetType, openid, 1, replacements[replaceIdx].fileData, QString(), // filename 可以为空，走图片池子复用
                                    expireTime, md5, ok, uploadedUrl, /*usePool=*/true);
                 if (ok) uploadedUrl += "&response-content-type=image%2Fpng";
            }

            // 更新 replacements 中的 newUrl
            if (!uploadedUrl.isEmpty()) {
                replacements[replaceIdx].newUrl = uploadedUrl;
                if (cache_db) {
                    QString fileMd5 = replacements[replaceIdx].fileMd5;
                    QString cacheKey = m_info->appid + ":imageB_" + fileMd5;
                    qint64 expire = QDateTime::currentSecsSinceEpoch() + 1430 * 60;
                    cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(uploadedUrl));
                }
            } else {
                // 上传失败，保留原路径
                replacements[replaceIdx].newUrl = replacements[replaceIdx].originalUrl;
            }
        }

        // ---------- 统一替换 text ----------
        std::sort(replacements.begin(), replacements.end(),
                  [](const ReplaceInfo &a, const ReplaceInfo &b) {
                      return a.start > b.start;
                  });

        for (const ReplaceInfo &ri : std::as_const(replacements)) {
            if (ri.newUrl.isEmpty()) {
                text.replace(ri.start, ri.length, QString());
                continue;
            }
            QString markdownImg;
            if (ri.isMdImg) {
                if (ri.needPadding) {
                    int w = (ri.width > 0) ? ri.width : 0;
                    int h = (ri.height > 0) ? ri.height : 0;
                    if (w > 0 || h > 0)
                        markdownImg = QString("![%1 #%2px #%3px](%4)").arg(ri.coreText).arg(w).arg(h).arg(ri.newUrl);
                    else
                        markdownImg = QString("![%1 #1000px #0px](%2)").arg(ri.coreText, ri.newUrl);
                } else {
                    markdownImg = QString("![%1](%2)").arg(ri.alt, ri.newUrl);
                }
            } else {
                int w = (ri.width > 0) ? ri.width : 1000;
                int h = ri.height;
                if (h > 0)
                    markdownImg = QString("![#%1px #%2px](%3)").arg(w).arg(h).arg(ri.newUrl);
                else
                    markdownImg = QString("![#1000px #0px](%2)").arg(ri.newUrl);
            }
            text.replace(ri.start, ri.length, markdownImg);
        }
    }

    else{

        bool firstProcessed = false;  // 用于 type==0 只处理第一个标签
        bool neiwang=false;//测试内网是否可用
        for (int idx = 0; idx < allTags.size(); ++idx) {
        ImgTag &tag = allTags[idx];
        QString newUrl = tag.url;
        // http(s) / qqbot-media:// 都算「外部地址」：不读文件、不上传，原样发出去
        const bool isPassThrough = isPassThroughImageUrl(newUrl);

        // ---------- 仅对本地非 HTTP 路径执行上传 ----------
        if (!isPassThrough && !newUrl.isEmpty()) {
            QString fileMd5;
            // 本地文件不存在（或不是文件）→ 用运行目录下的默认图「图片不存在.webp」顶上，
            // 否则下面的 calculateFileMD5AndSize 会失败、这个标签就原样残留在正文里
            if (!QFileInfo(newUrl).isFile()) {
                const QString placeholder = missingImagePlaceholderPath();
                if (placeholder.isEmpty())
                    continue;                           // 连默认图都没有 → 保持原行为
                newUrl = placeholder;
                tag.url = placeholder;                  // 回退路径也指向默认图
            }
            // 计算 MD5 并获取文件实际宽高（会写入 tag.width / tag.height）
            if (!calculateFileMD5AndSize(newUrl, fileMd5, tag.width, tag.height))
                continue;

            // 如果是 Markdown 图片且用户指定了尺寸，则恢复为用户指定的值
            if (tag.isMdImg && tag.hasUserSize) {
                tag.width = tag.userWidth;
                tag.height = tag.userHeight;
            }

            // 根据 type 进行上传和缓存
            if (type == 0) {
                // ========== 类型 0：富媒体上传，只处理第一个标签 ==========
                if (!firstProcessed) {
                    firstProcessed = true;

                    QString cacheKey = m_info->appid + ":imageA_" + fileMd5;
                    bool cacheValid = false;
                    QString cachedUrl;

                    // 缓存检查（纯读一次；过期就当未命中，不删 —— 见 cachedValueIfFresh）
                    if (cache_db && !fileMd5.isEmpty()) {
                        QString cached = cachedValueIfFresh(cacheKey);
                        if (!cached.isEmpty()) {
                            int sepIdx = cached.lastIndexOf("||||");
                            if (sepIdx != -1) {
                                qint64 expireTime = cached.left(sepIdx).toLongLong();
                                cachedUrl = cached.mid(sepIdx + 4);
                                if (QDateTime::currentSecsSinceEpoch() < expireTime)
                                    cacheValid = true;
                            }
                        }
                    }

                    if (cacheValid) {
                        newUrl = cachedUrl;
                    } else {
                        bool ok = false;



                        // type==0 用 file_info 发送（每次都要新的）→ 每次重新上传，不复用 COS 链接池
                        // （usePool=false → 直接走原始分片上传，不受池子那套占位/复写逻辑影响）
                        qint64 imgExpire = 0; QString imgMd5;
                        QString fileInfo = uploadRichMediaPoolA(targetType, openid, 1, newUrl, imgExpire, imgMd5, ok, /*usePool=*/false);

                        // 返回的就是裸 file_info，直接拿去发/缓存（以前要先从 "[image,path=…]" 里抠出来）
                        if (ok && !fileInfo.isEmpty()) {
                            newUrl = fileInfo;
                            if (cache_db) {
                                qint64 expire = QDateTime::currentSecsSinceEpoch() + 1440 * 60;
                                cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(newUrl));
                            }
                        } else {
                            newUrl = tag.url;
                        }
                    }
                    info = newUrl;
                }
                // 类型 0：所有图片标签均删除
                text.replace(tag.start, tag.length, QString());
            }
            else if (type == 2) {
                info = newUrl;
                text.replace(tag.start, tag.length, QString());
            }    
        }
        else {
            // ---------- HTTP 链接（或空路径）不上传，仅替换 ----------
            if (type == 0 || type == 2) {
                info = newUrl;
                text.replace(tag.start, tag.length, QString());
            } else if (type == 1) {
                // HTTP 链接，保留原有内容，但也要遵循 Markdown 图片的 alt 规则
                QString markdownImg;
                if (tag.isMdImg) {
                    if (tag.needPadding) {
                        // 无法获取尺寸，只保留核心文本（不加尺寸）
                        markdownImg = QStringLiteral("![%1](%2)").arg(tag.coreText,newUrl);
                    } else {
                        markdownImg = QStringLiteral("![%1](%2)").arg(tag.alt,newUrl);
                    }
                } else {
                    // 旧 [image] 标签，保持原逻辑（默认宽度 1000）
                    int w = (tag.width > 0) ? tag.width : 1000;
                    int h = tag.height;
                    if (h > 0)
                        markdownImg = QStringLiteral("![#%1px #%2px](%3)").arg(w).arg(h).arg(newUrl);
                    else
                        markdownImg = QStringLiteral("![#%1px #0px](%2)").arg(w).arg(newUrl);
                }
                text.replace(tag.start, tag.length, markdownImg);
            }
        }
    }
    }
    // ---------- 6. 处理其他 Markdown 链接 ----------

    if (type == 0 || type == 2) 
        text = convertMdLinksKeepHttp(text);
    else
        text = convertMarkdownLinksToXml(text);

    return text;

}
static QMutex g_cosPutPoolMutex;
static QList<CosPutPoolEntry> g_cosPutPool;

static const qint64 COS_POOL_TTL_MS   = 58 * 60 * 1000;  // 55 分钟超时删除

static const int    COS_POOL_MAX      = 10000;


// 生成一张随机像素的小 PNG：合法图片（能过 file_type=1 的内容类型检查），
// 像素随机 → 每次内容不同，避免缓存命中。池子新建链接的占位用。
QByteArray makePlaceholderPng()
{
    QImage img(8, 8, QImage::Format_RGB32);
    quint64 seed = QDateTime::currentMSecsSinceEpoch();
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            img.setPixel(x, y, qRgb((seed >> 33) & 0xFF, (seed >> 41) & 0xFF, (seed >> 49) & 0xFF));
        }
    }
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return buf.data();
}

// 从池子取一条可用链接。O(1) 无遍历：回池 append 即保序，队头永远最早入池——
// 只看队头：过期就弹出（55 分钟超时惰性清理），没过期直接取走。
// 取到返回 true 并移出池子。
bool takeCosPutEntry(CosPutPoolEntry &out)
{
    QMutexLocker locker(&g_cosPutPoolMutex);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    while (!g_cosPutPool.isEmpty() && nowMs >= g_cosPutPool.first().expireAt)
        g_cosPutPool.removeFirst();    // 队头过期 → 丢弃（55 分钟超时，惰性清理）
    if (g_cosPutPool.isEmpty()) return false;
    out = g_cosPutPool.takeFirst();
    return true;
}

// 取走的链接先暂存在这里（put 已完成，等这条消息发送完成）
// thread_local：一条消息的「上传图片 → 发送消息」在同一线程内完成，天然按消息隔离
static thread_local QList<CosPutPoolEntry> t_cosPutPending;

// put 成功后暂存（不入池），等消息发送完再回池
void parkCosPutEntry(const CosPutPoolEntry &entry)
{
    t_cosPutPending.append(entry);
}


// 把一组链接回池：立即可复用（无 CD）。跨线程安全（池有锁）。
void flushCosPutList(QList<CosPutPoolEntry> list)
{
    if (list.isEmpty()) return;
    QMutexLocker locker(&g_cosPutPoolMutex);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const CosPutPoolEntry &e : list) {
        if (nowMs >= e.expireAt) continue;              // 已过期直接丢弃
        if (g_cosPutPool.size() >= COS_POOL_MAX) break;
        g_cosPutPool.append(e);
    }
}

// 取走当前线程暂存的全部条目（转移所有权），交给异步发送回调持有
QList<CosPutPoolEntry> takeCosPutPending()
{
    QList<CosPutPoolEntry> out;
    out.swap(t_cosPutPending);
    return out;
}




// 消息发送完成后统一回池（同步路径用）
void flushCosPutPending()
{
    flushCosPutList(takeCosPutPending());
}

// RAII：发送函数退出时（无论发送成败）把本线程 pending 的链接全部回池
struct CosPutPendingGuard {
    ~CosPutPendingGuard() { flushCosPutPending(); }
};

// raw_url 加时间戳：重复链接有缓存，加时间戳才会实时读取 cos 文件
QString bustRawUrlCache(const QString &rawUrl)
{
    if (rawUrl.isEmpty()) return rawUrl;
    QString sep = rawUrl.contains('?') ? "&" : "?";
    return rawUrl + sep + "t=" + QString::number(QDateTime::currentMSecsSinceEpoch());
}




// processImageTags 纯回调版（堵塞版不动；send_messagesAsync / send_messagesAsync2 接线用）：
// 热路径（type==1 md 图片）全程零线程等待：
//   阶段1（调用线程同步执行，微秒级）：解析标签 + md5 + 缓存命中判断 + 读文件；
//     无本地待上传（纯 http / 全缓存命中）→ 直接完成，零线程零阻塞
//   阶段2：每个待上传项走 uploadimgCb（uploadimg 的纯回调版，内部回调链，不占线程），
//     原子计数聚合并发回调（回调在 NetManager 线程池线程触发）
//   阶段3：最后一个回调所在线程统一替换文本 + convert 链接 → onDone
// 备用上传也全回调：uploadimg 全渠道失败且有 fileData → uploadRichMediaPoolAsync
//   （prepare→put→finish→files 全异步，零线程）；其回调线程 park 的池条目经
//   takeCosPutPending 收进共享状态，完成时统一 re-park 到 onDone 线程 ——
//   「发送后回池」闭环不依赖特定线程
// 仅 type!=1（非 md 图片，低频）保留单线程包装（processImageTags 阻塞本体）
// 全部中间状态挂 shared_ptr，this 销毁安全（每个 touchpoint 用 appid 重查 m_botClients）。
void QQBotClient::processImageTagsAsync(const QString &textIn, int type, int targetType,
                                        const QString &openid,
                                        std::function<void(const QString &, const QString &, const QString &)> onDone)
{
    if (!onDone) return;
    const int appid = m_info->appid_int;

    // ---------- type 0/2：上传是阻塞型，保留单线程包装 ----------
    if (type != 1) {
        std::thread([this, appid, textIn, type, targetType, openid, onDone]() mutable {
            QQBotClient *c = m_botClients.value(appid);   // 防 this 已销毁（与 doPost 同款保护）
            if (!c) return;

            QString text = textIn;
            QString info, message_reference;
            c->processImageTags(text, type, info, targetType, openid, message_reference);
            if (m_botClients.value(appid) != c) return;   // 回调前再确认一次
            onDone(text, info, message_reference);
        }).detach();
        return;
    }

    // ---------- 共享状态 ----------
    struct ReplaceInfo {   // 字段与堵塞版一致
        int start;
        int length;
        QString newUrl;
        bool isMdImg;
        QString coreText;
        QString alt;
        int width;
        int height;
        bool needPadding;
        QString fileMd5;
        QString originalUrl;
        QByteArray fileData;
    };
    struct AsyncState {
        QMutex mutex;
        QString text;
        QString message_reference;
        QList<ReplaceInfo> replacements;
        std::atomic<int> pending{0};      // 未完成的待上传项（原子：回调并发聚合）
        std::atomic<bool> phase1Done{false};
        bool completed = false;           // 防重复收尾（mutex 内访问）
        QList<CosPutPoolEntry> parked;    // 备用线程 park 的池条目
    };
    auto st = std::make_shared<AsyncState>();
    st->text = textIn;

    // ---------- 收尾：替换文本 + re-park + convert + onDone ----------
    auto completeAll = [appid, st, onDone]() {
        QQBotClient *c = m_botClients.value(appid);
        if (!c) return;

        QString text, message_reference;
        {
            QMutexLocker locker(&st->mutex);
            if (st->completed) return;
            st->completed = true;

            QList<ReplaceInfo> reps = st->replacements;
            std::sort(reps.begin(), reps.end(),
                      [](const ReplaceInfo &a, const ReplaceInfo &b) { return a.start > b.start; });

            text = st->text;
            for (const ReplaceInfo &ri : std::as_const(reps)) {
                if (ri.newUrl.isEmpty()) {
                    text.replace(ri.start, ri.length, QString());
                    continue;
                }
                QString markdownImg;
                if (ri.isMdImg) {
                    if (ri.needPadding) {
                        int w = (ri.width > 0) ? ri.width : 0;
                        int h = (ri.height > 0) ? ri.height : 0;
                        if (w > 0 || h > 0)
                            markdownImg = QString("![%1 #%2px #%3px](%4)").arg(ri.coreText).arg(w).arg(h).arg(ri.newUrl);
                        else
                            markdownImg = QString("![%1 #1000px #0px](%2)").arg(ri.coreText, ri.newUrl);
                    } else {
                        markdownImg = QString("![%1](%2)").arg(ri.alt, ri.newUrl);
                    }
                } else {
                    int w = (ri.width > 0) ? ri.width : 1000;
                    int h = ri.height;
                    if (h > 0)
                        markdownImg = QString("![#%1px #%2px](%3)").arg(w).arg(h).arg(ri.newUrl);
                    else
                        markdownImg = QString("![#1000px #0px](%2)").arg(ri.newUrl);
                }
                text.replace(ri.start, ri.length, markdownImg);
            }
            message_reference = st->message_reference;

            // 备用线程 park 的池条目统一转到本线程 thread_local，发送时取走、发送回调里回池
            for (const CosPutPoolEntry &e : std::as_const(st->parked))
                parkCosPutEntry(e);
            st->parked.clear();
        }

        if (m_botClients.value(appid) != c) return;   // onDone 前再确认一次
        text = convertMarkdownLinksToXml(text);       // 与堵塞版 type==1 收尾一致
        onDone(text, QString(), message_reference);
    };

    // ---------- 聚合：计数减一，归零且阶段1已收口才收尾 ----------
    auto finishOne = [completeAll, st]() {
        if (st->pending.fetch_sub(1) == 1 && st->phase1Done.load())
            completeAll();
    };

    // ---------- 单项结果：空且备选数据在 → 全异步备用上传（uploadRichMediaPoolAsync）；否则写缓存 ----------
    auto handleResult = [appid, st, targetType, openid, finishOne](int replaceIdx, QString uploadedUrl) {
        QQBotClient *c = m_botClients.value(appid);

        bool needFallback = false;
        {
            QMutexLocker locker(&st->mutex);
            needFallback = uploadedUrl.isEmpty() && !st->replacements[replaceIdx].fileData.isEmpty();
        }
        if (needFallback) {
            QByteArray data;
            {
                QMutexLocker locker(&st->mutex);
                data = st->replacements[replaceIdx].fileData;
            }
            QQBotClient *c3 = m_botClients.value(appid);
            if (!c3) { finishOne(); return; }
            c3->uploadRichMediaPoolAsync(targetType, openid, 1, data, QString(), // filename 可以为空，走图片池子复用
                                         /*usePool=*/true,
                [appid, st, replaceIdx, finishOne](const QString &result, qint64, const QString &,
                                                   bool ok, const QString &outurl) {
                    QQBotClient *c2 = m_botClients.value(appid);
                    QString url = outurl;
                    if (ok) url += "&response-content-type=image%2Fpng";

                    {
                        QMutexLocker locker(&st->mutex);
                        // 本回调线程 park 的池条目收进共享状态（completeAll 时统一 re-park 到 onDone 线程）
                        st->parked.append(takeCosPutPending());
                        ReplaceInfo &ri = st->replacements[replaceIdx];
                        if (ok && !url.isEmpty()) {
                            ri.newUrl = url;
                            if (c2 && cache_db) {
                                QString cacheKey = c2->m_info->appid + ":imageB_" + ri.fileMd5;
                                qint64 expire = QDateTime::currentSecsSinceEpoch() + 1430 * 60;
                                cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(url));
                            }
                        } else {
                            ri.newUrl = ri.originalUrl;
                        }
                    }
                    finishOne();
                });
            return;
        }

        // 正常结果：写缓存 + 设置 newUrl
        {
            QMutexLocker locker(&st->mutex);
            ReplaceInfo &ri = st->replacements[replaceIdx];
            if (!uploadedUrl.isEmpty()) {
                ri.newUrl = uploadedUrl;
                if (c && cache_db) {
                    QString cacheKey = c->m_info->appid + ":imageB_" + ri.fileMd5;
                    qint64 expire = QDateTime::currentSecsSinceEpoch() + 1430 * 60;
                    cache_db->put(cacheKey, QString("%1||||%2").arg(expire).arg(uploadedUrl));
                }
            } else {
                // 上传失败，保留原路径
                ri.newUrl = ri.originalUrl;
            }
        }
        finishOne();
    };

    // ---------- 阶段1：解析 + 缓存 + 整理（调用线程同步执行） ----------
    QString &text = st->text;
    get_ref(text, st->message_reference);
    // 异步版只服务 type==1（markdown）：放行的 http / qqbot-media 图先补齐尺寸
    normalizePassThroughImageSizes(text);
    // 异步版只服务 type==1（markdown），走「放行版」正则
    const QRegularExpression &mdImgRe = mdImageRegexPassThrough();
    static const QRegularExpression sizeRe(R"(#(\d+)px)");

    struct ImgTag {
        int start;
        int length;
        int width;
        int height;
        bool isMdImg = false;
        bool needPadding = false;
        QString alt;
        QString coreText;
        int userWidth = 0;
        int userHeight = 0;
        bool hasUserSize = false;
        QString url;
    };
    QList<ImgTag> allTags;

    // 解析旧标签 [image]
    int searchFrom = 0;
    while (true) {
        int imgStart = text.indexOf(QLatin1String("[image"), searchFrom, Qt::CaseInsensitive);
        if (imgStart == -1) break;

        int imgEnd = imgStart + 1;
        int bracketDepth = 1;
        while (imgEnd < text.size() && bracketDepth > 0) {
            if (text[imgEnd] == '[') bracketDepth++;
            else if (text[imgEnd] == ']') bracketDepth--;
            ++imgEnd;
        }
        if (bracketDepth != 0) break;

        int tagLen = imgEnd - imgStart;
        int contentStart = imgStart + 6;
        while (contentStart < imgEnd - 1 && (text[contentStart].isSpace() || text[contentStart] == ','))
            ++contentStart;
        int contentLen = tagLen - (contentStart - imgStart) - 1;
        if (contentLen < 0) contentLen = 0;
        QStringView tagContentView = QStringView(text).mid(contentStart, contentLen);

        ImageInfo imgInfo = parseImageTagContent(tagContentView);
        if (!imgInfo.urlOrPath.isEmpty()) {
            ImgTag tag;
            tag.start = imgStart;
            tag.length = tagLen;
            tag.url = imgInfo.urlOrPath;
            tag.width = imgInfo.x;
            tag.height = imgInfo.y;
            tag.isMdImg = false;
            allTags.append(tag);
        }
        searchFrom = imgEnd;
    }

    // 解析 Markdown 图片标签 ![]()
    QRegularExpressionMatchIterator it = mdImgRe.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString alt = match.captured(1).trimmed();
        QString url = match.captured(2).trimmed();
        if (url.isEmpty()) continue;
        // 正则已排过；再兜一层大小写变体（HTTP:// 之类）——md 消息里这两类都原样发出去
        if (isPassThroughImageUrl(url)) continue;

        QRegularExpressionMatchIterator sizeIt = sizeRe.globalMatch(alt);
        QList<int> sizes;
        while (sizeIt.hasNext()) {
            QRegularExpressionMatch sizeMatch = sizeIt.next();
            sizes.append(sizeMatch.captured(1).toInt());
        }
        int count = sizes.size();

        QString coreText = alt;
        coreText.remove(sizeRe);

        bool needPadding = false;
        QString modifiedAlt = alt;
        int userWidth = 0, userHeight = 0;
        bool hasUserSize = false;

        if (count == 0) {
            needPadding = true;
            hasUserSize = false;
        } else if (count == 1) {
            modifiedAlt = alt.trimmed() + " #0px";
            needPadding = false;
            userWidth = sizes[0];
            userHeight = 0;
            hasUserSize = true;
        } else {
            needPadding = false;
            userWidth = sizes[0];
            userHeight = sizes[1];
            hasUserSize = true;
        }

        ImgTag tag;
        tag.start = match.capturedStart();
        tag.length = match.capturedLength();
        tag.url = url;
        tag.isMdImg = true;
        tag.alt = modifiedAlt;
        tag.coreText = coreText;
        tag.needPadding = needPadding;
        tag.userWidth = userWidth;
        tag.userHeight = userHeight;
        tag.hasUserSize = hasUserSize;
        tag.width = userWidth;
        tag.height = userHeight;

        allTags.append(tag);
    }

    // 没有任何图片标签：只处理其他 Markdown 链接后直接完成（同步、零线程）
    if (allTags.isEmpty()) {
        text = convertMarkdownLinksToXml(text);
        onDone(text, QString(), st->message_reference);
        return;
    }

    std::sort(allTags.begin(), allTags.end(),
              [](const ImgTag &a, const ImgTag &b) { return a.start > b.start; });

    // 遍历所有标签：缓存命中直接落位；未命中发起 uploadimgCb（纯回调，不占线程）
    for (int idx = 0; idx < allTags.size(); ++idx) {
        ImgTag &tag = allTags[idx];
        QString newUrl = tag.url;
        // http(s) / qqbot-media:// 都算「外部地址」：不读文件、不上传，原样发出去
        const bool isPassThrough = isPassThroughImageUrl(newUrl);

        if (!isPassThrough && !newUrl.isEmpty()) {
            QString fileMd5;
            // 与堵塞版一致：本地文件不存在 → 换成运行目录下的默认图「图片不存在.webp」
            if (!QFileInfo(newUrl).isFile()) {
                const QString placeholder = missingImagePlaceholderPath();
                if (placeholder.isEmpty())
                    continue;                       // 连默认图都没有 → 保持原行为
                newUrl = placeholder;
                tag.url = placeholder;
            }
            if (!calculateFileMD5AndSize(newUrl, fileMd5, tag.width, tag.height))
                continue;

            if (tag.isMdImg && tag.hasUserSize) {
                tag.width = tag.userWidth;
                tag.height = tag.userHeight;
            }

            // 缓存检查（纯读一次；过期就当未命中，不删 —— 见 cachedValueIfFresh）
            QString cacheKey = m_info->appid + ":imageB_" + fileMd5;
            bool cacheValid = false;
            QString cachedUrl;
            if (cache_db && !fileMd5.isEmpty()) {
                QString cached = cachedValueIfFresh(cacheKey);
                if (!cached.isEmpty()) {
                    int sepIdx = cached.lastIndexOf("||||");
                    if (sepIdx != -1) {
                        qint64 expireTime = cached.left(sepIdx).toLongLong();
                        cachedUrl = cached.mid(sepIdx + 4);
                        if (QDateTime::currentSecsSinceEpoch() < expireTime)
                            cacheValid = true;
                    }
                }
            }

            // 读取文件数据（用于备用上传）
            QByteArray fileData;
            if (!cacheValid) {
                QFile file(newUrl);
                if (file.open(QIODevice::ReadOnly)) {
                    fileData = file.readAll();
                    file.close();
                }
            }

            int replaceIdx = st->replacements.size();
            st->replacements.append({
                tag.start,
                tag.length,
                cacheValid ? cachedUrl : QString(),
                tag.isMdImg,
                tag.coreText,
                tag.alt,
                tag.width,
                tag.height,
                tag.needPadding,
                fileMd5,
                newUrl,
                fileData
            });

            if (!cacheValid) {
                // 需要上传：先计数再发起（发起可能同步重入回调，保证计数完整）
                ++st->pending;
                uploadimgCb(newUrl, [st, handleResult, replaceIdx](QString uploadedUrl) {
                    handleResult(replaceIdx, uploadedUrl);
                });
            }
        } else {
            // HTTP 链接或空路径
            st->replacements.append({
                tag.start,
                tag.length,
                newUrl,
                tag.isMdImg,
                tag.coreText,
                tag.alt,
                tag.width,
                tag.height,
                tag.needPadding,
                QString(),
                newUrl,
                QByteArray()
            });
        }
    }

    // 阶段1收口：无待上传项（全缓存命中）→ 同步收尾；有 → 最后一个回调收尾
    st->phase1Done = true;
    if (st->pending.load() == 0)
        completeAll();
}

// 参考 uploadRichMedia(QByteArray 版)，但走 put 链接池复用
// usePool=false：音视频/文件——实测复用链接服务端报「文件格式不对」，不做优化，
// 直接回退原始分片上传（uploadRichMedia），保险
QString QQBotClient::uploadRichMediaPool(int targetType, const QString& openid,int fileType,
                                         const QByteArray& data,const QString &filename,
                                         qint64& expireTime,QString &md5,bool &ok,QString &outurl,
                                         bool usePool) {

    ok=false;
    outurl.clear();
    expireTime = 0;
    if(data.isEmpty()) return QString();

    // targetType==4 不支持 100K 申请/池子流程（会报错）；
    // usePool=false（视频/语音/文件）→ 走原始分片上传：每次 upload_prepare 拿全新 presigned_url，
    //   逐分片 put，再 part_finish + files。只有图片（fileType==1）才启用池子复用。
    if (targetType == 4 || !usePool) {
        return uploadRichMedia(targetType, openid, fileType, data, filename, expireTime, md5, ok, outurl);
    }

    // ---- md5/sha1/md5_10m 一律用原文件的真实值（申请大小才固定 100K）----
    qint64 fileSize = data.size();
    QCryptographicHash md5Hash(QCryptographicHash::Md5);
    md5Hash.addData(data);
    md5 = md5Hash.result().toHex();
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(data);
    QString sha1 = sha1Hash.result().toHex();
    QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
    md5_10mHash.addData(data.left(10 * 1024 * 1024));
    QString md5_10m = md5_10mHash.result().toHex();

    CosPutPoolEntry entry;
    bool haveEntry = false;
    bool fromPool = false;
    bool freshLink = false;   // 新建链接：1KB 占位内容已注册 file_info/raw_url，真实内容随后复写

    // ---- 1. 从池子取一条可复用的 put 链接（取用即移出，顺带清理过期条目）----
    fromPool = takeCosPutEntry(entry);
    haveEntry = fromPool;

    // ---- 2. 池子没有 → 修改版 upload_prepare：file_size 固定 100K 申请 put 链接 ----
    if (!haveEntry) {
        const qint64 APPLY_SIZE = 100 * 1024;    // 申请 100K，cos 实际可传任意大小
        QJsonObject prepJson;
        // 注册声明 file_type=1（图片）：某些群「禁止非管理员上传文件」，类型 4 注册的
        // file_info 发消息会被拦 → 占位改用随机生成的小 PNG（合法图片，过类型 1 内容检查），
        // 注册后真实图片靠 put 复写（COS 层无类型概念）
        prepJson["file_type"] = 1;
        prepJson["file_name"] = "text.bin";
        prepJson["file_size"] = APPLY_SIZE;
        prepJson["md5"]       = md5;
        prepJson["sha1"]      = sha1;
        prepJson["md5_10m"]   = md5_10m;
        QString prepUrl = get_url(targetType, openid, "upload_prepare");
        QString response = PostSync(prepUrl, prepJson, QString(), 30000);
        if (response.isEmpty()) return QString();

        QJsonDocument respDoc = QJsonDocument::fromJson(response.toUtf8());
        if (respDoc.isNull()) return QString();
        QJsonObject respObj = respDoc.object();
        entry.uploadId = respObj["upload_id"].toString();
        if (entry.uploadId.isEmpty()) return response; // 错误信息

        QJsonArray parts = respObj["parts"].toArray();
        if (parts.isEmpty()) return QString("upload_prepare 未返回分片");
        QJsonObject part = parts[0].toObject();
        entry.partIndex    = part["index"].toInt();
        entry.presignedUrl = part["presigned_url"].toString();
        if (entry.presignedUrl.isEmpty()) return QString("upload_prepare 未返回 presigned_url");
        entry.expireAt = QDateTime::currentMSecsSinceEpoch() + COS_POOL_TTL_MS;  // 55 分钟超时

        // ---- 新建链接（md 图片路径）：先传随机小 PNG 占位快速注册 file_info/raw_url，----
        // 真实内容随后一次 put 复写 —— 耗时的 files 放在小占位对象上，真实图片只承担一次 put
        if (usePool) {
            // 占位内容：随机像素 PNG（每次不同），合法图片 + 避免缓存命中
            const QByteArray placeholder = makePlaceholderPng();
            QCryptographicHash phHash(QCryptographicHash::Md5);
            phHash.addData(placeholder);
            const QString phMd5 = phHash.result().toHex();

            try {
                put(entry.presignedUrl, placeholder, "application/octet-stream", 30000);
            } catch (const std::exception &) {
                return QString("put 占位失败(新建链接)");
            }

            // finish 按占位内容提交（block_size/md5 = 实际 put 的数据）
            QJsonObject phFin;
            phFin["upload_id"]  = entry.uploadId;
            phFin["part_index"] = entry.partIndex;
            phFin["block_size"] = placeholder.size();
            phFin["md5"]        = phMd5;
            qDebug() << PostSync(get_url(targetType, openid, "upload_part_finish"), phFin, QString(), 30000);

            // files 注册出 file_info / raw_url
            QJsonObject phFiles;
            phFiles["upload_id"] = entry.uploadId;
            QString phResp = PostSync(get_url(targetType, openid, "files"), phFiles, QString(), 30000);
            QJsonDocument phDoc = QJsonDocument::fromJson(phResp.toUtf8());
            QJsonObject phObj = phDoc.object();
            entry.fileInfo = phObj["file_info"].toString();
            entry.rawUrl   = phObj["raw_url"].toString();
            if (entry.fileInfo.isEmpty() || entry.rawUrl.isEmpty())
                return phResp;   // 注册失败，链接作废
            entry.infoFileType = fileType;   // 记「使用类型」（注册类型同为 1 图片）：md 图片捷径靠它防跨类型误用
            freshLink = true;
        }
    }

    // ---- 3. 把完整文件 put 到 cos（申请 100K 但实际可传任意大小）----
    // 链接此刻已不在池中：失败 → 直接丢弃（不还池）；成功 → 最后统一还池
    bool putOk = false;
    int retry = 0;
    int currentTimeout = 30000;
    while (retry < 3 && !putOk) {
        try {
            put(entry.presignedUrl, data, "application/octet-stream", currentTimeout);
            putOk = true;
        } catch (const std::exception &) {
            retry++;
            if (retry < 3) {
                QThread::msleep(1000 * (1 << (retry - 1)));
                currentTimeout += 10000;
            }
        }
    }
    if (!putOk) {
        if (freshLink)
            parkCosPutEntry(entry);   // 占位链接已注册有效，留着给下一个调用者复写
        return QString("put 文件到 cos 失败(重试3次)");
    }

    // ---- 3.5 复用捷径（md 图片）：put 覆盖 cos 内容即完成 —— 省掉 finish + files 两个 API，
    // 直接用已注册的 raw_url 加时间戳（链接实时反映本次 put 的数据）----
    // fromPool = 池里取的；freshLink = 新建链接（1KB 占位注册后复写）。infoFileType 防跨类型误用。
    if ((fromPool || freshLink) && usePool && !entry.fileInfo.isEmpty() && entry.infoFileType == fileType) {
        outurl = bustRawUrlCache(entry.rawUrl);
        expireTime = entry.expireAt / 1000;   // 剩余有效期（秒）
        ok = true;
        parkCosPutEntry(entry);               // 暂存，等消息发送完成后回池（无 CD）
        return entry.fileInfo;
    }

    // ---- 4. 提交 put 成功（upload_part_finish，按实际 put 的数据提交）----
    // （捷径未命中才会走到这：fileInfo 缺失等兜底全流程）
    {
        QJsonObject finishJson;
        finishJson["upload_id"]  = entry.uploadId;
        finishJson["part_index"] = entry.partIndex;
        finishJson["block_size"] = fileSize;
        finishJson["md5"]        = md5;
        QString finishUrl = get_url(targetType, openid, "upload_part_finish");
        qDebug() << PostSync(finishUrl, finishJson, QString(), 30000);
    }

    // ---- 5. 执行 /files 拿 file_info ----
    QJsonObject filesJson;
    filesJson["upload_id"] = entry.uploadId;
    QString filesUrl = get_url(targetType, openid, "files");
    QString filesResp = PostSync(filesUrl, filesJson, QString(), 30000);
    if (filesResp.isEmpty()) return QString();

    QJsonDocument filesRespDoc = QJsonDocument::fromJson(filesResp.toUtf8());
    if (filesRespDoc.isNull()) return QString();
    QJsonObject filesObj = filesRespDoc.object();
    QString file_info = filesObj["file_info"].toString();
    if (file_info.isEmpty()) {
        return filesResp; // 错误信息（链接已移出池，自然丢弃）
    }

    // ---- 6. 成功：链接暂存（存下 file_info/raw_url），消息发送完成后回池即可复用 ----
    entry.fileInfo = file_info;
    entry.rawUrl   = filesObj["raw_url"].toString();
    entry.infoFileType = fileType;
    parkCosPutEntry(entry);

    // raw_url 加时间戳，避免重复链接命中缓存
    outurl = bustRawUrlCache(filesObj["raw_url"].toString());
    expireTime = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
    ok = true;
    return file_info;
}

// ===========================================================================
// 媒体异步执行器 —— 一条常驻线程 + Qt 事件循环
// ---------------------------------------------------------------------------
// 为什么必须有它：QProcess 的 finished / errorOccurred 是**信号**，只有在「有 Qt 事件循环」
// 的线程里才会被投递。而本文件的调用方五花八门，全都没有事件循环：
//   · QThreadPool 的 worker —— `onTextMessageReceived` 就是把消息丢进全局线程池处理的
//   · Python 的 asyncio 后台线程（pluginpage 的 m_loop）
//   · 插件自己的线程（JS 宿主 / DLL 插件）
// 所以在调用方线程上 new 一个 QProcess 再等信号，等于永远收不到 —— 这就是原来只能
// `waitForFinished` 卡着的原因。
//
// 于是：起一条常驻线程跑 Qt 事件循环，所有「外部进程」和「需要延时的重试」都投到它上面：
//   · ffmpeg 探测/转码/切段 → QProcess 异步启动，进程退出才回调（不再 waitForFinished 卡 120s）
//   · 重试退避 → QTimer::singleShot，不 sleep 任何线程
// 调用方线程从头到尾**零阻塞**，也不再每次发送新建一条线程。
//
// 线程只做「解析输出 / 拼 JSON / 派发下一步」这类微秒级的事，耗时的都在 QProcess 或
// NetManager 的线程上，所以一条线程够用，不需要池子。
// ===========================================================================
static QThread *g_mediaThread = nullptr;
static QObject *g_mediaCtx    = nullptr;

static void ensureMediaThread()
{
    if (g_mediaThread) return;
    // 进程级常驻，不设 parent、不销毁（与 NetManager 的网络线程同款）
    g_mediaThread = new QThread();
    g_mediaThread->setObjectName(QStringLiteral("qiancao-media"));
    g_mediaThread->start();
    // 哨兵对象：只当 invokeMethod 的 context（决定任务落在哪个线程）。
    // 不需要 Q_OBJECT —— invokeMethod 的 functor 重载直接存拷贝并调用，不经过 moc。
    g_mediaCtx = new QObject();
    g_mediaCtx->moveToThread(g_mediaThread);
}

// 把任务投到媒体线程执行（已经在媒体线程则就地跑，省一次事件循环往返）
static void postToMedia(std::function<void()> fn)
{
    if (!fn) return;
    ensureMediaThread();
    if (QThread::currentThread() == g_mediaThread) { fn(); return; }
    QMetaObject::invokeMethod(g_mediaCtx, std::move(fn), Qt::QueuedConnection);
}

// 在媒体线程上延时执行（重试退避用；不 sleep 任何线程）
static void delayOnMedia(int ms, std::function<void()> fn)
{
    if (!fn) return;
    postToMedia([ms, fn]() {
        QTimer::singleShot(ms, g_mediaCtx, [fn]() { fn(); });
    });
}

// ---------------------------------------------------------------------------
// 「进程内 ffmpeg.dll」与「外部 ffmpeg.exe」的统一入口（**必须在媒体线程调用**）
//
//   blockingJob ：真正阻塞的那一小段（探测/转码/切段），会被丢到 QcFfmpeg 的
//                 单线程池里跑 —— 媒体线程本身仍然零阻塞，和以前起 QProcess 一样。
//   done        ：blockingJob 结束后**回到媒体线程**执行（所以里面可以安全地
//                 继续跑整条媒体链的下一次回调）。
//   fallback    ：最后手段 —— DLL 补不下来时走的老 QProcess 链路（现在只在
//                 没有 DLL 又下载失败、或者 Linux 上没编 .so 的情况下才会进来）。
//
// 为什么先 ensureAsync 再跑：发布版历史更新包只发 exe，老用户机器上根本没有
// ffmpeg.dll。第一次发语音时它会去 Gitee 下**那份 1.9MB 的小包**、只抽出 ffmpeg.dll
// （固定地址见 core/qcffmpeg.cpp 的 kFfmpegPackUrl），这期间这条请求会等它
// （而不是降级成「原样发」然后被 QQ 拒掉）。
// ---------------------------------------------------------------------------
static void runOnFfmpegDll(std::function<void()> blockingJob,
                           std::function<void()> done,
                           std::function<void()> fallback)
{
    if (!blockingJob) return;
    done = done ? done : std::function<void()>([]() {});

    if (QcFfmpeg::loaded()) {
        QcFfmpeg::runBlocking(blockingJob, [done]() {
            postToMedia(done);                 // 从后台线程跳回媒体线程
        });
        return;
    }

    QcFfmpeg::ensureAsync([blockingJob, done, fallback](bool ok) {
        // ensureAsync 的回调可能在网络线程 / 后台线程，先回媒体线程再决定
        postToMedia([ok, blockingJob, done, fallback]() {
            if (ok) {
                QcFfmpeg::runBlocking(blockingJob, [done]() {
                    postToMedia(done);
                });
                return;
            }
            if (fallback) fallback();
        });
    });
}

// 异步链（上传 / 媒体标签）跨多轮回调，中途账号或客户端可能已被销毁 ——
// 每一步进门都要先确认「自己还挂在全局表里」。
// ⚠ 必须是**自由函数**，且 appid 由调用方按值存进链状态后传进来：
//   写成成员函数会先解引用 this 去取 m_info->appid_int，而那一刻 this 可能已经析构（悬空访问）。
//   与本文件既有的 processImageTagsAsync / doPost 同款保护。
// ⚠ 守卫能成立的前提：销毁方是「先 m_botClients.take(appid) 再 deleteLater()」（见 accountpage.cpp），
//   所以表里查不到 == 这个指针已经不能再碰；value() 只比较指针，不解引用 self。
static bool clientAlive(const QQBotClient *self, int appid)
{
    return self && m_botClients.value(appid) == self;
}

// 把「网络回调」统一搬回媒体线程 —— 于是整条链的逻辑都是单线程的（不用操心锁），
// 同时手里有事件循环（重试退避 QTimer / 起 ffmpeg 都要）。
// ⚠ NetManager 的回调默认丢全局线程池，那些 worker 没有事件循环。
// ⚠ 进门第一件事就是 clientAlive 复检：请求在飞行途中（put 30s + 退避、prepare 30s…）
//   客户端完全可能已经被摘表销毁，那时再跑续写 lambda 就是往已析构对象上打。
//   查不到就静默丢掉这一步 —— 整条链已经没人等了，回调出去也无处可去。
//   ⚠ 这里是**唯一**的收口点：所有续写回调都套了 onMedia，所以不用每个 lambda 各写一遍。
static Callback onMedia(const QQBotClient *self, int appid, Callback cb)
{
    if (!cb) return cb;
    return [self, appid, cb](const QString &resp, QNetworkReply::NetworkError err) {
        postToMedia([self, appid, cb, resp, err]() {
            if (!clientAlive(self, appid)) return;
            cb(resp, err);
        });
    };
}

// 在媒体线程上跑一次外部进程；进程退出（或启动失败 / 超时被 kill）时**在媒体线程**回调 onDone。
//   code    ：退出码；<0 = 进程没能启动
//   err     ：stderr 全文（ffmpeg 的媒体信息就打在这里）
//   timedOut：是否超时被 kill
static void runProcessAsync(const QString &program, const QStringList &args, int timeoutMs,
                            std::function<void(int code, const QByteArray &err, bool timedOut)> onDone)
{
    if (!onDone) return;
    postToMedia([program, args, timeoutMs, onDone]() {
        QProcess *p = new QProcess();
        auto fired    = std::make_shared<bool>(false);
        auto timedOut = std::make_shared<bool>(false);

        // 统一出口：errorOccurred 与 finished 谁先到算谁，之后忽略（启动失败时只有 errorOccurred）
        auto finish = [p, fired, timedOut, onDone](int code) {
            if (*fired) return;
            *fired = true;
            const QByteArray err = p->readAllStandardError();
            p->deleteLater();                 // 媒体线程有事件循环，deleteLater 会被处理
            onDone(code, err, *timedOut);
        };

        QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
                         [finish](int code, QProcess::ExitStatus) { finish(code); });
        QObject::connect(p, &QProcess::errorOccurred, p,
                         [finish](QProcess::ProcessError) { finish(-1); });

        p->start(program, args);
        if (timeoutMs > 0) {
            QTimer::singleShot(timeoutMs, p, [p, timedOut]() {
                *timedOut = true;
                p->kill();               // kill 会走 finished(CrashExit)，靠 fired 去重
            });
        }
    });
}

// 带退避重试的异步 put：失败（err != NoError）按 1s/2s/4s 退避重试，最多 3 次。
// doPut(timeoutMs, cb) 负责发起一次 put（通常就是包一层 put2）。
// 对照同步版：那边是 try { put(...) } catch(...) 的循环，但 NetManager::put 出错时是
// set_value(空) 而不是抛异常，所以那段 catch 实际是死代码 —— 这里按注释写的「重试 3 次」的真实意图实现。
// ⚠ 写成自由函数递归（而不是自引用的 shared_ptr<std::function>）——后者会形成引用环永不释放。
// ⚠ 每轮进门先 clientAlive：put + 退避加起来可能几十秒，这期间账号/客户端可能已被销毁，
//   那时再调 doPut 就是往已析构对象上打（self 只做指针比较，不解引用）。
static void putRetryStep(int appid, const QQBotClient *self,
                         std::function<void(int timeoutMs, Callback)> doPut,
                         std::function<void(bool ok)> onDone, int used, int timeoutMs)
{
    if (!clientAlive(self, appid)) return;    // 客户端已销毁 → 整条链放弃（不再回调，也没人等了）
    doPut(timeoutMs, onMedia(self, appid, [doPut, onDone, used, timeoutMs, appid, self]
                             (const QString &body, QNetworkReply::NetworkError err) {
        if (err == QNetworkReply::NoError) { onDone(true); return; }
        if (used + 1 >= 3)                 { onDone(false); return; }
        delayOnMedia(1000 * (1 << used), [doPut, onDone, used, timeoutMs, appid, self]() {
            putRetryStep(appid, self, doPut, onDone, used + 1, timeoutMs + 10000);
        });
    }));
}

static void putRetryAsync(int appid, const QQBotClient *self,
                          std::function<void(int timeoutMs, Callback)> doPut,
                          std::function<void(bool ok)> onDone)
{
    putRetryStep(appid, self, std::move(doPut), std::move(onDone), 0, 30000);
}

// uploadRichMediaPool 的纯回调版：prepare → put → finish → files 全链路异步，零线程零阻塞。
// 与阻塞版逻辑逐行对应（取池/复用捷径/暂存回池机制完全一致），仅把三个 PostSync 换成 PostAsync、
// 阻塞 put 换成 put2（NetManager::putAsync，已具备 cos 内网直连）。
// 回调在 NetManager 线程池线程触发（无事件循环）→ put 重试为立即重试，不能用 QTimer 退避。
void QQBotClient::uploadRichMediaPoolAsync(int targetType, const QString &openid, int fileType,
                                           const QByteArray &data, const QString &filename, bool usePool,
                                           std::function<void(const QString &result, qint64 expireTime,
                                                              const QString &md5, bool ok, const QString &outurl)> onDone)
{
    const int appid = m_info->appid_int;
    if (!onDone) return;
    if (data.isEmpty()) {
        onDone(QStringLiteral("上传数据为空（文件读出来是 0 字节）"), 0, QString(), false, QString());
        return;
    }

    // usePool=false（视频/语音/文件）→ 一律转「原始分片上传」的异步版（uploadRichMediaAsync）。
    //   池子那套（100K 申请 + 链复用 + 占位 PNG）只给图片用：链复用会把同一个 COS 对象交给多条消息，
    //   异步 put 重试时极易互相覆盖。targetType==4（私聊）本来也不支持 100K 申请流程。
    if (targetType == 4 || !usePool) {
        uploadRichMediaAsync(targetType, openid, fileType, data, filename, onDone);
        return;
    }

    // ---- 共享状态（全链路异步，跨线程存活）----
    struct PoolAsyncState {
        CosPutPoolEntry entry;
        bool fromPool = false;
        bool usePool = false;
        int targetType = 0;
        QString openid;
        QByteArray data;
        qint64 fileSize = 0;
        int fileType = 0;
        QString md5, sha1, md5_10m;
        int retry = 0;
        int currentTimeout = 30000;
    };
    auto stp = std::make_shared<PoolAsyncState>();
    stp->usePool = usePool;
    stp->targetType = targetType;
    stp->openid = openid;
    stp->data = data;
    stp->fileSize = data.size();
    stp->fileType = fileType;

    // ---- 同步部分（调用线程，微秒级）：md5/sha1 用原文件真实值 + 取池 ----
    QCryptographicHash md5Hash(QCryptographicHash::Md5);
    md5Hash.addData(data);
    stp->md5 = md5Hash.result().toHex();
    QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
    sha1Hash.addData(data);
    stp->sha1 = sha1Hash.result().toHex();
    QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
    md5_10mHash.addData(data.left(10 * 1024 * 1024));
    stp->md5_10m = md5_10mHash.result().toHex();

    stp->fromPool = usePool ? takeCosPutEntry(stp->entry) : false;

    auto fail = [stp, appid, onDone](const QString &msg) {
        if (!m_botClients.value(appid)) return;
        onDone(msg, 0, stp->md5, false, QString());
    };

    // ---- 4/5. finish → files（put 成功后）----
    auto afterPut = [this, appid, stp, onDone, fail]() {
        QQBotClient *c = m_botClients.value(appid);
        if (!c) return;

        // ---- 3.5 复用捷径（md 图片）：put 覆盖 cos 内容即完成 —— 省掉 finish + files 两个 API，
        // 直接用首次返回的 raw_url 加时间戳（链接实时反映本次 put 的数据）----
        if (stp->fromPool && !stp->entry.fileInfo.isEmpty() && stp->entry.infoFileType == stp->fileType) {
            QString outurl = bustRawUrlCache(stp->entry.rawUrl);
            parkCosPutEntry(stp->entry);   // 暂存在本回调线程，等消息发送完成后回池（无 CD）
            onDone(stp->entry.fileInfo, stp->entry.expireAt / 1000, stp->md5, true, outurl);
            return;
        }

        QJsonObject finishJson;
        finishJson["upload_id"]  = stp->entry.uploadId;
        finishJson["part_index"] = stp->entry.partIndex;
        finishJson["block_size"] = stp->fileSize;
        finishJson["md5"]        = stp->md5;
        QString finishUrl = get_url(stp->targetType, stp->openid, "upload_part_finish");
        c->PostAsync(finishUrl, finishJson, QString(), 30000,
            [this, appid, stp, onDone, fail](const QString &finResp, QNetworkReply::NetworkError finErr) {
                QQBotClient *c2 = m_botClients.value(appid);
                if (!c2) return;
                QJsonObject filesJson;
                filesJson["upload_id"] = stp->entry.uploadId;
                QString filesUrl = get_url(stp->targetType, stp->openid, "files");
                c2->PostAsync(filesUrl, filesJson, QString(), 30000,
                    [this, appid, stp, onDone, fail](const QString &filesResp, QNetworkReply::NetworkError) {
                        QQBotClient *c3 = m_botClients.value(appid);
                        if (!c3) return;
                        if (filesResp.isEmpty()) { fail(QStringLiteral("files 注册无响应（网络超时）")); return; }
                        QJsonDocument doc = QJsonDocument::fromJson(filesResp.toUtf8());
                        if (doc.isNull()) { fail(filesResp.left(500)); return; }
                        QJsonObject filesObj = doc.object();
                        QString fileInfo = filesObj["file_info"].toString();
                        if (fileInfo.isEmpty()) { fail(filesResp); return; }   // 链接已移出池，自然丢弃

                        // ---- 6. 成功：链接暂存（存下 file_info/raw_url），消息发送完成后回池 ----
                        if (stp->usePool) {
                            stp->entry.fileInfo = fileInfo;
                            stp->entry.rawUrl   = filesObj["raw_url"].toString();
                            stp->entry.infoFileType = stp->fileType;   // 记使用类型，与阻塞版捷径防跨类型一致
                            parkCosPutEntry(stp->entry);   // 暂存在本回调线程，onDone 同线程可 takeCosPutPending 取走
                        }
                        QString outurl = bustRawUrlCache(filesObj["raw_url"].toString());
                        qint64 expire = QDateTime::currentSecsSinceEpoch() + filesObj["ttl"].toInt();
                        onDone(fileInfo, expire, stp->md5, true, outurl);
                    });
            });
    };

    // ---- 3. put 到 cos（put2 = NetManager::putAsync，含 cos 内网直连），3 次重试 ----
    auto tryPut = std::make_shared<std::function<void()>>();
    *tryPut = [this, appid, stp, afterPut, fail, tryPut]() {
        QQBotClient *c = m_botClients.value(appid);
        if (!c) return;
        c->put2(stp->entry.presignedUrl, stp->data, "application/octet-stream", stp->currentTimeout,
                [appid, stp, afterPut, fail, tryPut](const QString &putBody, QNetworkReply::NetworkError err) {
                    if (err == QNetworkReply::NoError) { afterPut(); return; }
                    stp->retry++;
                    if (stp->retry < 3) {
                        stp->currentTimeout += 10000;
                        if (m_botClients.value(appid)) (*tryPut)();   // 立即重试（回调线程无事件循环）
                    } else {
                        fail(QString("put 文件到 cos 失败(重试3次)"));
                    }
                });
    };

    // ---- 2. 池子没有 → upload_prepare（file_size 固定 100K，md5 用真实值）----
    if (stp->fromPool) {
        (*tryPut)();
        return;
    }
    QQBotClient *c = m_botClients.value(appid);
    if (!c) return;
    const qint64 APPLY_SIZE = 100 * 1024;    // 申请 100K，cos 实际可传任意大小
    QJsonObject prepJson;
    prepJson["file_type"] = fileType;
    prepJson["file_name"] = filename;
    prepJson["file_size"] = APPLY_SIZE;
    prepJson["md5"]       = stp->md5;
    prepJson["sha1"]      = stp->sha1;
    prepJson["md5_10m"]   = stp->md5_10m;
    QString prepUrl = get_url(targetType, openid, "upload_prepare");
    c->PostAsync(prepUrl, prepJson, QString(), 30000,
        [this, appid, stp, tryPut, fail](const QString &resp, QNetworkReply::NetworkError) {
            QQBotClient *c2 = m_botClients.value(appid);
            if (!c2) return;
            if (resp.isEmpty()) { fail(QStringLiteral("upload_prepare 无响应（网络超时）")); return; }
            QJsonDocument doc = QJsonDocument::fromJson(resp.toUtf8());
            if (doc.isNull()) { fail(resp.left(500)); return; }
            QJsonObject obj = doc.object();
            stp->entry.uploadId = obj["upload_id"].toString();
            if (stp->entry.uploadId.isEmpty()) { fail(resp); return; }
            QJsonArray parts = obj["parts"].toArray();
            if (parts.isEmpty()) { fail(QString("upload_prepare 未返回分片")); return; }
            QJsonObject part = parts[0].toObject();
            stp->entry.partIndex    = part["index"].toInt();
            stp->entry.presignedUrl = part["presigned_url"].toString();
            if (stp->entry.presignedUrl.isEmpty()) { fail(QString("upload_prepare 未返回 presigned_url")); return; }
            stp->entry.expireAt = QDateTime::currentMSecsSinceEpoch() + COS_POOL_TTL_MS;  // 55 分钟超时
            (*tryPut)();
        });
}


// ===========================================================================
// 上传的异步回调版
// 与同名同步函数逐段对应，只是把 PostSync → PostAsync、put → put2(回调)；
// 所有续写逻辑都经 onMedia() 回到媒体线程，所以整条链只有一条线程在跑，不用加锁。
// ===========================================================================

// 通用「POST /files 直到拿到 file_info」重试器。
// 语义：
//   · file_info 有值 → 成功；
//   · 平台说「超时」→ 再 POST（它在后台异步拉素材）；**最多 3 次**（每次 ≈5s，≈15s 上限）；
//   · 其它业务错误（下载失败 / 频率限制 / 参数错）→ **立刻失败，不重试**；
//   · 空响应 / 非 JSON（网络层）→ 也重试，同样最多 3 次。
//   ⚠ 以前是 10 次且「只要不是死文案就一直重试」→ 拉不到的链接会白卡 50 秒。
struct QQBotClient::FilesRegJob {
    int         appid      = 0;    // ⚠ 按值存下来：回调里靠它反查全局表，绝不回头解引用 this
    int         targetType = 0;
    QString     openid;
    QJsonObject body;
    int         timeoutMs = 30000;
    int         backoffMs = 0;     // >0 时按次递增退避（外链注册那条现在 = 0，不等待）
    int         left      = 3;     // 剩余尝试次数
    int         total     = 3;     // 初始次数（只用于日志「第 N/M 次」）
    QString     md5;               // 原样透传给 onDone
    MediaUploadDone onDone;
};

// 宽松 JSON 解析：平台回包偶尔夹隐藏字符（NUL / BOM / 代理页残留），裸 QJsonDocument::fromJson
// 会把整段判成 null —— 日志里看着是**完全合法的 JSON**，代码却走「非 JSON」分支，重试理由全错。
// 这里先剔 NUL，再截第一个 '{' 到最后一个 '}'，把这几种情况救回来。
// ⚠ 只用于「平台回包解析」，不要拿它做严格校验。
static bool parseJsonObjectLoose(const QString &response, QJsonObject &out)
{
    if (response.isEmpty()) return false;
    QString s = response;
    if (s.contains(QChar(0))) s.remove(QChar(0));          // NUL：qDebug 打出来看不见，fromJson 直接判死
    const int b = s.indexOf(QLatin1Char('{'));
    const int e = s.lastIndexOf(QLatin1Char('}'));
    if (b < 0 || e <= b) return false;                     // HTML / 纯文本网关页
    const QJsonDocument doc = QJsonDocument::fromJson(s.mid(b, e - b + 1).toUtf8());
    if (!doc.isObject()) return false;
    out = doc.object();
    return true;
}

void QQBotClient::filesRegStep(std::shared_ptr<FilesRegJob> job)
{
    if (!clientAlive(this, job->appid)) return;
    const QString url = get_url(job->targetType, job->openid, "files");

    PostAsync(url, job->body, QString(), job->timeoutMs,
        onMedia(this, job->appid, [this, job](const QString &response, QNetworkReply::NetworkError) {
            // 第几次 = 初始次数 - 剩余 + 1
            const int attempt = job->total - job->left + 1;
            // 退避：默认关闭（backoffMs = 0 → 失败立刻重发）。留着这个开关是为了以后想拉开间隔时
            // 只改 backoffMs 一处即可（>0 时按次递增：1.5s→3s→6s→12s）。
            const int waitMs = job->backoffMs > 0 ? (job->backoffMs << qMin(attempt - 1, 4)) : 0;
            // 重试一步：还有次数就再 POST 一次，用尽则把 msg 原样当失败文本交出去
            auto retryOrFail = [this, job, waitMs](const QString &msg) {
                if (job->left <= 1) {
                    job->onDone(msg, 0, job->md5, false, QString());
                    return;
                }
                job->left -= 1;
                if (waitMs > 0) delayOnMedia(waitMs, [this, job]() { filesRegStep(job); });
                else            filesRegStep(job);
            };

            // ⚠ 空 / 非 JSON 以前直接 `onDone(QString(),…)` → 上层拿到空串当正文发 → 被「内容不能为空」吞掉，
            //   用户那边就是「文件没发出来、日志也没有」。这里一律把**原始返回**当失败文本往上传。
            //   这两类属于「网络层没拿到有效响应」，重试有意义（次数 job->total=3）。
            if (response.isEmpty())
                { retryOrFail(QStringLiteral("文件注册无响应（网络超时）")); return; }

            // ⚠⚠ 这里**不能**再用裸 `QJsonDocument::fromJson(response.toUtf8())`：
            //   实测平台返回看着是完整 JSON（`{"message":"富媒体文件上传超时",...}`），却整段判成 null ——
            //   响应里夹了 NUL / BOM 之类的隐藏字符，一行歧义就被判「非 JSON」，
            //   于是永远走不到下面「平台说超时」的分支，重试理由全错。
            //   parseJsonObjectLoose 会先剔掉 NUL、再截第一个 '{' 到最后一个 '}'，能把这几种都救回来。
            QJsonObject r;
            if (!parseJsonObjectLoose(response, r)) {
                retryOrFail(response.left(500));   // 网关/代理回的 HTML 之类
                return;
            }
            const QString fileInfo = r["file_info"].toString();
            if (!fileInfo.isEmpty()) {
                job->onDone(fileInfo, QDateTime::currentSecsSinceEpoch() + r["ttl"].toInt(),
                            job->md5, true, r["raw_url"].toString());
                return;
            }
            // 平台回了业务错误 → **只有「超时」值得再 POST**：那是它在后台异步拉素材，立刻再打一次有时就能成。
            // ⚠ 其它一律立刻失败（以前是「只要不是明确业务码就一直重试」的思路，等于什么都重试）：
            //   · "富媒体文件下载失败"(40093007) = 它拉不到这个 URL（要鉴权的链接永远拉不到），
            //     重试 10 次结果一模一样，用户看到的就是「卡 50 秒然后失败」；
            //   · 频率限制 / 参数错 等业务码同理。
            const QString msg = r["message"].toString();
            if (msg.contains(QStringLiteral("超时")) && !msg.contains(QStringLiteral("下载失败")))
                { retryOrFail(response); return; }

            job->onDone(response, 0, job->md5, false, QString());   // 平台业务错误 → 直接失败
        }));
}

// uploadRichMedia_url 的异步版：POST /files 注册外链。
// 出参：result = file_info / 平台错误文本；expireTime 由 ttl 推算；md5、outurl 恒空（外链没有 raw_url）。
void QQBotClient::uploadRichMedia_urlAsync(int targetType, const QString &openid, int fileType,
                                           const QString &fileurl, MediaUploadDone onDone)
{
    if (!onDone) return;
    if (!fileurl.startsWith(QLatin1String("http"))) {
        onDone(QStringLiteral("外链格式不支持（必须以 http 开头）"), 0, QString(), false, QString());
        return;
    }
    // ⚠ QQ 自己的「文件接口」地址（api-bot.q.qq.com/v2/users/<openid>/files/...）**必须带 Authorization** 才能下，
    //   交给腾讯服务器去拉 → 它永远拉不到，只会一路回「富媒体文件下载失败 / 上传超时」。
    //   以前这种情况要白重试 10 次（≈50 秒）才报平台原文，现在直接给一句人话。
    if (fileurl.contains(QLatin1String("api-bot.q.qq.com")) && fileurl.contains(QLatin1String("/files"))) {
        onDone(QStringLiteral("这个链接是 QQ 的文件接口地址（需要鉴权），平台下载不了；请先把文件下载到本地再发"),
               0, QString(), false, QString());
        return;
    }

    auto job = std::make_shared<FilesRegJob>();
    job->appid      = m_info->appid_int;
    job->targetType = targetType;
    job->openid     = openid;
    // file_name 与同步版 uploadRichMedia_url 逐字对齐（那边一直带着 "网络文件.bin"，异步版以前漏了）
    job->body       = QJsonObject{{"file_type", fileType}, {"url", fileurl},
                                  {"file_name", QStringLiteral("网络文件.bin")}};
    job->timeoutMs  = 300000;
    // ⚠ 以前是 128ms —— 「富媒体文件上传超时」是平台**还在异步拉外链**，128ms 后立刻重打等于白打。
    //   现在按用户要求：**不加任何延迟**，失败就紧接着再 POST，共 3 次；3 次都超时就把平台原文交给上层。
    job->backoffMs  = 0;
    job->left       = 3;        // 最多 POST 3 次，不等待
    job->total      = 3;
    job->onDone     = onDone;
    filesRegStep(job);
}

// ── 本地文件多分片上传（uploadRichMedia 的异步版）──
struct QQBotClient::RichUploadJob {
    int     appid      = 0;          // ⚠ 按值存下来：回调里靠它反查全局表，绝不回头解引用 this
    int     targetType = 0;
    QString openid;
    QByteArray data;
    QString md5;
    QString uploadId;
    QString finishUrl;
    QList<QByteArray>  chunks;       // 各分片数据（按 parts 顺序切）
    QList<QString>     partUrls;     // 各分片的 presigned_url
    QList<QJsonObject> finishJsons;  // 各分片的 finish 请求体
    MediaUploadDone onDone;
    bool finished = false;           // 保证 onDone 只回调一次
    int  partIdx  = 0;               // 串行推进用（公网分支：put 游标）
    int  finishIdx = 0;              // 串行推进用（finish 游标，内网分支并发 put 完之后用）
    int  pending  = 0;               // 并发计数用（内网分支并发 put）
};

void QQBotClient::richUploadReport(std::shared_ptr<RichUploadJob> job, const QString &result,
                                   qint64 expireTime, bool ok, const QString &outurl)
{
    if (job->finished) return;
    job->finished = true;
    auto cb = job->onDone;
    if (cb) cb(result, expireTime, job->md5, ok, outurl);
}

void QQBotClient::richUploadPrepareDone(std::shared_ptr<RichUploadJob> job, const QString &response)
{
    if (!clientAlive(this, job->appid)) return;
    // ⚠ 空 / 非 JSON 一律把**原始返回**交出去（以前给空串 → 上层当正文发 → 被吞掉，用户什么也看不到）
    if (response.isEmpty()) { richUploadReport(job, QStringLiteral("upload_prepare 无响应（网络超时）"), 0, false, QString()); return; }
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    if (doc.isNull())       { richUploadReport(job, response.left(500), 0, false, QString()); return; }
    const QJsonObject r = doc.object();
    job->uploadId = r["upload_id"].toString();
    if (job->uploadId.isEmpty()) { richUploadReport(job, response, 0, false, QString()); return; }   // 错误信息

    // 切分片 + 备好每片的 finish 体（顺序、切片方式与同步版逐个对应）
    int start = 0;
    const QJsonArray parts = r["parts"].toArray();
    for (const QJsonValue &pv : parts) {
        const QJsonObject part = pv.toObject();
        const int blockSizeA = part["block_size"].toString().toInt();
        const QByteArray chunk = job->data.mid(start, blockSizeA);
        start += blockSizeA;
        job->chunks.append(chunk);
        job->partUrls.append(part["presigned_url"].toString());
        QJsonObject finishJson;
        finishJson["upload_id"]  = job->uploadId;
        finishJson["part_index"] = part["index"].toInt();
        finishJson["block_size"] = chunk.size();
        QCryptographicHash chunkMd5(QCryptographicHash::Md5);
        chunkMd5.addData(chunk);
        finishJson["md5"] = QString(chunkMd5.result().toHex());
        job->finishJsons.append(finishJson);
    }
    if (job->chunks.isEmpty()) { richUploadReport(job, QStringLiteral("upload_prepare 未返回分片"), 0, false, QString()); return; }

    if (g_neiw.isEmpty()) {
        // 公网：与同步版一致 —— 逐分片「put → finish」串行推进
        job->partIdx = 0;
        richUploadPutNext(job);
    } else {
        // 内网：与同步版一致 —— 先并发 put 全部分片，全部成功后再并发提交 finish
        job->pending = job->chunks.size();
        for (int i = 0; i < job->chunks.size(); ++i) {
            const QString partUrl = job->partUrls[i];
            const QByteArray chunk = job->chunks[i];
            putRetryAsync(job->appid, this,
                          [this, partUrl, chunk](int timeoutMs, Callback cb) {
                              put2(partUrl, chunk, QStringLiteral("application/octet-stream"), timeoutMs, cb);
                          },
                          [this, job, i](bool putOk) {
                              if (!putOk) {
                                  richUploadReport(job, QStringLiteral("分片%1重试多次失败")
                                                        .arg(job->finishJsons[i]["part_index"].toInt()), 0, false, QString());
                                  return;
                              }
                              if (--job->pending == 0) richUploadFinishAll(job);
                          });
        }
    }
}

// 串行分支：传第 partIdx 片 → 提交它 → 下一片
void QQBotClient::richUploadPutNext(std::shared_ptr<RichUploadJob> job)
{
    if (job->finished) return;
    if (job->partIdx >= job->chunks.size()) { richUploadFinishAll(job); return; }
    const int i = job->partIdx;
    const QString partUrl = job->partUrls[i];
    const QByteArray chunk = job->chunks[i];
    putRetryAsync(job->appid, this,
                  [this, partUrl, chunk](int timeoutMs, Callback cb) {
                      put2(partUrl, chunk, QStringLiteral("application/octet-stream"), timeoutMs, cb);
                  },
                  [this, job, i](bool putOk) {
                      if (!putOk) {
                          richUploadReport(job, QStringLiteral("在上传%1分片时重试多次失败")
                                                .arg(job->finishJsons[i]["part_index"].toInt()), 0, false, QString());
                          return;
                      }
                      PostAsync(job->finishUrl, job->finishJsons[i], QString(), 30000,
                          onMedia(this, job->appid, [this, job, i](const QString &resp, QNetworkReply::NetworkError err) {
                              if (job->finished) return;
                              // 与 richUploadFinishNext 一致：finish 失败就地断掉，别把错误拖到 /files 变成「上传超时」
                              if (err != QNetworkReply::NoError) {
                                  const int partIndex = job->finishJsons[i]["part_index"].toInt();
                                  richUploadReport(job,
                                                   resp.isEmpty()
                                                       ? QStringLiteral("分片%1确认失败（无响应）").arg(partIndex)
                                                       : resp.left(500),
                                                   0, false, QString());
                                  return;
                              }
                              job->partIdx += 1;
                              richUploadPutNext(job);
                          }));
                  });
}

// 所有 put 完成 → **逐片串行**提交 finish → 全部成功后才去 /files 注册
// ⚠ 平台要求 part_index 按顺序确认：上一片 finish 返回之后才能提交下一片。
//   以前这里是 for 循环把全部 finish 一次性 PostAsync 投出去（并发）→ 平台判「顺序错误」。
//   同步版（api2.cpp:399-403）就是逐个 PostSync、等返回再下一个，这里按同样语义改成回调链。
void QQBotClient::richUploadFinishAll(std::shared_ptr<RichUploadJob> job)
{
    if (job->finished) return;
    if (job->finishJsons.isEmpty()) { richUploadFinishAllFiles(job); return; }
    job->finishIdx = 0;
    richUploadFinishNext(job);          // 启动串行链
}

// 提交第 finishIdx 片 → 等它返回 → 再提交下一片；全部完成才进入 /files。
void QQBotClient::richUploadFinishNext(std::shared_ptr<RichUploadJob> job)
{
    if (job->finished) return;
    if (job->finishIdx >= job->finishJsons.size()) { richUploadFinishAllFiles(job); return; }
    const int i = job->finishIdx;
    PostAsync(job->finishUrl, job->finishJsons[i], QString(), 30000,
        onMedia(this, job->appid, [this, job, i](const QString &resp, QNetworkReply::NetworkError err) {
            if (job->finished) return;

            // finish 失败必须在这里断掉：继续走 /files 只会拿到「富媒体文件上传超时」这种查不到根因的错。
            if (err != QNetworkReply::NoError) {
                const int partIndex = job->finishJsons[i]["part_index"].toInt();
                richUploadReport(job,
                                 resp.isEmpty()
                                     ? QStringLiteral("分片%1确认失败（无响应）").arg(partIndex)
                                     : resp.left(500),
                                 0, false, QString());
                return;
            }
            job->finishIdx += 1;
            richUploadFinishNext(job);
        }));
}

void QQBotClient::richUploadFinishAllFiles(std::shared_ptr<RichUploadJob> job)
{
    if (job->finished) return;
    auto fr = std::make_shared<FilesRegJob>();
    fr->appid      = job->appid;
    fr->targetType = job->targetType;
    fr->openid     = job->openid;
    fr->body       = QJsonObject{{"upload_id", job->uploadId}};
    fr->timeoutMs  = 30000;
    fr->left       = 3;
    fr->total      = 3;
    fr->md5        = job->md5;
    fr->onDone     = [this, job](const QString &result, qint64 expire, const QString &, bool ok, const QString &outurl) {
        richUploadReport(job, result, expire, ok, outurl);
    };
    filesRegStep(fr);
}

void QQBotClient::uploadRichMediaAsync(int targetType, const QString &openid, int fileType,
                                       const QByteArray &data, const QString &filename,
                                       MediaUploadDone onDone)
{
    if (!onDone) return;
    const qint64 fileSize = data.size();

    // 2. 计算哈希值 —— upload_prepare 的 md5/sha1/md5_10m 是可选参数，不传也能正常上传，
    //    所以由全局开关 g_upload_calc_hash 控制（默认关）。腾讯并不校验/采信我们传的 md5
    //    （它自己算），小视频也不例外 —— 关着时连整文件 md5 都不算。
    QString md5, sha1, md5_10m;
    if (g_upload_calc_hash) {
        QCryptographicHash md5Hash(QCryptographicHash::Md5);
        md5Hash.addData(data);
        md5 = md5Hash.result().toHex();
        QCryptographicHash sha1Hash(QCryptographicHash::Sha1);
        sha1Hash.addData(data);
        sha1 = sha1Hash.result().toHex();
        QCryptographicHash md5_10mHash(QCryptographicHash::Md5);
        md5_10mHash.addData(data.left(10 * 1024 * 1024));
        md5_10m = md5_10mHash.result().toHex();
    }
    QString name = filename;
    if(name.isEmpty())
        name="文件.bin";
    // 视频分两路（与同步版一致）：≤80M 走快传；>80M 当文件（file_type=4）走分片
    if (fileType == 2) {
        if (fileSize <= 80LL * 1024 * 1024) {
            uploadSmallVideoAsync(targetType, openid, data, name, md5, sha1, md5_10m, onDone);
            return;
        }
        fileType = 4;
    }

    // 3. prepare：声明真实大小（有多大传多大）
    QJsonObject prepJson;
    prepJson["file_type"] = fileType;
    prepJson["file_name"] = name;
    prepJson["file_size"] = (qint64)fileSize;
    // 开关关着 → 三个摘要参数整组不传（平台当没给，照常上传）
    if (g_upload_calc_hash) {
        prepJson["md5"]     = md5;
        prepJson["sha1"]    = sha1;
        prepJson["md5_10m"] = md5_10m;
    }

    auto job = std::make_shared<RichUploadJob>();
    job->appid      = m_info->appid_int;
    job->targetType = targetType;
    job->openid     = openid;
    job->data       = data;
    job->md5        = md5;
    job->finishUrl  = get_url(targetType, openid, "upload_part_finish");
    job->onDone     = onDone;

    PostAsync(get_url(targetType, openid, "upload_prepare"), prepJson, QString(), 30000,
        onMedia(this, job->appid, [this, job](const QString &response, QNetworkReply::NetworkError) {
            richUploadPrepareDone(job, response);
        }));
}

// ── ≤80M 视频快传（uploadSmallVideo 的异步版）──
struct QQBotClient::SmallVideoJob {
    int     appid      = 0;          // ⚠ 按值存下来：回调里靠它反查全局表，绝不回头解引用 this
    int     targetType = 0;
    QString openid;
    QByteArray data;
    QString md5;
    QString uploadId;
    QString finishUrl;
    int     partIndex = 0;
    MediaUploadDone onDone;
    bool finished = false;
};

// 入口：prepare 固定申请 1K（返回 1 个分片 + put 链接）→ 整段写入 → 提交 → files 注册
void QQBotClient::uploadSmallVideoAsync(int targetType, const QString &openid, const QByteArray &data,
                                        const QString &filename, const QString &md5, const QString &sha1,
                                        const QString &md5_10m, MediaUploadDone onDone)
{
    if (!onDone) return;

    QJsonObject prepJson;
    prepJson["file_type"] = 2;
    prepJson["file_name"] = filename;
    prepJson["file_size"] = (qint64)data.size();
    // 摘要参数按全局开关传（默认不传）；下面 part_finish 同理，md5 空着就不塞这个字段
    if (g_upload_calc_hash) {
        prepJson["md5"]     = md5;
        prepJson["sha1"]    = sha1;
        prepJson["md5_10m"] = md5_10m;
    }

    auto job = std::make_shared<SmallVideoJob>();
    job->appid      = m_info->appid_int;
    job->targetType = targetType;
    job->openid     = openid;
    job->data       = data;
    job->md5        = md5;
    job->finishUrl  = get_url(targetType, openid, "upload_part_finish");
    job->onDone     = onDone;

    PostAsync(get_url(targetType, openid, "upload_prepare"), prepJson, QString(), 30000,
        onMedia(this, job->appid, [this, job](const QString &response, QNetworkReply::NetworkError) {
            smallVideoPrepareDone(job, response);
        }));
}

void QQBotClient::smallVideoReport(std::shared_ptr<SmallVideoJob> job, const QString &result,
                                   qint64 expireTime, bool ok, const QString &outurl)
{
    if (job->finished) return;
    job->finished = true;
    auto cb = job->onDone;
    if (cb) cb(result, expireTime, job->md5, ok, outurl);
}

void QQBotClient::smallVideoPrepareDone(std::shared_ptr<SmallVideoJob> job, const QString &response)
{
    if (!clientAlive(this, job->appid)) return;
    // 同 richUploadPrepareDone：空 / 非 JSON 也要把原始返回带出去，不能吞成空串
    if (response.isEmpty()) { smallVideoReport(job, QStringLiteral("upload_prepare 无响应（网络超时）"), 0, false, QString()); return; }
    QJsonDocument doc = QJsonDocument::fromJson(response.toUtf8());
    if (doc.isNull())       { smallVideoReport(job, response.left(500), 0, false, QString()); return; }
    const QJsonObject r = doc.object();
    job->uploadId = r["upload_id"].toString();
    if (job->uploadId.isEmpty()) { smallVideoReport(job, response, 0, false, QString()); return; }   // 错误信息

    const QJsonArray parts = r["parts"].toArray();
    if (parts.isEmpty()) { smallVideoReport(job, QStringLiteral("upload_prepare 未返回分片"), 0, false, QString()); return; }
    const QJsonObject part = parts[0].toObject();
    const QString presignedUrl = part["presigned_url"].toString();
    if (presignedUrl.isEmpty()) { smallVideoReport(job, QStringLiteral("upload_prepare 未返回 presigned_url"), 0, false, QString()); return; }
    job->partIndex = part["index"].toInt();

    // 2. 整段视频写入 put 链接（重试 3 次，退避 1s/2s）
    const QByteArray data = job->data;
    putRetryAsync(job->appid, this,
                  [this, presignedUrl, data](int timeoutMs, Callback cb) {
                      put2(presignedUrl, data, QStringLiteral("application/octet-stream"), timeoutMs, cb);
                  },
                  [this, job](bool putOk) {
                      if (!putOk) { smallVideoReport(job, QStringLiteral("put 视频数据失败(重试3次)"), 0, false, QString()); return; }
                      smallVideoSubmit(job);
                  });
}

void QQBotClient::smallVideoSubmit(std::shared_ptr<SmallVideoJob> job)
{
    if (job->finished) return;
    // 3. 按真实数据提交
    QJsonObject finJson;
    finJson["upload_id"]  = job->uploadId;
    finJson["part_index"] = job->partIndex;
    finJson["block_size"] = job->data.size();
    // md5 是「分片摘要」，但腾讯不采信我们给的值（它自己算）→ 开关关着时不传；
    // 空串别硬塞（格式非法），直接省掉这个字段
    if (!job->md5.isEmpty())
        finJson["md5"] = job->md5;
    PostAsync(job->finishUrl, finJson, QString(), 30000,
        onMedia(this, job->appid, [this, job](const QString &, QNetworkReply::NetworkError) {
            auto fr = std::make_shared<FilesRegJob>();
            fr->appid      = job->appid;
            fr->targetType = job->targetType;
            fr->openid     = job->openid;
            fr->body       = QJsonObject{{"upload_id", job->uploadId}};
            fr->timeoutMs  = 30000;
            fr->left       = 3;
            fr->total      = 3;
            fr->md5        = job->md5;
            fr->onDone     = [this, job](const QString &result, qint64 expire, const QString &, bool ok, const QString &outurl) {
                smallVideoReport(job, result, expire, ok, outurl);
            };
            filesRegStep(fr);
        }));
}

// uploadRichMediaPoolA 的异步版：与同步版同款分流 —— URL 走注册，本地文件读进内存后交给异步上传器。
// 纯转发：底层三个上传器（uploadRichMediaPoolAsync / uploadRichMediaAsync / uploadRichMedia_urlAsync）
// 回的本来就是**裸 file_info**（失败时是平台报错文本），直接透传即可 —— 以前这里还要补一层
// "[<类型>,path=<file_info>,md5=<md5>,Time=<过期秒>]" 包装，现在整条链只认裸 file_info，包装去掉了。
// 过期时间 / md5 由回调一并带出，上传缓存直接用它写 "<过期秒>||||…"。
//   ⚠ 失败时把平台报错原样交出去（与同步版 `return info;` 一致）——
//     远程音频超时长的判定就靠这个裸报错里的 "40093013"。
void QQBotClient::uploadRichMediaPoolAAsync(int targetType, const QString &openid, int fileType,
                                            const QString &filePath, bool usePool, MediaUploadDone onDone)
{
    if (!onDone) return;

    if (filePath.startsWith(QLatin1String("http"))) {
        uploadRichMedia_urlAsync(targetType, openid, fileType, filePath, onDone);
        return;
    }
    // ⚠ 这两条以前回调的是**空串**（ok=false, result=""）→ 上层拿空串当报错去发消息，
    //   send_* 判定「正文为空」直接吞掉，整条链就静默结束（用户：文件没发出去、日志也没有）。
    //   这里必须给出非空原因，上层才会把「发送失败」的提示发出来。
    if (!QFileInfo::exists(filePath)) {
        onDone(QStringLiteral("本地文件不存在"), 0, QString(), false, QString());
        return;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        onDone(QStringLiteral("本地文件读取失败"), 0, QString(), false, QString());
        return;
    }
    const QByteArray data = file.readAll();
    file.close();
    const QString filename = QFileInfo(filePath).fileName();
    // 池子只服务图片（targetType==4 不支持 100K 申请流程）；其余走原始分片上传的异步版
    if (usePool && targetType != 4) {
        uploadRichMediaPoolAsync(targetType, openid, fileType, data, filename, true, onDone);
        return;
    }

    uploadRichMediaAsync(targetType, openid, fileType, data, filename, onDone);
}

// ---------------------------------------------------------------------------
// 定位 ffmpeg 可执行文件 —— 音频链路的三个调用点共用（转 m4a / 只读探时长 / m4a 流复制切段）
//
// 查找顺序（第一个命中就用），三条路都命中不了时返回裸名字、交给 QProcess 走 PATH：
//   ① 设置界面里配的目录（g_config["ffmpeg"]，Windows 默认 "ffmpeg/"）
//   ② 主程序自己所在目录 —— **随包携带的那份放这儿**
//      Windows：和 qiancao.exe 同目录的 ffmpeg.exe
//      Linux  ：和 AppRun 里 $HERE/qiancao 同目录的 ffmpeg
//      ⚠ Linux 的 AppRun 只设 LD_LIBRARY_PATH / PYTHONHOME / PYTHONPATH，**没有改 PATH**，
//        而安装目录是 $HOME/qiancao（不在 PATH 里）—— 所以裸名字 "ffmpeg" 找不到
//        Gitee 补全包里的那份，必须靠这条命中。
//   ③ 裸名字 → 系统 PATH：用户自己 apt/dnf install ffmpeg 的场景，保持原行为不变
//
// ⚠ ② 命中的文件如果丢了执行位（从 tar 包里解出来常见），这里顺手补一下 ——
//   否则 QProcess 会以 "Permission denied" 静默失败，日志里只看到「ffmpeg 启动失败」。
// ---------------------------------------------------------------------------
static QString findFfmpegPath()
{
#ifdef Q_OS_WIN
    const QString kExe = QStringLiteral("ffmpeg.exe");
#else
    const QString kExe = QStringLiteral("ffmpeg");
#endif

    // ① 设置里配的目录
    if (!ffmpegdiv.isEmpty()) {
        const QString p = QDir(ffmpegdiv).filePath(kExe);
        if (QFileInfo(p).isFile())
            return p;
    }

    // ② 主程序同目录（AppImage / 安装目录 / exe 旁边）
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QString beside = QDir(appDir).filePath(kExe);
        const QFileInfo bi(beside);
        if (bi.isFile()) {
#ifndef Q_OS_WIN
            if (!bi.isExecutable())
                QFile::setPermissions(beside, bi.permissions()
                                      | QFileDevice::ExeOwner
                                      | QFileDevice::ExeGroup
                                      | QFileDevice::ExeOther);
#endif
            return beside;
        }
    }

    // ③ 交给系统 PATH
    return kExe;
}

// 音频转换产物的统一路径：**源文件同目录下的 tmp/ 子目录**，文件名沿用源名再补 .m4a
//   → /x/y/歌曲.mp3  ⇒  /x/y/tmp/歌曲.mp3.m4a
// 为什么不放源文件旁边（老写法 srcFilePath + ".m4a"）：
//   ui/chatpage.cpp 的「选择音频」文件对话框过滤器里有 *.m4a，产物放在同目录会被一起枚举出来，
//   用户每转一次就多看见一个中间产物。放进子目录后 QFileDialog 不展开子目录，就看不见了。
//   （文件名保留原后缀，顺带保证不同源文件不会互相覆盖）
// ⚠ 「产物路径」这个约定只有这一个出处：转换、复用检查、切段三处都必须走它，
//   否则复用失效（每次都重转），或者二次拼接出 <源>.m4a.m4a。
// ⚠ 返回空串 = 「这条链路不适用」：source 是 http URL（远程音频直传）或路径为空。
// ⚠ 这些产物**不做清理**（2026-10-03 拍板）：源音频可能在任意目录，程序压根不知道用户的音频文件
//   都放在哪，更不该替用户去删东西；`tmp/` 攒多了让用户自己删。别自作聪明加清理逻辑。
static QString audioM4aPathFor(const QString &srcFilePath)
{
    if (srcFilePath.isEmpty()
        || srcFilePath.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        return {};
    const QFileInfo fi(srcFilePath);
    return fi.absoluteDir().filePath(QStringLiteral("tmp/") + fi.fileName()
                                     + QStringLiteral(".m4a"));
}

QString convertAudioToSilk(const QString &srcFilePath)
{
    if (!QFile::exists(srcFilePath)) {
        //qWarning() << "源文件不存在:" << srcFilePath;
        return {};
    }

    // 去掉“小于1MB直接返回”的捷径（防止视频体积小但无音频的情况）
    // 无论大小，都走转换流程，确保输出格式统一

    QString ffmpegPath = findFfmpegPath();

    // 产物放 <源目录>/tmp/<源名>.m4a（见 audioM4aPathFor 的注释）
    const QString outputFilePath = audioM4aPathFor(srcFilePath);
    if (outputFilePath.isEmpty())          // 非本地文件（http URL）→ 不适用，原样发
        return srcFilePath;

    // ⚠ ffmpeg **不会**自己创建输出目录（实测：`Error opening output nodir/out.m4a:
    //   No such file or directory`，退出码 127，目录也不会被建出来）。少了这句 mkpath，
    //   就会静默走到下面的「exitCode() != 0 → 返回源路径」分支，表现成「音频怎么都压不下来」。
    if (!QDir().mkpath(QFileInfo(outputFilePath).absolutePath())) {
        AppendEventLog("音频转换目录创建失败: " + QFileInfo(outputFilePath).absolutePath());
        return srcFilePath;
    }

    // ① 进程内 DLL（首选）：24kHz / 单声道 / 32kbps AAC —— 与下面 ffmpeg.exe 的参数完全等价。
    //    DLL 里视频轨是整体跳过的（等价 -vn），所以「视频提取音频」也走这条。
    if (QcFfmpeg::load(nullptr)) {
        QString err;
        if (QcFfmpeg::convert(srcFilePath, outputFilePath, 24000, 1, 32, &err))
            return outputFilePath;

        // 「没有音轨」不是错误：按原文件返回（与旧行为一致，免得刷一条假报错）
        if (!err.contains(QStringLiteral("没有可用的音频轨")))
            AppendEventLog("ffmpeg.dll 转换失败:" + err);
        return srcFilePath;
    }

    QStringList ffmpegArgs = {
        "-y",                      // 覆盖输出
        "-i", srcFilePath,         // 输入（支持视频/音频）
        "-map", "0:a:0?",          // 【核心】明确取第一个音频轨，若无音频则跳过不报错
        "-vn",                     // 剔除视频画面
        "-c:a", "aac",             // 音频编码AAC
        "-b:a", "32k",             // 码率
        "-ar", "24000",            // 采样率
        "-ac", "1",                // 单声道
        outputFilePath
    };

    QProcess ffmpeg;
    ffmpeg.start(ffmpegPath, ffmpegArgs);

    if (!ffmpeg.waitForStarted()) {
        AppendEventLog("ffmpeg 启动失败");
        return srcFilePath;
    }

    // ⚠ 2026-10-02：转码现在全靠这一步（不再有进程内兜底），超时给宽一些 ——
    //   超时会被当作「转换失败」按原文件发送，而超长音频原样发大概率被 QQ 的 4:59 上限拒绝。
    if (!ffmpeg.waitForFinished(120000)) {
        AppendEventLog("ffmpeg 超时");
        ffmpeg.kill();
        return srcFilePath;
    }

    // 检查执行结果
    if (ffmpeg.exitCode() != 0) {
        QString err = ffmpeg.readAllStandardError();
        // 如果是“没有音频流”，这不是错误，按原文件返回即可
        if (!err.contains("Output file does not contain any stream")) {
            AppendEventLog("ffmpeg 转换失败:" + err);
        }
        return srcFilePath;
    }

    // 成功且生成文件
    return outputFilePath;
}


static const int AUDIO_SEG_MAX_SEC = 298;

static const qint64 kAudioSkipConvertBytes = 1024 * 1024;


static bool isCompactAudioContainer(const QString &suffix)
{
    static const QStringList kList = {
        QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("amr"),
        QStringLiteral("silk"), QStringLiteral("ogg"), QStringLiteral("aac"),
    };
    return kList.contains(suffix.toLower());
}

// 明确的视频后缀 —— 不可能是纯音频，直接判，连文件都不用打开（省一次 ffmpeg）
static bool hasDefiniteVideoExtension(const QString &suffix)
{
    static const QStringList kList = {
        QStringLiteral("avi"),  QStringLiteral("mkv"),  QStringLiteral("flv"),
        QStringLiteral("webm"), QStringLiteral("ts"),   QStringLiteral("m2ts"),
        QStringLiteral("mts"),  QStringLiteral("wmv"),  QStringLiteral("asf"),
        QStringLiteral("rm"),   QStringLiteral("rmvb"), QStringLiteral("mpg"),
        QStringLiteral("mpeg"), QStringLiteral("vob"),  QStringLiteral("ogv"),
        QStringLiteral("m4v"),  QStringLiteral("f4v"),
    };
    return kList.contains(suffix.toLower());
}

// 一次 `ffmpeg -i` 同时拿到「时长」与「容器里有没有视频轨」—— 两者都在 stderr 的媒体信息里。
// ⚠ 不给输出文件时 ffmpeg 会以非 0 退出，但信息照常打印完，不影响解析。
// 只对「体积够小、后缀又看不出是视频」的文件调用，所以这点进程开销只落在少数路径上。
struct MediaProbe
{
    double durationSec = -1;    // <0 = 未知（调用方按「不超长」处理）
    bool   hasVideo    = false;
};

static MediaProbe probeMediaInfo(const QString &filePath)
{
    MediaProbe info;

    // ① 进程内 DLL：直接读容器，没有进程启动开销（探测本来就只有几十毫秒，
    //    以前 fork 一个 ffmpeg.exe 比真正干活还贵）。
    if (QcFfmpeg::load(nullptr)) {
        const QcFfmpeg::Probe p = QcFfmpeg::probe(filePath);
        if (p.ok) {
            info.durationSec = (p.ms > 0) ? p.ms / 1000.0 : -1.0;
            info.hasVideo    = p.hasVideo;
        }
        return info;      // 读不出来（silk / 损坏文件）→ 照样是「未知」，不必再起 exe
    }

    // ② 没有 DLL → 外部 ffmpeg.exe（老链路，逻辑一个字没动）
    QProcess p;
    p.start(findFfmpegPath(), {"-i", filePath});
    if (!p.waitForStarted())
        return info;
    if (!p.waitForFinished(10000)) {
        p.kill();
        return info;
    }
    const QString err = QString::fromLocal8Bit(p.readAllStandardError());

    static QRegularExpression reDur("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)");
    const auto m = reDur.match(err);
    if (m.hasMatch())
        info.durationSec = m.captured(1).toInt() * 3600
                         + m.captured(2).toInt() * 60
                         + m.captured(3).toDouble();

    // 形如 "  Stream #0:0[0x1](und): Video: h264 (High), yuv420p, ..."
    // （音频轨是 "Audio:"、字幕轨是 "Subtitle:"，不会误命中）
    static QRegularExpression reVid(QStringLiteral("Stream #\\d+:\\d+.*:\\s*Video:"));
    info.hasVideo = reVid.match(err).hasMatch();
    return info;
}

static double probeAudioDurationSec(const QString &filePath);   // 定义在下面，audioCanSendAsIs 要用

// 这个音频是否可以直接原样发送（已是小体积容器，或本身就不大）
// ⚠ 视频例外：视频当音频发时必须先提取音轨转码，哪怕只有几百 KB 也不能原样直传 ——
//   否则发给 file_type=3 的是个视频容器而不是语音。
static bool audioCanSendAsIs(const QString &path)
{
    QString p = path;
    const int cut = p.indexOf(QLatin1Char('?'));   // URL 可能带 query，别把它算进后缀
    if (cut >= 0) p.truncate(cut);
    const QFileInfo fi(p);

    const qint64 sz = fi.size();                   // 不存在 / URL → 0，不会误判成「小」
    if (sz <= 0 || sz >= kAudioSkipConvertBytes)
        return false;                              // 体积偏大 → 交给转换流程决定要不要切段

    // 体积够小（<1MB）才值得往下判：
    //   ① 紧凑容器（opus/m4a/amr/silk/ogg/aac）：体积小不代表时长短（低码率能拖很久），
    //      所以仍然探一次时长，但不必判视频轨 —— 这些容器里不可能有画面
    //   ② 明确的视频后缀：必须先提音轨转码 → 不探内容，直接进转换
    //   ③ 其他（mp3/wav/flac/mp4/mov/3gp…）：探「有没有视频轨」+「有没有超长」
    //      （.mp4/.mov/.3gp 光看后缀分不出音视频，m4a 也是 mp4 容器）
    // ⚠ SILK 是 QQ 自己的语音格式，ffmpeg 完全不认（连时长都读不出来）→ 探了也是白搭
    //   一次进程启动。语音消息本身都很短，直接原样发。
    if (fi.suffix().compare(QLatin1String("silk"), Qt::CaseInsensitive) == 0)
        return true;

    if (isCompactAudioContainer(fi.suffix()))
        return probeAudioDurationSec(p) <= AUDIO_SEG_MAX_SEC;
    if (hasDefiniteVideoExtension(fi.suffix()))
        return false;

    const MediaProbe mi = probeMediaInfo(p);
    return !mi.hasVideo && mi.durationSec <= AUDIO_SEG_MAX_SEC;
}

// 探测媒体时长（秒）；失败返回 -1。用于「转换产物要不要切段」以及上面的超长判断。
static double probeAudioDurationSec(const QString &filePath)
{
    return probeMediaInfo(filePath).durationSec;
}

// 切段后不足 10 秒的尾巴直接丢弃不发（只有最后一段可能偏短，前面几段都是满的 segSec）。
// 探测失败（<=0）时保守保留 —— 宁可多发一段，也别把用户最后那截语音吞掉。
static const int AUDIO_SEG_MIN_SEC = 10;

static QStringList dropShortAudioTail(const QStringList &segs)
{
    if (segs.size() < 2)
        return segs;                        // 只剩一段就没有「尾巴」可言，原样返回
    const double tailSec = probeAudioDurationSec(segs.last());
    if (tailSec <= 0 || tailSec >= AUDIO_SEG_MIN_SEC)
        return segs;

    QStringList out = segs;
    out.removeLast();
    AppendEventLog(QStringLiteral("音频切段：结尾仅 %1 秒（不足 %2 秒），已丢弃不发")
                       .arg(tailSec, 0, 'f', 1).arg(AUDIO_SEG_MIN_SEC));
    return out;
}

// 把 m4a 按 segSec 秒流复制切段（不重编码），段文件名 = 原名去 .m4a + _seg000.m4a ...
// ⚠ tmp/ 目录就是专门放这些产物的：**只要已有 _seg*.m4a 就直接用，绝不再起 ffmpeg，也绝不删**
//   （哪怕只切出 1 段 —— 那也是「整段音频的流复制产物」，直接上传即可；删了下次又要重切一遍）。
// 返回值里已丢掉不足 AUDIO_SEG_MIN_SEC 的结尾段（只有 ≥2 段时才可能丢）。
static QStringList splitM4aSegments(const QString &m4aPath, int segSec)
{
    if (!m4aPath.endsWith(".m4a"))
        return {};                       // 只切转换产物；转换失败兜底的原文件不动
    const QString base = m4aPath.left(m4aPath.size() - 4);
    const QString filter = QFileInfo(base).fileName() + "_seg*.m4a";

    QDir dir = QFileInfo(m4aPath).absoluteDir();
    QStringList out;
    const QStringList exist = dir.entryList(QStringList{filter}, QDir::Files, QDir::Name);
    if (!exist.isEmpty()) {              // 之前切过 → 直接复用产物，不起进程
        for (const QString &f : exist)
            out << dir.filePath(f);
        return dropShortAudioTail(out);  // 复用旧产物时同样丢掉不足 10 秒的尾巴
    }

    // ① 进程内 DLL（首选）：流复制切段，不起进程；段名规则与下面 ffmpeg 完全一致
    //    （<base> 是「去掉最后一级扩展名」的基名，所以 歌曲.mp3.m4a → 歌曲.mp3_seg000.m4a）。
    if (QcFfmpeg::load(nullptr)) {
        QString err;
        const QStringList segs = QcFfmpeg::split(m4aPath, dir.absolutePath(), segSec,
                                                QStringLiteral("m4a"), &err);
        if (segs.isEmpty()) {
            if (!err.isEmpty())
                AppendEventLog("ffmpeg.dll 切段失败:" + err);
            return {};                   // 切不出来 → 调用方按整文件发（与旧行为一致）
        }
        return dropShortAudioTail(segs);
    }

    // ② 没有 DLL → 外部 ffmpeg.exe（老链路）
    QString ffmpegPath = findFfmpegPath();
    QProcess p;
    p.start(ffmpegPath, {
        "-y", "-i", m4aPath,
        "-c", "copy",                    // AAC 直接流复制，秒级完成
        "-f", "segment",
        "-segment_time", QString::number(segSec),
        "-reset_timestamps", "1",
        base + "_seg%03d.m4a"
    });
    if (!p.waitForStarted())
        return {};
    if (!p.waitForFinished(60000)) {
        p.kill();
        return {};
    }
    if (p.exitCode() != 0)
        return {};

    const QStringList files = dir.entryList(QStringList{filter}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        return {};                       // 一段都没切出来（ffmpeg 失败）→ 空表，调用方按整文件发
    for (const QString &f : files)
        out << dir.filePath(f);
    return dropShortAudioTail(out);
}

// ===========================================================================
// ffmpeg 三个链路的**异步回调版** —— 逻辑与上面的同步版逐个对应。
//
// 现在的主路是**进程内 ffmpeg.dll**：阻塞的那一小段被 runOnFfmpegDll 丢到
// QcFfmpeg 的单线程池，跑完再跳回媒体线程 —— 媒体线程仍然零阻塞（和以前
// 起 QProcess 的效果一样，但少了一次进程创建 + 参数解析）。
// DLL 补不下来时才退回 runProcessAsync + 外部 ffmpeg.exe。
// ===========================================================================

// 探测：时长 + 容器里有没有视频轨（同 probeMediaInfo）
static void probeMediaInfoAsync(const QString &filePath, std::function<void(MediaProbe)> onDone)
{
    if (!onDone) return;

    // 退路：解析 `ffmpeg -i` 的 stderr
    auto viaExe = [filePath, onDone]() {
        runProcessAsync(findFfmpegPath(), {"-i", filePath}, 10000,
            [onDone](int, const QByteArray &errBytes, bool) {
                MediaProbe info;
                const QString err = QString::fromLocal8Bit(errBytes);
                static QRegularExpression reDur("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)");
                const auto m = reDur.match(err);
                if (m.hasMatch())
                    info.durationSec = m.captured(1).toInt() * 3600
                                     + m.captured(2).toInt() * 60
                                     + m.captured(3).toDouble();
                // 形如 "  Stream #0:0[0x1](und): Video: h264 (High), yuv420p, ..."
                // （音频轨是 "Audio:"、字幕轨是 "Subtitle:"，不会误命中）
                static QRegularExpression reVid(QStringLiteral("Stream #\\d+:\\d+.*:\\s*Video:"));
                info.hasVideo = reVid.match(err).hasMatch();
                onDone(info);
            });
    };

    auto out = std::make_shared<MediaProbe>();
    runOnFfmpegDll(
        [filePath, out]() {
            const QcFfmpeg::Probe p = QcFfmpeg::probe(filePath);
            if (!p.ok) return;                       // 读不出来 → 保持「未知（-1）」
            out->durationSec = (p.ms > 0) ? p.ms / 1000.0 : -1.0;
            out->hasVideo    = p.hasVideo;
        },
        [out, onDone]() { onDone(*out); },
        viaExe);
}

// 探测媒体时长（秒）；失败返回 -1（同 probeAudioDurationSec）
static void probeAudioDurationSecAsync(const QString &filePath, std::function<void(double)> onDone)
{
    probeMediaInfoAsync(filePath, [onDone](MediaProbe mi) { onDone(mi.durationSec); });
}

// 这个音频能否原样发送（同 audioCanSendAsIs）。
// 能一眼判定的分支（体积不合适 / silk / 明确视频后缀）直接同步回调，不白起一次 ffmpeg 进程。
static void audioCanSendAsIsAsync(const QString &path, std::function<void(bool)> onDone)
{
    QString p = path;
    const int cut = p.indexOf(QLatin1Char('?'));   // URL 可能带 query，别把它算进后缀
    if (cut >= 0) p.truncate(cut);
    const QFileInfo fi(p);

    const qint64 sz = fi.size();                   // 不存在 / URL → 0，不会误判成「小」
    if (sz <= 0 || sz >= kAudioSkipConvertBytes) { onDone(false); return; }

    // SILK 是 QQ 自己的语音格式，ffmpeg 完全不认（连时长都读不出来）→ 直接原样发
    if (fi.suffix().compare(QLatin1String("silk"), Qt::CaseInsensitive) == 0) { onDone(true); return; }

    if (isCompactAudioContainer(fi.suffix())) {
        // 紧凑容器里不可能有画面，只判时长（低码率小体积也能拖很久）
        probeAudioDurationSecAsync(p, [onDone](double sec) { onDone(sec <= AUDIO_SEG_MAX_SEC); });
        return;
    }
    if (hasDefiniteVideoExtension(fi.suffix())) { onDone(false); return; }   // 视频当音频发必须先转码

    probeMediaInfoAsync(p, [onDone](MediaProbe mi) {
        onDone(!mi.hasVideo && mi.durationSec <= AUDIO_SEG_MAX_SEC);
    });
}

// 转成 32k 单声道 m4a（同 convertAudioToSilk）；失败/超时一律回调「源路径本身」，不把路径改坏
static void convertAudioToSilkAsync(const QString &srcFilePath, std::function<void(QString)> onDone)
{
    if (!onDone) return;
    if (!QFile::exists(srcFilePath)) { onDone(QString()); return; }

    const QString outputFilePath = audioM4aPathFor(srcFilePath);
    if (outputFilePath.isEmpty()) { onDone(srcFilePath); return; }   // 非本地文件（http URL）→ 不适用

    // ⚠ ffmpeg 不会自己创建输出目录（退出码 127），少了这句会静默走到「失败 → 原样发」
    if (!QDir().mkpath(QFileInfo(outputFilePath).absolutePath())) {
        AppendEventLog("音频转换目录创建失败: " + QFileInfo(outputFilePath).absolutePath());
        onDone(srcFilePath);
        return;
    }

    // 退路：外部 ffmpeg.exe（DLL 缺失且补包失败时才走）
    auto viaExe = [srcFilePath, outputFilePath, onDone]() {
        const QString ffmpegPath = findFfmpegPath();
        QStringList ffmpegArgs = {
            "-y",                      // 覆盖输出
            "-i", srcFilePath,         // 输入（支持视频/音频）
            "-map", "0:a:0?",          // 明确取第一个音频轨，若无音频则跳过不报错
            "-vn",                     // 剔除视频画面
            "-c:a", "aac",
            "-b:a", "32k",
            "-ar", "24000",
            "-ac", "1",
            outputFilePath
        };

        runProcessAsync(ffmpegPath, ffmpegArgs, 120000,
            [srcFilePath, outputFilePath, onDone](int code, const QByteArray &errBytes, bool timedOut) {
                if (timedOut) { AppendEventLog("ffmpeg 超时"); onDone(srcFilePath); return; }
                if (code < 0) { AppendEventLog("ffmpeg 启动失败"); onDone(srcFilePath); return; }
                if (code != 0) {
                    const QString err = QString::fromLocal8Bit(errBytes);
                    // 「没有音频流」不算错误：按原文件返回即可
                    if (!err.contains("Output file does not contain any stream"))
                        AppendEventLog("ffmpeg 转换失败:" + err);
                    onDone(srcFilePath);
                    return;
                }
                onDone(outputFilePath);
            });
    };

    auto okFlag  = std::make_shared<bool>(false);
    auto errText = std::make_shared<QString>();

    runOnFfmpegDll(
        [srcFilePath, outputFilePath, okFlag, errText]() {
            QString e;
            if (QcFfmpeg::convert(srcFilePath, outputFilePath, 24000, 1, 32, &e)) {
                *okFlag = true;
            } else {
                *errText = e;
            }
        },
        [srcFilePath, outputFilePath, okFlag, errText, onDone]() {
            if (*okFlag) { onDone(outputFilePath); return; }
            if (!errText->contains(QStringLiteral("没有可用的音频轨")))
                AppendEventLog("ffmpeg.dll 转换失败:" + *errText);
            onDone(srcFilePath);
        },
        viaExe);
}

// 同 dropShortAudioTail，但时长探测走异步（回调落在媒体线程）
static void dropShortAudioTailAsync(const QStringList &segs, std::function<void(QStringList)> onDone)
{
    if (segs.size() < 2) { onDone(segs); return; }     // 只剩一段 → 没有尾巴可丢
    probeAudioDurationSecAsync(segs.last(), [segs, onDone](double tailSec) {
        if (tailSec <= 0 || tailSec >= AUDIO_SEG_MIN_SEC) { onDone(segs); return; }

        QStringList out = segs;
        out.removeLast();
        AppendEventLog(QStringLiteral("音频切段：结尾仅 %1 秒（不足 %2 秒），已丢弃不发")
                           .arg(tailSec, 0, 'f', 1).arg(AUDIO_SEG_MIN_SEC));
        onDone(out);
    });
}

// 把 m4a 按 segSec 秒流复制切段（同 splitM4aSegments）；已有同名段文件直接复用，不起进程、也不删
// 返回值里已丢掉不足 AUDIO_SEG_MIN_SEC 的结尾段
static void splitM4aSegmentsAsync(const QString &m4aPath, int segSec, std::function<void(QStringList)> onDone)
{
    if (!m4aPath.endsWith(".m4a")) { onDone(QStringList()); return; }   // 只切转换产物
    const QString base = m4aPath.left(m4aPath.size() - 4);
    const QString filter = QFileInfo(base).fileName() + "_seg*.m4a";

    {
        QDir dir = QFileInfo(m4aPath).absoluteDir();
        const QStringList exist = dir.entryList(QStringList{filter}, QDir::Files, QDir::Name);
        if (!exist.isEmpty()) {                    // 之前切过 → 直接复用产物（tmp/ 就是放这个的）
            QStringList out;
            for (const QString &f : exist) out << dir.filePath(f);
            dropShortAudioTailAsync(out, onDone);
            return;
        }
    }

    // 退路：外部 ffmpeg.exe（DLL 缺失且补包失败时才走）
    auto viaExe = [m4aPath, base, filter, segSec, onDone]() {
        runProcessAsync(findFfmpegPath(),
            { "-y", "-i", m4aPath,
              "-c", "copy",                            // AAC 直接流复制，秒级完成
              "-f", "segment",
              "-segment_time", QString::number(segSec),
              "-reset_timestamps", "1",
              base + "_seg%03d.m4a" },
            60000,
            [m4aPath, filter, onDone](int code, const QByteArray &, bool timedOut) {
                if (timedOut || code != 0) { onDone(QStringList()); return; }
                QDir d = QFileInfo(m4aPath).absoluteDir();
                const QStringList files = d.entryList(QStringList{filter}, QDir::Files, QDir::Name);
                if (files.isEmpty()) { onDone(QStringList()); return; }   // 一段都没切出来 → 按整文件发
                QStringList out;
                for (const QString &f : files) out << d.filePath(f);
                dropShortAudioTailAsync(out, onDone);
            });
    };

    auto segs    = std::make_shared<QStringList>();
    auto errText = std::make_shared<QString>();

    runOnFfmpegDll(
        [m4aPath, segSec, segs, errText]() {
            QString e;
            *segs = QcFfmpeg::split(m4aPath, QFileInfo(m4aPath).absolutePath(), segSec,
                                    QStringLiteral("m4a"), &e);
            *errText = e;
        },
        [segs, errText, onDone]() {
            if (segs->isEmpty()) {
                if (!errText->isEmpty())
                    AppendEventLog("ffmpeg.dll 切段失败:" + *errText);
                onDone(QStringList());              // 切不出来 → 调用方按整文件发
                return;
            }
            dropShortAudioTailAsync(*segs, onDone);
        },
        viaExe);
}

// 远程音频（URL 直传被平台按时长拒绝时）允许下载到本地的体积上限
static const qint64 kRemoteAudioMaxBytes = 50LL * 1024 * 1024;   // 50MB

// 异步下载远程文件到本机临时目录，完成后回调本地路径（超限 / 失败回调空串）。
//   · 边下边判体积：Content-Length 或已收字节一旦超过 maxBytes 立刻中止，
//     **不落盘**（满足「先检查文件大小，超 50MB 就不下载」）
//   · 文件名 = qiancao_remote_<url 的 md5 前 16 位><后缀>：同一 URL 复用同一份，
//     顺带复用它转好的 .m4a 与已切好的分段，不会每次重下重转
//   · 不阻塞调用线程：下载在 NetManager 网络线程，落盘在回调线程（线程池）
static void downloadRemoteAudioToTempAsync(const QString &url, qint64 maxBytes,
                                           std::function<void(const QString &localPath)> onDone)
{
    // 走 NetManager 的连接池（原地 new QNAM + QEventLoop 会让 reply->deleteLater()
    // 永远等不到事件循环去处理，每下漏一份）。
    NetManager::instance()->downloadAsync(
        url, maxBytes, 120000,
        [url, onDone](const QByteArray &data, bool tooBig, QNetworkReply::NetworkError) {
            if (tooBig) {
                AppendEventLog(QStringLiteral("远程音频超过 50MB，放弃下载：") + url);
                onDone(QString());
                return;
            }
            if (data.isEmpty()) {
                AppendEventLog(QStringLiteral("远程音频下载失败：") + url);
                onDone(QString());
                return;
            }

            // 后缀沿用 URL 上的（ffmpeg 主要看内容，这只是让产物名好认一点）
            QString suffix;
            const QString pathPart = QUrl(url).path();
            const int dot = pathPart.lastIndexOf(QLatin1Char('.'));
            if (dot >= 0 && pathPart.size() - dot <= 6) {
                const QString s = pathPart.mid(dot + 1).toLower();
                if (s.size() >= 2 && s.size() <= 5
                    && QRegularExpression(QStringLiteral("^[a-z0-9]+$")).match(s).hasMatch())
                    suffix = QStringLiteral(".") + s;
            }

            const QString hash = QString::fromLatin1(
                QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
            const QString localPath = QDir(QDir::tempPath())
                .filePath(QStringLiteral("qiancao_remote_") + hash + suffix);

            QFile f(localPath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                AppendEventLog(QStringLiteral("远程音频落盘失败：") + localPath);
                onDone(QString());
                return;
            }
            f.write(data);
            f.close();
            onDone(localPath);
        });
}

// ===========================================================================
// 媒体标签异步链 —— sendOneMedia 的纯回调版
// ---------------------------------------------------------------------------
// 与同步版 sendOneMedia 逐段对应，区别只在「等待」全部换成回调：
//   md5/缓存 →（音频）ffmpeg 探测/转码/切段 → 上传 → send_Media，逐条标签推进。
// 这里面**没有任何阻塞点、也不新建线程**：ffmpeg 走 QProcess 的进程退出回调，
// 上传走 PostAsync/put2，延时的重试走 QTimer。
//
// 线程：链条每跨一步都经 onMedia() 回到媒体线程，所以下面的状态只在一条线程上读写，
//       不需要加锁；调用方线程在第一行就返回了。
// 顺序：与同步版一致 —— 标签从后往前处理（这样从 text 里删标签不会影响前面的索引）。
// ---------------------------------------------------------------------------
// 媒体上传缓存 —— 键固定是 "<mediaType>_<源文件md5>"，值 = **裸 file_info 列表**：
//       "<过期秒>||||<file_info1>||<file_info2>||<file_info3>"
//   · 单文件 / 切完只剩一段 → 只有 1 条；音频超长切成 N 段 → N 条
//     （一条 file_info 装不下整段音频，所以逐段存、命中时逐段重发）
//   · 过期时间统一挂在最前面（与本库其它缓存的 "时间||||值" 惯例一致）
//   兼容旧值：早期版本存的是 "[<类型>,path=<file_info>,md5=<md5>,Time=<过期秒>]" 整串，
//   过期时间藏在 ",Time=" 里、file_info 得靠 path= 抠 —— 老库里就是这种，读取侧一并认。
// 读取：返回「未过期、可直接喂 send_Media 的裸 file_info」列表；空 = 没缓存 / 已过期 / 认不出。
// ---------------------------------------------------------------------------
static QStringList mediaCachedInfos(const QString &cached)
{
    if (cached.isEmpty()) return QStringList();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    // ① 现行格式：过期秒在前，"||||" 之后是 "||" 分隔的裸 file_info 列表
    const int sepIdx = cached.indexOf(QStringLiteral("||||"));
    if (sepIdx > 0) {
        bool okNum = false;
        const qint64 expire = cached.left(sepIdx).trimmed().toLongLong(&okNum);
        if (okNum) {
            if (now >= expire) return QStringList();
            return cached.mid(sepIdx + 4).split(QStringLiteral("||"), Qt::SkipEmptyParts);
        }
    }

    // ② 历史格式（旧版本写进库的值）
    if (cached.startsWith(QLatin1Char('['))) {
        const int timeIdx = cached.lastIndexOf(QStringLiteral(",Time="));
        if (timeIdx != -1) {
            // ⚠ 缓存串末尾还有个 ']'，toLongLong() 要求整串都是数字，带 ']' 会整体转换失败返回 0
            QString expireStr = cached.mid(timeIdx + 6);
            const int rbIdx = expireStr.indexOf(QLatin1Char(']'));
            if (rbIdx >= 0) expireStr.truncate(rbIdx);
            bool okNum = false;
            const qint64 expire = expireStr.trimmed().toLongLong(&okNum);
            if (!okNum || now >= expire) return QStringList();
        }
        // 抠出 <file_info>：",md5=" 是包装串自己加的，md5 是十六进制、不会跟它自己撞
        const int pIdx = cached.indexOf(QStringLiteral("path="));
        if (pIdx < 0) return QStringList();
        int end = cached.lastIndexOf(QStringLiteral(",md5="));
        if (end <= pIdx) end = cached.lastIndexOf(QLatin1Char(']'));
        if (end <= pIdx) return QStringList();
        return QStringList{ cached.mid(pIdx + 5, end - pIdx - 5) };
    }

    // ③ 认不出：与旧代码的兜底一致，原样当命中
    return QStringList{ cached };
}

// 写入媒体上传缓存（各段全部处理完、收尾时调一次）
//   · 1 条（整文件 / 单段）与 N 条（真多段）同一个格式，读取侧按 "||" 拆开逐条重发
//   · 有段失败 → 不写（免得把半截音频缓存下来）
//   · expire<=0（平台没给 ttl）→ 不写（写了也立刻判过期）
// ⚠ key 必须以 appid 开头（2026-10-21）：
//   上传接口返回的 <file_info> 是**跟着上传它的机器人账号走的** —— 里面嵌着该 appid 的
//   file_uuid / file_id / 权限串，拿去给另一个 appid 发，平台直接判无效（表现：发送失败、
//   卡住、或者被当成非法文件）。同一份本地文件在 A 号传过，B 号绝对不能复用这条缓存。
//   所以库里所有上传缓存的键统一是 "<appid>:<类型>_<文件指纹>"，缺了 appid 就是跨号串号。
static void putMediaUploadCache(const QString &appid, const QString &mediaType, const QString &fileMd5,
                                const QStringList &infos, qint64 expire, bool allOk)
{
    if (!cache_db || fileMd5.isEmpty() || !allOk || infos.isEmpty() || expire <= 0) return;
    // 用 + 拼接，避免 arg 二次解析 '%'
    const QString key = appid + QLatin1Char(':') + mediaType + QLatin1Char('_') + fileMd5;
    cache_db->put(key, QStringLiteral("%1||||%2").arg(expire).arg(infos.join(QStringLiteral("||"))));
}

// 复制一份 ctx 但**去掉回调**。
// ⚠ 只有「本次发送的最后一条消息」才允许带回调：send_Media 每发一条（每个分段）都会在 HTTP
//   回调里触发一次 ctx.cb，4 段音频就把调用方回调 4 次 —— 插件/Python 那边每收到一次都会当成
//   「这一轮发完了」而继续下一步（甚至再发一轮），于是表现为「一直循环重复发这 4 个片段」。
//   契约见 send_messagesAsync 的注释：每条路径必须把 ctx.cb 触发**恰好一次**。
//   中间分段用这个版本：日志照记（pname/jsonString/openid 都保留），只是不回调。
static MessageLogContext ctxNoCb(const MessageLogContext &c)
{
    MessageLogContext r = c;
    r.cb = nullptr;
    return r;
}

// ===========================================================================
struct MediaTagMatch {              // 与 sendOneMedia 内部的 MatchInfo 同构
    int     start  = 0;
    int     length = 0;
    QString type;                   // "f"/"a"/"v"/"flie"…
    QString params;
};

// 单条标签的处理状态
struct QQBotClient::MediaTagCtx {
    QString     mediaType;          // file / audio / video
    int         fileType = 1;       // 1图片 2视频 3音频 4文件（本链只可能出现 2/3/4）
    QString     srcPath;            // 原始 path（或 URL）；产物路径一律由它推导
    QString     filePath;           // 会被替换成产物路径
    QString     m4aPath;
    QString     fileMd5;
    QStringList audioSegs;          // 音频超长切段列表；空 = 单文件直传
    QStringList cachedInfos;        // 缓存命中的整串列表（1 条 = 整文件/单段；N 条 = 切好的多段）
    QStringList segInfos;           // 本次各段上传成功的整串 —— 全部段处理完时一次性写缓存
    qint64      segExpire = 0;      // 多段缓存的统一过期时间（各段取最小）
    bool        segAllOk = true;    // 各段是否全部上传成功（有失败就不写缓存）
    bool        needUpload = true;
    bool        uploadStarted = false;  // 「上传 + 发送」只允许进入一次（任何回调重复触发都挡住）
    bool        fallbackDone  = false;  // 远程音频超时长的兜底链只允许走一次
    bool        done = false;       // 这条标签是否已收尾（防重复摘标签 / 重复推进）
};

// 整条链的状态
struct QQBotClient::MediaSendChain {
    int                  appid = 0;  // ⚠ 按值存下来：回调里靠它反查全局表，绝不回头解引用 this
    std::function<void(const QString &, const QString &)> onDone;
    QString              text;      // 逐条摘掉媒体标签后的文本
    QList<MediaTagMatch> matches;
    int                  idx = 0;   // 从 matches.size()-1 往前
    QString              response;  // 最终返回给调用方（= 最后一条标签的 send_Media 返回）
    int                  type = 0;
    QString              openid, pname, msgid;
    qint64               now_us = 0;
    bool                 is_wakeup = false, mode = false, noref = false;
    int                  sendType = 0;
    MessageLogContext    ctx;
    bool                 finished = false;
    // 摘掉全部媒体标签后**还有正文**吗（入口处一次性算好）。
    // 它决定「调用方回调 (ctx.cb) 归谁」：
    //   true  —— 正文+媒体混发（违反设计约定，但真出现就都发）：媒体一律不带回调，
    //            cb 归**正文**那次 send_messages*，即「回调只报正文是否成功，文件的成败不影响」(2026-10-20 拍板)
    //   false —— 纯媒体：cb 仍挂在最后一段媒体的 send_Media 上（与原来一致，时序 = 真发出去才回）
    bool                 hasText = false;
};

// 分段发送的推进状态（音频切段 / 远程音频兜底 共用）
struct QQBotClient::MediaSegJob {
    QStringList paths;
    int         idx = 0;
};

// 一条标签处理完：从待发文本里摘掉它 → 推进到下一条（全部处理完则收尾回调）
void QQBotClient::mediaChainFinishTag(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc)
{
    if (!tc || tc->done) return;
    tc->done = true;
    if (st->idx >= 0 && st->idx < st->matches.size()) {
        const MediaTagMatch &m = st->matches[st->idx];
        st->text.remove(m.start, m.length);
    }
    st->idx -= 1;
    mediaChainStep(st);
}

// 处理第 st->idx 条标签
void QQBotClient::mediaChainStep(std::shared_ptr<MediaSendChain> st)
{
    if (!clientAlive(this, st->appid)) return;

    // ── 全部标签处理完 → 收尾 ──
    if (st->idx < 0) {
        if (st->finished) return;
        st->finished = true;
        auto cb = st->onDone;
        st->onDone = nullptr;
        // 只有「摘完标签还剩正文」才往回调 —— 让调用方接着发正文（见 send_messagesAsync 的 sendTextPart）。
        // ⚠ 纯媒体（st->hasText=false）**不能**调：那条路径的 cb 已经由 send_Media 的 PostAsync
        //   触发过了（api2.cpp），这里再调一次 = 同一个回调触发两次（插件/Python 会当成两轮 → 循环重发）。
        if (cb && st->hasText) cb(st->text, st->response);
        return;
    }

    const MediaTagMatch m = st->matches[st->idx];

    // 规范化类型
    QString mediaType;
    if (m.type == "f" || m.type == "file" || m.type == "flie") mediaType = "file";
    else if (m.type == "a" || m.type == "audio") mediaType = "audio";
    else if (m.type == "v" || m.type == "video") mediaType = "video";
    else {                                   // 认不出的类型：直接摘掉，推进
        st->text.remove(m.start, m.length);
        st->idx -= 1;
        mediaChainStep(st);
        return;
    }

    static QRegularExpression pathRe(R"(path\s*=\s*([^,\]]+))");
    static QRegularExpression urlRe(R"(url\s*=\s*([^,\]]+))");
    QString pathArg = pathRe.match(m.params).captured(1).trimmed();
    const QString urlArg = urlRe.match(m.params).captured(1).trimmed();

    if (pathArg.isEmpty() && urlArg.isEmpty()) {   // 既没路径也没 URL：摘掉标签，推进
        st->text.remove(m.start, m.length);
        st->idx -= 1;
        mediaChainStep(st);
        return;
    }
    if (!urlArg.isEmpty() && urlArg.startsWith("http")) pathArg = urlArg;

    auto tc = std::make_shared<MediaTagCtx>();
    tc->mediaType = mediaType;
    tc->fileType  = (mediaType == "video") ? 2 : (mediaType == "audio") ? 3 : 4;
    tc->srcPath   = pathArg;
    tc->filePath  = pathArg;

    // ── 本地文件：md5 + 查上传缓存（命中就不用再传了）──
    //    外链（http）没有本地文件可算 md5，用 **URL 自身的 md5** 当缓存键 —— 同一首歌（点歌插件
    //    发的外链）再发一次就直接重放上次上传好的分段，不用再走「注册外链 → 40093013 → 下载 →
    //    转码 → 切段 → 逐段上传」整条兜底链（那条链慢到调用方会超时重试，表现得像"循环重复发"）。
    if (!tc->filePath.startsWith("http")) {
        int w = 0, h = 0;
        calculateFileMD5AndSize(tc->filePath, tc->fileMd5, w, h);
    } else {
        tc->fileMd5 = QString::fromLatin1(
            QCryptographicHash::hash(tc->filePath.toUtf8(), QCryptographicHash::Md5).toHex());
    }
    // 缓存检查（纯读一次；过期就当未命中，不删 —— 见 cachedValueIfFresh）
    if (cache_db && !tc->fileMd5.isEmpty()) {
        // ⚠ 带账号：<file_info> 跟着上传它的机器人走，跨号复用必失效（见 putMediaUploadCache 注释）
        const QString cacheKey = QString::number(st->appid) + QLatin1Char(':') + mediaType + QLatin1Char('_') + tc->fileMd5;
        // 命中可能是 1 条（整文件/单段），也可能是切好的 N 段（见 mediaCachedInfos）
        tc->cachedInfos = mediaCachedInfos(cachedValueIfFresh(cacheKey));
        if (!tc->cachedInfos.isEmpty())
            tc->needUpload = false;
    }

    // ── 本地音频：探测 →（必要时）转码 →（超长）切段 → 上传 ──
    // 对应同步版的 sendAsIs 判断 + 「先试切段、否则转码、再判超长」三连
    if (tc->needUpload && tc->fileType == 3 && !tc->srcPath.startsWith("http")) {
        const QString srcPath = tc->srcPath;
        tc->m4aPath = audioM4aPathFor(srcPath);
        if (!tc->m4aPath.isEmpty() && QFile::exists(tc->m4aPath))
            tc->filePath = tc->m4aPath;          // 上次转好的产物还在 → 直接复用

        audioCanSendAsIsAsync(tc->filePath, [this, st, tc, srcPath](bool sendAsIs) {
            if (!clientAlive(this, st->appid)) return;
            if (sendAsIs) { mediaChainUploadAndSend(st, tc); return; }   // 小体积容器/短视频 → 原样发

            // 时长探完：超长就切段（切不出多段则按单文件发）
            auto probeThenSplit = [this, st, tc](QStringList segs) {
                tc->audioSegs = segs;
                mediaChainUploadAndSend(st, tc);
            };
            // 判超长 → 超了再切段
            auto probeStep = [this, st, tc, probeThenSplit]() {
                probeAudioDurationSecAsync(tc->filePath, [this, st, tc, probeThenSplit](double d) {
                    if (!clientAlive(this, st->appid)) return;
                    if (d > AUDIO_SEG_MAX_SEC)
                        splitM4aSegmentsAsync(tc->filePath, AUDIO_SEG_MAX_SEC, probeThenSplit);
                    else
                        probeThenSplit(QStringList());       // 没超长 → 单文件直传
                });
            };
            auto convertStep = [this, st, tc, srcPath, probeStep]() {
                // ① 已经是 .m4a（多半就是上次转好的产物）→ 先试流复制切段（无损、秒级）
                if (tc->audioSegs.isEmpty() && tc->filePath.endsWith(QStringLiteral(".m4a"), Qt::CaseInsensitive)) {
                    splitM4aSegmentsAsync(tc->filePath, AUDIO_SEG_MAX_SEC,
                        [this, st, tc, probeStep](QStringList segs) {
                            tc->audioSegs = segs;
                            if (!tc->audioSegs.isEmpty()) { mediaChainUploadAndSend(st, tc); return; }
                            probeStep();     // 只切出 1 段 → 落到时长判断
                        });
                    return;
                }
                // ② 没有产物 → 转码（失败/超时它返回源路径本身，不会把路径改坏）
                if (tc->audioSegs.isEmpty() && !tc->m4aPath.isEmpty()) {
                    if (QFile::exists(tc->m4aPath)) {
                        tc->filePath = tc->m4aPath;
                        probeStep();
                    } else {
                        convertAudioToSilkAsync(srcPath, [this, st, tc, probeStep](const QString &converted) {
                            if (!converted.isEmpty()) tc->filePath = converted;
                            probeStep();
                        });
                    }
                    return;
                }
                probeStep();
            };
            convertStep();
        });
        return;
    }

    // 其余（视频 / 文件 / http 外链）直接进上传
    mediaChainUploadAndSend(st, tc);
}

// 进入「上传 + 发送」：needUpload=false 表示缓存命中，按 tc->cachedInfos 逐条发（可能多段）
void QQBotClient::mediaChainUploadAndSend(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc)
{
    if (!clientAlive(this, st->appid)) return;
    // ⚠ 单飞：这条标签只允许进「上传+发送」一次。音频链要跨 ffmpeg / 切段 / 探测好几个回调，
    //   任何一个回调重复触发都会再发一轮（表现为「同一批分段一直循环发」），这里一刀切死。
    if (tc->uploadStarted) return;
    tc->uploadStarted = true;

    if (!tc->needUpload) {
        // 缓存命中：1 条 = 整文件 / 单段；N 条 = 上次切好的多段 → 逐条原样重发
        QStringList infos;
        for (const QString &fi : std::as_const(tc->cachedInfos))
            if (!fi.isEmpty()) infos << fi;
        // ⚠ 只有**最后一条**带 ctx（里面是调用方回调）。每条都带 = 4 段回调 4 次 → 调用方
        //   以为每段都是一轮结束 → 又走下一轮，正是「循环重复发」的来源。
        // ⚠ 消息里还有正文（st->hasText）时，媒体**一条都不带回调** —— cb 归正文
        //   （契约「恰好一次」；回调只报正文成没成功，媒体成败不影响）。
        for (int i = 0; i < infos.size(); ++i) {
            const bool last = (i + 1 == infos.size());
            // ⚠ multiPart：这一批是「同一轮的多段媒体」。send_Media 里 setBuffer_250 只对**第一条**
            //   返回 ok=true（它把该消息从「未处理」翻成 250），第 2 条起一律 ok=false；noref 时原本
            //   会直接 return "{}" 把 2..N 段吞掉。多段必须传 true 绕过该去重（见 send_Media）。
            st->response = send_Media(st->type, st->openid, st->pname, infos[i],
                                      st->now_us, st->msgid, st->is_wakeup, st->noref,
                                      (!st->hasText && last) ? st->ctx : ctxNoCb(st->ctx), tc->fileType,
                                      /*multiPart=*/infos.size() > 1);
        }
        mediaChainFinishTag(st, tc);
        return;
    }

    auto job = std::make_shared<MediaSegJob>();
    job->paths = tc->audioSegs.isEmpty() ? QStringList{tc->filePath} : tc->audioSegs;
    mediaChainSegLoop(st, tc, job);
}

// fileType → 中文名（只用于「上传失败」的提示文本与事件日志）
static QString mediaKindLabel(int fileType)
{
    switch (fileType) {
    case 1:  return QStringLiteral("图片");
    case 2:  return QStringLiteral("视频");
    case 3:  return QStringLiteral("音频");
    case 4:  return QStringLiteral("文件");
    default: break;
    }
    return QStringLiteral("媒体");
}

// 逐段串行上传并发送（与同步版的分段循环一致）
void QQBotClient::mediaChainSegLoop(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc,
                                    std::shared_ptr<MediaSegJob> job)
{
    if (!clientAlive(this, st->appid)) return;
    if (job->idx >= job->paths.size()) {
        // 全部段处理完 → 写缓存：整文件/单段存 1 条、真多段存 N 条，同一格式 "过期秒||||info1||info2||…"
        putMediaUploadCache(QString::number(st->appid), tc->mediaType, tc->fileMd5, tc->segInfos, tc->segExpire, tc->segAllOk);
        mediaChainFinishTag(st, tc);
        return;
    }

    const QString upPath = job->paths[job->idx];
    job->idx += 1;
    // ⚠ 只有最后一段允许带 ctx（= 调用方回调）。每段都带 → 4 段回调 4 次 → 调用方每收一次
    //   都当成一轮结束 → 重复发。见 ctxNoCb 的注释（契约是「恰好一次」）。
    const bool lastSeg = (job->idx >= job->paths.size());

    // ⚠ 图片（fileType==1）继续复用池子；**视频/语音/文件一律不复用**（usePool=false）：
    //   这几类的分片上传本来就是每次 upload_prepare 拿全新 presigned_url，一旦复用池链接，
    //   异步链下 put 重试时链接可能已被别的消息取走 → 互相覆盖（内容串号）。
    //   池子只对图片那条「100K 申请 + 单分片复写」有意义。targetType==4（私聊）内部还会再挡一次。
    uploadRichMediaPoolAAsync(st->type, st->openid, tc->fileType, upPath, /*usePool=*/(tc->fileType == 1),
        [this, st, tc, job, upPath, lastSeg](const QString &fileInfo, qint64 expireTime, const QString &, bool ok, const QString &) {
            if (!clientAlive(this, st->appid)) return;

            // ⚠ 顺序必须与同步版一致：**先**判远程音频超时长，**后**判 ok。
            //   超时长时平台回的是报错文本（ok=false）；要是先 `if(!ok)` 就把报错当正文发掉了，
            //   下面这个兜底分支永远执行不到 —— 整条兜底链等于死代码。
            if (tc->fileType == 3 && fileInfo.contains("40093013") && upPath.startsWith(QLatin1String("http"))) {
                if (tc->fallbackDone) { mediaChainFinishTag(st, tc); return; }   // 兜底链只走一次
                tc->fallbackDone = true;
                mediaRemoteAudioFallback(st, tc, upPath, fileInfo);
                return;
            }

            if (!ok || fileInfo.isEmpty()) {
                // 上传失败 → 把平台报错当文本发出去（与同步版一致）
                // ⚠⚠ 关键：平台的**好几种失败路径回的是空串** —— 本地文件打不开 / 不存在
                //   （uploadRichMediaPoolAAsync 里 open 失败）、prepare 回空、JSON 解析失败。
                //   以前这里直接拿空串去 send_msgAsync → send_messagesAsync 判定「正文为空」→
                //   一条都不发、也不记日志 —— 用户那边就是「文件没发出来，日志里一个字都没有」。
                //   所以：空串必须兜一句人话，并且无论成败都落一条事件日志。
                //   （ok==true 但 fileInfo 为空同样是异常，一并按失败处理，不然会静默跳过发送）
                tc->segAllOk = false;               // 有段失败 → 本次不写缓存（避免缓存成半截音频）
                const QString kind = mediaKindLabel(tc->fileType);
                QString errInfo = fileInfo.trimmed();          // 非 const：send_* 收的是 QString&
 ;
                if (errInfo.isEmpty())
                    errInfo = QStringLiteral("[%1发送失败：平台未返回错误信息]").arg(kind);
                else
                    errInfo = errInfo.left(300);        // 平台原文（可能是 JSON / 网关返回的 HTML），太长的截断

                // ── 日志里补一条「本来要发的媒体」──
                // 以前失败时只把错误文本发出去，日志里就只剩「发送错误信息」那一条，完全看不到这次想发的是
                // 什么（视频/文件全没有 [video,path=…] 记录，事后查不出是哪个文件挂的）。
                // 这里按 send_Media 成功路径**同一个**标签形态补一行：`[video,path=<实际传的路径>,]`。
                //   · index 传 -1 → 只**新增**一行，不动 send_* 那条「待处理」缓冲区行（序号/msg_seq 保持原样）；
                //   · pname 里的 %1 先自己替换成耗时 —— index<0 时 addmsglog 算不出 diff，会显示 0ms。
                {
                    const QString tagName = (tc->fileType == 1) ? QStringLiteral("image")
                                          : (tc->fileType == 2) ? QStringLiteral("video")
                                          : (tc->fileType == 3) ? QStringLiteral("audio")
                                                                : QStringLiteral("file");
                    QString logPname = st->pname;
                    if (logPname.contains(QStringLiteral("%1"))) {
                        const double elapsedMs = (QDateTime::currentMSecsSinceEpoch() * 1000.0
                                                  - static_cast<double>(st->now_us)) / 1000.0;
                        logPname = logPname.arg(QString::number(elapsedMs, 'f', 1));
                    }
                    addmsglog(errInfo, -1, logPname,
                              QStringLiteral("[%1,path=%2,]").arg(tagName, upPath),
                              st->now_us, st->type, st->openid,
                              /*tstat=*/nullptr, /*recordErr=*/false);   // 这条只是「本来要发的媒体」，真正的报错紧接着还会走一次 send_*
                }

                if (st->ctx.openid.isEmpty())
                    send_messages(st->type, st->openid, st->pname, errInfo, st->msgid,
                                  st->is_wakeup, st->mode, st->sendType, st->noref);
                else
                    // 有正文（st->hasText）时不占回调：cb 归后面那条正文
                    send_msgAsync(st->type, st->openid, st->pname, errInfo, st->msgid,
                                  st->is_wakeup, st->mode, st->sendType, st->noref,
                                  (!st->hasText && lastSeg) ? st->ctx.cb : Callback());
                mediaChainSegLoop(st, tc, job);
                return;
            }

            // 成功：累计本段的裸 file_info + 发送；缓存等全部段处理完再一次性写
            // （见 mediaChainSegLoop 开头）—— 多段时每段各写一次会互相覆盖，只留最后一段。
            if (!fileInfo.isEmpty()) {
                tc->segInfos << fileInfo;
                if (expireTime > 0)
                    tc->segExpire = (tc->segExpire == 0) ? expireTime : qMin(tc->segExpire, expireTime);
                st->response = send_Media(st->type, st->openid, st->pname, fileInfo,
                                          st->now_us, st->msgid, st->is_wakeup, st->noref,
                                          (!st->hasText && lastSeg) ? st->ctx : ctxNoCb(st->ctx), tc->fileType,
                                          /*multiPart=*/job->paths.size() > 1);
            }
            mediaChainSegLoop(st, tc, job);
        });
}

// 远程音频超时长的兜底链（全部异步）：
//   下载（≤50MB）→ 转码 32k 单声道 m4a → 按 4:58 切段 → 逐段上传发送。
// 任一步失败就把平台原报错当文本发出去（与同步版一致）。
void QQBotClient::mediaRemoteAudioFallback(std::shared_ptr<MediaSendChain> st, std::shared_ptr<MediaTagCtx> tc,
                                           const QString &url, const QString &errInfo)
{
    if (!clientAlive(this, st->appid)) return;

    downloadRemoteAudioToTempAsync(url, kRemoteAudioMaxBytes,
        [this, st, tc, errInfo](const QString &localFile) {
            // 下载回调在线程池线程（还顺手落了个盘）→ 搬回媒体线程再继续
            postToMedia([this, st, tc, errInfo, localFile]() {
                if (!clientAlive(this, st->appid)) return;

                auto sendOriginError = [this, st, errInfo]() {
                    QString e = errInfo;             // 非 const：send_* 收的是 QString&
                    if (st->ctx.openid.isEmpty())
                        send_messages(st->type, st->openid, st->pname, e, st->msgid,
                                      st->is_wakeup, st->mode, st->sendType, st->noref);
                    else
                        // 有正文时 cb 归正文（这里只是"报错提示"，不算本轮结果）
                        send_msgAsync(st->type, st->openid, st->pname, e, st->msgid,
                                      st->is_wakeup, st->mode, st->sendType, st->noref,
                                      st->hasText ? Callback() : st->ctx.cb);
                };

                if (localFile.isEmpty()) {           // 超 50MB / 下载失败（日志已在下载里记过）
                    sendOriginError();
                    mediaChainFinishTag(st, tc);
                    return;
                }

                // 切好段 → 复用主链的分段发送循环
                auto afterSplit = [this, st, tc, errInfo, sendOriginError](QStringList segs) {
                    if (!clientAlive(this, st->appid)) return;
                    if (segs.isEmpty()) {
                        AppendEventLog(QStringLiteral("远程音频超时长处理失败（无法切段），按原内容输出：") + errInfo);
                        sendOriginError();
                        mediaChainFinishTag(st, tc);
                        return;
                    }
                    auto job = std::make_shared<MediaSegJob>();
                    job->paths = segs;
                    mediaChainSegLoop(st, tc, job);
                };

                // 拿到可用的本地音频 → 探时长 → 切段（或直接用本地这份重发）
                auto afterConvert = [this, st, tc, afterSplit](const QString &usePath) {
                    if (!clientAlive(this, st->appid)) return;
                    probeAudioDurationSecAsync(usePath, [this, st, tc, afterSplit, usePath](double durSec) {
                        if (!clientAlive(this, st->appid)) return;
                        if (durSec > AUDIO_SEG_MAX_SEC)
                            splitM4aSegmentsAsync(usePath, AUDIO_SEG_MAX_SEC, afterSplit);
                        else if (durSec > 0)
                            afterSplit(QStringList{ usePath });   // 本地这份其实没超长 → 直接重发一次
                        else
                            afterSplit(QStringList());            // 探不到时长 → 走失败兜底
                    });
                };

                // 产物路径一律由「源」推导；已有产物直接复用，否则异步转码
                const QString m4a = audioM4aPathFor(localFile);
                if (!m4a.isEmpty() && QFile::exists(m4a))
                    afterConvert(m4a);
                else
                    convertAudioToSilkAsync(localFile, afterConvert);
            });
        });
}

// sendOneMedia 的纯回调版入口：扫出全部媒体标签，逐条推进，最后回调 onDone。
//   没有媒体标签 → 直接同步回调（零线程零等待，绝大多数发送走这条）
//   有媒体标签 → 交给媒体线程上的异步链（调用方线程立刻返回）
void QQBotClient::sendOneMediaAsync(int type, const QString &openid, const QString &pname, QString text,
                                    qint64 now_us, const QString &msgid, bool is_wakeup, bool mode,
                                    int sendType, bool noref, const MessageLogContext &ctx,
                                    std::function<void(const QString &, const QString &)> onDone)
{
    if (!onDone) return;

    // 与 sendOneMedia 内部**同一份**正则（改一处必须改另一处）
    static QRegularExpression re(R"(\[(f(?:ile)?|a(?:udio)?|v(?:ideo)?|flie)\s*,\s*([^\]]+)\])",
                                 QRegularExpression::CaseInsensitiveOption);

    auto st = std::make_shared<MediaSendChain>();
    st->appid    = m_info->appid_int;
    st->onDone   = onDone;
    st->text     = text;
    st->type     = type;
    st->openid   = openid;
    st->pname    = pname;
    st->msgid    = msgid;
    st->now_us   = now_us;
    st->is_wakeup = is_wakeup;
    st->mode     = mode;
    st->sendType = sendType;
    st->noref    = noref;
    st->ctx      = ctx;

    {
        QRegularExpressionMatchIterator it = re.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch mm = it.next();
            MediaTagMatch mt;
            mt.start  = static_cast<int>(mm.capturedStart());
            mt.length = static_cast<int>(mm.capturedLength());
            mt.type   = mm.captured(1).toLower();
            mt.params = mm.captured(2);
            st->matches.append(mt);
        }
    }

    if (st->matches.isEmpty()) {          // 无媒体标签 → 零线程零等待
        onDone(text, QString());
        return;
    }

    // 摘掉**全部**标签后还剩正文吗 —— 决定 ctx.cb 归谁（见 MediaSendChain::hasText 的注释）。
    // 与链内逐条摘除的算法一致：从后往前 remove，前面的下标就不会被挪动。
    {
        QString rest = text;
        for (int i = st->matches.size() - 1; i >= 0; --i)
            rest.remove(st->matches[i].start, st->matches[i].length);
        st->hasText = !rest.trimmed().isEmpty();
    }

    st->idx = st->matches.size() - 1;     // 与同步版一致：从后往前处理
    postToMedia([this, st]() { mediaChainStep(st); });
}

QString QQBotClient::sendOneMedia(int type, const QString &openid,const QString &pname,QString &text,qint64 now_us,
                                  const QString &msgid,bool is_wakeup,bool mode,int 发送类型,bool noref,const MessageLogContext &ctx)
{
    // 匹配短标签或全名标签：f/file, a/audio, v/video, flie(笔误)
    static QRegularExpression re(R"(\[(f(?:ile)?|a(?:udio)?|v(?:ideo)?|flie)\s*,\s*([^\]]+)\])",
                          QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = re.globalMatch(text);

    struct MatchInfo {
        int start;
        int length;
        QString type;   // "f", "a", "v", "flie", etc.
        QString params;
    };
    QList<MatchInfo> matches;

    // 第一步：收集所有匹配位置和原始信息
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        matches.append({static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()),
                        m.captured(1).toLower(), m.captured(2)});
    }
    QString response;
    // 第二步：从后往前处理（删除时不影响前面的索引）
    for (int i = matches.size() - 1; i >= 0; --i) {
        const MatchInfo &info = matches[i];
        QString rawType = info.type;

        // 规范化类型
        QString mediaType;
        if (rawType == "f" || rawType == "file") mediaType = "file";
        else if (rawType == "a" || rawType == "audio") mediaType = "audio";
        else if (rawType == "v" || rawType == "video") mediaType = "video";
        else if (rawType == "flie") mediaType = "file";   // 常见拼写错误
        else continue;
        static QRegularExpression pathRe(R"(path\s*=\s*([^,\]]+))");
        static QRegularExpression urlRe(R"(url\s*=\s*([^,\]]+))");

        QString filePath = pathRe.match(info.params).captured(1).trimmed();
        QString fileUrl  = urlRe.match(info.params).captured(1).trimmed();

        if (filePath.isEmpty() && fileUrl.isEmpty()) {
            text.remove(info.start, info.length);
            continue;
        }
        if(!fileUrl.isEmpty() && fileUrl.startsWith("http"))
        {
            filePath=fileUrl;
        }
        bool needUpload = true;
        QStringList audioSegs;   // 音频超长切段列表；空 = 单文件直传
        QStringList cachedInfos; // 缓存命中的整串列表（1 条 = 整文件/单段；N 条 = 切好的多段）
        QString fileInfo,fileMd5;
        int fileType = 1;
        if (mediaType == "video") fileType = 2;
        else if (mediaType == "audio") fileType = 3;
        else if (mediaType == "file") fileType = 4;
        if(!filePath.startsWith("http"))
        {
            int w=0,h=0;
            calculateFileMD5AndSize(filePath,fileMd5,w,h);
        }
        else
        {
            // 外链没有本地文件可算 md5 → 用 URL 自身的 md5 当缓存键（与异步链同款）：
            // 同一条外链再发一次直接重放上次上传好的分段/整串，不用重新注册+下载+转码+切段。
            fileMd5 = QString::fromLatin1(
                QCryptographicHash::hash(filePath.toUtf8(), QCryptographicHash::Md5).toHex());
        }
        // 缓存检查（纯读一次；过期就当未命中，不删 —— 见 cachedValueIfFresh）
        if (cache_db && !fileMd5.isEmpty()) {
            // ⚠ 带账号：<file_info> 跟着上传它的机器人走，跨号复用必失效
            //   账号标识与异步链同一口径（appid_int），否则同一文件走同步/异步两条路会各存一份、互不命中
            QString cacheKey = QString::number(m_info->appid_int) + QLatin1Char(':') + mediaType + QLatin1Char('_') + fileMd5;
            // 命中可能是 1 条（整文件/单段），也可能是切好的 N 段（见 mediaCachedInfos）
            cachedInfos = mediaCachedInfos(cachedValueIfFresh(cacheKey));
            if (!cachedInfos.isEmpty())
                needUpload = false;
        }
        if(!filePath.startsWith("http"))
        {
            if(needUpload && fileType==3)
            {
                needUpload=true;

                // 源文件路径单独留一份：产物路径一律由「源」推导。
                // （filePath 下面会被替换成产物路径，再拿它去推就会拼出 <源>.m4a.m4a）
                const QString srcPath = filePath;
                // 产物路径：<源目录>/tmp/<源名>.m4a；空串 = 非本地文件（http URL），整条转换链路不适用
                const QString m4aPath = audioM4aPathFor(srcPath);

                // 之前 ffmpeg 转好的产物还在 → 直接复用，不必再重转一份
                if (!m4aPath.isEmpty() && QFile::exists(m4aPath))
                    filePath = m4aPath;

                // 已经是小体积容器（opus/m4a/amr…）或本身就不大 → 原样发，转换纯属浪费算力。
                // 只有这种「看着够小」的才值得花一次只读探测：小容器也可能是超长低码率音频。
                // （大文件走短路求值，根本不探，直接进转换）
                // audioCanSendAsIs() 内部已经判过「容器里有没有视频轨」和「时长有没有超 4:59」
                const bool sendAsIs = audioCanSendAsIs(filePath);

                if (!sendAsIs) {
                    // ① 已经是 .m4a（多半就是上次转好的产物）→ 直接流复制切段：无损、秒级、不重编码
                    if (filePath.endsWith(QStringLiteral(".m4a"), Qt::CaseInsensitive))
                        audioSegs = splitM4aSegments(filePath, AUDIO_SEG_MAX_SEC);

                    // ⚠ 只对本地文件走转码/切段：m4aPath 为空 = 源是 http URL，
                    //   那种情况直接原样上传（远程音频我们本地没有文件可转）
                    if (audioSegs.isEmpty() && !m4aPath.isEmpty()) {
                        // ② 转码：ffmpeg → 32kbps 单声道 .m4a。
                        //    码率/采样率/声道数与原来的进程内 Opus 完全一致，产物体积也就基本不变
                        //    （QQ 只校验时长和大小，不校验容器格式）。
                        if (QFile::exists(m4aPath)) {
                            filePath = m4aPath;                  // 已有产物（复用检查那步之外再兜一次）
                        } else {
                            const QString converted = convertAudioToSilk(srcPath);
                            // 转换失败时它返回源路径本身；为空（理论上不会）就保持原样发
                            if (!converted.isEmpty())
                                filePath = converted;
                        }

                        // 超长音频 → 切成多段，走下面的循环逐段上传发送
                        // （splitM4aSegments 只对 .m4a 生效：转换失败时 filePath 还是原文件，
                        //   它会直接返回空 = 原样发，不会把路径改坏）
                        if (probeAudioDurationSec(filePath) > AUDIO_SEG_MAX_SEC)
                            audioSegs = splitM4aSegments(filePath, AUDIO_SEG_MAX_SEC);
                    }
                }
            }

        }
        bool ok = true;
        if (needUpload) {
            // 音频超长时 audioSegs 是各分段路径；否则就是 filePath 本身
            const QStringList upPaths = audioSegs.isEmpty() ? QStringList{filePath} : audioSegs;
            QStringList segInfos;      // 本次各段上传成功的整串 —— 整条循环结束后一次性写缓存
            qint64      segExpire = 0; // 多段缓存的统一过期时间（各段取最小）
            bool        segAllOk = true;
            for (int si = 0; si < upPaths.size(); ++si) {
                const QString upPath = upPaths[si];
                // ⚠ 只有**最后一段**带 ctx（= 调用方回调）。send_Media 每发一条就触发一次 ctx.cb，
                //   4 段的音频会把调用方回调 4 次 —— 插件/Python 每收到一次都当成「这一轮发完了」，
                //   于是又走下一轮 = 「一直循环重复发这 4 个片段」。契约是「恰好一次」。
                const bool lastSeg = (si + 1 == upPaths.size());
                const MessageLogContext segCtx = lastSeg ? ctx : ctxNoCb(ctx);
                const Callback segCb = lastSeg ? ctx.cb : Callback();
                ok = true;
                qint64 expireTime = 0;
                QString md5;
                QString uploadedUrl;
                /*
                if (g_cnb.e) {
                    uploadedUrl = uploadFileSync(upPath);
                }
                // 2. COS
                if (uploadedUrl.isEmpty() && g_cos.e) {
                    uploadedUrl = uploadFileSync_cos(upPath);
                }
                */
                if(uploadedUrl.isEmpty())
                {
                    uploadedUrl = upPath;
                }
                // 图片（fileType==1）继续复用池子；视频/语音/文件一律不复用（usePool=false）：
                // 这几类走原始分片上传，每次 upload_prepare 拿全新 presigned_url；复用池链接在异步重试下会互相覆盖。
                // expireTime/md5 是引用出参（上传缓存要拿过期时间写 "<过期秒>||||…"）
                fileInfo = uploadRichMediaPoolA(type, openid, fileType, uploadedUrl, expireTime, md5, ok,
                                                /*usePool=*/(fileType == 1));
                if(fileType==3 && fileInfo.contains("40093013") && uploadedUrl.startsWith("http"))//上传音频时长超过限制
                {
                    // ── 远程音频（URL 直传）超时长 ──
                    // QQ 对 URL 直传的音频是在服务端做时长校验的，超 4:59 就回 40093013。
                    // 处理：先看体积（超 50MB 直接不下载）→ 拉回本地 → 转成 32k 单声道 m4a
                    //       → 按 AUDIO_SEG_MAX_SEC 切成 4:58 的分段逐条发。
                    // 整条链路丢进回调异步做（下载在网络线程，转码/上传在回调线程），
                    // 不占住当前 worker 线程；任一步失败就把平台原报错当文本发出去。
                    downloadRemoteAudioToTempAsync(uploadedUrl, kRemoteAudioMaxBytes,
                        [this, type, openid, pname, fileType, msgid, is_wakeup, mode, 发送类型, noref, now_us, segCtx, segCb, fileInfo]
                        (const QString &localFile) {
                            // 失败兜底：把平台返回的报错内容原样发出去
                            auto sendOriginError = [&]() {
                                QString errInfo = fileInfo;   // 非 const：send_* 收的是 QString&
                                if (segCtx.openid.isEmpty())
                                    send_messages(type,openid,pname,errInfo,msgid,is_wakeup,mode,发送类型,noref);
                                else
                                    send_msgAsync(type,openid,pname,errInfo,msgid,is_wakeup,mode,发送类型,noref,segCb);
                            };

                            if (localFile.isEmpty()) {        // 超 50MB / 下载失败（日志已在下载里记过）
                                sendOriginError();
                                return;
                            }

                            // 产物路径一律由「源」推导（见 audioM4aPathFor 的注释）
                            QString usePath = audioM4aPathFor(localFile);
                            if (usePath.isEmpty() || !QFile::exists(usePath))
                                usePath = convertAudioToSilk(localFile);   // 失败时它返回源路径本身

                            QStringList segs;
                            const double durSec = probeAudioDurationSec(usePath);
                            if (durSec > AUDIO_SEG_MAX_SEC)
                                segs = splitM4aSegments(usePath, AUDIO_SEG_MAX_SEC);
                            else if (durSec > 0)
                                segs = QStringList{ usePath };   // 本地这份其实没超长 → 直接重发一次

                            if (segs.isEmpty()) {
                                AppendEventLog(QStringLiteral("远程音频超时长处理失败（无法切段），按原内容输出：")
                                               + fileInfo);
                                sendOriginError();
                                return;
                            }

                            for (int gi = 0; gi < segs.size(); ++gi) {
                                bool okSeg = true;
                                qint64 segExp = 0; QString segMd5;
                                QString segInfo = uploadRichMediaPoolA(type, openid, fileType, segs[gi], segExp, segMd5, okSeg, /*usePool=*/false);   // 非 const：send_messages/send_msgAsync 收的是 QString&
                                // 同样只有最后一段带回调（见上面 lastSeg 的注释）
                                const bool lastInner = (gi + 1 == segs.size());
                                if (!okSeg) {
                                    if (segCtx.openid.isEmpty())
                                        send_messages(type,openid,pname,segInfo,msgid,is_wakeup,mode,发送类型,noref);
                                    else
                                        send_msgAsync(type,openid,pname,segInfo,msgid,is_wakeup,mode,发送类型,noref,
                                                      lastInner ? segCb : Callback());
                                } else if (!segInfo.isEmpty()) {
                                    send_Media(type, openid,pname, segInfo,now_us, msgid,is_wakeup,noref,
                                               lastInner ? segCtx : ctxNoCb(segCtx), fileType,
                                               /*multiPart=*/segs.size() > 1);
                                }
                            }
                        });
                    text.remove(info.start, info.length);   // 标签照样要从待发文本里摘掉
                    continue;                               // 下载/转码/发送都在回调里做了
                }
                if(!ok)
                {
                    segAllOk = false;   // 有段失败 → 本次不写缓存（避免缓存成半截音频）
                    if(segCtx.openid.isEmpty())
                        send_messages(type,openid,pname,fileInfo,msgid,is_wakeup,mode,发送类型,noref);
                    else
                        send_msgAsync(type,openid,pname,fileInfo,msgid,is_wakeup,mode,发送类型,noref,segCb);
                }else if (!fileInfo.isEmpty()) {
                    // 累计本段的裸 file_info；真正的写缓存放到整条循环之后一次性做（见下）——
                    // 逐段各写一次会互相覆盖，最后只剩最后一段。
                    segInfos << fileInfo;
                    if (expireTime > 0)
                        segExpire = (segExpire == 0) ? expireTime : qMin(segExpire, expireTime);
                }

                if (ok && !fileInfo.isEmpty()) {

                    response = send_Media(type, openid,pname, fileInfo,now_us, msgid,is_wakeup,noref,segCtx, fileType,
                                          /*multiPart=*/upPaths.size() > 1);
                }
            }

            // 写缓存：整文件/单段存 1 条、真多段存 N 条，同一格式 "过期秒||||info1||info2||…"
            putMediaUploadCache(QString::number(m_info->appid_int), mediaType, fileMd5, segInfos, segExpire, segAllOk);
        }else{
            // 缓存命中：1 条 = 整文件 / 单段；N 条 = 上次切好的多段 → 逐条原样重发
            QStringList infos;
            for (const QString &fi : std::as_const(cachedInfos))
                if (!fi.isEmpty()) infos << fi;
            for (int i = 0; i < infos.size(); ++i) {
                // 同样只有最后一条带 ctx（回调恰好一次）—— 见上面 lastSeg 的注释
                const bool last = (i + 1 == infos.size());
                response = send_Media(type, openid,pname, infos[i],now_us, msgid,is_wakeup,noref,
                                      last ? ctx : ctxNoCb(ctx), fileType,
                                      /*multiPart=*/infos.size() > 1);
            }
        }
        qDebug () << text;
        text.remove(info.start, info.length);
        qDebug() <<text;
    }

    return response;
}



void QQBotClient::initjgt(QJsonObject &json,const QJsonArray &prompt_keyboard,const QString &message_reference, const QString &msgid, bool is_wakeup,int logindex)
{
    if (!message_reference.isEmpty()) {
        QJsonObject refObj;
        refObj["message_id"] = message_reference;
        refObj["ignore_get_message_error"] = false;
        json["message_reference"] = refObj;
    }
    if(logindex!=1)
         json["msg_seq"] = m_info->message_sent;


    if (!is_wakeup) {
        if (msgid.contains("INTERACTION") || msgid.contains("FRIEND_ADD") || msgid.contains("GROUP_MEMBER") || msgid.startsWith("GROUP_JOIN_REQUEST")) //GROUP_MEMBER_ADD
            json["event_id"] = msgid;
        else
            json["msg_id"] = msgid;
    } else {
        json["is_wakeup"] = is_wakeup;
    }
    if (!prompt_keyboard.isEmpty()) {
        json["prompt_keyboard"] = QJsonObject{
            {"keyboard", QJsonObject{
                            {"content", QJsonObject{
                                            {"rows", QJsonArray{
                                                         QJsonObject{{"buttons", prompt_keyboard}}
                                                     }}
                                        }}
                        }}
        };
    }
    if(logPage->wanzjson) logPage->onNewLogAdded(QJsonDocument(json).toJson());
}


QJsonObject parseLabelsToKeyboard(const QString &labelsText) {
    QJsonArray rowsArray;

    //qDebug() << labelsText;
    const QStringList lines = labelsText.split('\r', Qt::SkipEmptyParts);
    for (const QString &line : lines) {

        QRegularExpression re(R"(\[([^\]]*)\])");
        QRegularExpressionMatchIterator it = re.globalMatch(line);

        QJsonArray buttonsArray;
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            QString content = match.captured(1).trimmed(); // 去掉首尾空格

            // 按逗号分割字段（最多9个字段，索引0~8）
            QStringList fields = content.split(',');
            while (fields.size() < 9) fields.append(QString()); // 补足空字段


            QString label = fields[0].trimmed();
            QString actionData = fields[1].trimmed();
            int actionType = fields[2].trimmed().isEmpty() ? 2 : fields[2].trimmed().toInt();
            bool enter = (fields[3].trimmed() == "1");   // 立即发送
            bool reply = (fields[4].trimmed() == "1");   // 引用
            int style = fields[5].trimmed().isEmpty() ? 1 : fields[5].trimmed().toInt();
            QString modalContent = fields[6].trimmed();
            QString modalConfirm = fields[7].trimmed();
            QString modalCancel = fields[8].trimmed();

            // 跳过标题为空的按钮（可选）
            if (label.isEmpty()) continue;

            // ---------- 构建按钮 JSON（参考 ButtonData::toJson） ----------
            QJsonObject buttonObj;

            // render_data
            QJsonObject renderData;
            renderData["label"] = label;
            renderData["visited_label"] = label;   // 与原逻辑一致，通常相同
            if (style == 9999) {
                QJsonObject styleObj;
                styleObj["font_size"] = "small";
                renderData["style"] = styleObj;
            } else {
                renderData["style"] = style;
            }
            buttonObj["render_data"] = renderData;

            // action
            QJsonObject action;
            action["type"] = actionType;
            action["data"] = actionData;
            action["unsupport_tips"] = "当前版本不支持该按钮";
            if (reply) action["reply"] = reply;   // 引用
            if (enter) action["enter"] = enter;   // 立即发送
            // anchor 默认不设置

            // permission (默认所有人可用)
            QJsonObject permission;
            permission["type"] =2;
            action["permission"] = permission;

            // modal（仅当弹出内容非空时添加）
            if (!modalContent.isEmpty()) {
                QJsonObject modal;
                modal["content"] = modalContent;
                if (!modalConfirm.isEmpty()) modal["confirm_text"] = modalConfirm;
                if (!modalCancel.isEmpty()) modal["cancel_text"] = modalCancel;
                action["modal"] = modal;
            }

            // subscribe_data 本例暂不处理
            buttonObj["action"] = action;

            buttonsArray.append(buttonObj);
        }

        if (!buttonsArray.isEmpty()) {
            QJsonObject rowObj;
            rowObj["buttons"] = buttonsArray;
            rowsArray.append(rowObj);
        }
    }

    QJsonObject result;
    result["rows"] = rowsArray;
    return result;
}
void QQBotClient::bianl(int type,int log, QString &text,QJsonValue  &keyboard,QJsonArray &prompt_keyboard,const QString &openid,QString &mb)
{
    QString keyboard_data = extractBetween(text,"#b:#","#b:#");
    if(!keyboard_data.isEmpty())
        text=replaceBetweenAll(text,"#b:#","#b:#","");
    mb = extractBetween(text,"#mb:#","#mb:#");
    if(!mb.isEmpty())
        text=replaceBetweenAll(text,"#mb:#","#mb:#","");
    int index = mapTypeToTabIndex(type);

    Message log2;
    g_logdb[index]->readLog(m_info->appid,openid,log,log2);
    const QList<mdbtn> &bts = m_info->mdbtnlist;
    for (const mdbtn &bt : bts)
    {

        bool isok=false;
        for(int i=0;i< bt.zl.size();++i)
        {
            switch (bt.pplx) {
            case 0:
                if(QString::compare(log2.msg, bt.zl[i], Qt::CaseInsensitive) != 0) continue; //判断等于
                break;
            case 1:
                if(!log2.msg.startsWith(bt.zl[i],Qt::CaseInsensitive)) continue; //判断头部
                break;
            case 2:
                if(!log2.msg.contains(bt.zl[i],Qt::CaseInsensitive)) continue; //判断包含
                break;
            case 3:
                if(!text.contains(bt.zl[i],Qt::CaseInsensitive)) continue;  //判断text 包含
                break;
            default:
                continue;
            }
            bool ok=false;
            for(int i2=0;i2< bt.jzc.size();++i2)
            {
                if(text.contains(bt.jzc[i2]))
                {
                    ok=true;
                    break;
                }
            }
            if(ok) continue;

            int len = bt.hxc.size();
            if (len > 64) len = 64;                 // 最多只考虑前 64 个（与易语言一致）
            int want = qMin(len, 3);                // 最多取 3 个
            quint64 usedMask = 0;                   // 每一位代表一个索引是否被选过
            for (int i = 0; i < want; ++i) {
                int idx;
                for (int tries = 0; tries < 128; ++tries) {
                    idx = QRandomGenerator::global()->bounded(len);   // 0 ~ len-1
                    if (!(usedMask & (1ULL << idx))) {
                        usedMask |= (1ULL << idx);   // 标记已使用
                        break;
                    }
                }
                QJsonObject button;
                button["id"] = QString("A%1").arg(QRandomGenerator::global()->bounded(40, 23124));
                QJsonObject renderData;
                renderData["label"] = bt.hxc[idx];
                renderData["style"] = 2;
                button["render_data"] = renderData;
                prompt_keyboard.append(button);
            }
            keyboard = bt.btnjson;
            isok=true;
            break;
        }
        if(isok) break;
    }

    if (keyboard.isUndefined() || keyboard.isNull() || (keyboard.isObject() && keyboard.toObject().isEmpty()))
    {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(keyboard_data.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError)
        {
            if (doc.isObject()) {
                keyboard = doc.object();
            } else if (doc.isArray()) {
                keyboard = doc.array();  // 这就兼容了你说的“实际是数组”的情况
            } else {
                keyboard = parseLabelsToKeyboard(keyboard_data);
            }
        }
        else
        {
            keyboard = parseLabelsToKeyboard(keyboard_data);
        }
    }
    //小尾巴

    const QList<zdywb> &wb= m_info->zdywblist;
    for (const zdywb &w : wb)
    {
        bool isok=false;
        for(int i=0;i< w.zl.size();++i)
        {
            switch (w.pplx) {
            case 0:
                if(QString::compare(log2.msg, w.zl[i], Qt::CaseInsensitive) != 0) continue; //判断等于
                break;
            case 1:
                if(!log2.msg.startsWith(w.zl[i],Qt::CaseInsensitive)) continue; //判断头部
                break;
            case 2:
                if(!log2.msg.contains(w.zl[i],Qt::CaseInsensitive)) continue; //判断包含
                break;
            case 3:
                if(!text.contains(w.zl[i],Qt::CaseInsensitive)) continue;  //判断text 包含
                break;
            default:
                continue;
            }
            bool ok=false;
            for(int i2=0;i2< w.jzc.size();++i2)
            {
                if(text.contains(w.jzc[i2]))
                {
                    ok = true;
                    break;
                }
            }
            if(ok) continue;
            for(int i2=0;i2<w.thck.size();++i2)
            {
                text.replace(w.thck[i2],w.thcv[i2]);

            }

            if(!w.data.isEmpty())  
            {
                QString data = w.data;
                data.replace("【*】",text);
                text = data;
            }
            isok=true;
            break;
        }


        if(isok) break;
    }


    if(text.contains("{{name}}"))
    {
        if(g_botdb.contains(m_info->appid_int))  {
            auto *db = g_botdb [m_info->appid_int];
            QString username;
            db->getOrUpdateUser(openid,username);
            text.replace("{{name}}", username);
        }

    }


    text.replace("{{appid}}", m_info->appid);
    text.replace("{{botname}}", m_info->nickname);
    text.replace("{{group}}", openid);
    text.replace("{{user}}", log2.user);
    text.replace("{{msg}}", log2.msg);
    text.replace("{{昵称}}", log2.name);
    text.replace("{{msgid}}", log2.ch);
    static QRegularExpression re("\\{\\{([^}]+)\\}\\}");
    QRegularExpressionMatchIterator it = re.globalMatch(text);

    QList<QPair<int, int>> ranges;
    QStringList replacements;

    while (it.hasNext()) {
        auto match = it.next();
        QString inner = match.captured(1).trimmed();

        // 只处理含有逗号的关键字（参数化）
        if (!inner.contains(','))
            continue;

        QStringList parts = inner.split(',');
        if (parts.isEmpty())
            continue;

        QString keyword = parts[0].trimmed();
        QString replacement;

        if (keyword == "随机数") {
            int minVal = 0, maxVal = 100;   // 默认范围
            if (parts.size() >= 3) {
                minVal = parts[1].trimmed().toInt();
                maxVal = parts[2].trimmed().toInt();
            } else if (parts.size() == 2) {
                maxVal = parts[1].trimmed().toInt();
            }
            if (minVal > maxVal) qSwap(minVal, maxVal);
            int random = QRandomGenerator::global()->bounded(minVal, maxVal + 1);
            replacement = QString::number(random);
        }
        else if (keyword == "选择") {
            // 从第2个参数开始均为选项
            if (parts.size() < 2) {
                replacement = match.captured(0);  // 参数不足则保留原样
            } else {
                QStringList options;
                for (int i = 1; i < parts.size(); ++i) {
                    options << parts[i].trimmed();
                }
                int idx = QRandomGenerator::global()->bounded(options.size());
                replacement = options[idx];
            }
        }
        else if (keyword == "日期") {
            QString format = "yyyy-MM-dd hh:mm:ss";   // 默认格式
            if (parts.size() >= 2) {
                format = parts[1].trimmed();
            }
            replacement = QDateTime::currentDateTime().toString(format);
        }
        else {
            // 未知关键字：原样保留
            replacement = match.captured(0);
        }

        ranges.append(qMakePair(match.capturedStart(0), match.capturedLength(0)));
        replacements.append(replacement);
    }

    // 从后往前替换
    for (int i = ranges.size() - 1; i >= 0; --i) {
        text.replace(ranges[i].first, ranges[i].second, replacements[i]);
    }
}


QString processText(const QString &text, int timeoutMs = 30000);
QPair<int, QString> splitWrappedMsgId(const QString &wrapped);
QString QQBotClient::send_msgAsync(int type, const QString &openid,const QString &pname, QString &text,
                              const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref,Callback cb)
{
    CosPutPendingGuard _cosPutPoolGuard;   // 发送完成后（含异常路径）把本次用过的 put 链接立即回池
    if(type==18) type =0;

    if(type<0 || type >3 ) return R"({"msg":"发送类型错误 不在0-3之间"})";

    // 插件侧的 pname 传的是**发起方裸 uuid**（展示标签一律以 '[' 开头）→ 按该插件**自己的**规则表
    // 把正文里的 [注册原名](...) 改写成改名后的指令（插件只认注册名，改不了这个）。
    // 框架 / AI / WebUI / 关键词匹配传的 "[某某|%1ms]" → uuid 空 → 正文一字不动，零副作用。
    text = redirectPluginCmdsMarkdown(text, senderUuidFromPname(pname), m_info->appid_int);

    QString newtext;
    if(m_info->xxwb.isEmpty())
        newtext = text;
    else
        newtext = text + m_info->xxwb;
    if(text.contains("#python"))
    {
        MessageEvent ev;
        ev.appid = m_info->appid_int;
        ev.groupId = openid;
        ev.msgId=msgid;
        ev.type = type;
        newtext =python_code(text,ev);
    }
    if(text.contains("[get url") || text.contains("[post url")){
        auto [index, realMsgId] = splitWrappedMsgId(msgid);
        QJsonValue keyboard;
        QJsonArray prompt_keyboard;
        QString mb;
        QString textB = normalizeNewlinesToCR(newtext); //处理换行
        bianl(type,index,textB,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴
        auto *processor = new AsyncApiProcessor(textB, [this,type,openid,pname,msgid,is_wakeup,mode,sendType,noref,mb,prompt_keyboard,keyboard,cb](const QString &result) {
            QString text = result;
            return send_messagesAsync2(type,openid,pname,text,msgid,is_wakeup,mode,sendType,noref,mb,prompt_keyboard,keyboard,cb);
        });
        processor->start();
        return "{}";
    }

    // cb 必须透传下去，否则调用方等不到回调（send_messagesAsync 里才会 ctx.cb = cb）
    //QElapsedTimer t;
    //t.start();
    QString r = send_messagesAsync(type,openid,pname,newtext,msgid,is_wakeup,mode,sendType,noref,cb);
    //qDebug () << t.elapsed();
    return r;
}

// 发送耗时统计：账本结构体在 qqbotclient.h（MsgTimeStat）—— 因为「处理/图片/发送」三段要拼进
// 「[插件名|%1ms]」那个方括号，而拼的动作在 core/api2.cpp 的 addmsglog 收尾处。
// 这里只负责在关键节点 mark() / addMedia() / addSend()，再把账本交给 addmsglog。

QString QQBotClient::send_messages(int type, const QString &openid,const QString &pname, QString &text,
                                    const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref)
{
    CosPutPendingGuard _cosPutPoolGuard;   // 发送完成后（含异常路径）把本次用过的 put 链接立即回池
    auto st = std::make_shared<MsgTimeStat>();   // 耗时统计账本（开关关掉时全程空转）
    if(type!=18){
        if(type<0 || type >3 ) return R"({"msg":"发送类型错误 不在0-3之间"})";
    }

    // 同 send_msgAsync：pname 是插件侧传的裸 uuid 时，按该插件把 [注册原名]() 改成改名后的指令
    // （改名可以按机器人分开配 → 传本客户端的 appid，取这个号实际生效的名字）
    text = redirectPluginCmdsMarkdown(text, senderUuidFromPname(pname), m_info->appid_int);

    QString newtext;
    if(m_info->xxwb.isEmpty())
        newtext = text;
    else
        newtext = text+m_info->xxwb;

    if(text.contains("#python"))
    {
        MessageEvent ev;
        ev.appid = m_info->appid_int;
        ev.groupId = openid;
        ev.msgId=msgid;
        ev.type = type;
        newtext =python_code(text,ev);
    }

    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    QJsonValue keyboard;
    QJsonArray prompt_keyboard;
    QString message_reference,mb;

    newtext=normalizeNewlinesToCR(newtext); //处理换行

    bianl(type,index,newtext,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴
    if(newtext.contains("[get url") || newtext.contains("[post url"))
        newtext = processText(newtext);
    // 处理耗时基准：addmsglog 里 [插件名|%1ms] 的那个 %1 就是它。
    // ⚠ 这行**不归 g_msg_time_stat 管** —— 全项目十几个 "[xx|%1ms]" 串都吃这个值，
    //   关掉统计只能停掉「新增的图片/返回耗时」，不能让它们全变成 0ms。
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    QString newtext2 = sendOneMedia(type,openid,pname,newtext,now_us,msgid,is_wakeup,mode,sendType,noref,MessageLogContext());//检查也没有要发送 的语言视频 文件 原位修改text
    if (newtext.isEmpty()) return newtext2;

    bool mbise= mb.isEmpty();
    if(newtext.isEmpty() && mbise)
    {
        QString response = R"({"message":"发送内容不能为空"})";
        addmsglog(response,index,pname,text,now_us,type,openid,st);
        return response;
    }
    int seq_index=0;
    bool ok=false;
    if(index>=0){

        g_logdb[type+1]->setBuffer_250(index,ok);
    }
    if(ok)
        seq_index = 1;
    else if(noref) return "{}";
    else seq_index = QRandomGenerator::global()->bounded(2,100);

    QString response,fileinfo;
    if(type==1 || type ==3)
    {
        if(!mbise)
        {
            st->mark();   // 图片耗时起点
            newtext = processImageTags(mb,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            st->addMedia();
            QString textA = forbidden->filterText(newtext);
            response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
            st->addSend();
            addmsglog(response,index,pname,text,now_us,type,openid,st);
            return response;
        }
        if(!mode && m_info->markdown_pd_mb || mode && sendType==2) //模板
        {
            st->mark();
            newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            st->addMedia();
            QString textA = forbidden->filterText(newtext);
            //response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup);
            response = R"({"message":"暂时不支持模板方式"})";
        }else if(!mode && m_info->markdown_pd || mode && sendType==1) //原生
        {
            st->mark();
            newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
            st->addMedia();
            QString textA = forbidden->filterText(newtext);//违禁词过滤
            response = send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
            st->addSend();
        }else {
            st->mark();
            newtext = processImageTags(newtext,2,fileinfo,type,openid,message_reference);//处理图片 + 回复
            st->addMedia();
            QString textA = forbidden->filterText(newtext);//违禁词过滤
            QString url = get_url(type, openid, "messages");
            response = send_messages_pd(url,realMsgId,textA,fileinfo,message_reference,seq_index,MessageLogContext(),noref);
            st->addSend();
        }
        addmsglog(response,index,pname,newtext,now_us,type,openid,st);
        return response;
    }
    if(!mbise)
    {
        st->mark();
        newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        st->addMedia();
        QString textA = forbidden->filterText(newtext);
        response = send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
        st->addSend();
    }
    if(!mode && m_info->markdown || mode && sendType==1)
    {
        st->mark();
        newtext = processImageTags(newtext,1,fileinfo,type,openid,message_reference);//处理图片 + 回复
        st->addMedia();
        QString textA = forbidden->filterText(newtext);
        response = send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
        st->addSend();
    }else{

        st->mark();
        newtext = processImageTags(newtext,0,fileinfo,type,openid,message_reference);//处理图片 + 回复
        st->addMedia();
        QString textA = forbidden->filterText(newtext);
        response = send_messages(type, openid, textA,fileinfo,prompt_keyboard, message_reference, realMsgId, is_wakeup,seq_index,MessageLogContext(),noref);
        st->addSend();
    }
    addmsglog(response,index,pname,newtext,now_us,type,openid,st);
    return response;


}

// send_messagesAsync / send_messagesAsync2 的**公共实现**（派发逻辑只此一份）。
//   两者真正的差别只有两处，全部用参数表达，不再各写一遍 200 行：
//     ① preParsed：Async2 的 mb / 按钮 / 小尾巴是 send_msgAsync 在**取 api 之前**就解析好的
//        （[get url] 分支：按钮必须先解析，正文要等 api 回来才是最终版），这里不能重复 bianl；
//     ② 媒体：Async2 原先调**同步** sendOneMedia（会把主线程堵在转码/上传里），现在两者都走异步链。
//   seq_index 两者完全一致：命中序号 → 1；否则随机 2..99（原先 Async2 写死 2，已统一）。
// ⚠ 回调契约：**每条路径都必须把 ctx.cb 触发恰好一次** —— send_messages* 是唯一会触发它的地方，
//   所以凡是不派发 send_messages* 就返回的路径（纯媒体 / noref / 模板不支持），都在下面手工补一次。
QString QQBotClient::sendMessagesAsyncCore(int type, const QString &openid, const QString &pname, QString text,
                                          const QString &msgid, bool is_wakeup, bool mode, int sendType,
                                          bool noref, Callback cb, bool preParsed,
                                          const QString &mbIn, const QJsonArray &promptKbIn,
                                          const QJsonValue &kbIn)
{
    CosPutPendingGuard _cosPutPoolGuard;   // 发送完成后（含异常路径）把本次用过的 put 链接立即回池
    // 耗时统计账本（开关关掉时全程空转）：挂到 ctx 上（见下面的 ctxT），由 addmsglog 收尾结算。
    auto st = std::make_shared<MsgTimeStat>();

    QString newtext = text;
    if(!preParsed && text.contains("#python"))
    {
        MessageEvent ev;
        ev.appid = m_info->appid_int;
        ev.groupId = openid;
        ev.msgId=msgid;
        ev.type = type;
        newtext =python_code(text,ev);
    }

    // 处理耗时基准（[插件名|%1ms] 的 %1）—— 同上，不归 g_msg_time_stat 管
    auto now = std::chrono::steady_clock::now();
    qint64 now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    MessageLogContext ctx;
    ctx.index = 0;
    ctx.pname = pname;                     // 拷贝
    ctx.jsonString = text;
    ctx.now_us = now_us;
    ctx.type = type;
    ctx.openid = openid;
    ctx.cb =cb;
    auto [index, realMsgId] = splitWrappedMsgId(msgid);
    ctx.index = index;
    QJsonValue keyboard = kbIn;
    QJsonArray prompt_keyboard = promptKbIn;
    QString message_reference,mb = mbIn;
    QString textB;
    if (preParsed) {
        // 换行归一化 + bianl 都已由调用方做过（send_msgAsync），这里照搬原文，重复解析会把
        // 同一场合的按钮/小尾巴再拆一遍（按钮会叠加、小尾巴会重复追加）
        textB = newtext;
    } else {
        textB = normalizeNewlinesToCR(newtext); //处理换行
        bianl(type,index,textB,keyboard,prompt_keyboard,openid,mb);//挂载按钮解析 小尾巴
    }



    // ── 媒体标签：异步（回调版）；正文发送挪进回调，等媒体处理完再走 ──
    // 原先是同步 sendOneMedia —— 带媒体的异步发送会把**调用线程**（Python 那条共享 asyncio 线程、
    // 插件页线程、以及 Async2 走的主线程）堵在 ffmpeg 转码 / 上传网络等待里。改成回调版后立刻返回 "{}"。
    const QString msgIdCopy = realMsgId;   // structured binding 不能被 lambda 捕获（C++17），先拷贝
    const int indexCopy = index;
    const QString mbCopy = mb;

    auto sendTextPart = [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, indexCopy, mbCopy,
                         is_wakeup, mode, sendType, noref, ctx, st](const QString &textB, const QString &mediaResp) {
        // 账本跟着 ctx 往下走：send_messages* 收尾（addmsglog）时把「图/返/总」拼进 pname 方括号。
        // 开关关掉时 ctxT 就是 ctx 的副本，cb 原样，零额外开销。
        MessageLogContext ctxT = ctx;
        ctxT.tstat = st;
        // 正文被媒体标签吃光：媒体那步已经在 sendOneMediaAsync 里发完了，但下面**所有**分支都以
        // send_messages* 结尾，ctx.cb 只在那些函数里被触发 —— 直接 return 的话调用方的回调
        // 永远不会来（插件按 _ASYNC_TIMEOUT 干等 60s，Python 的 await 同理）。
        // 这里把媒体那次的响应原样交出去；mediaResp 为空 = 压根没有媒体（消息本来就是空的）→ 回 {} 当作"已处理"。
        if (textB.isEmpty()) {
            if (ctxT.cb) ctxT.cb(mediaResp.isEmpty() ? QStringLiteral("{}") : mediaResp,
                                 QNetworkReply::NoError);
            return;
        }

        bool mbise= mbCopy.isEmpty();
        int seq_index=0;
        bool ok=false;
        if(indexCopy>=0){

              g_logdb[type+1]->setBuffer_250(indexCopy,ok);
        }
        if(ok)
            seq_index = 1;
        else if(noref) {
            // 与上面纯媒体同理：这条路径不派发 send_messages*，ctx.cb 不会被触发 → 调用方干等超时
            if (ctxT.cb) ctxT.cb(QStringLiteral("{}"), QNetworkReply::NoError);
            return;
        }
        else seq_index = QRandomGenerator::global()->bounded(2,100);

        QString fileinfo;
        if(type==1 || type ==3)
        {
            if(!mbise)
            {
                st->mark();   // 图片耗时起点（回调里 addMedia 收口）
                processImageTagsAsync(mbCopy,1,type,openid,
                    [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctxT, st, noref]
                    (const QString &tb, const QString &, const QString &mr) {
                        st->addMedia();
                        QString textA = forbidden->filterText(tb);
                        send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctxT,noref);
                    });
                return;
            }
            if(!mode && m_info->markdown_pd_mb || mode && sendType==2) //模板
            {
                // 与同步版一致：模板方式暂不支持（同步版这里返回 {"message":"暂时不支持模板方式"}，
                // 异步版无返回值，记一条日志）。这里也**不再**预处理图片 —— 反正不发，
                // processImageTags 只会白跑一遍图片下载/处理。
                AppendEventLog(QStringLiteral("异步发送：模板方式（markdown_pd_mb / sendType==2）暂不支持，已跳过正文发送"));
                // 没有派发 send_messages*，必须自己把本次的「响应」交出去（与同步版返回值对齐）
                if (ctxT.cb) ctxT.cb(QStringLiteral(R"({"message":"暂时不支持模板方式"})"),
                                     QNetworkReply::NoError);
            }else if(!mode && m_info->markdown_pd || mode && sendType==1) //原生
            {
                st->mark();
                processImageTagsAsync(textB,1,type,openid,
                    [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctxT, st, noref]
                    (const QString &tb, const QString &, const QString &mr) {
                        st->addMedia();
                        QString textA = forbidden->filterText(tb);//违禁词过滤
                        send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctxT, noref);
                    });
            }else {
                st->mark();
                processImageTagsAsync(textB,2,type,openid,
                    [this, type, openid, msgIdCopy, seq_index, ctxT, st, noref]
                    (const QString &tb, const QString &fi, const QString &mr) {
                        st->addMedia();
                        QString textA = forbidden->filterText(tb);//违禁词过滤
                        QString url = get_url(type, openid, "messages");
                        send_messages_pd(url,msgIdCopy,textA,fi,mr,seq_index,ctxT,noref);
                    });
            }
            return;
        }

        if(!mbise) //模板 一般用不到
        {
            st->mark();
            processImageTagsAsync(textB,1,type,openid,
                [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctxT, st, noref]
                (const QString &tb, const QString &, const QString &mr) {
                    st->addMedia();
                    QString textA = forbidden->filterText(tb);
                    send_messages_mb(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctxT, noref);
                });
        }
        if(!mode && m_info->markdown || mode && sendType==1)
        {
            st->mark();
            processImageTagsAsync(textB,1,type,openid,
                [this, type, openid, prompt_keyboard, keyboard, msgIdCopy, is_wakeup, seq_index, ctxT, st, noref]
                (const QString &tb, const QString &, const QString &mr) {
                    st->addMedia();
                    QString textA = forbidden->filterText(tb);//违禁词过滤
                    send_messages_markdown(type, openid, textA, prompt_keyboard,keyboard,mr, msgIdCopy, is_wakeup,seq_index,ctxT, noref);
                });
        }else{
            st->mark();
            processImageTagsAsync(textB,0,type,openid,
                [this, type, openid, prompt_keyboard, msgIdCopy, is_wakeup, seq_index, ctxT, st, noref]
                (const QString &tb, const QString &fi, const QString &mr) {
                    st->addMedia();
                    QString textA = forbidden->filterText(tb);//违禁词过滤
                    send_messages(type, openid, textA,fi,prompt_keyboard, mr, msgIdCopy, is_wakeup,seq_index,ctxT, noref);
                });
        }
    };

    sendOneMediaAsync(type, openid, pname, textB, now_us, msgid, is_wakeup, mode, sendType, noref, ctx,
                      [sendTextPart](const QString &textOut, const QString &mediaResp) {
                          sendTextPart(textOut, mediaResp);
                      });
    return "{}";
}
// ── 两个公开入口：都只是「预处理差异」的描述，派发逻辑全在 sendMessagesAsyncCore ──
QString QQBotClient::send_messagesAsync(int type, const QString &openid,const QString &pname, QString &text,
                                   const QString &msgid,bool is_wakeup,bool mode,int sendType,bool noref,Callback cb)
{
    // preParsed=false：自己跑 #python / 换行归一化 / bianl
    return sendMessagesAsyncCore(type, openid, pname, text, msgid, is_wakeup, mode, sendType, noref, cb,
                                 /*preParsed=*/false, QString(), QJsonArray(), QJsonValue());
}

QString QQBotClient::send_messagesAsync2(int type, const QString &openid, const QString &pname, QString &text,
                                         const QString &msgid, bool is_wakeup, bool mode, int sendType, bool noref, const QString &mb2,
                                         const QJsonArray &prompt_keyboard, const QJsonValue  &keyboard, Callback cb)
{
    // 与 send_messagesAsync 共用 sendMessagesAsyncCore，差别只在预处理：
    //   mb / 按钮 / 小尾巴 已由 send_msgAsync 在**取 api 之前**解析好（[get url] 分支：按钮必须先
    //   解析，而正文要等 api 异步回来才是最终版本）→ preParsed=true，不再 bianl，
    //   也不重复跑 #python 和 normalizeNewlinesToCR（那两件在 send_msgAsync 里已经做完了）。
    // 顺带修掉两个老问题：媒体改走异步链（原来同步 sendOneMedia 会把主线程堵在转码/上传里），
    // 且不会再出现「纯媒体时 send_Media 先触发一次 ctx.cb、正文再触发一次」的双回调。
    return sendMessagesAsyncCore(type, openid, pname, text, msgid, is_wakeup, mode, sendType, noref, cb,
                                 /*preParsed=*/true, mb2, prompt_keyboard, keyboard);
}

