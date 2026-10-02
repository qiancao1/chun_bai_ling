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
#   libs/opus     : libopus 1.5.2（Opus 编码，BSD-3）—— 直接用官方 CMake 编译
#   libs/faad2    : FAAD2 2.11.2 的 libfaad（AAC 解码，GPL-2.0-or-later）
#   libs/decoders : dr_mp3 / dr_flac / dr_wav / minimp4 / stb_vorbis（解封装+解码）
#                   + ogg_packer.c（取自 libopusenc，BSD-3，负责 Ogg 分页与 CRC）
# 故意不走 FetchContent：这些库版本不常动，随仓库走才能保证离线、可复现地编出来。
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

if(NOT TARGET qa_faad2)
    # faad2 自带的 CMake 会一次编出 faad / faad_drm / faad_fixed / faad_drm_fixed 四份，
    # 这里只要最普通的一份，所以自己列源文件（libfaad 不依赖生成出来的 faad.h，只认 neaacdec.h）
    file(GLOB QA_FAAD2_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/faad2/libfaad/*.c")
    add_library(qa_faad2 STATIC ${QA_FAAD2_SOURCES})
    target_include_directories(qa_faad2
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/libs/faad2/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/faad2/libfaad)
    target_compile_definitions(qa_faad2 PRIVATE
        HAVE_INTTYPES_H=1 HAVE_MEMCPY=1 HAVE_STRING_H=1 HAVE_STRINGS_H=1
        HAVE_SYS_STAT_H=1 HAVE_SYS_TYPES_H=1 HAVE_LRINTF=1
        APPLY_DRC PACKAGE_VERSION="2.11.2")
    if(MSVC)
        target_compile_options(qa_faad2 PRIVATE /wd4702 /wd4820 /wd5045)
    else()
        target_compile_options(qa_faad2 PRIVATE -ffloat-store -w)
        find_library(QA_MATH_LIB m)
        if(QA_MATH_LIB)
            target_link_libraries(qa_faad2 PUBLIC ${QA_MATH_LIB})
        endif()
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
        qa_faad2                 # 进程内 AAC 解码

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
        qa_faad2                 # 进程内 AAC 解码

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