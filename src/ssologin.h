#ifndef SSOLOGIN_H
#define SSOLOGIN_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QPair>

class QNetworkAccessManager;
class QNetworkCookieJar;

// ISC SSO 统一认证登录（账号密码 → Cookie）
//
// 流程（依据登录页 / 前端脚本逆向）:
//   1. GET 登录页，解析隐藏域 lt / execution 与页面下发的 RSA 公钥 encryptionKey
//   2. 密码加密: md5(pwd) + 8位随机串 + pwd，按 RsaUtils.js 的小端 16-bit
//      分块 RSA 加密（每块 254 字节，逐块 hex，块间空格分隔）
//   3. POST 表单（wangsheng=单位代码, username, password, lt, execution,
//      _eventId=submit）到 /isc_sso/login?service=<gwxt入口>
//   4. 302 回跳 service?ticket=ST-xxx → gwxt 校验票据下发 JSESSIONID2，
//      iscsso 域下发 CASTGC —— 全程用独立 CookieJar 收集
//   5. 汇总为 Cookie 头字符串，交给 DownloadManager::setCookie()
class SsoLogin : public QObject
{
    Q_OBJECT

public:
    explicit SsoLogin(QObject *parent = nullptr);

    void login(const QString &unitCode, const QString &username,
               const QString &password);

    // 单位代码表（代码, 名称）— 取自登录页单位选择列表
    static const QVector<QPair<QString, QString>> &unitList();

    // RSA 加密密码（严格复刻 RsaUtils.js 的 encryptedString）
    static QString encryptPassword(const QString &password,
                                   const QString &modulusHex,
                                   const QString &exponentHex);

signals:
    void logLine(const QString &msg);
    void succeeded(const QString &cookieHeader);
    void failed(const QString &error);

private:
    enum Phase { FetchPage, PostForm, Follow };

    void fetchLoginPage();
    void submitForm();
    void onFinished(class QNetworkReply *reply);
    QString collectCookies();
    void fail(const QString &msg);

    QNetworkAccessManager *m_nam;
    QNetworkCookieJar     *m_jar;

    QString m_serviceUrl;   // gwxt 入口（service 参数，也是最终落点）
    QString m_loginUrl;     // 登录页 = /isc_sso/login?service=<编码后的 service>
    QString m_unitCode, m_username, m_password;
    QString m_lt, m_execution, m_encryptionKey;
    bool    m_rsaPass = true;
    bool    m_smPass  = false;
    Phase   m_phase   = FetchPage;
    int     m_hops    = 0;
};

#endif // SSOLOGIN_H
