/* ============================================================================
 *  probe.c —— 对自编译的 FFmpeg 库做端到端验证（只读，不改任何文件）
 *
 *  完整走一遍工程实际要用的链路：
 *      avformat_open_input → find_stream_info → find_best_stream
 *      → parameters_to_context → avcodec_open2
 *      → av_read_frame / send_packet / receive_frame（解出第一帧 PCM）
 *  外加打印每个流的类型（对应 core/libavio.h 的 libavHasVideoTrack）与容器时长
 *  （对应 libavProbeDurationMs）。
 *
 *  ⚠ 这个文件两个平台共用，路径按各自产物目录给：
 *
 *  Windows（在 libs/ffmpeg 下，输出到 _build/）：
 *      gcc -O2 -o _build/probe.exe probe.c \
 *          -Iwin-x64/include -Lwin-x64/bin -lavformat -lavcodec -lavutil -lshell32
 *      PATH="win-x64/bin:$PATH" ./_build/probe.exe <媒体文件> ...
 *
 *  Linux（在 libs/ffmpeg 下；静态库，要把依赖的 -lm -lpthread 一起写上）：
 *      gcc -O2 -o _build-linux/probe probe.c -Ilinux-x64/include \
 *          -Llinux-x64/lib -lavformat -lavcodec -lavutil -lswresample -lm -lpthread
 *      ./_build-linux/probe <媒体文件> ...
 *
 *  exit code = 失败的文件个数（0 = 全过）。
 * ============================================================================ */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <shellapi.h>
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>

#ifdef _WIN32
/* FFmpeg 在 Windows 上把文件路径当 UTF-8 解（内部转 UTF-16 再开）。
   MinGW 的 main() 拿到的是 ANSI(CP936) 字节 → 中文路径会烂。
   所以绕开 argv，直接用宽字符命令行转 UTF-8 —— 这也正是 Qt 那条链路的做法
   （QString::toUtf8() 给 FFmpeg）。*/
static char **utf8_argv(int *argc_out)
{
    int n = 0, i;
    LPWSTR *w = CommandLineToArgvW(GetCommandLineW(), &n);
    char **a;
    if (!w) return NULL;
    a = (char **)calloc(n + 1, sizeof(char *));
    for (i = 0; i < n; i++) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
        a[i] = (char *)malloc(len);
        WideCharToMultiByte(CP_UTF8, 0, w[i], -1, a[i], len, NULL, NULL);
    }
    LocalFree(w);
    *argc_out = n;
    return a;
}
#endif

static int probe_one(const char *path)
{
    AVFormatContext *fmt = NULL;
    AVCodecContext  *dec = NULL;
    AVPacket        *pkt = NULL;
    AVFrame         *frm = NULL;
    const AVCodec   *codec = NULL;
    int ret, ast, got = 0;

    printf("\n=== %s ===\n", path);

    /* 1) 解封装：这一步最吃 demuxer + 协议（file） */
    ret = avformat_open_input(&fmt, path, NULL, NULL);
    if (ret < 0) {
        char e[256];
        av_strerror(ret, e, sizeof e);
        printf("  [失败] avformat_open_input: %s\n", e);
        return 1;
    }

    ret = avformat_find_stream_info(fmt, NULL);
    if (ret < 0) {
        printf("  [失败] find_stream_info ret=%d\n", ret);
        avformat_close_input(&fmt);
        return 1;
    }

    printf("  容器   : %s\n", fmt->iformat ? fmt->iformat->name : "?");
    printf("  流数   : %u   总时长: %.2f s\n", fmt->nb_streams,
           fmt->duration != AV_NOPTS_VALUE ? (double)fmt->duration / AV_TIME_BASE : -1.0);

    /* 各流类型（对应 mediaFileHasVideoTrack()：只要有一个 VIDEO 流就算视频文件） */
    {
        unsigned k;
        int has_video = 0;
        printf("  流明细 :");
        for (k = 0; k < fmt->nb_streams; k++) {
            enum AVMediaType t = fmt->streams[k]->codecpar->codec_type;
            const char *tn = av_get_media_type_string(t);
            printf(" #%u=%s(%s)", k, tn ? tn : "?",
                   avcodec_get_name(fmt->streams[k]->codecpar->codec_id));
            if (t == AVMEDIA_TYPE_VIDEO) has_video = 1;
        }
        printf("\n  含视频轨: %s\n", has_video ? "是" : "否");
    }

    /* 2) 找最佳音频流（decoder_ret 直接把解码器给我们，省掉遍历） */
    ast = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (ast < 0) {
        printf("  [提示] 没有音频流（纯视频文件正常）\n");
        avformat_close_input(&fmt);
        return 0;
    }
    printf("  音频流 : #%d  codec=%s  sr=%d  声道=%d  时长=%.2f s\n",
           ast, codec ? codec->name : "?",
           fmt->streams[ast]->codecpar->sample_rate,
           fmt->streams[ast]->codecpar->ch_layout.nb_channels,
           fmt->streams[ast]->duration != AV_NOPTS_VALUE
               ? (double)fmt->streams[ast]->duration * av_q2d(fmt->streams[ast]->time_base)
               : -1.0);

    /* 3) 开解码器 */
    dec = avcodec_alloc_context3(codec);
    if (!dec) { printf("  [失败] alloc_context3\n"); avformat_close_input(&fmt); return 1; }

    ret = avcodec_parameters_to_context(dec, fmt->streams[ast]->codecpar);
    if (ret < 0) { printf("  [失败] parameters_to_context ret=%d\n", ret); goto done; }

    ret = avcodec_open2(dec, codec, NULL);
    if (ret < 0) { printf("  [失败] avcodec_open2 ret=%d\n", ret); goto done; }
    printf("  解码器 : 已打开（%s）\n", dec->codec->long_name ? dec->codec->long_name : codec->name);

    /* 4) 真解一帧 PCM 出来 */
    pkt = av_packet_alloc();
    frm = av_frame_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != ast) { av_packet_unref(pkt); continue; }
        if (avcodec_send_packet(dec, pkt) < 0) { av_packet_unref(pkt); continue; }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(dec, frm) == 0) {
            if (frm->nb_samples > 0) {
                printf("  PCM    : sample_fmt=%s  nb_samples=%d  sr=%d  首样本=%d\n",
                       av_get_sample_fmt_name(frm->format), frm->nb_samples,
                       frm->sample_rate,
                       frm->data[0] ? ((short *)(frm->data[0]))[0] : 0);
                got = 1;
            }
            av_frame_unref(frm);
        }
        if (got) break;
    }
    printf("  [结果] %s\n", got ? "解码成功 ✔" : "未解出 PCM ✘");

done:
    av_frame_free(&frm);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return got ? 0 : 2;
}

int main(int argc, char **argv)
{
    int i, bad = 0;

#ifdef _WIN32
    argv = utf8_argv(&argc);   /* 换成 UTF-8 参数的副本，中文路径才不会烂 */
    if (!argv) return 3;
#endif

    printf("libavutil    : %u.%u.%u\n", avutil_version() >> 16,
           (avutil_version() >> 8) & 0xff, avutil_version() & 0xff);
    printf("libavcodec   : %u.%u.%u\n", avcodec_version() >> 16,
           (avcodec_version() >> 8) & 0xff, avcodec_version() & 0xff);
    printf("libavformat  : %u.%u.%u\n", avformat_version() >> 16,
           (avformat_version() >> 8) & 0xff, avformat_version() & 0xff);
    printf("configuration: %s\n", avcodec_configuration());

    if (argc < 2) { printf("\n用法: probe.exe <媒体文件> ...\n"); return 2; }

    for (i = 1; i < argc; i++)
        bad += probe_one(argv[i]) != 0;

    printf("\n==== 共 %d 个文件，%d 个失败 ====\n", argc - 1, bad);
    return bad;
}
