cmake_minimum_required(VERSION 3.16)

project(qiancao VERSION 0.1 LANGUAGES C CXX)

# ==================== 全局设置 ====================
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_definitions(-DNOMINMAX)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wno-attributes)
endif()


# ==================== Python 版本选择（Windows / Linux 自动切换） ====================
if(WIN32)
    set(USE_FREETHREADING_PYTHON ON)
else()
    set(USE_FREETHREADING_PYTHON OFF)
endif()

set(KEY_CONTENT "")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/key.txt")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/key.txt" KEY_CONTENT)
    string(STRIP "${KEY_CONTENT}" KEY_CONTENT)
else()
    message(WARNING "key.txt not found, using EMPTY default key!")
endif()

if(MSVC)
    string(REGEX REPLACE "/showIncludes" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REGEX REPLACE "/showIncludes" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

# ---------- Windows：使用 t 版 Python（手动配置） ----------
if(WIN32 AND USE_FREETHREADING_PYTHON)
    set(PYTHON_FT_ROOT "C:/Users/Airuan/AppData/Local/Python/pythoncore-3.14t-64"
        CACHE PATH "Path to free-threaded Python installation")

    set(Python_EXECUTABLE "${PYTHON_FT_ROOT}/python3.14t.exe" CACHE FILEPATH "Python interpreter")
    set(Python_INCLUDE_DIRS "${PYTHON_FT_ROOT}/include" CACHE PATH "Python include directories")
    set(Python_LIBRARIES "${PYTHON_FT_ROOT}/libs/python314t.lib" CACHE FILEPATH "Python library")
    set(Python_LIBRARY "${Python_LIBRARIES}" CACHE FILEPATH "Python library (alias)")

    set(Python_USE_DEBUG_LIBRARIES FALSE CACHE BOOL "Do not use debug Python libraries")
    set(Python3_USE_DEBUG_LIBRARIES FALSE CACHE BOOL "Do not use debug Python libraries")

    set(Python_Interpreter_FOUND TRUE CACHE INTERNAL "")
    set(Python_Development_FOUND TRUE CACHE INTERNAL "")
    set(Python_FOUND TRUE CACHE INTERNAL "")

    if(WIN32)
        set(PYTHON_MODULE_EXTENSION ".pyd" CACHE STRING "Python module extension")
    else()
        set(PYTHON_MODULE_EXTENSION ".so" CACHE STRING "Python module extension")
    endif()
    set(PYTHON_MODULE_PREFIX "" CACHE STRING "Python module prefix")
    set(PYTHON_IS_DEBUG "0" CACHE STRING "Python is debug build")
    set(PYBIND11_FINDPYTHON ON CACHE BOOL "Use FindPython")

    message(STATUS "Using free-threaded Python (t version) for Windows")
else()
    set(Python_EXECUTABLE "/usr/local/bin/python3.14t" CACHE FILEPATH "" FORCE)
    set(Python_INCLUDE_DIRS "/usr/local/include/python3.14t" CACHE PATH "" FORCE)
    set(Python_LIBRARIES "/usr/local/lib/libpython3.14t.so" CACHE FILEPATH "" FORCE)
    set(Python_LIBRARY "${Python_LIBRARIES}" CACHE FILEPATH "" FORCE)
    set(Python_Interpreter_FOUND TRUE CACHE INTERNAL "")
    set(Python_Development_FOUND TRUE CACHE INTERNAL "")
    set(Python_FOUND TRUE CACHE INTERNAL "")
    # 阻止 pybind11 重新查找 Python
    set(PYBIND11_FINDPYTHON OFF CACHE BOOL "" FORCE)
endif()

message(STATUS "Python Configuration:")
message(STATUS "  Executable: ${Python_EXECUTABLE}")
message(STATUS "  Include dir: ${Python_INCLUDE_DIRS}")
message(STATUS "  Library: ${Python_LIBRARIES}")
if(USE_FREETHREADING_PYTHON)
    message(STATUS "  Module extension: ${PYTHON_MODULE_EXTENSION}")
endif()

# ==================== Qt 查找（平台自动切换） ====================
if(WIN32)
    set(CMAKE_PREFIX_PATH "C:\\Aisy\\Qt\\qt-5.15.3-dynamic-msvc2019-x86_64")
    find_package(Qt5 REQUIRED COMPONENTS Widgets WebSockets Network)
else()
    set(Qt5_DIR "/usr/lib/x86_64-linux-gnu/cmake/Qt5")
    find_package(Qt5 REQUIRED COMPONENTS Widgets WebSockets Network)
endif()
find_package(OpenSSL REQUIRED) #在win使用的是 1.1.1 linux是 3.0.2
# ==================== pybind11（优先使用本地，否则从 Gitee 下载） ====================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/pybind11/CMakeLists.txt")
    message(STATUS "Using local pybind11 from libs/pybind11")
    add_subdirectory(libs/pybind11)
else()
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://gitee.com/mirrors/pybind11.git
        GIT_TAG v3.0.4
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# ==================== 项目源文件列表（全部小写，与 Windows 文件系统一致） ====================
set(RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc")
if(EXISTS "${RESOURCE_FILE}")
    set(RESOURCE_SRC resources.qrc)
    message(STATUS "✅ resources.qrc found")
else()
    set(RESOURCE_SRC "")
    message(STATUS "⏭️ resources.qrc not found, skip")
endif()


set(PROJECT_SOURCES_ALL
    main.cpp
    mainwindow.cpp
    mainwindow.h
    ${RESOURCE_SRC}
    db/botdb.cpp db/botdb.h db/image-server.cpp db/lmdbkv.cpp db/lmdbkv.h db/logdb.cpp db/logdb.h
    ui/accountpage.cpp ui/accountpage.h ui/addaccountdialog.cpp ui/addaccountdialog.h
    ui/cardwidget.cpp ui/cardwidget.h ui/chatpage.cpp ui/chatpage.h
    ui/flowlayout.cpp ui/flowlayout.h ui/homepage.cpp ui/homepage.h
    ui/logpage.cpp ui/logpage.h ui/pluginpage.cpp ui/pluginpage.h
    ui/sandboxwindow.h ui/sandboxwindow.cpp
    ui/scrolltextdialog.h
    ui/buttoneditor.h ui/buttoneditor.cpp
    ui/botruleconfigwidget.h ui/botruleconfigwidget.cpp
    ui/textreplaceconfigwidget.h ui/textreplaceconfigwidget.cpp
    ui/set.h ui/set.cpp
    ui/themecolors.h ui/themecolors.cpp ui/themecolorwidget.h ui/themecolorwidget.cpp
    ui/keywordmatchconfigwidget.h ui/keywordmatchconfigwidget.cpp
    ui/keywordpunishconfigwidget.h ui/keywordpunishconfigwidget.cpp
    ui/blacklistpage.h ui/blacklistpage.cpp
    ui/forbiddenwordpage.h ui/forbiddenwordpage.cpp
    ui/scheduleconfigwidget.h ui/scheduleconfigwidget.cpp
    python/sharedmemorybridge.cpp python/sharedmemorybridge.h
    core/global.h core/global.cpp
    core/api.cpp
    core/accountinfo.h core/accountinfo.cpp
    core/qqbotclient.h core/qqbotclient.cpp
    python/python_api.cpp
    core/ahocorasick.h
    core/qq_bind_login.h
    plugin/node_process.h plugin/node_process.cpp
    plugin/node_plugin_manager.h plugin/node_plugin_manager.cpp
    core/placeholderlineedit.h core/placeholderlineedit.cpp
    core/placeholdertextedit.h core/placeholdertextedit.cpp
    ui/cpphighlighter.h ui/cpphighlighter.cpp
    ui/htmltoimagewidget.h ui/htmltoimagewidget.cpp
    network/screenshotsyncclient.h network/screenshotsyncclient.cpp
    ai/aiwidget.h ai/aiwidget.cpp
    ai/aiwidget2.cpp
    ai/role.h
    ai/plts.h ai/plts.cpp ai/plts.ui
    ai/aisxw.h ai/aisxw.cpp ai/aisxw.ui
    ai/bqbgl.h ai/bqbgl.cpp ai/bqbgl.ui
    core/machinekey.h
    ai/qunguan.h ai/qunguan.cpp ai/qunguan.ui
    ui/statuschartwidget.h ui/statuschartwidget.cpp
    network/clientconnection.h network/clientconnection.cpp
    network/websocketserver.h network/websocketserver.cpp
    network/webuiadmin.h network/webuiadmin.cpp
    ai/aifujia.h ai/aifujia.cpp ai/aifujia.ui
    ui/chatwidget.h
    ui/appwindow.h ui/appwindow.cpp
    db/vectormemory.h
    python/pythonparser.h python/pythonparser.cpp
    plugin/pluginmarketwindow.h plugin/pluginmarketwindow.cpp
    network/netmanager.h network/netmanager.cpp
    plugin/pluginmarket.h
    core/cnbuploader.h
    ui/menupanelwidget.h ui/menupanelwidget.cpp
    ui/ui_menupanelwidget.ui
    core/apiprocessor.h core/apiprocessor.cpp
    ai/nickreviewwidget.h ai/nickreviewwidget.cpp
    core/audiodecoder.h core/audiodecoder.cpp
    core/opusconvert.h core/opusconvert.cpp
    core/libavio.h core/libavio.cpp
    # 第三方解码/封装（单文件库：实现只在 decoder_impl.c 里生成一次）
    libs/decoders/decoder_impl.c
    libs/decoders/stb_vorbis.c
    libs/decoders/ogg_packer.c
)


if(NOT WIN32)
    # Linux / macOS 下不需要 Windows 独有的共享内存桥接
    list(REMOVE_ITEM PROJECT_SOURCES_ALL
        python/sharedmemorybridge.cpp
        python/sharedmemorybridge.h
    )
endif()
# ==================== 进程内音频转码（Opus）用的第三方库 ====================
# 源码全部随仓库走，Windows / Linux 都不需要另外装包：
#   libs/opus     : libopus 1.5.2（Opus **编码**，BSD-3）—— 直接用官方 CMake 编译
#   libs/decoders : dr_mp3 / dr_flac / dr_wav / minimp4 / stb_vorbis（解封装 + 解码备用路径）
#                   + ogg_packer.c（取自 libopusenc，BSD-3，负责 Ogg 分页与 CRC）
# 故意不走 FetchContent：这些库版本不常动，随仓库走才能保证离线、可复现地编出来。
#
# ⚠ AAC 解码（MP4/M4A 里的音轨、裸 ADTS）**已经不用 faad2** 了 —— 改由 libs/ffmpeg
#   那份自编译 FFmpeg（core/libavio.cpp）承担。faad2 是 GPL-2.0-or-later，静态链进主程序
#   会把整个 qiancao.exe 变成 GPL-3；去掉它之后二进制里只剩 BSD-3 / MIT / CC0 / LGPL-2.1。
if(NOT TARGET opus)
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING        OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS       OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE   OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    # 1.5.x 的神经网络新特性（DRED/OSCE）用不到，关掉就不需要那 18MB 的 dnn/ 源码
    set(OPUS_DRED OFF CACHE BOOL "" FORCE)
    set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
    # EXCLUDE_FROM_ALL：不拖慢默认构建，作为 qiancao 的依赖照常编译
    add_subdirectory(libs/opus EXCLUDE_FROM_ALL)
endif()

# （原 libs/faad2 已移除：AAC 解码改走下面的 libav，许可证也一并干净了。）

# ==================== 自编译的精简版 FFmpeg（libav） ====================
# libs/ffmpeg/ 里是从源码编出来的一份 x64 / LGPL-2.1 FFmpeg，只保留音频解封装 + 解码。
# 用来替掉 core/api.cpp 里的一部分 ffmpeg.exe 兜底：
#   · 时长探测（自研 audiodecoder 认不出的容器）
#   · 罕见容器（amr/wma/ac3/非 AAC 音轨…）解码 → 仍旧走进程内 libopus
#
# ⚠ Windows 与 Linux 是**两份独立产物**，构建脚本和目录都分开：
#     win-x64/    ← build-win.sh（MinGW-w64 编，**动态库** .dll + import lib）
#     linux-x64/  ← build-linux.sh（本机 gcc 编，**静态库** .a）
#   Windows 必须是动态库：MinGW 产的 .a 无法被 MSVC 链接，只能给 DLL + import lib；
#   Linux 用静态库：产物自包含，省掉 .so 的 RPATH / 版本符号链接 / 运行期拷贝。
#
# 用法与限制见 libs/ffmpeg/README.md。不想要它、或还没有对应平台的那份产物时：
#     cmake -DQIANCAO_WITH_LIBAV=OFF ..
# 关掉后 core/libavio.cpp 照常参与编译（所有函数返回「不可用」），行为与接入前一致。
option(QIANCAO_WITH_LIBAV "链接自编译的精简版 FFmpeg（libs/ffmpeg）" ON)

if(WIN32)
    set(QA_LIBAV_SUBDIR "win-x64")
else()
    set(QA_LIBAV_SUBDIR "linux-x64")
endif()
set(QA_LIBAV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/ffmpeg/${QA_LIBAV_SUBDIR}")

# 这里只做「探测 + 攒参数」——target_* 系列必须在 add_executable(qiancao) 之后调用
# （见下面「自编译 FFmpeg：实际生效」那一段）。
set(QA_LIBAV_ENABLED OFF)
set(QA_LIBAV_DLLS "")
if(QIANCAO_WITH_LIBAV AND EXISTS "${QA_LIBAV_DIR}/include/libavcodec/avcodec.h")
    set(QA_LIBAV_ENABLED ON)

    if(MSVC)
        # 自编 FFmpeg 用 dlltool 生成的 import library，link.exe 直接认（已实测）
        set(QA_LIBAV_LINK_LIBS
            "${QA_LIBAV_DIR}/bin/avformat.lib"
            "${QA_LIBAV_DIR}/bin/avcodec.lib"
            "${QA_LIBAV_DIR}/bin/avutil.lib"
            "${QA_LIBAV_DIR}/bin/swresample.lib")
    elseif(WIN32)
        # Windows 上的非 MSVC（MinGW/Ninja+gcc）走 GNU import lib
        set(QA_LIBAV_LINK_LIBS
            "${QA_LIBAV_DIR}/lib/libavformat.dll.a"
            "${QA_LIBAV_DIR}/lib/libavcodec.dll.a"
            "${QA_LIBAV_DIR}/lib/libavutil.dll.a"
            "${QA_LIBAV_DIR}/lib/libswresample.dll.a")
    else()
        # Linux：静态库，按完整路径链，不依赖系统装没装 libav*-dev。
        #
        # ⚠ 顺序不能乱：GNU ld 对 .a 是**单遍**扫描，一个库只在「此时有未定义符号」时才会
        #   被拉取；一旦扫过去就不再回头。所以必须让「用别人的」排在「被用的」前面：
        #     libavformat → libavcodec → libswresample → libavutil
        #   （依赖关系：format 用 codec/swr/util；codec 用 swr/util；swr 用 util。）
        #   把 avutil 写在 swresample 前面 → swresample 里的 av_malloc/av_log/声道布局
        #   全成 undefined reference。（Windows 是 import lib，MSVC 无此限制，故那边无所谓。）
        #   m / pthread：libav*.pc 里 Libs 声明的额外依赖（-latomic 实测不需要，库里无
        #   __atomic_* 符号，见 README 坑表）。
        set(QA_LIBAV_LINK_LIBS
            "${QA_LIBAV_DIR}/lib/libavformat.a"
            "${QA_LIBAV_DIR}/lib/libavcodec.a"
            "${QA_LIBAV_DIR}/lib/libswresample.a"
            "${QA_LIBAV_DIR}/lib/libavutil.a"
            m pthread)
    endif()

    # 运行期依赖只对 Windows（动态库）有意义：这几条 DLL **必须在 exe 同目录**，
    # 否则进程一启动就报「找不到 avcodec-XX.dll」（是加载期依赖，不是用到才加载）。
    # Linux 那份是静态库，没有这一步。
    if(WIN32)
        file(GLOB QA_LIBAV_DLLS "${QA_LIBAV_DIR}/bin/*.dll")
    endif()
endif()

# ==================== 构建目标 ====================
if(WIN32)
    add_executable(qiancao WIN32 ${PROJECT_SOURCES_ALL}




    )
else()
    add_executable(qiancao ${PROJECT_SOURCES_ALL})
endif()

# Windows 资源文件（仅 Windows）
if(WIN32)
    target_sources(qiancao PRIVATE app.rc)
endif()

# ==================== 链接库和包含目录（平台自动切换） ====================
if(WIN32)
    set(LMDB_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB/lmdb.lib)
    target_link_libraries(qiancao PRIVATE
        Qt5::Widgets
        Qt5::WebSockets
        Qt5::Network
        ${LMDB_LIB_PATH}
        pybind11::module
        ${Python_LIBRARIES}
        OpenSSL::Crypto          # 对应 libcrypto
        OpenSSL::SSL             # 对应 libssl
        opus                     # 进程内 Opus 编码
    )
else()
    find_library(LMDB_LIB_PATH NAMES lmdb liblmdb PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib)
    if(NOT LMDB_LIB_PATH)
        message(FATAL_ERROR "LMDB library not found. Install with: sudo apt install liblmdb-dev")
    else()
        message(STATUS "Found LMDB: ${LMDB_LIB_PATH}")
    endif()
    target_link_libraries(qiancao PRIVATE
        Qt5::Widgets
        Qt5::WebSockets
        Qt5::Network
        ${LMDB_LIB_PATH}
        pybind11::module
        ${Python_LIBRARIES}
        OpenSSL::Crypto          # 对应 libcrypto
        OpenSSL::SSL             # 对应 libssl
        opus                     # 进程内 Opus 编码
    )
endif()





target_include_directories(qiancao PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/db
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/python
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/hnswlib
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/decoders
    ${Python_INCLUDE_DIRS}
)

# ==================== 自编译 FFmpeg（libav）：实际生效 ====================
# 必须在 add_executable(qiancao) 之后；探测与参数在上面的 libav 段落里攒好。
if(QA_LIBAV_ENABLED)
    target_include_directories(qiancao PRIVATE "${QA_LIBAV_DIR}/include")
    target_compile_definitions(qiancao PRIVATE QIANCAO_WITH_LIBAV=1)
    target_link_libraries(qiancao PRIVATE ${QA_LIBAV_LINK_LIBS})

    if(WIN32 AND QA_LIBAV_DLLS)
        add_custom_command(TARGET qiancao POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${QA_LIBAV_DLLS}
                    "$<TARGET_FILE_DIR:qiancao>"
            COMMENT "拷贝自编译 FFmpeg 动态库到输出目录")
    endif()

    message(STATUS "libav: 已启用（${QA_LIBAV_SUBDIR}）")
else()
    if(NOT QIANCAO_WITH_LIBAV)
        message(STATUS "libav: 已显式关闭（-DQIANCAO_WITH_LIBAV=OFF）")
    else()
        message(STATUS "libav: 未启用 —— 缺 ${QA_LIBAV_DIR}/include")
        if(WIN32)
            message(STATUS "       在 Windows 上跑：bash libs/ffmpeg/build-win.sh")
        else()
            message(STATUS "       在 Linux  上跑：bash libs/ffmpeg/build-linux.sh")
        endif()
    endif()
    message(STATUS "       这几处会照旧回退 ffmpeg.exe：时长探测兜底、罕见格式转 m4a")
endif()

if(USE_FREETHREADING_PYTHON)
    target_compile_definitions(qiancao PRIVATE Py_GIL_DISABLED=1)
endif()

# ==================== 平台特定编译选项 ====================
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
    target_compile_options(qiancao PRIVATE /utf-8)
    target_compile_definitions(qiancao PRIVATE
        QT_NO_ACCESSIBILITY
        _SCL_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS
        ENCRYPTION_KEY="${KEY_CONTENT}"
    )
elseif(UNIX)

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -g")

    target_compile_definitions(qiancao PRIVATE
        QT_NO_ACCESSIBILITY
        ENCRYPTION_KEY="${KEY_CONTENT}"
    )
endif()

# ==================== macOS / iOS 应用设置 ====================
set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.qiancao)
set_target_properties(qiancao PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS qiancao
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)