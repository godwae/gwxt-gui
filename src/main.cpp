#include <QApplication>
#include "mainwindow.h"
#include "theme.h"

int main(int argc, char *argv[])
{
    // 禁用 Qt5 Bearer Management 网络探测，避免首次 QNetworkRequest 卡顿 3-5 秒
    qputenv("QT_BEARER_POLL_TIMEOUT", "0");

    // 高DPI适配（Qt 5.6 ~ Qt 5.x；Qt 6 默认启用无需设置）
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    QApplication app(argc, argv);
    // org/app 名先设置，ThemeManager 的 QSettings 持久化依赖它
    app.setOrganizationName("gwxt");
    app.setApplicationName("题库下载器");
    app.setApplicationVersion("1.1");

    // 加载已保存的主题模式并应用全局 QSS（须在 MainWindow 构造前）
    ThemeManager::instance();

    MainWindow w;
    w.show();

    return app.exec();
}
