#ifndef QCFFMPEG_H
#define QCFFMPEG_H

/* ===========================================================================
 * qcffmpeg.h —— 自建精简 ffmpeg.dll 的**动态加载**封装
 *
 * 为什么要这一层：
 *   · 发布版里不一定带 ffmpeg.dll（历史更新包只发 exe），所以不能静态 import，
 *     否则缺 DLL 时进程直接起不来 —— 必须 QLibrary 运行时加载；
 *   · 用到才加载：全进程只有第一次真正需要转码/探测/切段时才 LoadLibrary，
 *     不发语音的进程永远不会多加载 4.87MB 的 DLL；
 *   · 加载前先看文件在不在：不在就去 Gitee release 拉**完整包**，从里面把
 *     ffmpeg.dll 取出来放到程序目录（单飞，失败不重试）。
 *
 * 和旧链路的关系：
 *   本模块**不删除**外部 ffmpeg.exe 那条路。DLL 不可用时（下载失败 / 非 Windows /
 *   用户自己放了个 exe），core/api.cpp 里仍会退回 `QProcess + ffmpeg.exe`，
 *   所以任何一边坏掉都不会让「发语音」整个不可用。
 *
 * 线程：
 *   · load()/probe()/convert()/split() 都是**阻塞**的，调用线程会等到底；
 *   · 异步链请用 runBlocking()：扔进本模块专用的单线程池（串行 = 不会同时压满 CPU），
 *     跑完在**那个后台线程**回调 —— 调用方自己负责再投回媒体线程；
 *   · ensureAsync() 的 onDone 可能在**网络/线程池线程**触发，同样由调用方转线程。
 * =========================================================================== */

#include <QString>
#include <QStringList>
#include <functional>

namespace QcFfmpeg {

// Windows: ffmpeg.dll ／ Linux: ffmpeg.so
QString fileName();

// 惰性加载（线程安全）。成功返回 true；失败返回 false，原因写进 err（可为 nullptr）。
// ⚠ 失败**不缓存**：这次没找到文件不代表下次还没有（可能刚下载完）。
bool load(QString *err = nullptr);

// 只读查询：已经加载好返回 true（不会触发加载动作）
bool loaded();

// 当前实际加载的 DLL 完整路径（未加载时为空）
QString dllPath();

// 上一次失败的原始错误串（来自 DLL 的 qc_last_error，UTF-8）
// ⚠ **只能在同一线程取**：DLL 的错误串存在调用线程的 TLS 里。
//   A 线程里 convert() 失败 → 必须还在 A 线程里取错误；
//   换到主线程/别的 worker 调本函数只会拿到空串。
//   所以 convert()/split() 的 err 出参是主用法，本函数只做补充。
QString lastError();

struct Probe
{
    bool   ok       = false;    // DLL 读成功
    double ms       = -1;       // 时长（毫秒）；<0 = 未知
    bool   hasVideo = false;    // 容器里有视频轨（封面图也算）
    bool   hasAudio = false;
};

// 同步：探测时长 + 有没有视频轨/音频轨
Probe probe(const QString &src);

// 同步：转音频格式（视频轨整个跳过 = 视频提取音频）
//   sampleRate/channels/bitrateKbps 传 0 = 跟随源 / 编码器默认
// 成功返回 true
bool convert(const QString &src, const QString &dst,
             int sampleRate, int channels, int bitrateKbps, QString *err = nullptr);

// 同步：按 maxSec 秒**流复制**切段，段名 <src 基名>_seg000.<ext>（与工程约定一致）
// 返回各段完整路径；失败返回空表，原因写进 err
QStringList split(const QString &src, const QString &outDir, int maxSec,
                  const QString &ext, QString *err = nullptr);

// 保证 DLL 可用：已加载 → 立刻 onDone(true)；
// 文件不存在 → 去 release 下完整包并取出 ffmpeg.dll（单飞：并发调用共享一次下载）。
// ⚠ onDone 可能在任意后台线程触发。
void ensureAsync(std::function<void(bool ok)> onDone);

// 把「阻塞的 DLL 调用」丢到本模块专用单线程池执行，结束后在后台线程回调 done。
// 入队顺序即执行顺序（不会并发进 ffmpeg，避免多路转码把机器拖死）。
void runBlocking(std::function<void()> job, std::function<void()> done);

} // namespace QcFfmpeg

#endif // QCFFMPEG_H
