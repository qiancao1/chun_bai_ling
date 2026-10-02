/* ============================================================================
 *  msvc-shim.c —— 让 MSVC 能直接静态链接 MinGW 编出来的 FFmpeg 目标文件
 *
 *  背景：FFmpeg 只能用 MinGW/GCC 编（configure 是 POSIX sh，代码用 C99/GCC 扩展），
 *        但主程序 qiancao.exe 是 MSVC 编的。默认走「动态库 bin/*.dll + import lib」
 *        （已验证 link.exe 能直接链）—— 那条路**不需要本文件**。
 *
 *        本文件只服务于另一条路：把 libav*.a **全静态**链进 qiancao.exe，
 *        好处是发布包只剩一个 exe，不用再带 4 个 DLL。
 *
 *  做法：MinGW 编的目标文件会引用 11 个 MSVC 没有的符号（GNU 扩展 + POSIX），
 *        这里全部补上。实测缺口就是这 11 个，没有别的。
 *
 *  ⚠ 只在「静态链接 MinGW 目标文件」时编译本文件；动态库那条路编译它会重复定义。
 *  ⚠ /utf-8 编译（本文件含中文注释）。
 * ============================================================================ */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <windows.h>

/* ---------------------------------------------------------------------------
 * 1) MinGW 的 ANSI stdio 别名
 *    MinGW 默认 __USE_MINGW_ANSI_STDIO=1，把 printf 家族改写成 __mingw_*，
 *    而这些函数的语义就是 **C99**。MSVC 2015+ 的 vsnprintf/vfprintf 已是
 *    C99 语义（返回「本该写入的长度」、截断时仍以 '\0' 结尾）→ 直接转发即可。
 * ------------------------------------------------------------------------- */
int __cdecl __mingw_vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    return vsnprintf(s, n, fmt, ap);
}

int __cdecl __mingw_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    return vfprintf(f, fmt, ap);
}

int __cdecl __mingw_vsscanf(const char *s, const char *fmt, va_list ap)
{
    return vsscanf(s, fmt, ap);          /* MSVC 2015+ 提供 vsscanf */
}

double __cdecl __mingw_strtod(const char *s, char **end)
{
    return strtod(s, end);
}

/* ---------------------------------------------------------------------------
 * 2) GNU 扩展 sincos / sincosf（一次同时算 sin 和 cos）
 * ------------------------------------------------------------------------- */
void __cdecl sincos(double x, double *s, double *c)
{
    if (s) *s = sin(x);
    if (c) *c = cos(x);
}

void __cdecl sincosf(float x, float *s, float *c)
{
    if (s) *s = sinf(x);
    if (c) *c = cosf(x);
}

/* ---------------------------------------------------------------------------
 * 3) POSIX 时间函数
 *    FFmpeg 的 struct timeval / timespec 在 MinGW x64 下都是「两个 64 位」，
 *    这里用 void* 按两个 long long 写，避免依赖 MSVC 头里的结构体定义。
 * ------------------------------------------------------------------------- */
#define QA_EPOCH_DIFF_SEC 11644473600LL      /* 1601-01-01 → 1970-01-01 */

static unsigned long long qa_now_100ns(void)
{
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;                        /* 100ns，自 1601 */
}

/* gettimeofday(struct timeval *tv, void *tz) —— tv = { int64 sec, int64 usec } */
int __cdecl gettimeofday(void *tv, void *tz)
{
    (void)tz;
    if (tv) {
        long long *v = (long long *)tv;
        unsigned long long t = qa_now_100ns();
        v[0] = (long long)(t / 10000000ULL) - QA_EPOCH_DIFF_SEC;
        v[1] = (long long)((t % 10000000ULL) / 10ULL);
    }
    return 0;
}

/* clock_gettime(int clk_id, struct timespec *ts) —— ts = { int64 sec, int64 nsec }
 * FFmpeg 只用 CLOCK_MONOTONIC（av_gettime_relative）；MinGW 里它是 1。
 * 这里 0 当 REALTIME、其余一律当 MONOTONIC（GetTickCount64 单调、且不受校时影响）。 */
int __cdecl clock_gettime(int clk_id, void *ts)
{
    if (!ts)
        return 0;
    long long *v = (long long *)ts;
    if (clk_id == 0) {                        /* CLOCK_REALTIME */
        long long ns = (long long)(qa_now_100ns() * 100ULL)
                       - QA_EPOCH_DIFF_SEC * 1000000000LL;
        v[0] = ns / 1000000000LL;
        v[1] = ns % 1000000000LL;
    } else {                                  /* CLOCK_MONOTONIC */
        unsigned long long ms = GetTickCount64();
        v[0] = (long long)(ms / 1000ULL);
        v[1] = (long long)((ms % 1000ULL) * 1000000ULL);
    }
    return 0;
}

/* nanosleep(const struct timespec *req, struct timespec *rem) */
int __cdecl nanosleep(const void *req, void *rem)
{
    if (rem) {
        long long *r = (long long *)rem;
        r[0] = r[1] = 0;
    }
    if (req) {
        const long long *v = (const long long *)req;
        long long ms = v[0] * 1000LL + v[1] / 1000000LL;
        if (ms > 0)
            Sleep((DWORD)(ms > 0x7FFFFFFFLL ? 0x7FFFFFFFLL : ms));
    }
    return 0;
}

/* mkstemp(char *tmpl) —— 原子创建临时文件，返回 fd（失败 -1） */
int __cdecl mkstemp(char *tmpl)
{
    if (!tmpl || _mktemp_s(tmpl, strlen(tmpl) + 1) != 0)
        return -1;
    return _open(tmpl, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY,
                 _S_IREAD | _S_IWRITE);
}

/* ---------------------------------------------------------------------------
 * 4) ___chkstk_ms —— GCC 的栈探测函数
 *    x64 下它和 MSVC 的 __chkstk 约定完全一样（调用方把要分配的字节数放 RAX，
 *    被调方负责探测并调整栈）→ 直接做符号别名最省事，不用写 .asm。
 * ------------------------------------------------------------------------- */
#if defined(_MSC_VER)
#  pragma comment(linker, "/alternatename:___chkstk_ms=__chkstk")
#endif
