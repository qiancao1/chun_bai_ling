cmake_minimum_required(VERSION 3.16)

project(qiancao VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(LMDB_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB/lmdb.lib)



find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets WebSockets Network Charts)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets WebSockets Network Charts)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
)

# 将源文件列表提取到一个变量中，避免重复
set(PROJECT_SOURCES_ALL
    ${PROJECT_SOURCES}
    resources.qrc

    qt/sharedmemorybridge.cpp qt/SharedMemoryBridge.h
    qt/global.h qt/global.cpp
    qt/api.cpp
    qt/accountinfo.h qt/accountinfo.cpp
    qt/qqbotclient.h qt/qqbotclient.cpp
    db/botdb.cpp db/botdb.h db/image-server.cpp db/lmdbkv.cpp db/lmdbkv.h
    ui/accountpage.cpp ui/accountpage.h ui/addaccountdialog.cpp ui/addaccountdialog.h
    ui/cardwidget.cpp ui/cardwidget.h ui/chatpage.cpp ui/chatpage.h
    ui/flowlayout.cpp ui/flowlayout.h ui/homepage.cpp ui/homepage.h
    ui/logpage.cpp ui/logpage.h ui/pluginpage.cpp ui/PluginPage.h
    qt/eventtask.cpp qt/eventtask.h
    qt/accountinfo.cpp qt/AccountInfo.h
    db/botdb.cpp db/botdb.h db/image-server.cpp db/lmdbkv.cpp db/lmdbkv.h db/logdb.cpp db/logdb.h
    qt/python_api.cpp
    ui/sandboxwindow.h ui/sandboxwindow.cpp
    qt/RingBuffer.h
    ui/plugindepdialog.h ui/plugindepdialog.cpp
    ui/buttoneditor.h ui/buttoneditor.cpp
    ui/botruleconfigwidget.h ui/botruleconfigwidget.cpp
    ui/textreplaceconfigwidget.h ui/textreplaceconfigwidget.cpp
    ui/set.h ui/set.cpp
    ui/keywordmatchconfigwidget.h ui/keywordmatchconfigwidget.cpp
    qt/ahocorasick.h
    qt/ffmpeg_silk.cpp
    ui/blacklistpage.h ui/blacklistpage.cpp
    ui/forbiddenwordpage.h ui/forbiddenwordpage.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(qiancao
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES_ALL}
    )
    # 如果有 Android 相关的特殊设置，可以在这里添加
    # set_property(TARGET qiancao APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
else()
    if(ANDROID)
        add_library(qiancao SHARED ${PROJECT_SOURCES_ALL})
        # set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(qiancao ${PROJECT_SOURCES_ALL})
    endif()
endif()

if(MSVC)
    target_compile_options(qiancao PRIVATE /utf-8)
    target_compile_definitions(qiancao PRIVATE

        QT_NO_ACCESSIBILITY
        _SCL_SECURE_NO_WARNINGS
        _CRT_SECURE_NO_WARNINGS
    )

else()
    # 非 Windows 平台，保持原有逻辑
    set(LMDB_LIB_PATH ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB/liblmdb.a)
endif()


target_link_libraries(qiancao PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::WebSockets
    Qt${QT_VERSION_MAJOR}::Network

    ${LMDB_LIB_PATH}
    Qt${QT_VERSION_MAJOR}::Charts
)

target_link_directories(qiancao PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/pybind11/libs
)


target_include_directories(qiancao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/pybind11/include)
target_include_directories(qiancao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/pybind11/Lib/site-packages/pybind11/include)
target_include_directories(qiancao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_include_directories(qiancao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/db)
target_include_directories(qiancao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qt)
target_include_directories(qiancao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(qiancao PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/libs/LMDB)




if(WIN32)
    target_sources(qiancao PRIVATE app.rc)
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
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
