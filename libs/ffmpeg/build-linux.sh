#!/usr/bin/env bash
# ============================================================================
#  纯白铃铛 —— 自编译精简版 FFmpeg（**Linux / x86_64 / LGPL**）
#
#  产物落在 linux-x64/，与 Windows 的 win-x64/ 并列、互不干扰。
#  CMake 会按平台自动挑对应的那一份（见 CMakeLists.txt 的 QA_LIBAV_SUBDIR）。
#
#  与 Windows 那份的两点差异（都是有意的）：
#    1. Linux 上编**静态库**（.a）—— 产物自包含，不用操心 .so 的 RPATH /
#       版本号符号链接 / 运行期拷贝。Windows 那边只能是动态库，因为
#       MinGW 编出来的 .a 无法被 MSVC 链接，只能用 DLL + import lib。
#    2. 构建目录是 _build-linux/（与 Windows 的 _build/ 并列，互不干扰）。
#       ⚠⚠ 但 `_src/` 源码树是**两平台共用**的 in-source 树：在 A 平台编过之后直接换
#          B 平台编，make 会因为「.o 比 .c 新」漏编一部分文件，把 A 平台的目标文件
#          原样打进 B 平台的库（症状：undefined reference to `__mingw_vsnprintf`、
#          ld 收到 COFF relocation 直接段错误）。step_prepare 里已有自动检测 + 重建。
#
#  依赖（Ubuntu/Debian 一把装齐）：
#      sudo apt install build-essential nasm pkg-config
#      # 32 位/ARM 交叉编译才需要额外工具链
#
#  用法：
#      bash libs/ffmpeg/build-linux.sh                 # 全流程
#      STAGE=configure bash libs/ffmpeg/build-linux.sh # 只到 configure
#      STAGE=build     bash libs/ffmpeg/build-linux.sh # 跳过 configure（已配置过时用）
#      STAGE=verify    bash libs/ffmpeg/build-linux.sh # 只体检产物
#      FFVER=8.1.3     bash libs/ffmpeg/build-linux.sh # 换版本
#      JOBS=4          bash libs/ffmpeg/build-linux.sh # 限制并行
#
#  产物（linux-x64/）：
#      lib/      libavformat.a  libavcodec.a  libavutil.a  libswresample.a
#                + pkgconfig/
#      include/  FFmpeg 头文件
#
#  编进去的内容（白名单见 ff-config-flags.sh / whitelist-decoders.txt，两平台共用）：
#      · 常用解封装器（30 个：mov/matroska/mp3/flac/ogg/wav/aac/ts/avi/asf/amr/rm/flv/…）
#      · 常用解析器（8 个）
#      · 常用**音频**解码器（53 个，视频解码器一个不要）
#      · 编码器 / 封装器：0（编码交给 libopus，容器交给 ogg_packer）
#      即 libav 在本项目里是「只读」的：只解、不编、不写容器。
#
#  注：Linux 这份是**静态库**（无 DLL），所以不会碰到 Windows 那边的
#      「.def 生成命令行被截断在 8192 字节」的坑（见 ff-config-flags.sh 顶部）。
# ============================================================================

set -euo pipefail

# ---- 路径（全 POSIX） ----
_self="${BASH_SOURCE[0]}"
case "$_self" in
    */*) _dir="${_self%/*}" ;;
    *)   _dir="." ;;
esac
HERE="$(cd "$_dir" && pwd)"
OUT="$HERE/linux-x64"       # Linux 产物落这儿
SRC="$HERE/_src"            # 源码包与解压结果 —— ⚠ 与 Windows 那份**同一棵树**，跨平台复用
                            #   前必须清干净（见头部 ⚠⚠ 与 step_prepare 的自动检测）
BLD="$HERE/_build-linux"    # 中间产物

FFVER="${FFVER:-7.1.5}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
STAGE="${STAGE:-all}"
TARBALL="ffmpeg-$FFVER.tar.xz"
FFSRC="$SRC/ffmpeg-$FFVER"

die() { echo "错误：$*" >&2; exit 1; }

# 白名单与枚举逻辑放这儿，Windows / Linux 两份脚本共用同一份（别再各抄一遍）
source "$HERE/ff-config-flags.sh"

# ---------------------------------------------------------------------------
step_prepare() {
    echo "== [1/4] 准备：检查工具链 + 取源码 =="

    command -v gcc  >/dev/null || die "找不到 gcc（apt install build-essential）"
    command -v make >/dev/null || die "找不到 make"
    # x86 汇编优化：nasm 或 yasm 二者其一即可；都没有就用 --disable-x86asm（慢一点但能用）
    HAVE_ASM=1
    if ! command -v nasm >/dev/null && ! command -v yasm >/dev/null; then
        HAVE_ASM=0
        echo "   ⚠ 没装 nasm/yasm → 关掉 x86 汇编优化（能编，性能略降）"
    fi
    echo "   gcc : $(gcc --version | head -1)"

    mkdir -p "$SRC" "$BLD" "$OUT"

    if [ ! -f "$SRC/$TARBALL" ]; then
        echo "   下载 $TARBALL ..."
        curl -L --fail --retry 3 -o "$SRC/$TARBALL" "https://ffmpeg.org/releases/$TARBALL"
    fi
    echo "   源码包: $(du -h "$SRC/$TARBALL" | cut -f1)"

    # --- 跨平台污染检测（重要，别删）--------------------------------------
    # _src 是 win / linux **共用**的 in-source 源码树。在 A 平台编过之后换 B 平台，
    # make 的增量判断只看 .o 与 .c 的时间戳（.c 是源码包的原始日期，很旧），于是
    # 会「漏编」一部分文件 —— A 平台编出来的目标文件被原样打包进 B 平台的库里。
    # 症状（实测 2026-10-02）：
    #     链接期 undefined reference to `__mingw_vsnprintf'（外平台符号）
    #     ld: BFD ... assertion fail ../../bfd/reloc.c:8580
    #     collect2: fatal error: ld terminated with signal 11 [Segmentation fault]
    # 判据：读 .o 的文件头 magic —— ELF = 7f454c46（Linux），COFF-x64 = 6486（MinGW）。
    # 只要 libavutil 下有一个不是本平台的，就整棵删掉重解压（.tar.xz 还在，几秒钟）。
    if ls "$FFSRC"/libavutil/*.o >/dev/null 2>&1; then
        _bad=0; _sample=""
        for _o in "$FFSRC"/libavutil/*.o; do
            _m="$(od -An -tx1 -N4 "$_o" 2>/dev/null | tr -d ' \n' || true)"
            if [ "$_m" != "7f454c46" ]; then _bad=$((_bad + 1)); _sample="${_o##*/}"; fi
        done
        if [ "$_bad" -gt 0 ]; then
            echo "   ⚠ 源码树里有 $_bad 个**非本平台**的目标文件（$_sample 等）"
            echo "     —— 它被另一个平台编过；不删的话这些 .o 会被直接打进 .a"
            rm -rf "$FFSRC"
        fi
    fi

    # 用 configure 是否存在判断「解压完整」——中途被打断时能自动重来
    if [ ! -f "$FFSRC/configure" ]; then
        rm -rf "$FFSRC"
        echo "   解压..."
        if command -v xz >/dev/null; then
            tar -xf "$SRC/$TARBALL" -C "$SRC"
        else
            echo "   （没有 xz，改用 Python 的 lzma）"
            PY="$(command -v python3 || command -v python || true)"
            [ -n "$PY" ] || die "既没有 xz 也没有 python3，无法解 .tar.xz"
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

    # 名单在这里取：demuxer / parser 是 ff-config-flags.sh 里的静态常用表，
    # decoder 读 whitelist-decoders.txt。两平台共用同一份。
    local demuxers parsers decoders
    demuxers=$(qc_ff_demuxer_list)
    parsers=$(qc_ff_parser_list)
    decoders=$(qc_ff_decoder_list "$HERE/whitelist-decoders.txt") || die "读取解码器白名单失败"
    echo "   白名单：解封装器 $(qc_ff_count "$demuxers") 个 · 解析器 $(qc_ff_count "$parsers") 个 · 音频解码器 $(qc_ff_count "$decoders") 个"
    echo

    set -- \
        --prefix="$OUT" \
        --arch=x86_64 \
        --enable-static --disable-shared --enable-pic \
        --disable-gpl --disable-nonfree --disable-version3 \
        --disable-programs --disable-doc --disable-debug \
        --disable-network --disable-autodetect \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
        --disable-everything \
        --enable-protocol="$QC_FF_PROTOCOLS" \
        --enable-demuxer="$demuxers" \
        --enable-parser="$parsers" \
        --enable-decoder="$decoders" \
        --extra-cflags="-O2"
    [ "$HAVE_ASM" = "1" ] || set -- "$@" --disable-x86asm

    ./configure "$@"

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
    echo "   configure 完成"
}

# ---------------------------------------------------------------------------
step_make() {
    echo "== [3/4] 编译（最慢的一步）=="
    cd "$FFSRC"
    make -j"$JOBS"
    echo
    echo "== 安装到 $OUT =="
    make install
}

# ---------------------------------------------------------------------------
step_verify() {
    echo "== [4/4] 验证产物 =="
    cd "$OUT"

    echo
    echo "--- 静态库与位数 ---"
    for f in lib/libavformat.a lib/libavcodec.a lib/libavutil.a lib/libswresample.a; do
        if [ -f "$f" ]; then
            printf "  %-24s %s  OK\n" "$(basename "$f")" "$(du -h "$f" | cut -f1)"
        else
            printf "  %-24s 缺失 ✘\n" "$(basename "$f")"
        fi
    done

    echo
    echo "--- 许可证 ---"
    grep -E "^CONFIG_(GPL|NONFREE|GPLV3)=" "$FFSRC/config.h" 2>/dev/null \
        || grep -E "^#define CONFIG_(GPL|NONFREE|GPLV3)" "$FFSRC/config.h" 2>/dev/null \
        || echo "  （查不到，去 $FFSRC/config.h 看 CONFIG_GPL）"

    echo
    echo "--- 链接自检（用 probe.c 编一个最小客户端）---"
    if [ -f "$HERE/probe.c" ]; then
        mkdir -p "$BLD"
        # ⚠ 库顺序必须和 CMakeLists.txt 的 Linux 分支一致（format→codec→swr→util）：
        #   GNU ld 对 .a 单遍扫描，把 avutil 写在 swresample 前面会让后者的
        #   av_malloc/av_log 等符号变 undefined。顺序错了这个自检会给出假绿灯。
        if gcc -O2 -o "$BLD/probe" "$HERE/probe.c" \
                -I"$OUT/include" -L"$OUT/lib" \
                -lavformat -lavcodec -lswresample -lavutil -lm -lpthread \
                2>"$BLD/probe-build.log"; then
            echo "  编译+链接 OK → $BLD/probe"
            if [ -n "${PROBE_FILE:-}" ]; then
                echo
                "$BLD/probe" "$PROBE_FILE" || true
            else
                echo "  （想真解一个文件：PROBE_FILE=<媒体文件> bash libs/ffmpeg/build-linux.sh STAGE=verify）"
            fi
        else
            echo "  失败，见 $BLD/probe-build.log"
            tail -20 "$BLD/probe-build.log"
        fi
    fi

    echo
    echo "体积："
    du -sh lib include 2>/dev/null || true
    echo "完成。"
}

# ---------------------------------------------------------------------------
case "$STAGE" in
    prepare)   step_prepare ;;
    configure) step_prepare; step_configure ;;
    build)     step_prepare; step_make; step_verify ;;   # 跳过 configure（已成功配置过时用）
    make)      step_prepare; step_configure; step_make; step_verify ;;
    verify)    step_verify ;;                            # 只重新体检产物
    all)       step_prepare; step_configure; step_make; step_verify ;;
    *)         die "未知 STAGE=$STAGE（可选：prepare | configure | build | verify | make | all）" ;;
esac
