cmake_minimum_required(VERSION 3.16)

project(qiancao VERSION 0.1 LANGUAGES CXX)

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
    db/botdb.cpp db/botdb.h db/image-server.cpp db/lmdbkv.cpp db/lmdbkv.h
    ui/accountpage.cpp ui/accountpage.h ui/addaccountdialog.cpp ui/addaccountdialog.h
    ui/cardwidget.cpp ui/cardwidget.h ui/chatpage.cpp ui/chatpage.h
    ui/flowlayout.cpp ui/flowlayout.h ui/homepage.cpp ui/homepage.h
    ui/logpage.cpp ui/logpage.h ui/pluginpage.cpp ui/pluginpage.h
    ui/sandboxwindow.h ui/sandboxwindow.cpp
    ui/buttoneditor.h ui/buttoneditor.cpp
    ui/botruleconfigwidget.h ui/botruleconfigwidget.cpp
    ui/textreplaceconfigwidget.h ui/textreplaceconfigwidget.cpp
    ui/set.h ui/set.cpp
    ui/keywordmatchconfigwidget.h ui/keywordmatchconfigwidget.cpp
    ui/keywordpunishconfigwidget.h ui/keywordpunishconfigwidget.cpp
    ui/blacklistpage.h ui/blacklistpage.cpp
    ui/forbiddenwordpage.h ui/forbiddenwordpage.cpp
    ui/scheduleconfigwidget.h ui/scheduleconfigwidget.cpp
    python/sharedmemorybridge.cpp python/sharedmemorybridge.h
    qt/global.h qt/global.cpp
    qt/api.cpp
    qt/accountinfo.h qt/accountinfo.cpp
    qt/qqbotclient.h qt/qqbotclient.cpp

    qt/accountinfo.cpp qt/accountinfo.h
    db/botdb.cpp db/botdb.h db/image-server.cpp db/lmdbkv.cpp db/lmdbkv.h db/logdb.cpp db/logdb.h
    python/python_api.cpp
    qt/ahocorasick.h
    qt/qq_bind_login.h
    plugin/node_process.h plugin/node_process.cpp
    plugin/node_plugin_manager.h plugin/node_plugin_manager.cpp
    qt/placeholderlineedit.h qt/placeholderlineedit.cpp
    qt/placeholdertextedit.h qt/placeholdertextedit.cpp
    ui/cpphighlighter.h ui/cpphighlighter.cpp
    ui/htmltoimagewidget.h ui/htmltoimagewidget.cpp
    network/screenshotsyncclient.h network/screenshotsyncclient.cpp
    ai/aiwidget.h ai/aiwidget.cpp
    qt/role.h
    ai/plts.h ai/plts.cpp ai/plts.ui
    ai/aisxw.h ai/aisxw.cpp ai/aisxw.ui
    ai/bqbgl.h ai/bqbgl.cpp ai/bqbgl.ui
    qt/jjm.h
    ai/qunguan.h ai/qunguan.cpp ai/qunguan.ui
    ui/statuschartwidget.h ui/statuschartwidget.cpp
    network/clientconnection.h network/clientconnection.cpp
    network/websocketserver.h network/websocketserver.cpp
    ai/aifujia.h ai/aifujia.cpp ai/aifujia.ui
    ui/chatwidget.h
    ui/appwindow.h ui/appwindow.cpp
    db/vectormemory.h
    python/pythonparser.h python/pythonparser.cpp
    plugin/pluginmarketwindow.h plugin/pluginmarketwindow.cpp
    network/netmanager.h network/netmanager.cpp
    plugin/pluginmarket.h
    qt/cnbuploader.h
    ui/menupanelwidget.h ui/menupanelwidget.cpp
    ui/ui_menupanelwidget.ui
    qt/apiprocessor.h qt/apiprocessor.cpp
    ai/nickreviewwidget.h ai/nickreviewwidget.cpp
)
if(NOT WIN32)
    # Linux / macOS 下不需要 Windows 独有的共享内存桥接
    list(REMOVE_ITEM PROJECT_SOURCES_ALL
        python/sharedmemorybridge.cpp
        python/sharedmemorybridge.h
    )
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

    )
endif()





target_include_directories(qiancao PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/db
    ${CMAKE_CURRENT_SOURCE_DIR}/qt
    ${CMAKE_CURRENT_SOURCE_DIR}/network
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/python
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/hnswlib
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