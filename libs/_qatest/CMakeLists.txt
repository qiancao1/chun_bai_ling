cmake_minimum_required(VERSION 3.16)
project(qatest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "C:/Users/Airuan/Documents/QTCode/qiancao")

# 源文件是 UTF-8 无 BOM 且含中文注释，MSVC 不加 /utf-8 会按 GBK 切字 → 一片假语法错
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(WIN32)
    set(CMAKE_PREFIX_PATH "C:\\Aisy\\Qt\\qt-5.15.3-dynamic-msvc2019-x86_64")
endif()
find_package(Qt5 REQUIRED COMPONENTS Core)

# ---- libopus（和主工程完全一样的开关）----
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING        OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS       OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE   OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_DRED OFF CACHE BOOL "" FORCE)
set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
add_subdirectory(${ROOT}/libs/opus ${CMAKE_CURRENT_BINARY_DIR}/opus EXCLUDE_FROM_ALL)

# ---- 自编译 FFmpeg（libav）：与主工程同一份产物 ----
# 原来这里挂的是 faad2（GPL-2.0-or-later）。MP4/M4A 的音轨与裸 ADTS AAC 现在都靠 libav，
# 所以本测试工程也接同一份 libav —— 这样才能测到和线上完全一样的链路。
# 产物目录 / 链接方式与主工程 CMakeLists 的「自编译的精简版 FFmpeg」那一段保持一致。
if(WIN32)
    set(LIBAV_SUBDIR "win-x64")
else()
    set(LIBAV_SUBDIR "linux-x64")
endif()
set(LIBAV_DIR "${ROOT}/libs/ffmpeg/${LIBAV_SUBDIR}")

set(LIBAV_ENABLED OFF)
set(LIBAV_DLLS "")
set(LIBAV_LINK "")
if(EXISTS "${LIBAV_DIR}/include/libavcodec/avcodec.h")
    set(LIBAV_ENABLED ON)
    if(MSVC)
        set(LIBAV_LINK
            "${LIBAV_DIR}/bin/avformat.lib"
            "${LIBAV_DIR}/bin/avcodec.lib"
            "${LIBAV_DIR}/bin/avutil.lib"
            "${LIBAV_DIR}/bin/swresample.lib")
    elseif(WIN32)
        set(LIBAV_LINK
            "${LIBAV_DIR}/lib/libavformat.dll.a"
            "${LIBAV_DIR}/lib/libavcodec.dll.a"
            "${LIBAV_DIR}/lib/libavutil.dll.a"
            "${LIBAV_DIR}/lib/libswresample.dll.a")
    else()
        set(LIBAV_LINK
            "${LIBAV_DIR}/lib/libavformat.a"
            "${LIBAV_DIR}/lib/libavcodec.a"
            "${LIBAV_DIR}/lib/libavutil.a"
            "${LIBAV_DIR}/lib/libswresample.a"
            m pthread)
    endif()
    if(WIN32)
        file(GLOB LIBAV_DLLS "${LIBAV_DIR}/bin/*.dll")
    endif()
else()
    message(WARNING "没找到 ${LIBAV_DIR}/include —— 本测试将不接 libav，MP4/AAC 用例会失败。"
                    "先跑 libs/ffmpeg/build-win.sh 编出来。")
endif()

add_executable(qatest
    main.cpp
    ${ROOT}/core/audiodecoder.cpp
    ${ROOT}/core/opusconvert.cpp
    ${ROOT}/core/libavio.cpp
    ${ROOT}/libs/decoders/decoder_impl.c
    ${ROOT}/libs/decoders/stb_vorbis.c
    ${ROOT}/libs/decoders/ogg_packer.c
)
target_include_directories(qatest PRIVATE ${ROOT}/core ${ROOT}/libs/decoders)
target_link_libraries(qatest PRIVATE Qt5::Core opus)

if(LIBAV_ENABLED)
    target_include_directories(qatest PRIVATE "${LIBAV_DIR}/include")
    target_compile_definitions(qatest PRIVATE QIANCAO_WITH_LIBAV=1)
    target_link_libraries(qatest PRIVATE ${LIBAV_LINK})

    # Windows 上那几个 DLL 是**加载期依赖**，必须和 qatest.exe 放一起
    if(WIN32 AND LIBAV_DLLS)
        add_custom_command(TARGET qatest POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBAV_DLLS}
                    $<TARGET_FILE_DIR:qatest>)
    endif()
endif()
