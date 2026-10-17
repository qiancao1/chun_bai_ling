#include "qcffmpeg.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>
#include <QRunnable>
#include <QUrl>

#include <memory>

#include "global.h"       // ffmpegdiv / AppendEventLog
#include "netmanager.h"   // 复用全局网络线程（不新起 QNAM）

/* ===========================================================================
 * 0. DLL 里的 C 接口（与 libs/ffmpeg/qc_ffmpeg.h 必须逐字段一致）
 *
 * ⚠ 故意**不 include** 那个头：它把 QCFF_API 定义成 __declspec(dllexport)，
 *   被 exe 编译单元包含会往 exe 的导出表里塞垃圾（还会触发 LNK4197 警告）。
 *   这里只镜像需要的那几个声明，结构体是 5 个 int 的 POD，布局没有歧义。
 * =========================================================================== */
struct QcAudioOptsLocal
{
    int sample_rate;
    int channels;
    int bitrate_kbps;
    int quality;
    int overlay;
};

namespace {

typedef const char *(*Fn_version)(void);
typedef int (*Fn_last_error)(char *buf, int buf_size);
typedef int (*Fn_probe_media)(const char *src, double *out_ms,
                              int *out_has_video, int *out_has_audio);
typedef int (*Fn_probe_dur)(const char *src, double *out_ms);
typedef int (*Fn_convert)(const char *src, const char *dst, const QcAudioOptsLocal *opts);
typedef int (*Fn_split)(const char *src, const char *out_dir, int max_sec,
                        const char *ext, char *paths_buf, int buf_size, int *out_count);
typedef int (*Fn_split_convert)(const char *src, const char *out_dir, int max_sec,
                                const char *ext, const QcAudioOptsLocal *opts,
                                char *paths_buf, int buf_size, int *out_count);

struct Api
{
    QLibrary         *lib = nullptr;
    Fn_version        version      = nullptr;
    Fn_last_error     last_error   = nullptr;
    Fn_probe_media    probe_media  = nullptr;   // 新版才有，缺了就用 probe_dur 兜底
    Fn_probe_dur      probe_dur    = nullptr;
    Fn_convert        convert      = nullptr;
    Fn_split          split        = nullptr;
    Fn_split_convert  split_conv   = nullptr;
    QString           path;
};

Api      g_api;
QMutex   g_apiMu;

QString takeLastError(const Api &a)
{
    if (!a.last_error) return QString();
    char buf[1024] = {0};
    a.last_error(buf, (int)sizeof buf);
    return QString::fromUtf8(buf);
}

// 取函数表快照：只在拷贝时持锁，**真正调用期间不持锁**。
// ⚠ 这一点很关键：转码一个几分钟的视频要几十秒，持锁的话媒体线程里任何一次
//   loaded()/probe() 都会被卡住，整条媒体链就停了。函数指针是常量，拷贝走没问题。
bool snapshot(Api *out)
{
    QMutexLocker lk(&g_apiMu);
    if (!g_api.lib) return false;
    *out = g_api;
    return true;
}

// 依次尝试的候选路径：程序目录 → 程序目录/ffmpeg → 设置里的目录 → AppData
QStringList candidateDirs()
{
    QStringList dirs;
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        dirs << appDir;
        dirs << QDir(appDir).filePath(QStringLiteral("ffmpeg"));
    }
    if (!ffmpegdiv.isEmpty())
        dirs << ffmpegdiv;

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dataDir.isEmpty())
        dirs << dataDir;

    dirs.removeDuplicates();
    return dirs;
}

// 真正去 dlopen 一次；成功则把函数表填好
bool tryLoadFrom(const QString &file)
{
    QLibrary *lib = new QLibrary(file);
    if (!lib->load()) {
        const QString e = lib->errorString();
        lib->unload();
        delete lib;
        Q_UNUSED(e);
        return false;
    }

    Api a;
    a.lib        = lib;
    a.path       = file;
    a.version    = (Fn_version)   lib->resolve("qc_version");
    a.last_error = (Fn_last_error)lib->resolve("qc_last_error");
    a.probe_dur  = (Fn_probe_dur) lib->resolve("qc_probe_duration_ms");
    a.probe_media= (Fn_probe_media)lib->resolve("qc_probe_media");
    a.convert    = (Fn_convert)   lib->resolve("qc_convert_audio");
    a.split      = (Fn_split)     lib->resolve("qc_split_audio");
    a.split_conv = (Fn_split_convert)lib->resolve("qc_split_convert_audio");

    // probe_dur / convert / split 缺一个就说明不是我们那份 DLL（或版本太老）
    if (!a.probe_dur || !a.convert || !a.split) {
        lib->unload();
        delete lib;
        return false;
    }

    g_api = a;
    return true;
}

} // namespace

/* ===========================================================================
 * 1. 加载
 * =========================================================================== */
namespace QcFfmpeg {

QString fileName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("ffmpeg.dll");
#else
    return QStringLiteral("ffmpeg.so");
#endif
}

bool loaded()
{
    QMutexLocker lk(&g_apiMu);
    return g_api.lib != nullptr;
}

QString dllPath()
{
    QMutexLocker lk(&g_apiMu);
    return g_api.path;
}

QString lastError()
{
    Api a;
    if (!snapshot(&a)) return QString();
    return takeLastError(a);
}

bool load(QString *err)
{
    {
        QMutexLocker lk(&g_apiMu);
        if (g_api.lib) return true;
    }

    // 先按候选目录找文件 —— 文件不存在时**不**尝试 dlopen（省一次无谓的 LoadLibrary，
    // 而且 QLibrary 失败会往 Qt 日志里刷一行，用户看不懂）
    const QStringList dirs = candidateDirs();
    for (const QString &d : dirs) {
        const QString file = QDir(d).filePath(fileName());
        if (!QFileInfo(file).isFile())
            continue;

        QMutexLocker lk(&g_apiMu);      // 两个线程同时进来时只让一个真正 dlopen
        if (g_api.lib) return true;
        if (tryLoadFrom(file))
            return true;
    }

    if (err)
        *err = QStringLiteral("未找到可用的 %1（已查：%2）")
                   .arg(fileName(), dirs.join(QStringLiteral(" | ")));
    return false;
}

} // namespace QcFfmpeg

/* ===========================================================================
 * 2. 调用封装
 * =========================================================================== */
namespace QcFfmpeg {

Probe probe(const QString &src)
{
    Probe p;
    if (!load(nullptr)) return p;

    Api a;
    if (!snapshot(&a)) return p;

    const QByteArray u8 = src.toUtf8();
    double ms = -1;
    int    hv = 0, ha = 0;

    int r;
    if (a.probe_media) {
        r = a.probe_media(u8.constData(), &ms, &hv, &ha);
    } else {
        // 老版 DLL 没有合并探测：只有时长，没有轨信息（按「没视频」处理，
        // 到不了这里的输入都已经过后缀判定）
        r = a.probe_dur(u8.constData(), &ms);
        ha = 1;
    }
    if (r < 0) return p;

    p.ok       = true;
    p.ms       = ms;
    p.hasVideo = (hv != 0);
    p.hasAudio = (ha != 0);
    return p;
}

bool convert(const QString &src, const QString &dst,
             int sampleRate, int channels, int bitrateKbps, QString *err)
{
    if (!load(err)) return false;

    QcAudioOptsLocal o;
    o.sample_rate  = sampleRate;
    o.channels     = channels;
    o.bitrate_kbps = bitrateKbps;
    o.quality      = 0;
    o.overlay      = 1;            // 覆盖旧产物（tmp/ 里可能留着上次的）

    const QByteArray s = src.toUtf8();
    const QByteArray d = dst.toUtf8();

    Api a;
    if (!snapshot(&a)) { if (err) *err = QStringLiteral("ffmpeg.dll 未加载"); return false; }

    const int r = a.convert(s.constData(), d.constData(), &o);
    if (r < 0) {
        if (err) *err = takeLastError(a);
        return false;
    }
    return true;
}

QStringList split(const QString &src, const QString &outDir, int maxSec,
                  const QString &ext, QString *err)
{
    QStringList out;
    if (!load(err)) return out;

    const QByteArray s  = src.toUtf8();
    const QByteArray od = outDir.toUtf8();
    const QByteArray e  = ext.toUtf8();

    // 8KB 够放 60 段以上的绝对路径；不够时 DLL 会返回真实所需大小，这里再补一次
    QByteArray buf(8192, '\0');
    int count = 0;
    int need  = 0;

    Api a;
    if (!snapshot(&a)) { if (err) *err = QStringLiteral("ffmpeg.dll 未加载"); return out; }

    need = a.split(s.constData(), od.constData(), maxSec, e.constData(),
                   buf.data(), buf.size(), &count);
    if (need < 0) {
        if (err) *err = takeLastError(a);
        return out;
    }
    if (need > buf.size()) {
        buf.resize(need);
        count = 0;
        need = a.split(s.constData(), od.constData(), maxSec, e.constData(),
                       buf.data(), buf.size(), &count);
        if (need < 0) {
            if (err) *err = takeLastError(a);
            return out;
        }
    }

    const QString all = QString::fromUtf8(buf.constData());
    for (const QString &one : all.split(QLatin1Char('\n'))) {
        const QString t = one.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

} // namespace QcFfmpeg

/* ===========================================================================
 * 3. 专用后台线程池（串行跑阻塞调用）
 * =========================================================================== */
namespace {

class JobRunnable : public QRunnable
{
public:
    JobRunnable(std::function<void()> job, std::function<void()> done)
        : m_job(std::move(job)), m_done(std::move(done)) {}

    void run() override
    {
        if (m_job) m_job();
        if (m_done) m_done();
    }

private:
    std::function<void()> m_job;
    std::function<void()> m_done;
};

QThreadPool *jobPool()
{
    static QThreadPool *pool = nullptr;
    static QMutex mu;
    if (!pool) {
        QMutexLocker lk(&mu);
        if (!pool) {
            pool = new QThreadPool();
            // 单线程 = 同一时刻只有一个 ffmpeg 在跑：转码本来就吃满一个核，
            // 多路并发只会互相拖慢；而且媒体链本来就是排队走的。
            pool->setMaxThreadCount(1);
        }
    }
    return pool;
}

} // namespace

namespace QcFfmpeg {

void runBlocking(std::function<void()> job, std::function<void()> done)
{
    if (!job) return;
    jobPool()->start(new JobRunnable(std::move(job), std::move(done)));  // 自动 delete
}

} // namespace QcFfmpeg

/* ===========================================================================
 * 4. 缺失时下载「完整包」并取出 ffmpeg.dll
 * ---------------------------------------------------------------------------
 * 发布版历史更新包只发 exe，老用户机器上没有 ffmpeg.dll ——
 * 第一次要发语音时在后台补上，用户完全无感：
 *   ① 拉 gitee release 的 latest（与「检查更新」同一个 API）
 *   ② 优先找**专门的 ffmpeg 小包**（名字含 ffmpeg 的 .7z/.zip）—— 几 MB，快
 *   ③ 没有的话退回**完整包**（win → 名字含 win / linux → 名字含 linux），
 *      从里面只抽 ffmpeg.dll 出来，其余文件不落地
 *   ④ 用随程序的 7za.exe（Linux 用 7zz/7z）解压，只解这一个文件
 * 全程单飞：并发调用共享一次下载，失败也共享同一个 false。
 * =========================================================================== */
namespace {

/* ⚠ **唯一的取包地址**：缺 DLL 时直接下这一份（1.9MB，里面就一个 ffmpeg.dll）。
 *
 * 为什么不用 `releases/latest` API 去挑：
 *   2026-10-17 实测 latest 是 1.4.3.123，里面只有几百 MB 的完整包，而这份小包挂在
 *   **老版本 v1.1.2.14** 下 —— 走 API 会跳过 1.9MB 跑去下几百 MB，对用户是灾难。
 *   直接钉死链接：少一次网络请求，也不会因为 API 限流/资产改名而失效。
 *
 * 换 tag / 换文件名时**只需要改这一行**。
 * 平台：本包只含 Windows 的 ffmpeg.dll；Linux 版要 ffmpeg.so，留空 = 不自动补
 *       （core/api.cpp 会退回外部 ffmpeg）。
 */
#ifdef Q_OS_WIN
const char *kFfmpegPackUrl =
    "https://gitee.com/linglan2/chun-bai-ling-dang/releases/download/v1.1.2.14/ffmpeg-win.7z";
#else
const char *kFfmpegPackUrl = "";
#endif

// 小包体积上限（1.9MB 的实际包，给到 96MB 是为了以后换包不至于立刻被判太大）
const qint64 kFfmpegPackCap = 96LL * 1024 * 1024;

struct DlState
{
    QMutex                            mu;
    bool                              running = false;
    QList<std::function<void(bool)>>  pending;
};

DlState g_dl;

void dlFinish(bool ok)
{
    QList<std::function<void(bool)>> cbs;
    {
        QMutexLocker lk(&g_dl.mu);
        g_dl.running = false;
        cbs.swap(g_dl.pending);
    }
    for (auto &cb : cbs)
        if (cb) cb(ok);
}

// 找解压工具（与 plugin/plugininstaller.h 的 resolveSevenZip 同规则，但不引那坨头）
QString resolveUnzip()
{
#ifdef Q_OS_WIN
    // ⚠ 只返回**真的存在**的路径。以前这里查不到就直接返回字面量 "7za.exe"，
    //   于是 runTool 静默 waitForStarted 失败（-1）、stderr 也是空，
    //   最后错报成「包里没有 ffmpeg.dll」—— 完全误导排查方向。
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/7za.exe");
    if (QFileInfo::exists(beside)) return beside;
    return QStandardPaths::findExecutable(QStringLiteral("7za.exe"));
#else
    const QStringList cands = { QStringLiteral("7zz"), QStringLiteral("7z"),
                                QStringLiteral("7za") };
    for (const QString &c : cands) {
        const QString p = QStandardPaths::findExecutable(c);
        if (!p.isEmpty()) return p;
    }
    return QString();
#endif
}

int runTool(const QString &program, const QStringList &args, int timeoutMs,
            QByteArray *errOut = nullptr)
{
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(10000))
        return -1;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(3000);
        return -2;
    }
    if (errOut) *errOut = p.readAllStandardError();
    return p.exitCode();
}

// 把包里的 ffmpeg.dll 抽到 targetDir
bool extractDllFromPackage(const QString &pkg, const QString &targetDir, QString *err)
{
    const QString want = QcFfmpeg::fileName();

    QString tmpDir = QDir(QDir::tempPath()).filePath(QStringLiteral("qiancao_ffmpeg_x"));
    QDir(tmpDir).removeRecursively();
    QDir().mkpath(tmpDir);

    // 包内可能是根目录，也可能套了一层文件夹 → 抽完统一递归找
    auto findDll = [&tmpDir, &want]() -> QString {
        QDirIterator it(tmpDir, { want }, QDir::Files, QDirIterator::Subdirectories);
        return it.hasNext() ? it.next() : QString();
    };

    const QString tool = resolveUnzip();
    QString errText;
    int     rc = 0;

    if (!tool.isEmpty()) {
        // ① 只抽这一个文件（几百 KB 的 IO）。`-r` 允许它位于子目录里。
        QByteArray se;
        rc = runTool(tool,
                { QStringLiteral("x"), pkg,
                  QStringLiteral("-r"), want,
                  QStringLiteral("-o") + tmpDir,
                  QStringLiteral("-y"), QStringLiteral("-aoa") },
                300000, &se);
        errText = QString::fromLocal8Bit(se).trimmed();
    }

#ifdef Q_OS_WIN
    if (findDll().isEmpty() && !tool.isEmpty()) {
        runTool(tool,
                { QStringLiteral("x"), pkg,
                  QStringLiteral("-r"), QStringLiteral("*.dll"),
                  QStringLiteral("-o") + tmpDir,
                  QStringLiteral("-y"), QStringLiteral("-aoa") },
                300000, nullptr);
    }
#else
    if (findDll().isEmpty()) {
        const QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
        if (!unzip.isEmpty()) {
            runTool(unzip,
                    { QStringLiteral("-o"), QStringLiteral("-j"), pkg,
                      QStringLiteral("*.so"), QStringLiteral("-d"), tmpDir },
                    300000, nullptr);
        }
    }
#endif

    const QString found = findDll();
    if (found.isEmpty()) {
        if (err) {
            if (tool.isEmpty())
                *err = QStringLiteral("未找到解压工具（Windows 需要程序目录下的 7za.exe，"
                                      "Linux 需要 7zz/7z 或 unzip）");
            else if (rc != 0)
                *err = QStringLiteral("解压失败（%1 退出码 %2）：%3")
                           .arg(QFileInfo(tool).fileName()).arg(rc)
                           .arg(errText.isEmpty() ? QStringLiteral("无输出") : errText);
            else
                *err = QStringLiteral("解压成功但包里没有 %1").arg(want);
        }
        QDir(tmpDir).removeRecursively();
        return false;
    }

    const QString dst = QDir(targetDir).filePath(want);
    // 走到这里说明 load() 已经失败过 —— 目标位置就算有文件也是坏的/版本不对。
    // 它此刻必然**没有被加载**（加载成功就不会来下载），所以可以安全覆盖。
    QFile::remove(dst);
    if (!QFile::copy(found, dst)) {
        if (err) *err = QStringLiteral("复制到 %1 失败（目录不可写？）").arg(dst);
        QDir(tmpDir).removeRecursively();
        return false;
    }

    QDir(tmpDir).removeRecursively();
    return true;
}

// 落地目录：优先程序目录（和 exe 放一起，AppRun / 安装目录都能找到），
// 不可写时退到 AppData（candidateDirs 里也查这个位置）
QString installDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty() && QFileInfo(appDir).isWritable())
        return appDir;

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!dataDir.isEmpty()) {
        QDir().mkpath(dataDir);
        return dataDir;
    }
    return appDir;
}

// 下包 → 落盘 → 只抽出 ffmpeg.dll → 重新 load。下载在 netmanager 线程，
// 解压（几百 KB IO + 可能几十 MB 的完整包）扔本模块的后台线程，不占网络线程。
void downloadAndExtract(const QString &url, qint64 capBytes, std::function<void(bool)> done)
{
    if (url.isEmpty()) {
        if (done) done(false);
        return;
    }

    const QString label = QFileInfo(QUrl(url).path()).fileName();

    NetManager::instance()->downloadAsync(url, capBytes, 600000,
        [url, label, done](const QByteArray &data, bool tooBig, QNetworkReply::NetworkError e) {
            if (tooBig || e != QNetworkReply::NoError || data.isEmpty()) {
                AppendEventLog(QStringLiteral("ffmpeg.dll 下载失败（%1）：%2")
                                   .arg(tooBig ? QStringLiteral("包太大")
                                               : QStringLiteral("网络错误"),
                                        label));
                if (done) done(false);
                return;
            }

            auto okFlag = std::make_shared<bool>(false);
            QcFfmpeg::runBlocking(
                [data, url, okFlag]() {
                    QString suffix = QStringLiteral(".7z");
                    const QString path = QUrl(url).path();
                    if (path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
                        suffix = QStringLiteral(".zip");

                    const QString pkg = QDir(QDir::tempPath())
                        .filePath(QStringLiteral("qiancao_ffmpeg_pack") + suffix);
                    QFile f(pkg);
                    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                        AppendEventLog(QStringLiteral("ffmpeg.dll 下载失败：临时文件不可写"));
                        return;
                    }
                    f.write(data);
                    f.close();

                    QString e2;
                    const bool ok = extractDllFromPackage(pkg, installDir(), &e2);
                    QFile::remove(pkg);
                    if (!ok) {
                        AppendEventLog(QStringLiteral("ffmpeg.dll 解包失败：") + e2);
                        return;
                    }

                    QString le;
                    if (!QcFfmpeg::load(&le)) {
                        AppendEventLog(QStringLiteral("ffmpeg.dll 已就位但加载失败：") + le);
                        return;
                    }
                    *okFlag = true;
                },
                [okFlag, done]() {
                    if (*okFlag)
                        AppendEventLog(QStringLiteral("ffmpeg.dll 已补齐，后续音频走进程内转码"));
                    if (done) done(*okFlag);
                });
        });
}

} // namespace

namespace QcFfmpeg {

void ensureAsync(std::function<void(bool ok)> onDone)
{
    if (!onDone) return;

    if (load(nullptr)) { onDone(true); return; }      // 已经有了，直接过

    {
        QMutexLocker lk(&g_dl.mu);
        if (g_dl.running) {                           // 别人正在下 → 排队等结果
            g_dl.pending.append(onDone);
            return;
        }
        g_dl.running = true;
        g_dl.pending.append(onDone);
    }

    AppendEventLog(QStringLiteral("缺少 %1，正在下载补齐…").arg(fileName()));

    // 直接下那一个固定地址（kFfmpegPackUrl）—— 不查 release API，
    // 少一次请求，也不会因为「包挂在老 tag 上」而挑错（见常量处的说明）。
    downloadAndExtract(QString::fromLatin1(kFfmpegPackUrl), kFfmpegPackCap,
                       [](bool ok) { dlFinish(ok); });
}

} // namespace QcFfmpeg
