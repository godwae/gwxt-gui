#ifndef THEME_H
#define THEME_H

#include <QObject>
#include <QString>
#include <QTimer>

class QWidget;

/// 主题管理器 — 浅色 / 深色 / 跟随系统
///
/// - 以 token 化 QSS 统一渲染全部控件（见 buildQss）
/// - 复选框对勾 / 不确定态 / 下拉箭头图标在运行时绘制为 PNG（无资源文件依赖，
///   便携版部署不缺插件）
/// - 跟随系统: Windows 读注册表 AppsUseLightTheme（Win7 无此键 → 浅色），
///   Linux 读 gsettings color-scheme / gtk-theme；System 模式下轮询变化
/// - 深色标题栏: 动态加载 dwmapi.dll，仅 Win10 1809+ 生效，Win7 上调用失败即忽略
/// - 用户选择通过 QSettings 持久化
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum Mode {
        Light = 0,
        Dark = 1,
        System = 2
    };
    Q_ENUM(Mode)

    static ThemeManager *instance();

    /// 切换主题，立即应用并持久化
    void setMode(Mode mode);

    Mode mode() const { return m_mode; }

    /// 当前实际生效是否深色（System 已解析为具体值）
    bool isDark() const { return m_dark; }

    /// 系统是否偏好深色
    /// Windows: HKCU\...\Themes\Personalize\AppsUseLightTheme（缺失 → false）
    /// Linux:   gsettings color-scheme → gtk-theme → GTK_THEME 环境变量
    static bool systemPrefersDark();

    /// Windows 10 1809+ 深色标题栏；其他系统（含 Win7）安全无操作
    static void applyDarkTitleBar(QWidget *widget, bool dark);

signals:
    /// QSS 已应用后发出，UI 可做配套调整（日志重着色、标题栏等）
    void themeChanged(bool dark);

private:
    explicit ThemeManager(QObject *parent = nullptr);

    void applyTheme();
    QString buildQss() const;
    void generateIcons();

    Mode m_mode = System;
    bool m_dark = false;

    // 运行时生成的指示器图标路径（每次切主题重绘，文件名含主题避免缓存串色）
    QString m_checkPath;
    QString m_dashPath;
    QString m_chevronPath;

    // System 模式下轮询系统主题变化
    QTimer *m_sysTimer = nullptr;
};

#endif // THEME_H
