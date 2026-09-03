# ── 技能等级评价题库下载器 — qmake 项目文件 ─────────────────────
# 适用于 Windows 7 + Qt 5.15.x (MinGW / MSVC 2017/2019)
# 在 Qt Creator 中打开此文件即可编译

QT       += core gui widgets network
TARGET    = gwxt-gui
TEMPLATE  = app

CONFIG   += c++17
CONFIG   -= app_bundle

# Windows: 不弹出控制台
win32:CONFIG += windows

INCLUDEPATH += src

# 源码统一为 UTF-8：明确告知编译器按 UTF-8 读取源文件。
# 否则 MinGW 在中文 Windows 下可能按 GBK 代码页误读中文，导致
# QStringLiteral 被截断而报 expected ')' 之类的语法错误。
win32-g++:QMAKE_CXXFLAGS   += -finput-charset=UTF-8 -fexec-charset=UTF-8
win32-msvc*:QMAKE_CXXFLAGS += /utf-8

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/downloadmanager.cpp \
    src/parser.cpp \
    src/csvwriter.cpp \
    src/ssologin.cpp \
    src/logindialog.cpp \
    src/theme.cpp

HEADERS += \
    src/mainwindow.h \
    src/downloadmanager.h \
    src/parser.h \
    src/csvwriter.h \
    src/ssologin.h \
    src/logindialog.h \
    src/theme.h

# Windows 资源文件
win32:RC_FILE = src/app.rc

# ── 静态构建支持（standalone 单文件 exe）─────────────────
# 用静态编译的 Qt 的 qmake 构建即自动生效（见 scripts/build-qt-static.bat）。
# 目录版（动态 DLL）构建时此分支不激活，行为不变。
static {
    # windowsvista 基础样式（QSS 未覆盖的控件 fallback，如 QMessageBox 细节）
    QTPLUGIN.styles     = qwindowsvistastyle
    # MinGW 运行时静态链接，产出不依赖 libgcc/libstdc++ DLL
    QMAKE_LFLAGS       += -static-libgcc -static-libstdc++
}
