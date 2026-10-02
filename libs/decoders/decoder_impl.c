/*
 * 本文件 = 「单文件解码库」的唯一实例化点（本工程自己的胶水文件，非第三方代码）。
 *
 * dr_mp3 / dr_flac / dr_wav / minimp4 都是 header-only 库：
 * 只有定义了 IMPLEMENTATION 宏的那个编译单元才会生成实现，其它地方只拿到声明。
 * 这里故意用 **.c** 编译（而不是塞进 .cpp），因为这些代码是 C99 风格，
 * 在 C++ 下可能出现 void* 隐式转换之类的兼容问题。
 *
 *   dr_libs  (dr_mp3/dr_flac/dr_wav) : 公共领域 / MIT-0（作者 mackron）
 *   minimp4                          : CC0-1.0（作者 lieff）
 */
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION
#define MINIMP4_IMPLEMENTATION

#include "dr_mp3.h"
#include "dr_flac.h"
#include "dr_wav.h"
#include "minimp4.h"
