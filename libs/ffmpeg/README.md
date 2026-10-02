# 自编译精简版 FFmpeg（LGPL）—— 给 qiancao 当进程内 libav 用

这里放的是**自己从源码编出来**的一份 FFmpeg，只保留音频解封装 + 解码，用来替掉
`core/api.cpp` 里的一部分「起外部 `ffmpeg.exe` 进程」的兜底。

上层封装在 **`core/libavio.{h,cpp}`**（不暴露任何 libav 类型，只给三个函数）。
CMake 侧由 `QIANCAO_WITH_LIBAV` 控制，可整体关掉。

## 为什么不用现成的

| 现成的东西 | 问题 |
|---|---|
| 系统里的 `ffmpeg.exe`（gyan.dev 7.1.1，87 MB） | `--enable-gpl --enable-version3`，是 **GPL 构建**；且体积 87 MB；还得起进程 |
| 发布包里的 `ffmpeg.exe`（**xihan123/FFmpeg-Audio** 8.0.1，5.7 MB） | ✅ 实测是 **LGPL-2.1**、x64、全静态（只依赖系统 DLL）、纯音频裁剪 —— **可放心随包分发**，见下「两个 ffmpeg 别混」 |
| QQ 自带的 `C:\Aisy\QQNT\versions\*\ffmpeg.dll`（2.9 MB） | 虽是 x64 + LGPL，但**编译期砍掉了 `file` 协议**（`avformat_open_input` 直接报 "Protocol not found"），只能自己喂 IO；版本停在 **FFmpeg 3.4**；还是 QQ 的文件、不能随本项目分发 |
| `Documents\菲菲\ffmpeg\ffmpeg.dll`（2.0 MB） | **根本不是 FFmpeg**：导出的是 `convertToSilk` / `epl_convertToSilkBin`（易语言那套），是转 SILK 用的 |

自己编的好处：**LGPL 干净**、体积小、版本可控、能进仓库、不依赖别人的安装目录。

## 为什么出来一堆 DLL，而不是 1 个 `ffmpeg.exe`

**两件事要分开看。**

**① `ffmpeg.exe` 是「程序」，我们要的是「库」。**
`build-win.sh` 里写了 `--disable-programs` —— **故意不编命令行工具**。目的是让 `qiancao.exe`
直接用 API（进程内），而不是每转一条音频起一个进程。
你包里那个 5.7 MB 的 exe 恰好相反：`--enable-ffmpeg --enable-ffprobe --enable-static --disable-shared`
—— 它编了程序，并把这几个库**静态链进了同一个 exe**，所以看起来是「一个文件」。

**② DLL 有 4 个，是因为 FFmpeg 源码本来就分模块**（上游默认如此，每个有独立 ABI 主版本号，
所以文件名带着 59 / 61 这种数字，升级时会一起变）：

| 库 | 职责 | 本构建的产物 |
|---|---|---|
| `libavutil` | 基础层：内存、数学、日志、字典、时间 | `avutil-59.dll` |
| `libavcodec` | 编解码 | `avcodec-61.dll` |
| `libavformat` | 解封装 / 封装（认容器、读流） | `avformat-61.dll` |
| `libswresample` | 采样格式 / 采样率转换 | `swresample-5.dll` |
| `libswscale` / `libavfilter` / `libavdevice` | 缩放 / 滤镜 / 设备 | **没编**（纯音频用不到） |

**能不能合成一个？** 能，但不划算：

- **合进 `qiancao.exe`（全静态）** —— 得让 **MSVC 去链 MinGW 编的 `.a`**，风险高
  （MinGW 运行时符号、libgcc、CRT 约定），没验证过，Linux 那份能这么干是因为那边本来就用 gcc。
- **合成一个 `ffmpeg.dll`** —— 就是 Chromium 那个 2.9 MB QQ dll 的做法：先编静态库，
  再 `-shared -Wl,--whole-archive` 打成一个。多一道手工步骤，**体积不变**。
- 收益只有「文件数 4 → 1」。

真正「一个文件」的方案其实就是那个 5.7 MB 的 `ffmpeg.exe`，但两者是不同取舍：

| | 4 个 DLL（现在） | 只带 `ffmpeg.exe`（`-DQIANCAO_WITH_LIBAV=OFF`） |
|---|---|---|
| 工作方式 | **进程内** 调 API | 每次 `QProcess` 起进程 |
| 速度 | 快，无进程启动开销 | 每条音频多几十~几百 ms |
| 文件数 | 4 个 DLL | 1 个 exe |
| 格式覆盖 | 30 个常用容器 + 53 个音频解码器（含 aiff/au/caf/tta/mpc/dsf/w64） | 全靠 exe（覆盖面更大） |

所以现在的「**两个都带、DLL 优先、exe 兜底**」是最优组合。

## 能不能只留一个文件？（2026-10-02 实测：能）

| 做法 | 状态 | 发布包里的文件数 |
|---|---|---|
| 现状：4 个 DLL + import lib（动态链接） | ✅ 已在用 | 5 |
| **全静态链进 `qiancao.exe`** | ✅ **实测通过** | **1** |
| 合成一个 `ffmpeg.dll`（= Chromium 那套） | 可行，未做 | 2 |

### 全静态那条路是怎么走通的

把 `_src/ffmpeg-*/{libavutil,libavcodec,libavformat,libswresample}/**/*.o`
用 `ar rcs` 打成 4 个 `.a`，交给 MSVC `link.exe` —— 它**能读 GNU ar 归档**，只报
**11 个未解析符号**，全是「MinGW/POSIX 有、MSVC 没有」的：

| 缺口 | 补法 |
|---|---|
| `__mingw_vsnprintf` / `__mingw_vfprintf` / `__mingw_vsscanf` / `__mingw_strtod` | 转发给 MSVC 的 `vsnprintf` / `vfprintf` / `vsscanf` / `strtod`（VC2015+ 已是 **C99 语义**） |
| `sincos` / `sincosf` | `sin()+cos()` |
| `gettimeofday` / `clock_gettime` / `nanosleep` | `GetSystemTimeAsFileTime` / `GetTickCount64` / `Sleep` |
| `mkstemp` | `_mktemp_s` + `_open(…, _O_CREAT \| _O_EXCL)` |
| `___chkstk_ms` | `#pragma comment(linker, "/alternatename:___chkstk_ms=__chkstk")`（x64 下两者约定完全一致） |

全部实现在 **`msvc-shim.c`**（约 150 行）。**只在静态链接时需要**，动态库那条路编译它会重复定义。

实测：`t_static.exe` **2.98 MB**，删掉所有 `av*.dll` 仍跑通 5 种格式
（wma / alac.m4a / ogg-vorbis / 中文 `甩葱歌.m4a` / 含视频轨的 mp4，时长探测与视频轨判断全对）。

### 要付的代价

- 主 exe 体积 **+约 3 MB**（但省掉 4 个 DLL 共 4.3 MB，净减）
- 每次 FFmpeg 升级要重跑「`.o` → `.a` → link」
- 上面用的是 **shared 构建留下的 `.o`**（带 PIC）—— 能用，但正式做法是让脚本出
  `--enable-static --disable-shared` 的干净静态库
- **LGPL-2.1 §6**：静态链接要能让接收者**重新链接** → 本项目源码公开（LGPL-3.0），
  给出源码 + 构建脚本即满足
- ⚠ 这跟 `libs/faad2`（GPL-2.0-or-later）是**两件事**：faad2 也是静态链进去的，
  所以整个二进制早已按 GPL-3 走；libav 的 LGPL 不改变这一点

## 目录与两份产物

Windows 与 Linux 是**两份独立产物**，脚本和目录都分开（CMake 按平台自动挑）：

```
libs/ffmpeg/
  build-win.bat    ← Windows 一键外壳（只负责找到 bash → 调 build-win.sh。⚠ cmd 跑不了 configure，
                     所以这个 .bat 本身编不了 FFmpeg，它只是个启动器）
  build-win.sh     ← Windows 构建（MinGW-w64，出**动态库**）
  build-linux.sh   ← Linux  构建（本机 gcc，出**静态库**）
  probe.c          两个平台共用的端到端验证程序（构建脚本会自动拿它做链接自检）
  win-x64/         Windows 产物
      bin/   avcodec-*.dll  avformat-*.dll  avutil-*.dll  swresample-*.dll
             + 同名 .lib（MSVC 直接链这个）
      lib/   libav*.dll.a（MinGW import lib）、*.def（完整导出表）
      include/  FFmpeg 头文件
      share/ffmpeg/examples/  官方示例（decode_audio.c 正好就是这个场景，可当参考）
  linux-x64/       Linux 产物（跑完 build-linux.sh 才有）
      lib/   libavformat.a libavcodec.a libavutil.a libswresample.a
      include/
  _src/ _build/ _build-linux/   ← 源码包与中间产物，不用提交/不用拷贝（已 gitignore）
```

**为什么两边形式不同**（都是有意的）：

| | Windows | Linux |
|---|---|---|
| 形式 | 动态库 `.dll` | 静态库 `.a` |
| 原因 | MinGW 编出来的 `.a` **无法被 MSVC 链接**（目标文件格式不同），只能给 DLL + import lib | 产物自包含，省掉 `.so` 的 RPATH / 版本号符号链接 / 运行期拷贝 |
| 运行期 | ⚠ `bin/*.dll` **必须与 `qiancao.exe` 同目录**（CMake 已加 POST_BUILD 自动拷贝） | 无额外要求 |

## 没有 `win-x64/` 也能编译 —— 别一视同仁（2026-10-02 核实）

CMake 对这几份第三方目录的依赖**程度不一样**：

| 目录 | CMake 判定 | 缺了会怎样 |
|---|---|---|
| `libs/opus`（随仓库源码） | `add_subdirectory` **无条件**（CMakeLists:220）+ 链 `opus`（:326/:344） | **配置阶段直接失败** |
| `libs/decoders`（随仓库源码） | 3 个 `.c` 列进 `add_executable`（:187-189）+ include（:363） | **`Cannot find source file`** |
| **本目录的产物 `win-x64/` `linux-x64/`** | `if(QIANCAO_WITH_LIBAV AND EXISTS ".../avcodec.h")`（:253） | ✅ **静默降级**：`QA_LIBAV_ENABLED=OFF`，不链、不定义宏，**照常编译**，只是少了进程内 libav 那层 |

**所以「只想改主程序源码」的人根本不需要编 FFmpeg** —— 直接 `cmake` + 构建就行，音频只是少一层兜底
（罕见容器退回外部 `ffmpeg.exe`）。想要完整能力，再按下面走一遍。

## 怎么编

### Windows

**两条路，等价**：

```bat
:: ① 双击 / cmd：一键外壳（自动找 bash，再把参数转交给 build-win.sh）
libs\ffmpeg\build-win.bat
libs\ffmpeg\build-win.bat STAGE=configure      :: 只测工具链，不编译
libs\ffmpeg\build-win.bat STAGE=build JOBS=8
```

```bash
# ② Git Bash / MSYS2 shell 里直接跑（STAGE 既可当环境变量，也可当命令行参数）
bash libs/ffmpeg/build-win.sh                   # 全流程
bash libs/ffmpeg/build-win.sh STAGE=configure   # 只到 configure（⚠ 5~15 分钟，上千项编译探测）
bash libs/ffmpeg/build-win.sh STAGE=build       # 跳过 configure，只编译安装（已配置过时用）
bash libs/ffmpeg/build-win.sh STAGE=verify      # 只重新体检产物 + 重生成 .def + 链接自检
PROBE_FILE=某音频.mp3 bash libs/ffmpeg/build-win.sh STAGE=verify   # 顺带真解一个文件
```

**工具链要求（三者任选其一；脚本按优先级自动探测，不用改代码）**：

| 方案 | 装什么 | 说明 |
|---|---|---|
| ① Strawberry Perl → `C:\Strawberry` | 一个安装包 | 自带 MinGW-w64 gcc + gmake + nasm，**不需要 MSYS2**（开发机用的就是这套） |
| ② MSYS2 → `C:\msys64` | `pacman -S mingw-w64-x86_64-gcc make nasm` | 社区更常见；⚠ make 在这里叫 `make`，Strawberry 里叫 `gmake`，脚本两个都认 |
| ③ 已有的 MinGW-w64 | 自己装进 PATH | `gcc -dumpmachine` 必须是 `x86_64-w64-mingw32` |

另外还需要一个 **bash**（`build-win.bat` 会依次找 `%ProgramFiles%\Git\bin\bash.exe`、
`%LOCALAPPDATA%\Programs\Git\bin\bash.exe`、`C:\msys64\usr\bin\bash.exe`、`C:\Strawberry\c\bin\bash.exe`，
最后查 PATH；⚠ 会**主动跳过** `C:\Windows\System32\bash.exe` —— 那是 WSL 的启动器，不是 MSYS shell）。

> ⚠ `build-win.bat` **刻意只写 ASCII**：cmd.exe 按 OEM 代码页解析 `.bat`，里面写中文到了别人机器上
> 必乱码。中文提示全部由 `build-win.sh` 在 bash 里输出（`.bat` 进来先 `chcp 65001` 保证显示正常）。
>
> ⚠ 开发机的 `cmd.exe` 被工具策略禁用，所以 AI 没法替你跑 `.bat`，这一步得你自己点。

> ⚠ 解 `.tar.xz` 优先用 `7za`/`7z`/`7zr`（PATH → 脚本同目录 → 本机遗留路径），
> 都没有就落到 **Python `tarfile` 兜底**，不会中断。

### Linux

```bash
sudo apt install build-essential nasm      # 一次性依赖
bash libs/ffmpeg/build-linux.sh            # 全流程
bash libs/ffmpeg/build-linux.sh STAGE=build
PROBE_FILE=某音频.mp3 bash libs/ffmpeg/build-linux.sh STAGE=verify
```

两个脚本都支持环境变量 `FFVER`（默认 `7.1.5`）、`JOBS`、`STAGE`
（`prepare`/`configure`/`build`/`verify`/`make`/`all`）。

## 编进去了什么

`--disable-everything` 之后按三张名单往回加，**只加常用的**：

| 类别 | 数量 | 说明 |
|---|---|---|
| 协议 | 2 | `file`、`pipe` |
| 解封装器 demuxer | **30** | `mov`(mp4/m4a/3gp)·`matroska`(mkv/webm)·`mp3`·`flac`·`ogg`·`wav`·`aac`(ADTS)·`mpegts`·`mpegps`·`avi`·`asf`(wma/wmv)·`amr`·`rm`·`flv`·`ac3`·`eac3`·`dts`·`wv`·`ape`·`aiff`·`au`·`caf`·`oma`·`tta`·`mpc`·`dsf`·`w64`·`truehd`·`loas`·`voc` |
| 解析器 parser | **8** | `aac, aac_latm, mpegaudio, flac, vorbis, opus, ac3, dca` |
| **音频**解码器 | **53** | 见 `whitelist-decoders.txt`：主流 + 5.1 影院 + WMA + 语音/老格式 + 全系 PCM/ADPCM + DSD |
| 视频解码器 | **0** | 我们只取音轨、不解视频；砍掉 h264/hevc/av1/vp9 省下 10 MB 以上 |
| 编码器 / 封装器 | **0** | 编码交给 libopus、容器交给 ogg_packer —— 所以 libav 在本项目里是**只读**的：只解、不编、不写容器 |

名单集中在两个文件里、两平台共用，想扩支持范围只改这两处：

- **`whitelist-decoders.txt`** —— 音频解码器名单（每行一个名字，`#` 开头是注释）
- **`ff-config-flags.sh`** —— 协议 / demuxer / parser 三张静态名单 + 读解码器名单的函数，
  `build-win.sh` / `build-linux.sh` 都会 `source` 它

> ⚠ `--enable-decoder=` 列表里个别名字**拼错不会报错**（configure 只在「整体一个都没匹配上」时
> 才给一句 `did not match anything`，个别错名是静默忽略的）。加名字前先对一遍源码：
> `grep -o 'ff_[a-z0-9_]*_decoder' libavcodec/allcodecs.c | sed 's/^ff_//;s/_decoder$//' | sort -u`
> configure 跑完脚本会打印「实际编入的组件数」，可用来核对有没有被静默漏掉。

### ⚠⚠ 为什么白名单**不能开太大**：8192 字节命令行红线（2026-10-02 实测）

Windows 上 `.dll` 需要一份导出表 `.def`，而生成它的 `makedef` 要**收下该库全部 `.o`**
—— 这是整条构建里最长的一条命令行。**原生 `gmake.exe` 把 recipe 交给 MSYS `sh.exe` 时，
命令行在 8192 字节处被硬截断**：

```
Object does not exist: lib
gmake: *** [ffbuild/library.mak:118: libavcodec/avcodec-61.dll] Error 1
```

最后那个 `lib` **不是文件名、也不是拼错的名字，而是被砍剩的残片**（真有该文件时它打印完整路径）。
实测数据：命令总长 10405 字节 → 第 8183 字节处切断 + 3 字节残片 + `"sh -c "` 6 字节 = **正好 8192**；
430 个参数只剩下 353 个。**同一条命令用 `sh -c` 手动重放是成功的**，所以别去怀疑 FFmpeg 或格式支持 —— 就是长度。

- 触发条件：`全量 demuxer(301) + 全部 parser(60) + 全音频解码器(178)` → 10405 字节，**必挂**
- 本目录这份「常用」名单远在红线以下；`build-win.sh` 在 configure 之后会**干跑一遍 make
  量最长的那条 makedef 命令行**，超 8000 字节直接告警（余量也一并打印）
- Linux 走静态库、没有 `.def` 这一步，所以不受影响

许可证：`--disable-gpl --disable-nonfree --disable-version3` → **LGPL-2.1**
（`CONFIG_GPL=0 CONFIG_NONFREE=0 CONFIG_GPLV3=0`，`EXTRALIBS` 为空）。

> Linux 那份是静态链接。LGPL-2.1 §6 对静态链接有「提供可重链接的目标文件」的要求 ——
> 对源码分发的项目，把本目录的构建脚本 + `FFVER` 指定的官方 tarball 一起给出即可满足。

> 🔁 **改完名单必须重跑 configure**：直接 `bash libs/ffmpeg/build-win.sh`（全流程）。
> 只用 `STAGE=build` 是跳过 configure 的，白名单改动不会生效。
> Windows 上改完记得看 configure 结尾那句「.def 生成命令长度自检」。

## 实测验证结果（2026-10-02，ffmpeg 7.1.5 / Windows x64）

| 检查项 | 结果 |
|---|---|
| 位数 | 4 个 DLL 全是 `pei-x86-64` ✔ |
| 运行时依赖 | 只有 `KERNEL32` + `api-ms-win-crt-*`(UCRT) + 互相引用（avutil 另需 `bcrypt`）→ **无 libgcc_s / libwinpthread 拖油瓶** ✔ |
| 许可证 | `CONFIG_GPL=0 CONFIG_NONFREE=0 CONFIG_GPLV3=0`、`EXTRALIBS=` 空 → **LGPL-2.1** ✔ |
| 容器解封装 | m4a/mp4（mov）、mp3、flac、ogg、wav、asf(wma) 全部 `avformat_open_input` 成功 ✔ |
| 解码出 PCM | aac / mp3 / flac / vorbis / opus / alac / wma / pcm 全部**真解出 PCM** ✔ |
| 中文路径 | `甩葱歌.m4a`、`Documents\菲菲\...` 正常 ✔ |
| 视频轨识别 | mp4 正确报出 `#0=audio(aac) #1=video(h264)`（音轨在第 0 或第 1 都能定位）✔ |
| **MSVC 链接** | `cl.exe` 编译 + `link.exe` 直接链 `bin\*.lib` → **成功**，产物运行输出 `aac decoder found = aac` ✔ |
| **真机跑 core/libavio.cpp** | MSVC + Qt5Core 编成独立 exe，5 个真实文件（wma/alac/ogg/中文 m4a/mp4）**全部通过** ✔ |

验证程序就是 `probe.c`（两个构建脚本的 `STAGE=verify` 会自动编译它做链接自检）：

```bash
# Windows
gcc -O2 -o _build/probe.exe probe.c -Iwin-x64/include -Lwin-x64/bin \
    -lavformat -lavcodec -lavutil -lswresample -lshell32
PATH="win-x64/bin:$PATH" ./_build/probe.exe <媒体文件> ...
# Linux
gcc -O2 -o _build-linux/probe probe.c -Ilinux-x64/include -Llinux-x64/lib \
    -lavformat -lavcodec -lavutil -lswresample -lm -lpthread
```

`probe.c` 内部用 `GetCommandLineW` + `WideCharToMultiByte(CP_UTF8)` 取参——因为
FFmpeg 在 Windows 上把路径当 **UTF-8** 解，而 MinGW 的 `main()` 拿到的是 CP936 字节。
`core/libavio.cpp` 里则是 `QString::toUtf8()`，天然正确。

## 在 C++ 里用（重要，别踩）

**⚠⚠ FFmpeg 的头文件不带 `extern "C"`**（7.1.5 实测：整个 include 树里一处都没有、
也不看 `__cplusplus`）。用 C 写（`.c`）时直接 include 没问题，**C++ 里必须自己包**：

```cpp
#ifndef __STDC_CONSTANT_MACROS          // ⚠ 必须在 <stdint.h> 之前
#  define __STDC_CONSTANT_MACROS
#endif
#include <QString>                       // Qt 头照常
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}
```

不包的现象：编译期**没有任何报错**，链接近似一片

```
error LNK2019: 无法解析的外部符号 "unsigned int __cdecl avutil_version(void)"
                (?avutil_version@@YAIXZ)
```

（C++ 修饰名 vs 库里的 C 符号）。`core/libavio.cpp` 已按上面写好。

另外 `libavutil/common.h` 在 C++ 下会 `#error missing -D__STDC_CONSTANT_MACROS`；
MSVC 的 `<inttypes.h>` 自带 `UINT64_C` 所以侥幸能过，**glibc 下会直接报错** ——
所以那个宏要放到文件最开头。

## MSVC 工程怎么链

**可以直接链**（已实测）：`bin\*.lib` 是 FFmpeg 用 `dlltool -l` 从完整 `.def` 生成的
标准 COFF import library，MSVC `link.exe` 能吃下，不用转换。
万一某个 VS 版本不认，用 `.def` 转一份：

```
lib /def:libs\ffmpeg\win-x64\lib\avcodec-61.def /machine:x64 /out:avcodec.lib
```

CMake 侧已经接好（见根目录 `CMakeLists.txt` 的「自编译的精简版 FFmpeg（libav）」段）：

```cmake
cmake .. -DQIANCAO_WITH_LIBAV=OFF      # 不想用它就关掉（core/libavio.cpp 会退化成空实现）
```

要点：

1. **DLL 必须和 exe 放一起**（或进 PATH），否则 `qiancao.exe` **一启动就失败**
   （是加载期依赖，不是用到才加载）。CMake 加了 POST_BUILD 自动拷贝。
2. CRT 一致：MinGW 用的是 **UCRT**，MSVC 2015+ 也是 UCRT → 堆不冲突。
3. 内存一律用 `av_malloc` / `av_free`，别跨边界 `free()`。
4. 别把 MSVC 的 `FILE*` 交给 FFmpeg（传**文件路径字符串**即可）。
5. 用 MSVC 编自己的 .cpp 且含中文注释时记得 **`/utf-8`**（否则 C4819 + 按 936 切字）。

## 这套脚本踩过的坑（都已在脚本/封装里规避）

| 现象 | 原因 | 解法 |
|---|---|---|
| configure 报 **"Sanity test failed"** | Git Bash 继承了 Windows 反斜杠 `TMPDIR=C:\Users\...\Temp`，sh 把 `\` 当转义吃掉 → 路径变成 `C:UsersAiruan...` | `export TMPDIR="/tmp"` |
| 明明成功却报「没有生成 config.mak」 | FFmpeg **4.0 起**构建文件挪进了 **`ffbuild/`** | 检查 `ffbuild/config.mak` |
| `make: command not found` | FFmpeg 的 Makefile 认死 `make`，Strawberry 只提供 **`gmake`** | 把 `gmake.exe` 复制成临时 `make.exe` 并前置 PATH |
| `.tar.xz` 解不开 | 本机**没有 `xz`** | Windows 用 `7za`（Python `lzma` 兜底）；Linux 一般有 `xz` |
| 解压到一半被打断、目录残缺 | 超时 / 中断 | 用「**`configure` 文件是否存在**」判断完整性，可自动重解压 |
| `dlltool -z x.def --export-all-symbols x.dll` 只得到一行 `EXPORTS` | **dlltool 只认 `.o` / `.def`，喂 DLL 无效** | 直接用 FFmpeg 链接时自己生成的 `libavcodec/avcodec-61.def` 等 |
| configure 在 Git Bash 下要 **10 分钟以上** | MSYS 的 fork 很慢，它要做上千项编译探测 | 正常现象；`STAGE=build` 可跳过这一步 |
| C++ 链接一堆 LNK2019 `?avutil_version@@YAIXZ` | FFmpeg 头不带 `extern "C"` | 自己 `extern "C" { #include ... }`（见上） |
| 编译挂在 `libavcodec/avcodec-61.dll`，报 **`Object does not exist: lib`** | `makedef` 的**命令行被截断在 8192 字节**（原生 make → MSYS sh），最后那个 `lib` 是残片 | 缩减白名单（见上文「8192 字节红线」）；不是格式不支持、也别改 FFmpeg |
| **换平台后编出来的库链不上**：`undefined reference to '__mingw_vsnprintf'`，`ld: BFD assertion fail reloc.c:8580`，`ld terminated with signal 11` | **`_src/` 是两平台共用的 in-source 树**，里面留着另一个平台编的 `.o`（COFF）。make 只看 `.o`/`.c` 时间戳（`.c` 是源码包原始日期，很旧）→ **漏编**，把外平台目标文件原样打进了本平台的库 | 两个脚本的 `step_prepare` 已加**跨平台污染检测**：读 `.o` 文件头（ELF=`7f454c46` / COFF-x64=`6486`），发现异平台残留就删树重解压。手动验证：`od -An -tx1 -N4 _src/ffmpeg-7.1.5/libavutil/bprint.o` |
| Linux 编主工程时一片 `undefined reference to 'av_malloc' / 'av_log' …` | **`-l` 顺序写反**：GNU `ld` 对 `.a` 单遍扫描，「用别人的」必须排在「被用的」前，即 `avformat → avcodec → swresample → avutil` | 见 `CMakeLists.txt` Linux 分支的注释；⚠ **`build-linux.sh` 的 probe 自检必须用同一顺序**，否则 probe 只碰到少量符号仍会通过 → 假绿灯（已对齐） |
| `.a` 拷到 Windows 后 `.pc` 里的 `prefix=/home/…` 是死路径 | `make install` 把**构建机路径**写进了 `lib/pkgconfig/*.pc` | 无害：`CMakeLists.txt` 按**完整路径**直接链 `.a`，不走 pkg-config。只有真用 `pkg-config` 时才需要改，或干脆别把 `lib/pkgconfig` 拷过来 |

> 校验 `.a` 里有没有混进外平台目标文件（比 `od` 单个文件更彻底）：
> `.a` 头是 `!<arch>`（`213c6172`），**要看的是归档内成员** —— `ar t x.a` 列名、
> `ar p x.a name.o \| od -An -tx1 -N4` 看头。⚠ GNU ar 的成员名带结尾 `/`
> （`bprint.o/`），按 `.o` 过滤时别漏。

> 提示：`--disable-everything` 之后**没有编码器**，想造测试文件得用别的 ffmpeg
> （比如 `Documents\菲菲\ffmpeg\ffmpeg.exe`，它是 GPL 构建，仅本地测试用、不参与分发）。

## 想改配置

**加/减格式**：只改两个文件里的名单，别动 `./configure` 那段（它是从名单拼出来的）：

- `whitelist-decoders.txt` —— 音频解码器（一行一个）
- `ff-config-flags.sh` 里的 `QC_FF_PROTOCOLS` / `QC_FF_DEMUXERS` / `QC_FF_PARSERS`

⚠ Windows 上加完**必须重跑 configure 并看那句长度自检**，别让 makedef 的命令行越过 8192 字节。

其它开关改对应脚本里的 `./configure` 那一段：

| 想干嘛 | 加什么 |
|---|---|
| 支持 http/https 拉流 | 去掉 `--disable-network`，加 `--enable-protocol=http,https,tcp`（需要 TLS 后端，会引入依赖） |
| 编个精简 `ffmpeg.exe` | 去掉 `--disable-programs`，并放开需要的 encoder/muxer |
| 体积更小（牺牲速度） | 加 `--enable-small` |
| 不需要 swresample | 去掉它的链接（本项目已有 `MonoResampler`；不过 `libavio.cpp` 用 swresample 做「任意采样格式 → 交错 float32」的归一，仍建议留着） |

## 它在上层被用在哪儿

`core/libavio.h` 只暴露三个函数，`core/api.cpp` 里的用法：

| 函数 | 用在哪 | 替掉了什么 |
|---|---|---|
| `libavProbeDurationMs` | `probeAudioDurationSec()` 的第 ② 层 | 原来「自研探不出 → 直接 ffmpeg -i」 |
| `libavHasVideoTrack` | `audioCanSendAsIs()` 的复核 | 自研版只看 `.mp4/.mov` 的 moov |
| `libavDecodeAudioFile` | 转换链路的 ②' 层（`convertAudioToOpusSegmentsEx`） | 原来「解不出 → ffmpeg 转 m4a」 |

层序：**① 自研 audiodecoder → ② libav（本目录）→ ③ ffmpeg.exe**，每层只在前一层失败时介入。

### 接入 libav 之后，`ffmpeg.exe` 还剩哪些用途

`kAllowFfmpegFallback`（`core/api.cpp`）默认 `true`，但**触发面已经只剩这三条**：

| # | 场景 | 首选 | 若没有 `ffmpeg.exe` |
|---|---|---|---|
| 1 | 时长探测：自研 + libav **都认不出**的容器 | `ffmpeg -i` 解析 stderr | 时长未知 → **不判超长** → 原样发（可能发出一条 >298s 的） |
| 2 | **m4a > 298s 切段** | `ffmpeg -c copy` 流复制（无损、秒级） | 落到 ②′/② 进程内重编码切段 —— **功能不丢**，只是慢一点，且体积更小 |
| 3 | 自研 + libav **都解不出**的编码 | `convertAudioToSilk` → 转 m4a | 原样发，**体积压不下来** |

第 2 条是唯一有性能意义的一条：它就是「无损秒级切段」和「重编码一遍」的差别。

**libav 也覆盖不到的格式**（=`ffmpeg.exe` 真正不可替代的场合）：

- **SILK**（QQ 语音）—— FFmpeg 自己就不支持（既没有 demuxer 也没有 decoder），补白名单也没用
- 需要外部库的游戏音乐格式（`.nsf/.spc/.vgm/.mod/.xm/.it`… 走 libgme / libmodplug / libopenmpt）—— 有意排除
- 少数私有格式

> 2026-10-02 一度把白名单扩成「全 demuxer + 全 parser + 178 个音频解码器」，
> 结果撞上上面那条 **8192 字节命令行红线**、编译挂在 `.def` 那步；随后缩回「常用」名单。
> 现在的名单已经把原先的盲点 **AIFF / AU / CAF / TTA / Musepack / DSD(dsf)** 覆盖掉了；
> 仍未覆盖的是 Shorten / MLP / 外部音乐库格式 / SILK。

换言之：**常规音视频（m4a/mp4/mp3/flac/ogg/opus/wav/amr/wma/ac3/dts/mkv/avi/flv/ts/rm/wmv/aiff/caf/tta/mpc/dsf…）现在一层进程都不用起。**

### 许可证：三个「ffmpeg」别混

| 对象 | 位置 | 许可证 | 能不能随包分发 |
|---|---|---|---|
| **本目录的 libav** | `libs/ffmpeg/{win-x64,linux-x64}/` | **LGPL-2.1** | ✅ 能（Windows 动态 / Linux 静态，义务见上） |
| **发布包里的 `ffmpeg.exe`**（5.7 MB / 8.0.1） | `Desktop/qiancao-v1.0.0.0/ffmpeg.exe` | **LGPL-2.1** ✅ | ✅ 能 |
| gyan.dev 的 `ffmpeg.exe`（87 MB / 7.1.1） | `Documents\菲菲\ffmpeg\`、`C:\Aisy\小工具\` | **GPL-3**（`--enable-gpl --enable-version3`） | ❌ **别带** |

那个 5.7 MB 的是 **xihan123/FFmpeg-Audio**（GitHub Actions 自动构建的「仅音频」裁剪版）：
x64、全静态（只依赖 `bcrypt/KERNEL32/msvcrt/SHELL32/USER32`）、留了 `ffmpeg`+`ffprobe`，
configure 里**没有** `--enable-gpl` / `--enable-nonfree` / `--enable-version3` → **LGPL-2.1**。

> ⚠ 仓库自己的 license 和**它产出二进制的 license** 是两回事，后者只由 FFmpeg 的 configure 开关决定。
> 判据就是 `ffmpeg -version` 那一行 configuration 里有没有 `--enable-gpl`。

`ffmpegdiv` 来自设置项（`ui/set.cpp`，`g_config["ffmpeg"]`，默认 `"ffmpeg/"`），也就是它是**运行期可选的外部程序**。

**分发义务（LGPL）**：只需在发布包里附一句来源说明即可 ——

```
ffmpeg.exe —— FFmpeg 8.0.1，LGPL-2.1
  构建：https://github.com/xihan123/FFmpeg-Audio
  源码：https://ffmpeg.org/download.html#releases
libs/ffmpeg —— FFmpeg 7.1.5，LGPL-2.1（构建脚本见本目录 build-win.sh / build-linux.sh）
```

它是**独立进程**，没有链进 `qiancao.exe` → 不传染主程序，也**不触发** LGPL §6 那条
「静态链接须提供可重链接目标文件」的义务（那条只针对 Linux 那份静态 libav）。

> **红线只有一条**：**别把那个 87 MB 的 gyan.dev 版塞进发布包**。两个文件**同名**，别拿混。
