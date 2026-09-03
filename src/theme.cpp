#include "theme.h"

#include <QApplication>
#include <QWidget>
#include <QSettings>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPair>
#include <QVector>
#include <algorithm>
#include <functional>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <QLibrary>
#endif

// ═══════════════════════════════════════════════════════════════
//  单例
// ═══════════════════════════════════════════════════════════════
ThemeManager *ThemeManager::instance()
{
    static ThemeManager inst;
    return &inst;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_sysTimer(new QTimer(this))
{
    // 读取上次保存的模式（QSettings 依赖 main() 中已设置的 org/app 名）
    QSettings s;
    const int saved = s.value(QStringLiteral("theme/mode"),
                              static_cast<int>(System)).toInt();
    m_mode = (saved >= Light && saved <= System) ? static_cast<Mode>(saved) : System;

    m_sysTimer->setInterval(4000);
    connect(m_sysTimer, &QTimer::timeout, this, [this]() {
        if (m_mode == System && systemPrefersDark() != m_dark)
            applyTheme();
    });

    applyTheme();
}

void ThemeManager::setMode(Mode mode)
{
    m_mode = mode;
    QSettings s;
    s.setValue(QStringLiteral("theme/mode"), static_cast<int>(mode));
    applyTheme();
}

// ═══════════════════════════════════════════════════════════════
//  系统深色偏好检测 — Win7 无深色概念，安全落到浅色
// ═══════════════════════════════════════════════════════════════
bool ThemeManager::systemPrefersDark()
{
#ifdef Q_OS_WIN
    QSettings reg(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const QVariant v = reg.value(QStringLiteral("AppsUseLightTheme"));
    if (!v.isValid())
        return false;   // Win7/8/8.1 与早期 Win10 无此键 → 浅色
    return !v.toBool();
#else
    // freedesktop color-scheme (GNOME 42+ / KDE)
    {
        QProcess p;
        p.start(QStringLiteral("gsettings"),
                {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                 QStringLiteral("color-scheme")});
        if (p.waitForFinished(800) && p.exitCode() == 0
            && QString::fromUtf8(p.readAllStandardOutput())
                   .contains(QStringLiteral("dark"), Qt::CaseInsensitive))
            return true;
    }
    // GTK 主题名包含 dark（如 Adwaita-dark）
    {
        QProcess p;
        p.start(QStringLiteral("gsettings"),
                {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                 QStringLiteral("gtk-theme")});
        if (p.waitForFinished(800) && p.exitCode() == 0
            && QString::fromUtf8(p.readAllStandardOutput())
                   .contains(QStringLiteral("dark"), Qt::CaseInsensitive))
            return true;
    }
    if (qEnvironmentVariable("GTK_THEME")
            .contains(QStringLiteral("dark"), Qt::CaseInsensitive))
        return true;
    return false;
#endif
}

// ═══════════════════════════════════════════════════════════════
//  Windows 深色标题栏 — 动态解析 dwmapi，属性不支持时静默忽略
//  （Win7 的 dwmapi 存在但不认识该属性，返回错误码 → 无副作用）
// ═══════════════════════════════════════════════════════════════
void ThemeManager::applyDarkTitleBar(QWidget *widget, bool dark)
{
#ifdef Q_OS_WIN
    if (!widget || !widget->testAttribute(Qt::WA_WState_Created))
        return;

    QLibrary lib(QStringLiteral("dwmapi.dll"));
    if (!lib.load())
        return;
    using FnT = LONG (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    auto fn = reinterpret_cast<FnT>(lib.resolve("DwmSetWindowAttribute"));
    if (!fn)
        return;

    const BOOL value = dark ? TRUE : FALSE;
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 20H1+/1809 部分 build)
    if (fn(hwnd, 20, &value, sizeof(value)) != 0)
        fn(hwnd, 19, &value, sizeof(value));   // 早期 build 用 19
#else
    Q_UNUSED(widget);
    Q_UNUSED(dark);
#endif
}

// ═══════════════════════════════════════════════════════════════
//  应用主题
// ═══════════════════════════════════════════════════════════════
void ThemeManager::applyTheme()
{
    m_dark = (m_mode == Dark) || (m_mode == System && systemPrefersDark());

    generateIcons();
    qApp->setStyleSheet(buildQss());

    if (m_mode == System)
        m_sysTimer->start();
    else
        m_sysTimer->stop();

    emit themeChanged(m_dark);
}

// ═══════════════════════════════════════════════════════════════
//  运行时绘制指示器图标（PNG 写入临时目录）
//  不依赖 qrc / qsvg 插件，任何部署形态下都不缺图
// ═══════════════════════════════════════════════════════════════
void ThemeManager::generateIcons()
{
    const QString dir = QDir::tempPath() + QStringLiteral("/gwxt-theme");
    QDir().mkpath(dir);

    auto save = [&dir](const QString &file,
                       const std::function<void(QPainter &)> &draw) -> QString {
        const QString path = dir + QLatin1Char('/') + file;
        QPixmap pm(24, 24);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        draw(p);
        p.end();
        pm.save(path, "PNG");
        return path;
    };

    // 对勾 / 横线颜色 = 主按钮上的文字色（保证对比度）
    const QColor onAccent = m_dark ? QColor(0x0D, 0x10, 0x17)
                                   : QColor(0xFF, 0xFF, 0xFF);
    const QColor glyph = m_dark ? QColor(0x9A, 0xA3, 0xB6)
                                : QColor(0x8E, 0x99, 0xAB);

    m_checkPath = save(m_dark ? QStringLiteral("check-dark.png")
                              : QStringLiteral("check-light.png"),
                       [onAccent](QPainter &p) {
        QPen pen(onAccent, 2.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawPolyline(QPolygonF() << QPointF(6.0, 12.6) << QPointF(10.2, 16.6)
                                   << QPointF(18.0, 7.6));
    });

    m_dashPath = save(m_dark ? QStringLiteral("dash-dark.png")
                             : QStringLiteral("dash-light.png"),
                      [onAccent](QPainter &p) {
        p.setPen(Qt::NoPen);
        p.setBrush(onAccent);
        p.drawRoundedRect(QRectF(6.0, 10.6, 12.0, 2.9), 1.4, 1.4);
    });

    m_chevronPath = save(QStringLiteral("chevron.png"),
                         [glyph](QPainter &p) {
        QPen pen(glyph, 2.2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.drawPolyline(QPolygonF() << QPointF(7.5, 9.8) << QPointF(12.0, 14.2)
                                   << QPointF(16.5, 9.8));
    });
}

// ═══════════════════════════════════════════════════════════════
//  QSS — token 模板 + 双主题色板
//  设计语言: 中性冷灰表面 + 靛蓝强调色, 12px 卡片圆角, 细描边分层,
//  8px 间距栅格。深色模式用高亮度强调色 + 深色文字保证对比度。
// ═══════════════════════════════════════════════════════════════
QString ThemeManager::buildQss() const
{
    QVector<QPair<QString, QString>> t;
    if (m_dark) {
        t = {
            {QStringLiteral("bg"),               QStringLiteral("#0C0F15")},
            {QStringLiteral("bgSoft"),           QStringLiteral("#181D28")},
            {QStringLiteral("card"),             QStringLiteral("#12161F")},
            {QStringLiteral("cardBorder"),       QStringLiteral("#232A38")},
            {QStringLiteral("borderStrong"),     QStringLiteral("#313A4C")},
            {QStringLiteral("inputBg"),          QStringLiteral("#161B26")},
            {QStringLiteral("titleText"),        QStringLiteral("#F2F4F9")},
            {QStringLiteral("text"),             QStringLiteral("#E4E8F0")},
            {QStringLiteral("textSec"),          QStringLiteral("#9AA3B6")},
            {QStringLiteral("textMut"),          QStringLiteral("#6A7385")},
            {QStringLiteral("accent"),           QStringLiteral("#818CF8")},
            {QStringLiteral("accentHover"),      QStringLiteral("#97A1FB")},
            {QStringLiteral("accentPressed"),    QStringLiteral("#6E79F2")},
            {QStringLiteral("onAccent"),         QStringLiteral("#0D1017")},
            {QStringLiteral("accentText"),       QStringLiteral("#A5AFFF")},
            {QStringLiteral("accentSoft"),       QStringLiteral("rgba(129,140,248,0.13)")},
            {QStringLiteral("accentSoftHover"),  QStringLiteral("rgba(129,140,248,0.20)")},
            {QStringLiteral("accentSoftBorder"), QStringLiteral("rgba(129,140,248,0.38)")},
            {QStringLiteral("violet"),           QStringLiteral("#9F7CFA")},
            {QStringLiteral("danger"),           QStringLiteral("#F2777D")},
            {QStringLiteral("dangerSoft"),       QStringLiteral("rgba(242,119,125,0.12)")},
            {QStringLiteral("dangerBorder"),     QStringLiteral("rgba(242,119,125,0.38)")},
            {QStringLiteral("logBg"),            QStringLiteral("#0A0D13")},
            {QStringLiteral("logBorder"),        QStringLiteral("#1E2534")},
            {QStringLiteral("logText"),          QStringLiteral("#C6CEDD")},
            {QStringLiteral("tipBg"),            QStringLiteral("#1B2230")},
            {QStringLiteral("tipText"),          QStringLiteral("#E4E8F0")},
            {QStringLiteral("tipBorder"),        QStringLiteral("#2C3547")},
            {QStringLiteral("scrollThumb"),      QStringLiteral("#2B3346")},
            {QStringLiteral("scrollThumbHover"), QStringLiteral("#3B4660")},
        };
    } else {
        t = {
            {QStringLiteral("bg"),               QStringLiteral("#F3F5F8")},
            {QStringLiteral("bgSoft"),           QStringLiteral("#E9EDF2")},
            {QStringLiteral("card"),             QStringLiteral("#FFFFFF")},
            {QStringLiteral("cardBorder"),       QStringLiteral("#E2E7EE")},
            {QStringLiteral("borderStrong"),     QStringLiteral("#CBD3DE")},
            {QStringLiteral("inputBg"),          QStringLiteral("#FFFFFF")},
            {QStringLiteral("titleText"),        QStringLiteral("#0D1420")},
            {QStringLiteral("text"),             QStringLiteral("#171E2C")},
            {QStringLiteral("textSec"),          QStringLiteral("#4A5568")},
            {QStringLiteral("textMut"),          QStringLiteral("#8E99AB")},
            {QStringLiteral("accent"),           QStringLiteral("#4F46E5")},
            {QStringLiteral("accentHover"),      QStringLiteral("#4338CA")},
            {QStringLiteral("accentPressed"),    QStringLiteral("#3730A3")},
            {QStringLiteral("onAccent"),         QStringLiteral("#FFFFFF")},
            {QStringLiteral("accentText"),       QStringLiteral("#4338CA")},
            {QStringLiteral("accentSoft"),       QStringLiteral("#EEF1FE")},
            {QStringLiteral("accentSoftHover"),  QStringLiteral("#E2E7FD")},
            {QStringLiteral("accentSoftBorder"), QStringLiteral("#C9CFFA")},
            {QStringLiteral("violet"),           QStringLiteral("#8B5CF6")},
            {QStringLiteral("danger"),           QStringLiteral("#D92D20")},
            {QStringLiteral("dangerSoft"),       QStringLiteral("#FEF0EF")},
            {QStringLiteral("dangerBorder"),     QStringLiteral("#F5C6C2")},
            {QStringLiteral("logBg"),            QStringLiteral("#FBFCFD")},
            {QStringLiteral("logBorder"),        QStringLiteral("#E2E7EE")},
            {QStringLiteral("logText"),          QStringLiteral("#3A4657")},
            {QStringLiteral("tipBg"),            QStringLiteral("#151B26")},
            {QStringLiteral("tipText"),          QStringLiteral("#EEF1F6")},
            {QStringLiteral("tipBorder"),        QStringLiteral("#2A3242")},
            {QStringLiteral("scrollThumb"),      QStringLiteral("#C9D1DC")},
            {QStringLiteral("scrollThumbHover"), QStringLiteral("#AAB4C2")},
        };
    }
    // 路径加引号: Windows 用户目录可能含空格（C:/Users/John Doe/...），
    // 不加引号 QSS url() 会在空格处截断
    t.append({QStringLiteral("check"),   QLatin1Char('"') + m_checkPath + QLatin1Char('"')});
    t.append({QStringLiteral("dash"),    QLatin1Char('"') + m_dashPath + QLatin1Char('"')});
    t.append({QStringLiteral("chevron"), QLatin1Char('"') + m_chevronPath + QLatin1Char('"')});

    // 按 token 名长度降序替换，避免 @accent 误吃 @accentHover 的前缀
    std::sort(t.begin(), t.end(), [](const auto &a, const auto &b) {
        return a.first.size() > b.first.size();
    });

    QString css = QStringLiteral(
        /* ── 全局 ─────────────────────────────────────────── */
        "* { font-family: \"Segoe UI\", \"Microsoft YaHei\", \"Noto Sans CJK SC\", sans-serif;"
        "     font-size: 13px; }"
        "QWidget { color: @text; }"
        "QMainWindow, QDialog { background: @bg; }"

        /* ── 卡片与文字 ───────────────────────────────────── */
        "QFrame#card { background: @card; border: 1px solid @cardBorder; border-radius: 12px; }"
        "QLabel#cardTitle { font-size: 13px; font-weight: 600; color: @titleText; }"
        "QLabel#hint { color: @textMut; font-size: 12px; }"
        "QLabel#pill { background: @bgSoft; color: @textSec; border-radius: 10px;"
        "              padding: 3px 10px; font-size: 12px; }"
        "QLabel#appTitle { font-size: 17px; font-weight: 700; color: @titleText; }"
        "QLabel#appSub { color: @textMut; font-size: 12px; }"

        /* ── 输入框 ───────────────────────────────────────── */
        "QLineEdit { background: @inputBg; border: 1px solid @cardBorder; border-radius: 8px;"
        "            padding: 8px 12px; color: @text;"
        "            selection-background-color: @accent; selection-color: @onAccent; }"
        "QLineEdit:hover { border-color: @borderStrong; }"
        "QLineEdit:focus { border-color: @accent; }"
        "QLineEdit:disabled { background: @bgSoft; color: @textMut; }"

        /* ── 下拉框 ───────────────────────────────────────── */
        "QComboBox { background: @inputBg; border: 1px solid @cardBorder; border-radius: 8px;"
        "            padding: 8px 12px; min-width: 110px; color: @text; }"
        "QComboBox:hover { border-color: @borderStrong; }"
        "QComboBox:focus { border-color: @accent; }"
        "QComboBox:disabled { background: @bgSoft; color: @textMut; }"
        "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right;"
        "                       width: 26px; border-left: 1px solid @cardBorder; }"
        "QComboBox::down-arrow { image: url(@chevron); width: 12px; height: 12px; }"
        "QComboBox QAbstractItemView { background: @card; color: @text;"
        "    border: 1px solid @cardBorder; border-radius: 8px; padding: 4px; outline: none;"
        "    selection-background-color: @accentSoft; selection-color: @text; }"

        /* ── 按钮（通用基线 → QMessageBox 等无名按钮也现代） ── */
        "QPushButton { background: @card; color: @text; border: 1px solid @cardBorder;"
        "              border-radius: 8px; padding: 7px 16px; font-weight: 500; }"
        "QPushButton:hover { background: @bgSoft; border-color: @borderStrong; }"
        "QPushButton:pressed { background: @bgSoft; }"
        "QPushButton:disabled { background: @bgSoft; color: @textMut; border-color: @cardBorder; }"

        "QPushButton#primaryBtn { background: @accent; color: @onAccent; border: none;"
        "                         padding: 9px 20px; font-weight: 600; }"
        "QPushButton#primaryBtn:hover { background: @accentHover; }"
        "QPushButton#primaryBtn:pressed { background: @accentPressed; }"
        "QPushButton#primaryBtn:disabled { background: @bgSoft; color: @textMut; }"

        "QPushButton#secondaryBtn { background: @accentSoft; color: @accentText;"
        "                           border: 1px solid @accentSoftBorder; }"
        "QPushButton#secondaryBtn:hover { background: @accentSoftHover; border-color: @accent; }"
        "QPushButton#secondaryBtn:pressed { background: @accentSoftHover; }"
        "QPushButton#secondaryBtn:disabled { background: @bgSoft; color: @textMut;"
        "                                    border-color: @cardBorder; }"

        "QPushButton#toolBtn { background: transparent; color: @textSec;"
        "                      border: 1px solid transparent; padding: 5px 12px; font-size: 12px; }"
        "QPushButton#toolBtn:hover { background: @bgSoft; color: @text; border-color: @cardBorder; }"
        "QPushButton#toolBtn:pressed { background: @bgSoft; }"
        "QPushButton#toolBtn:disabled { background: transparent; }"

        "QPushButton#dangerBtn { background: transparent; color: @danger;"
        "                        border: 1px solid @dangerBorder; }"
        "QPushButton#dangerBtn:hover { background: @dangerSoft; border-color: @danger; }"
        "QPushButton#dangerBtn:pressed { background: @dangerSoft; }"
        "QPushButton#dangerBtn:disabled { background: transparent; color: @textMut;"
        "                                 border-color: @cardBorder; }"

        /* ── 分段切换（主题选择器） ────────────────────────── */
        "QFrame#seg { background: @bgSoft; border: 1px solid @cardBorder; border-radius: 10px; }"
        "QPushButton[seg=\"true\"] { background: transparent; color: @textSec;"
        "    border: 1px solid transparent; border-radius: 7px; padding: 4px 14px;"
        "    font-size: 12px; font-weight: 500; }"
        "QPushButton[seg=\"true\"]:hover { color: @text; }"
        "QPushButton[seg=\"true\"]:checked { background: @card; color: @text;"
        "    border-color: @cardBorder; font-weight: 600; }"

        /* ── 进度条 ───────────────────────────────────────── */
        "QProgressBar { background: @bgSoft; border: 1px solid @cardBorder;"
        "               border-radius: 5px; min-height: 8px; max-height: 8px; }"
        "QProgressBar::chunk { border-radius: 5px;"
        "    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "        stop:0 @accent, stop:1 @violet); }"

        /* ── 复选框 ───────────────────────────────────────── */
        "QCheckBox { color: @text; spacing: 8px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border: 1px solid @borderStrong;"
        "                       border-radius: 5px; background: @inputBg; }"
        "QCheckBox::indicator:hover { border-color: @accent; }"
        "QCheckBox::indicator:checked { background: @accent; border-color: @accent;"
        "                               image: url(@check); }"
        "QCheckBox::indicator:indeterminate { background: @accent; border-color: @accent;"
        "                                     image: url(@dash); }"
        "QCheckBox::indicator:disabled { background: @bgSoft; border-color: @cardBorder; }"
        "QCheckBox:disabled { color: @textMut; }"

        /* ── 列表 ─────────────────────────────────────────── */
        "QListWidget { background: @card; border: 1px solid @cardBorder; border-radius: 10px;"
        "              padding: 6px; outline: none; color: @text; }"
        "QListWidget::item { padding: 7px 10px; border-radius: 7px; margin: 1px 0; color: @text; }"
        "QListWidget::item:hover { background: @bgSoft; }"
        "QListWidget::item:selected { background: @accentSoft; color: @text; }"

        /* ── 滚动条 ───────────────────────────────────────── */
        "QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: @scrollThumb; border-radius: 4px; min-height: 32px; }"
        "QScrollBar::handle:vertical:hover { background: @scrollThumbHover; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
        "QScrollBar:horizontal { background: transparent; height: 9px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: @scrollThumb; border-radius: 4px; min-width: 32px; }"
        "QScrollBar::handle:horizontal:hover { background: @scrollThumbHover; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; border: none; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }"

        /* ── 日志区（终端风格，随主题换肤） ────────────────── */
        "QTextEdit#logView { background: @logBg; color: @logText; border: 1px solid @logBorder;"
        "    border-radius: 10px; padding: 10px;"
        "    selection-background-color: @accent; selection-color: @onAccent;"
        "    font-family: \"Cascadia Code\", \"Consolas\", \"JetBrains Mono\","
        "                 \"Noto Sans Mono CJK SC\", monospace; font-size: 12px; }"

        /* ── 菜单 / 提示 ──────────────────────────────────── */
        "QMenu { background: @card; color: @text; border: 1px solid @cardBorder;"
        "        border-radius: 8px; padding: 6px; }"
        "QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }"
        "QMenu::item:selected { background: @accentSoft; }"
        "QMenu::separator { height: 1px; background: @cardBorder; margin: 4px 8px; }"
        "QToolTip { background: @tipBg; color: @tipText; border: 1px solid @tipBorder;"
        "           border-radius: 6px; padding: 6px 10px; font-size: 12px; }"
    );

    for (const auto &kv : t)
        css.replace(QLatin1Char('@') + kv.first, kv.second);

    return css;
}
