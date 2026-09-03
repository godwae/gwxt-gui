#!/usr/bin/env bash
set -euo pipefail

# ================================================================
#  技能等级评价题库下载器 — Linux 便携版打包脚本
#  自动收集 Qt5 依赖库，产出可直接运行的目录
# ================================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
APPDIR="$PROJECT_DIR/gwxt-gui-portable"

echo "========================================"
echo "  技能等级评价题库下载器 — Linux 便携打包"
echo "========================================"
echo ""

# ── 1. 编译 Release ──────────────────────────────────────
echo "[1/4] 编译 Release 版本..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"

if [ ! -f "$BUILD_DIR/gwxt-gui" ]; then
    echo "[错误] 编译产物未找到: $BUILD_DIR/gwxt-gui"
    exit 1
fi
echo "[OK] 编译完成"

# ── 2. 收集依赖 ──────────────────────────────────────
echo "[2/4] 收集 Qt5 运行时依赖..."

rm -rf "$APPDIR"
mkdir -p "$APPDIR/lib/plugins"

# 主程序
cp "$BUILD_DIR/gwxt-gui" "$APPDIR/"

# 找到 Qt5 安装路径
QT5_LIB_DIR=""
for candidate in /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib; do
    if [ -f "$candidate/libQt5Core.so.5" ]; then
        QT5_LIB_DIR="$candidate"
        break
    fi
done

if [ -z "$QT5_LIB_DIR" ]; then
    echo "[WARN] 未找到系统 Qt5 库目录，尝试 pkg-config..."
    QT5_LIB_DIR=$(pkg-config --variable=libdir Qt5Core 2>/dev/null || echo "")
fi

copy_qt_lib() {
    local libname="$1"
    local found=""
    for search in "$QT5_LIB_DIR" /usr/lib64 /usr/lib/x86_64-linux-gnu /usr/lib; do
        if [ -f "$search/$libname" ]; then
            found="$search/$libname"
            break
        fi
    done
    if [ -n "$found" ]; then
        cp "$found" "$APPDIR/lib/"
        echo "       $libname"
    else
        echo "       [WARN] $libname not found"
    fi
}

echo "      收集 Qt 核心库..."
for lib in \
    libQt5Core.so.5 \
    libQt5Gui.so.5 \
    libQt5Widgets.so.5 \
    libQt5Network.so.5 \
    libQt5DBus.so.5 \
    libQt5XcbQpa.so.5 \
    ; do
    copy_qt_lib "$lib"
done

# 收集 libicu / libssl 等间接依赖（不完全静态，但覆盖最常见场景）
echo "      收集关键系统依赖..."
for lib in \
    libicui18n.so.?? \
    libicuuc.so.?? \
    libicudata.so.?? \
    libssl.so.3 \
    libcrypto.so.3 \
    ; do
    copy_qt_lib "$lib" || true
done

# 平台插件
echo "      收集 Qt 平台插件..."
QT5_PLUGIN_DIR=""
for candidate in \
    /usr/lib64/qt5/plugins \
    /usr/lib/qt5/plugins \
    /usr/lib/x86_64-linux-gnu/qt5/plugins \
    ; do
    if [ -d "$candidate/platforms" ]; then
        QT5_PLUGIN_DIR="$candidate"
        break
    fi
done

if [ -n "$QT5_PLUGIN_DIR" ]; then
    mkdir -p "$APPDIR/lib/plugins"
    if [ -d "$QT5_PLUGIN_DIR/platforms" ]; then
        cp -r "$QT5_PLUGIN_DIR/platforms" "$APPDIR/lib/plugins/"
        echo "       platforms/"
    fi
else
    echo "       [WARN] Qt5 plugins 目录未找到"
fi

# ── 3. 创建启动脚本 ───────────────────────────────────
echo "[3/4] 创建启动脚本..."
cat > "$APPDIR/start.sh" << 'RUNEOF'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$DIR/lib/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$DIR/lib/plugins/platforms"
exec "$DIR/gwxt-gui" "$@"
RUNEOF
chmod +x "$APPDIR/start.sh"

# ── 4. 验证 ────────────────────────────────────────
echo "[4/4] 验证打包结果..."
EXE_COUNT=0
LIB_COUNT=0
PLUGIN_COUNT=0
[ -f "$APPDIR/gwxt-gui" ] && EXE_COUNT=1
LIB_COUNT=$(find "$APPDIR/lib" -name "*.so*" -type f 2>/dev/null | wc -l)
PLUGIN_COUNT=$(find "$APPDIR/lib/plugins" -type f 2>/dev/null | wc -l)

TOTAL_SIZE=$(du -sh "$APPDIR" 2>/dev/null | cut -f1)

echo ""
echo "========================================"
echo "  打包完成！"
echo "  目录: $APPDIR"
echo "  文件: 程序 $EXE_COUNT + 库 $LIB_COUNT + 插件 $PLUGIN_COUNT"
echo "  大小: ${TOTAL_SIZE:-未知}"
echo "  运行: $APPDIR/start.sh"
echo "========================================"
echo ""
echo "  提示: 如需跨 Linux 发行版使用，建议用 linuxdeployqt 或 AppImage。"
echo "  https://github.com/probonopd/linuxdeployqt"
