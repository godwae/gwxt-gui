cmake_minimum_required(VERSION 3.14)
project(gwxt-gui VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt5 (compatible with Windows 7) ──────────────────────────
# 需要 Qt5::Sql 用于读取浏览器 Cookie 数据库
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/downloadmanager.cpp
    src/parser.cpp
    src/csvwriter.cpp
    src/ssologin.cpp
    src/logindialog.cpp
    src/theme.cpp
)

set(HEADERS
    src/mainwindow.h
    src/downloadmanager.h
    src/parser.h
    src/csvwriter.h
    src/ssologin.h
    src/logindialog.h
    src/theme.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Network
)

# 源码统一为 UTF-8：让编译器按 UTF-8 读取源文件。
# 否则 MinGW/MSVC 在中文 Windows 下会用 GBK 代码页误读中文，
# 导致 QStringLiteral 被截断而报 expected ')' 之类的语法错误。
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# ── Windows: 链接 DPAPI / BCrypt ────────────────────────────
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# ── Install ──────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
