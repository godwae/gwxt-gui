#include "logindialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QSettings>
#include <QScrollBar>

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , m_sso(new SsoLogin(this))
{
    setWindowTitle(QStringLiteral("应用内登录 — 统一权限认证中心"));
    setModal(true);
    resize(440, 480);

    auto *lay = new QVBoxLayout(this);
    lay->setSpacing(10);

    // ── 单位 ──
    auto *unitLabel = new QLabel(QStringLiteral("所在单位:"));
    m_unitCombo = new QComboBox;
    for (const auto &u : SsoLogin::unitList())
        m_unitCombo->addItem(QStringLiteral("%1 (%2)").arg(u.second, u.first), u.first);
    lay->addWidget(unitLabel);
    lay->addWidget(m_unitCombo);

    // ── 账号 / 密码 ──
    auto *userLabel = new QLabel(QStringLiteral("用户名:"));
    m_userEdit = new QLineEdit;
    m_userEdit->setPlaceholderText(QStringLiteral("请输入用户名"));
    lay->addWidget(userLabel);
    lay->addWidget(m_userEdit);

    auto *passLabel = new QLabel(QStringLiteral("密码:"));
    m_passEdit = new QLineEdit;
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    lay->addWidget(passLabel);
    lay->addWidget(m_passEdit);

    // ── 登录按钮 ──
    m_loginBtn = new QPushButton(QStringLiteral("登 录"));
    m_loginBtn->setObjectName("primaryBtn");
    m_loginBtn->setCursor(Qt::PointingHandCursor);
    lay->addWidget(m_loginBtn);

    m_status = new QLabel(QStringLiteral("无需浏览器，应用内直接完成统一身份认证"));
    m_status->setObjectName("hint");
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    // ── 日志 ──
    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(120);
    lay->addWidget(m_log, 1);

    // 上次登录的单位/账号
    QSettings s(QStringLiteral("gwxt-gui"), QStringLiteral("gwxt-gui"));
    const QString lastUnit = s.value(QStringLiteral("login/unit"), QStringLiteral("fj")).toString();
    const int idx = m_unitCombo->findData(lastUnit);
    if (idx >= 0)
        m_unitCombo->setCurrentIndex(idx);
    m_userEdit->setText(s.value(QStringLiteral("login/username")).toString());

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLogin);
    connect(m_sso, &SsoLogin::logLine,  this, &LoginDialog::onLog);
    connect(m_sso, &SsoLogin::succeeded, this, &LoginDialog::onSucceeded);
    connect(m_sso, &SsoLogin::failed,   this, &LoginDialog::onFailed);
}

void LoginDialog::onLogin()
{
    const QString user = m_userEdit->text().trimmed();
    const QString pass = m_passEdit->text();
    if (user.isEmpty() || pass.isEmpty()) {
        m_status->setText(QStringLiteral("请输入用户名和密码"));
        return;
    }

    setBusy(true);
    m_log->clear();
    m_status->setText(QStringLiteral("登录中…（SSO 校验可能需要十几秒，请勿关闭）"));

    const QString code = m_unitCombo->currentData().toString();
    QSettings s(QStringLiteral("gwxt-gui"), QStringLiteral("gwxt-gui"));
    s.setValue(QStringLiteral("login/unit"), code);
    s.setValue(QStringLiteral("login/username"), user);

    m_sso->login(code, user, pass);
}

void LoginDialog::onSucceeded(const QString &cookieHeader)
{
    m_status->setText(QStringLiteral("登录成功 ✓"));
    emit loginSuccess(cookieHeader);
    accept();
}

void LoginDialog::onFailed(const QString &error)
{
    setBusy(false);
    m_status->setText(error);
}

void LoginDialog::onLog(const QString &msg)
{
    m_log->append(msg);
    auto *sb = m_log->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void LoginDialog::setBusy(bool busy)
{
    m_loginBtn->setEnabled(!busy);
    m_unitCombo->setEnabled(!busy);
    m_userEdit->setEnabled(!busy);
    m_passEdit->setEnabled(!busy);
}
