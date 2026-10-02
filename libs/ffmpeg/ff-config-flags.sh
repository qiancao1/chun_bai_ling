#!/usr/bin/env bash
# ============================================================================
#  纯白铃铛 —— 自编译 FFmpeg 的 configure 白名单（Windows / Linux 共用）
#
#  由 build-win.sh / build-linux.sh 以 `source` 引入，**不要直接执行**。
#
#  ── 编进去的东西 = 常用音频解码器 + 常用解封装器 + 少量解析器 ──
#
#  纯白铃铛只做「从媒体文件里取出音轨 → 解成 PCM」，之后交给 libopus 重编码 +
#  ogg_packer 封 Ogg。所以：
#      视频解码器  —— 一个不要（白名单外的 demuxer 也照样能把视频里的音轨取出来）
#      解封装器    —— 常用那些就够
#      编码器/封装器 —— 一个不要，那是 libopus + ogg_packer 的活
#  即 libav 在本项目里是**只读**的：只解、不编、不写容器。
#
# ⚠⚠⚠ 红线：白名单**不能开太大** ⚠⚠⚠
#
#  原生 gmake.exe 把 recipe 交给 MSYS sh.exe 时，命令行在 **8192 字节** 处被硬截断
#  （实测：命令第 8183 字节 + 3 字节残片 + "sh -c " 6 字节 = 正好 8192）。
#  截断点恰好落在 libavcodec 生成 .def 的那一步 —— makedef 要收下**全部** .o，
#  是整条构建里最长的命令。症状长这样：
#
#      Object does not exist: lib
#      gmake: *** [ffbuild/library.mak:118: libavcodec/avcodec-61.dll] Error 1
#
#  注意最后那个参数 `lib` 不是文件、也不是名字写错，而是**被砍剩的残片**
#  （真有那个文件时它会打印完整路径）。同一条命令用 `sh -c` 手动重放是能成功的，
#  所以别去改 FFmpeg 或找「格式不支持」—— 就是命令行长度。
#
#  参考量级（7.1.5 / x64 / --disable-everything 之后往回加）：
#      全量 demuxer(301) + 全部 parser(60) + 全音频解码器(178) → 命令 10405 字节，必挂
#      本文件这份「常用」                                        → 见 build-win.sh 的自检输出
#  所以 build-win.sh 在 configure 之后会**干跑一遍 make，量最长的 makedef 命令行**，
#  超 8000 字节直接告警。想扩格式就加完再跑一次 configure 看那个数字。
# ============================================================================

# ---------------------------------------------------------------------------
#  协议：只要读本地文件 / 管道，不要网络（--disable-network 下网络协议本来就起不来）
# ---------------------------------------------------------------------------
QC_FF_PROTOCOLS='file,pipe'

# ---------------------------------------------------------------------------
#  解封装器（常用容器）
#     mov      = mp4 / m4a / mov / 3gp / m4b
#     matroska = mkv / webm（mkv 里常见 flac/aac/opus/dts 音轨）
#     asf      = wma / wmv        rm/mpegps = 老 rm/rmvb、VOB
# ---------------------------------------------------------------------------
QC_FF_DEMUXERS='mov,matroska,mp3,flac,ogg,wav,aac,mpegts,mpegps,avi,asf,amr,rm,flv,ac3,eac3,dts,wv,ape,aiff,au,caf,oma,tta,mpc,dsf,w64,truehd,loas,voc'

# ---------------------------------------------------------------------------
#  解析器：只留常见容器真正需要的几个
#    （有些容器 / 裸流不挂 parser 连音轨参数都认不出来；parser 很小但**别整 * 全开**，
#      60 个全开大约要多 1.5 KB 命令行，正踩在 8192 那条线上）
# ---------------------------------------------------------------------------
QC_FF_PARSERS='aac,aac_latm,mpegaudio,flac,vorbis,opus,ac3,dca'

# ---------------------------------------------------------------------------
#  接口（build-*.sh 用）
# ---------------------------------------------------------------------------
qc_ff_demuxer_list() { printf '%s' "$QC_FF_DEMUXERS"; }
qc_ff_parser_list()  { printf '%s' "$QC_FF_PARSERS";  }

# qc_ff_decoder_list <whitelist-decoders.txt 路径>
#   读每行一个名字（# 开头是注释），拼成逗号分隔
qc_ff_decoder_list() {
    local wl="$1" out
    if [ ! -f "$wl" ]; then
        echo "错误：找不到解码器白名单 $wl" >&2
        return 1
    fi
    out=$(grep -vE '^[[:space:]]*(#|$)' "$wl" \
            | tr -d ' \t\r' \
            | tr '\n' ',' | sed 's/,$//') || true
    [ -n "$out" ] || { echo "错误：解码器白名单 $wl 里一个名字都没有" >&2; return 1; }
    printf '%s' "$out"
}

# qc_ff_count <逗号分隔的名单>   → 打印条数（给日志用）
qc_ff_count() {
    printf '%s' "$1" | tr ',' '\n' | grep -c . || true
}
