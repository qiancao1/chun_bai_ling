cmake_minimum_required(VERSION 3.16)

project(qiancao VERSION 0.1 LANGUAGES CXX)

# ==================== 全局设置 ====================
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(LMDB_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB/lmdb.lib)

# ==================== Python 版本选择 ====================
# ========== Python 版本选择开关 ==========
# 使用 t 版：设为 ON
# 使用标准版：设为 OFF
set(USE_FREETHREADING_PYTHON ON)   # <--- 改这一行：ON=t版，OFF=标准版
# ======================================

if(USE_FREETHREADING_PYTHON)
    # ---------- t 版手动配置 ----------
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

    # pybind11 需要的扩展名
    if(WIN32)
        set(PYTHON_MODULE_EXTENSION ".pyd" CACHE STRING "Python module extension")
    elseif(APPLE)
        set(PYTHON_MODULE_EXTENSION ".so" CACHE STRING "Python module extension")
    else()
        set(PYTHON_MODULE_EXTENSION ".so" CACHE STRING "Python module extension")
    endif()
    set(PYTHON_MODULE_PREFIX "" CACHE STRING "Python module prefix")
    set(PYTHON_IS_DEBUG "0" CACHE STRING "Python is debug build")
    set(PYBIND11_FINDPYTHON ON CACHE BOOL "Use FindPython")

    message(STATUS "Using free-threaded Python (t version)")
else()
    # ---------- 标准 Python：自动查找 ----------
    find_package(Python 3.14 REQUIRED COMPONENTS Interpreter Development)
    # 如果你系统里装的是其他版本（比如 3.12），把上面的 3.14 改成 3.12 即可
    message(STATUS "Using system Python (standard build)")
endif()

message(STATUS "Python Configuration:")
message(STATUS "  Executable: ${Python_EXECUTABLE}")
message(STATUS "  Include dir: ${Python_INCLUDE_DIRS}")
message(STATUS "  Library: ${Python_LIBRARIES}")
if(USE_FREETHREADING_PYTHON)
    message(STATUS "  Module extension: ${PYTHON_MODULE_EXTENSION}")
endif()

# ==================== Qt 查找 ====================
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets WebSockets Network Charts)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets WebSockets Network Charts)

# ==================== pybind11（自动下载） ====================
include(FetchContent)
FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v3.0.4
)
FetchContent_MakeAvailable(pybind11)

# ==================== 项目源文件列表 ====================
set(PROJECT_SOURCES_ALL
    main.cpp
    mainwindow.cpp
    mainwindow.h
    resources.qrc

    db/botdb.cpp db/botdb.h db/image-server.cpp db/lmdbkv.cpp db/lmdbkv.h

    ui/accountpage.cpp ui/accountpage.h ui/addaccountdialog.cpp ui/addaccountdialog.h
    ui/cardwidget.cpp ui/cardwidget.h ui/chatpage.cpp ui/chatpage.h
    ui/flowlayout.cpp ui/flowlayout.h ui/homepage.cpp ui/homepage.h
    ui/logpage.cpp ui/logpage.h ui/pluginpage.cpp ui/PluginPage.h
    ui/sandboxwindow.h ui/sandboxwindow.cpp
    ui/plugindepdialog.h ui/plugindepdialog.cpp
    ui/buttoneditor.h ui/buttoneditor.cpp
    ui/botruleconfigwidget.h ui/botruleconfigwidget.cpp
    ui/textreplaceconfigwidget.h ui/textreplaceconfigwidget.cpp
    ui/set.h ui/set.cpp
    ui/keywordmatchconfigwidget.h ui/keywordmatchconfigwidget.cpp
    ui/blacklistpage.h ui/blacklistpage.cpp
    ui/forbiddenwordpage.h ui/forbiddenwordpage.cpp
    ui/scheduleconfigwidget.h ui/scheduleconfigwidget.cpp

    qt/sharedmemorybridge.cpp qt/SharedMemoryBridge.h
    qt/global.h qt/global.cpp
    qt/api.cpp
    qt/accountinfo.h qt/accountinfo.cpp
    qt/qqbotclient.h qt/qqbotclient.cpp
    qt/eventtask.cpp qt/eventtask.h
    qt/accountinfo.cpp qt/AccountInfo.h
    db/botdb.cpp db/botdb.h db/image-server.cpp db/lmdbkv.cpp db/lmdbkv.h db/logdb.cpp db/logdb.h
    qt/python_api.cpp
    qt/RingBuffer.h
    qt/ahocorasick.h
    qt/ffmpeg_silk.cpp
    qt/node_process.h qt/node_process.cpp
    qt/node_plugin_manager.h qt/node_plugin_manager.cpp
    qt/placeholderlineedit.h qt/placeholderlineedit.cpp
    qt/placeholdertextedit.h qt/placeholdertextedit.cpp
    ui/cpphighlighter.h ui/cpphighlighter.cpp
    htmltoimagewidget.h htmltoimagewidget.cpp
)

# 或对于 Qt5: qt5_add_ui(myapp ai.ui)
# ==================== 构建目标 ====================
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(qiancao
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES_ALL}
    )
else()
    if(ANDROID)
        add_library(qiancao SHARED ${PROJECT_SOURCES_ALL})
    else()
        add_executable(qiancao ${PROJECT_SOURCES_ALL}
            qt/screenshotsyncclient.h qt/screenshotsyncclient.cpp
            ai.h ai.cpp ai.ui
            ui/aiwidget.h ui/aiwidget.cpp
            qt/Role.h
            plts.h plts.cpp plts.ui



        )
    endif()
endif()

# ==================== 平台特殊处理 ====================
if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
    target_compile_options(qiancao PRIVATE /utf-8)
    target_compile_definitions(qiancao PRIVATE
        QT_NO_ACCESSIBILITY
        _SCL_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS
    )
else()
    set(LMDB_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB/liblmdb.a)
endif()

if(WIN32)
    target_sources(qiancao PRIVATE app.rc)
endif()

# ==================== 链接库和包含目录 ====================
target_link_libraries(qiancao PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::WebSockets
    Qt${QT_VERSION_MAJOR}::Network
    ${LMDB_LIB_PATH}
    Qt${QT_VERSION_MAJOR}::Charts
    pybind11::module
    ${Python_LIBRARIES}
)

target_include_directories(qiancao PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/db
    ${CMAKE_CURRENT_SOURCE_DIR}/qt
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB
    ${Python_INCLUDE_DIRS}
)

# 只有使用 t 版时才需要定义 Py_GIL_DISABLED=1
if(USE_FREETHREADING_PYTHON)
    target_compile_definitions(qiancao PRIVATE Py_GIL_DISABLED=1)
endif()

# ==================== macOS / iOS 应用设置 ====================
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.qiancao)
endif()
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

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(qiancao)
endif()