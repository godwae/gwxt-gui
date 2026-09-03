#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>

#include "ssologin.h"

// 应用内账号密码登录对话框 — 替代"浏览器登录 + 抓 Cookie"流程
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

signals:
    void loginSuccess(const QString &cookieHeader);

private slots:
    void onLogin();
    void onSucceeded(const QString &cookieHeader);
    void onFailed(const QString &error);
    void onLog(const QString &msg);

private:
    void setBusy(bool busy);

    QComboBox  *m_unitCombo;
    QLineEdit  *m_userEdit;
    QLineEdit  *m_passEdit;
    QPushButton *m_loginBtn;
    QLabel     *m_status;
    QTextEdit  *m_log;
    SsoLogin   *m_sso;
};

#endif // LOGINDIALOG_H
