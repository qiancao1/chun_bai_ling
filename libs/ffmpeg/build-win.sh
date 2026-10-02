#!/usr/bin/env bash
# ============================================================================
#  纯白铃铛 —— 自编译精简版 FFmpeg（**Windows / x64 / LGPL**）
#
#  产物落在 win-x64/（Linux 那一份走 build-linux.sh → linux-x64/，两者互不干扰）。
#
#  动机：api.cpp 目前靠外部 ffmpeg.exe 兜底（时长探测、超长切段、罕见格式转换），
#        而机器上那个 87MB 的 exe 是 gyan.dev 的 --enable-gpl 构建。
#        这里编一份干净、可再分发的 x64 DLL，直接链进 qiancao.exe，不再起进程。
#
#  用法（在 Git Bash 中执行）：
#      bash libs/ffmpeg/build-win.sh                   # 全流程
#      STAGE=configure bash libs/ffmpeg/build-win.sh   # 只到 configure（快速验证环境）
#      STAGE=make      bash libs/ffmpeg/build-win.sh   # 编译+安装（不重复 configure）
#      STAGE=build     bash libs/ffmpeg/build-win.sh   # 跳过 configure（已配置过时用）
#      STAGE=verify    bash libs/ffmpeg/build-win.sh   # 只重新体检产物 + 重生成 .def
#      FFVER=8.1.3     bash libs/ffmpeg/build-win.sh   # 换 FFmpeg 版本
#      JOBS=4          bash libs/ffmpeg/build-win.sh   # 限制并行数
#
#  产物（win-x64/）：
#      bin/      avcodec-*.dll  avformat-*.dll  avutil-*.dll  swresample-*.dll
#                + 同名 .lib（MSVC 直接链这个，已实测 link.exe 认）
#      lib/      libav*.dll.a（MinGW import lib）、*.def（完整导出表）
#      include/  FFmpeg 头文件
#
#  编进去的内容（白名单见 ff-config-flags.sh / whitelist-decoders.txt，两平台共用）：
#      · 常用解封装器（30 个：mov/matroska/mp3/flac/ogg/wav/aac/ts/avi/asf/amr/rm/flv/…）
#      · 常用解析器（8 个）
#      · 常用**音频**解码器（53 个，视频解码器一个不要）
#      · 编码器 / 封装器：0（编码交给 libopus，容器交给 ogg_packer）
#      即 libav 在本项目里是「只读」的：只解、不编、不写容器。
#
#  ⚠ 白名单**不能开太大**：libavcodec 的 .def 生成命令一超过 8192 字节就会被
#    原生 make → MSYS sh 那一跳截断，报 "Object does not exist: lib"（残片）而失败。
#    configure 之后本脚本会干跑 make 量一下这个长度，超线就告警。详见 ff-config-flags.sh 顶部。
# ============================================================================

set -euo pipefail

# ---- 路径（一律 POSIX 路径，configure 是在 MSYS sh 下跑的） ----
_self="${BASH_SOURCE[0]}"
case "$_self" in
    */*) _dir="${_self%/*}" ;;
    *)   _dir="." ;;
esac
HERE="$(cd "$_dir" && pwd)"
OUT="$HERE/win-x64"        # Windows 产物落这儿（Linux 那份在 linux-x64/）
SRC="$HERE/_src"           # 源码包与解压结果 —— ⚠ 与 Linux 那份**同一棵树**，跨平台复用
                           #   前必须清干净（见 step_prepare 的跨平台污染检测）
BLD="$HERE/_build"         # 中间产物（临时 make.exe、probe 等）
TMPBIN="$BLD/toolbin"

FFVER="${FFVER:-7.1.5}"
JOBS="${JOBS:-${NUMBER_OF_PROCESSORS:-4}}"
STAGE="${STAGE:-all}"
TARBALL="ffmpeg-$FFVER.tar.xz"
FFSRC="$SRC/ffmpeg-$FFVER"

# ---- 工具链：Strawberry 自带的 MinGW-w64（posix 线程 / ucrt / SEH） ----
export PATH="/c/Strawberry/c/bin:/c/Strawberry/perl/bin:$PATH"

# ⚠ Git Bash 下 TMPDIR/TEMP 可能是 Windows 反斜杠路径（C:\Users\...\Temp），
# configure 的 sanity test 会把反斜杠当转义吃掉 → 变成 C:UsersAiruanAppDataLocalTemp
# → "Sanity test failed"。这里强制换成 MSYS 认得的 /tmp。
export TMPDIR="/tmp"

die() { echo "错误：$*" >&2; exit 1; }

# 白名单与枚举逻辑放这儿，Windows / Linux 两份脚本共用同一份（别再各抄一遍）
source "$HERE/ff-config-flags.sh"

# ---------------------------------------------------------------------------
step_prepare() {
    echo "== [1/4] 准备：检查工具链 + 取源码 =="

    command -v gcc   >/dev/null || die "找不到 gcc（应为 C:\\Strawberry\\c\\bin\\gcc.exe）"
    command -v gmake >/dev/null || die "找不到 gmake"
    command -v nasm  >/dev/null || die "找不到 nasm（x86 汇编优化需要）"

    case "$(gcc -dumpmachine)" in
        x86_64-w64-mingw32) ;;
        *) die "gcc 不是 x86_64-w64-mingw32，而是：$(gcc -dumpmachine)" ;;
    esac
    echo "   gcc : $(gcc --version | head -1)"

    # FFmpeg 的 Makefile 认死 'make' 这个命令名，本机只有 gmake → 就地造一个
    mkdir -p "$TMPBIN"
    cp -f "$(command -v gmake)" "$TMPBIN/make.exe"
    export PATH="$TMPBIN:$PATH"
    echo "   make: $(make --version | head -1)"

    mkdir -p "$SRC" "$BLD" "$OUT"

    if [ ! -f "$SRC/$TARBALL" ]; then
        echo "   下载 $TARBALL ..."
        curl -L --fail --retry 3 -o "$SRC/$TARBALL" "https://ffmpeg.org/releases/$TARBALL"
    fi
    echo "   源码包: $(du -h "$SRC/$TARBALL" | cut -f1)"

    # --- 跨平台污染检测（重要，别删）--------------------------------------
    # _src 是 win / linux **共用**的 in-source 源码树。在 A 平台编过之后换 B 平台，
    # make 的增量判断只看 .o 与 .c 的时间戳（.c 是源码包的原始日期，很旧），于是
    # 会「漏编」一部分文件 —— 另一个平台编出来的目标文件被原样打进本平台的库。
    # 判据：读 .o 的文件头 magic —— ELF = 7f454c46（Linux），COFF-x64 = 6486（MinGW）。
    # 只要 libavutil 下有一个不是 COFF 的，就整棵删掉重新解压。
    if ls "$FFSRC"/libavutil/*.o >/dev/null 2>&1; then
        _bad=0; _sample=""
        for _o in "$FFSRC"/libavutil/*.o; do
            _m="$(od -An -tx1 -N4 "$_o" 2>/dev/null | tr -d ' \n' || true)"
            if [ "$_m" != "6486" ]; then _bad=$((_bad + 1)); _sample="${_o##*/}"; fi
        done
        if [ "$_bad" -gt 0 ]; then
            echo "   ⚠ 源码树里有 $_bad 个**非本平台**的目标文件（$_sample 等）"
            echo "     —— 它被 Linux 编过；不删的话这批 .o 会被直接打进 .dll"
            rm -rf "$FFSRC"
        fi
    fi

    # 用 configure 是否存在判断「解压完整」——中途被打断时能自动重来
    if [ ! -f "$FFSRC/configure" ]; then
        rm -rf "$FFSRC"
        SEVENZA="$(command -v 7za || command -v 7z || true)"
        if [ -z "$SEVENZA" ] && [ -x "/c/Users/Airuan/Documents/QTCode/7za.exe" ]; then
            SEVENZA="/c/Users/Airuan/Documents/QTCode/7za.exe"
        fi
        if [ -n "$SEVENZA" ]; then
            echo "   解压（7za：.tar.xz → .tar → 目录）..."
            ( cd "$SRC" && "$SEVENZA" x -y "$TARBALL" >/dev/null ) || die "7za 解 .tar.xz 失败"
            ( cd "$SRC" && "$SEVENZA" x -y "${TARBALL%.xz}" >/dev/null ) || die "7za 解 .tar 失败"
            rm -f "$SRC/${TARBALL%.xz}"
        else
            echo "   解压（Python lzma 兜底）..."
            PY="$(command -v python || command -v python3 || true)"
            [ -n "$PY" ] || die "既没有 7za 也没有 python，无法解 .xz"
            "$PY" -c "import tarfile,sys; tarfile.open(sys.argv[1]).extractall(sys.argv[2])" \
                   "$SRC/$TARBALL" "$SRC"
        fi
    fi
    [ -f "$FFSRC/configure" ] || die "解压后仍找不到 $FFSRC/configure"
    echo "   源码就绪：$FFSRC"
}

# ---------------------------------------------------------------------------
step_configure() {
    echo "== [2/4] configure =="
    cd "$FFSRC"

    echo "   Windows/MSYS 下 configure 要跑上千项编译探测，通常 5~15 分钟；"
    echo "   下面输出会很多，属正常现象，请耐心等待。"
    echo

    # 名单在这里取：demuxer / parser 是 ff-config-flags.sh 里的静态常用表，
    # decoder 读 whitelist-decoders.txt。两平台共用同一份。
    local demuxers parsers decoders
    demuxers=$(qc_ff_demuxer_list)
    parsers=$(qc_ff_parser_list)
    decoders=$(qc_ff_decoder_list "$HERE/whitelist-decoders.txt") || die "读取解码器白名单失败"
    echo "   白名单：解封装器 $(qc_ff_count "$demuxers") 个 · 解析器 $(qc_ff_count "$parsers") 个 · 音频解码器 $(qc_ff_count "$decoders") 个"
    echo

    ./configure \
        --prefix="$OUT" \
        --arch=x86_64 \
        --enable-shared --disable-static --enable-pic \
        --disable-gpl --disable-nonfree --disable-version3 \
        --disable-programs --disable-doc --disable-debug \
        --disable-network --disable-autodetect \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
        --disable-everything \
        --enable-protocol="$QC_FF_PROTOCOLS" \
        --enable-demuxer="$demuxers" \
        --enable-parser="$parsers" \
        --enable-decoder="$decoders" \
        --extra-ldflags="-static -static-libgcc"

    # FFmpeg 4.0 之后构建文件在 ffbuild/ 下（config.h 仍在根目录）
    [ -f ffbuild/config.mak ] || die "configure 没有生成 ffbuild/config.mak（看上面的报错）"

    echo
    echo "   --- 关键配置核对（ffbuild/config.mak）---"
    grep -E "^CONFIG_(GPL|NONFREE|VERSION3)=" ffbuild/config.mak || echo "   CONFIG_GPL / NONFREE / VERSION3 均未置位 ✔"

    # 实际编进去的数量（名单里写错的名字会被静默忽略，这里能一眼看出少没少）
    if [ -f config_components.h ]; then
        echo "   --- 实际编入的组件数 ---"
        printf "      解封装器 %s · 解析器 %s · 解码器 %s · 编码器 %s · 封装器 %s\n" \
            "$(grep -cE '^#define CONFIG_[A-Z0-9_]+_DEMUXER 1' config_components.h)" \
            "$(grep -cE '^#define CONFIG_[A-Z0-9_]+_PARSER 1'  config_components.h)" \
            "$(grep -cE '^#define CONFIG_[A-Z0-9_]+_DECODER 1' config_components.h)" \
            "$(grep -cE '^#define CONFIG_[A-Z0-9_]+_ENCODER 1' config_components.h)" \
            "$(grep -cE '^#define CONFIG_[A-Z0-9_]+_MUXER 1'   config_components.h)"
        echo "      （编码器/封装器应为 0 —— 本项目不需要；解码器多于白名单条数属正常，"
        echo "        是 demuxer 的 select 依赖顺带拉进来的）"
    fi

    # ⚠ .def 生成命令的长度自检 —— 8192 字节红线，详见 ff-config-flags.sh 顶部。
    #   干跑一次 make，量最长的那条 makedef 命令行（就是 libavcodec 那个）。
    echo "   --- .def 生成命令长度自检（红线 8192 字节）---"
    local _l _len _worst=0
    while IFS= read -r _l; do
        _len=${#_l}
        if [ "$_len" -gt "$_worst" ]; then _worst=$_len; fi
    done < <(gmake -n 2>/dev/null | grep 'windows/makedef' || true)
    if [ "$_worst" -eq 0 ]; then
        echo "      （没抓到 makedef 命令行，跳过自检）"
    elif [ "$_worst" -gt 8000 ]; then
        printf "      最长 %s 字节   ⚠⚠ 超线！编译到 .def 那步会被截断（Object does not exist: lib）\n" "$_worst"
        echo "      请缩减 whitelist-decoders.txt / QC_FF_DEMUXERS / QC_FF_PARSERS 后重跑 configure"
    else
        printf "      最长 %s 字节   ✔ 安全（余量 %s 字节）\n" "$_worst" "$((8192 - _worst))"
    fi
    echo "   configure 完成"
}

# ---------------------------------------------------------------------------
step_make() {
    echo "== [3/4] 编译（最慢的一步，视核数约 3~15 分钟）=="
    cd "$FFSRC"
    gmake -j"$JOBS"
    echo
    echo "== 安装到 $OUT =="
    gmake install
}

# ---------------------------------------------------------------------------
step_verify() {
    echo "== [4/4] 验证产物 =="
    cd "$OUT"

    echo
    echo "--- 位数与运行时依赖（依赖里除 KERNEL32/ucrtbase 外应尽量为空）---"
    for f in bin/*.dll; do
        [ -e "$f" ] || continue
        printf "  %-22s " "$(basename "$f")"
        if objdump -f "$f" | grep -q "pei-x86-64"; then printf "x64  "; else printf "!!非x64:"; fi
        printf "依赖: "
        objdump -p "$f" | grep "DLL Name" | sed 's/.*DLL Name: *//' | tr '\n' ' '
        echo
    done

    echo
    echo "--- 收集给 MSVC 用的 .def ---"
    # ⚠ 不要用 `dlltool -z xxx.dll`：dlltool 只认 .o/.def，喂 DLL 会生成一个
    #    只有 "EXPORTS" 一行的空文件。FFmpeg 链接 DLL 时自己就把 .def 生成在源码树里了
    #    （libavcodec/avcodec-61.def 等，SLIB_CREATE_DEF_CMD），直接搬过来即可。
    #    bin/*.lib 也是 FFmpeg 用同一份 .def 经 dlltool -l 生成的 import library。
    for f in bin/*.dll; do
        [ -e "$f" ] || continue
        b="$(basename "$f" .dll)"
        srcdef="$(find "$FFSRC" -maxdepth 2 -name "$b.def" 2>/dev/null | head -1)"
        if [ -n "$srcdef" ]; then
            cp -f "$srcdef" "lib/$b.def"
            n="$(grep -cvE '^[[:space:]]*(;|EXPORTS|LIBRARY|$)' "lib/$b.def" || echo '?')"
            printf "  %-26s %s 个导出\n" "lib/$b.def" "$n"
        else
            printf "  %-26s 未找到（跳过，MSVC 可用 bin/%s.lib）\n" "lib/$b.def" "$b"
        fi
    done

    echo
    echo "--- 链接自检（用 probe.c 编一个最小客户端，验证 import lib 真能用）---"
    if [ -f "$HERE/probe.c" ]; then
        mkdir -p "$BLD"
        if gcc -O2 -o "$BLD/probe.exe" "$HERE/probe.c" \
                -I"$OUT/include" -L"$OUT/bin" \
                -lavformat -lavcodec -lavutil -lswresample -lshell32 \
                2>"$BLD/probe-build.log"; then
            echo "  编译+链接 OK → $BLD/probe.exe"
            if [ -n "${PROBE_FILE:-}" ]; then
                echo
                PATH="$OUT/bin:$PATH" "$BLD/probe.exe" "$PROBE_FILE" || true
            else
                echo "  （想真解一个文件：PROBE_FILE=<媒体文件> bash libs/ffmpeg/build-win.sh STAGE=verify）"
            fi
        else
            echo "  失败，见 $BLD/probe-build.log"
            tail -20 "$BLD/probe-build.log"
        fi
    fi

    echo
    echo "体积："
    du -sh bin lib include 2>/dev/null || true
    echo "完成。"
}

# ---------------------------------------------------------------------------
case "$STAGE" in
    prepare)   step_prepare ;;
    configure) step_prepare; step_configure ;;
    build)     step_prepare; step_make; step_verify ;;   # 跳过 configure（已成功配置过时用）
    make)      step_prepare; step_configure; step_make; step_verify ;;
    verify)    step_verify ;;                            # 只重新体检产物 + 重生成 .def
    all)       step_prepare; step_configure; step_make; step_verify ;;
    *)         die "未知 STAGE=$STAGE（可选：prepare | configure | build | verify | make | all）" ;;
esac
