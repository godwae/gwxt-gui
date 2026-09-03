#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QTextEdit>
#include <QProgressBar>
#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QPair>
#include "downloadmanager.h"
#include "parser.h"
#include "logindialog.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void showEvent(QShowEvent *event) override;

private slots:
    void onFetchSubjects();
    void onAccountLogin();
    void onSubjectsReady(const QList<SubjectEntry> &subjects);
    void onFilterChanged(const QString &text);
    void onSelectAll();
    void onDeselectAll();
    void onInvertSelection();
    void onStartDownload();
    void onAbort();
    void onProgressUpdate(int current, int total);
    void onLog(const QString &msg);
    void onError(const QString &err);
    void onAllFinished();
    void onThemeChanged(bool dark);

private:
    void setupUi();
    void setUiEnabled(bool enabled);
    QList<SubjectEntry> getCheckedSubjects() const;
    void renderAllLogs();

    // ── 网络 ──
    DownloadManager *m_downloader;

    // ── 控件 ──
    QPushButton *m_accountLoginBtn; // 应用内账号密码登录（ISC SSO）
    QLineEdit   *m_filterEdit;
    QListWidget *m_subjectList;
    QCheckBox   *m_selectAllCb;

    QPushButton *m_fetchBtn;
    QPushButton *m_invertBtn;
    QPushButton *m_downloadBtn;
    QPushButton *m_abortBtn;

    // 主题分段切换: 浅色 / 深色 / 系统
    QPushButton *m_segLight = nullptr;
    QPushButton *m_segDark = nullptr;
    QPushButton *m_segSystem = nullptr;

    QComboBox   *m_formatCombo;
    QLineEdit   *m_saveDirEdit;
    QPushButton *m_browseDirBtn;

    QProgressBar *m_progressBar;
    QLabel       *m_progressLabel;
    QTextEdit    *m_logView;

    // ── 数据 ──
    QList<SubjectEntry> m_allSubjects;

    // 应用内登录取得的会话 Cookie（重启后需重新登录）
    QString m_sessionCookie;

    // 日志缓存（时间戳, 消息）— 切主题时整体重着色
    QList<QPair<QString, QString>> m_logLines;
};

#endif // MAINWINDOW_H
