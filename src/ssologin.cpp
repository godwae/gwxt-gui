#include "ssologin.h"

#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QSet>
#include <QtGlobal>

// ─────────────────────────────────────────────────────────
//  常量
// ─────────────────────────────────────────────────────────
static const QString USER_AGENT = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36");

static const char *kIscssoBase = "http://iscsso.sgcc.com.cn";
static const char *kGwxtHost   = "gwxt.sgcc.com.cn";
static const char *kServicePath =
    "http://gwxt.sgcc.com.cn/www/command/WebSiteControl"
    "?flag=goToLogin&flag=goToLogin";

// ─────────────────────────────────────────────────────────
//  大整数（32-bit 肢，小端）— 仅为 RSA 加密服务
// ─────────────────────────────────────────────────────────
namespace {

using Limbs = QVector<quint32>;

void trim(Limbs &a)
{
    while (a.size() > 1 && a.last() == 0)
        a.removeLast();
}

int cmp(const Limbs &a, const Limbs &b)
{
    const int n = qMax(a.size(), b.size());
    for (int i = n - 1; i >= 0; --i) {
        const quint32 x = i < a.size() ? a.at(i) : 0;
        const quint32 y = i < b.size() ? b.at(i) : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

// 要求 a >= b
Limbs sub(Limbs a, const Limbs &b)
{
    quint64 borrow = 0;
    for (int i = 0; i < a.size(); ++i) {
        const quint64 y = (i < b.size() ? b.at(i) : 0) + borrow;
        const quint64 x = a.at(i);
        quint64 r;
        if (x >= y) { r = x - y; borrow = 0; }
        else        { r = x + (1ULL << 32) - y; borrow = 1; }
        a[i] = quint32(r);
    }
    trim(a);
    return a;
}

Limbs mul(const Limbs &a, const Limbs &b)
{
    if (a.isEmpty() || b.isEmpty())
        return Limbs(1, 0);
    Limbs r(a.size() + b.size(), 0);
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i) == 0)
            continue;
        quint64 carry = 0;
        for (int j = 0; j < b.size(); ++j) {
            const quint64 cur = quint64(a.at(i)) * b.at(j) + r.at(i + j) + carry;
            r[i + j] = quint32(cur & 0xFFFFFFFFULL);
            carry = cur >> 32;
        }
        int k = i + b.size();
        while (carry && k < r.size()) {
            const quint64 cur = quint64(r.at(k)) + carry;
            r[k] = quint32(cur & 0xFFFFFFFFULL);
            carry = cur >> 32;
            ++k;
        }
    }
    trim(r);
    return r;
}

// r = a mod m（逐位进减法，模数 ≤ 4096 bit，规模小、性能足够）
Limbs mod(const Limbs &a, const Limbs &m)
{
    Limbs r(1, 0);
    const int bits = a.size() * 32;
    for (int i = bits - 1; i >= 0; --i) {
        // r <<= 1
        quint32 carry = 0;
        for (int k = 0; k < r.size(); ++k) {
            const quint32 next = r.at(k) >> 31;
            r[k] = (r.at(k) << 1) | carry;
            carry = next;
        }
        if (carry)
            r.append(carry);
        // r |= bit_i(a)
        const quint32 word = a.at(i >> 5);
        if ((word >> (i & 31)) & 1u)
            r[0] |= 1;
        if (cmp(r, m) >= 0)
            r = sub(r, m);
    }
    return r;
}

Limbs modpow(const Limbs &base, quint64 e, const Limbs &m)
{
    Limbs result(1, 1);
    Limbs b = mod(base, m);
    while (e) {
        if (e & 1)
            result = mod(mul(result, b), m);
        e >>= 1;
        if (e)
            b = mod(mul(b, b), m);
    }
    return result;
}

int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// JS biFromHex: 从右往左每 4 个 hex 字符一个 16-bit 数字（小端数字序）
Limbs parseHexToLimbs(const QString &hex)
{
    const QByteArray h = hex.trimmed().toLatin1();
    const int n = h.size();
    QVector<quint16> digits((n + 3) / 4, 0);
    for (int i = 0; i < n; ++i) {
        const char c = h.at(n - 1 - i);
        digits[i / 4] |= quint16(hexVal(c) << (4 * (i % 4)));
    }
    Limbs out((digits.size() + 1) / 2, 0);
    for (int j = 0; j < digits.size(); ++j) {
        if (j & 1) out[j / 2] |= quint32(digits.at(j)) << 16;
        else       out[j / 2] |= digits.at(j);
    }
    trim(out);
    return out;
}

Limbs digitsToLimbs(const QVector<quint16> &digits)
{
    Limbs out((digits.size() + 1) / 2, 0);
    for (int j = 0; j < digits.size(); ++j) {
        if (j & 1) out[j / 2] |= quint32(digits.at(j)) << 16;
        else       out[j / 2] |= digits.at(j);
    }
    trim(out);
    return out;
}

// JS biHighIndex: 最高非零 16-bit 数字下标
int biHighIndex16(const Limbs &x)
{
    const int total = x.size() * 2;
    for (int d = total - 1; d > 0; --d) {
        const quint16 v = (d & 1) ? quint16(x.at(d / 2) >> 16)
                                  : quint16(x.at(d / 2) & 0xFFFF);
        if (v)
            return d;
    }
    return 0;
}

// JS biToHex: 从最高非零数字起，每个 16-bit 数字固定 4 个小写 hex 字符
QString limbsToHex16(const Limbs &x)
{
    QVector<quint16> digits(x.size() * 2, 0);
    for (int i = 0; i < x.size(); ++i) {
        digits[2 * i]     = quint16(x.at(i) & 0xFFFF);
        digits[2 * i + 1] = quint16(x.at(i) >> 16);
    }
    int hi = digits.size() - 1;
    while (hi > 0 && digits.at(hi) == 0)
        --hi;
    QString out;
    for (int i = hi; i >= 0; --i)
        out += QString::number(digits.at(i), 16).rightJustified(4, QLatin1Char('0'));
    return out;
}

// 解析隐藏域值（value 可能在 name 属性之前或之后，先取整段 <input> 再提 value）
QString hiddenInputValue(const QString &html, const QString &name)
{
    static const QRegularExpression vre(QStringLiteral("value=\"([^\"]*)\""));
    QRegularExpression re(QStringLiteral("<input[^>]*name=\"%1\"[^>]*>")
                              .arg(QRegularExpression::escape(name)),
                          QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(html);
    if (!m.hasMatch())
        return QString();
    const auto mv = vre.match(m.captured(0));
    return mv.hasMatch() ? mv.captured(1) : QString();
}

QString extractErrorText(const QString &html)
{
    static const QRegularExpression reErr(
        QStringLiteral("<div class=\"error_div\">\\s*(.*?)\\s*</div>"),
        QRegularExpression::DotMatchesEverythingOption |
        QRegularExpression::CaseInsensitiveOption);
    const auto m = reErr.match(html);
    if (!m.hasMatch())
        return QString();
    QString text = m.captured(1);
    text.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    return text.trimmed();
}

// 对 envelope 做分块 RSA（对应 JS: RSAUtils.encryptedString(key, envelope)）
QString encryptEnvelope(const QString &envelope, const QString &modulusHex,
                        const QString &exponentHex)
{
    // a[] = UTF-16 码元（与 JS charCodeAt 一致）
    QVector<quint32> a;
    a.reserve(envelope.size());
    for (const QChar &ch : envelope)
        a.append(ch.unicode());

    const Limbs m = parseHexToLimbs(modulusHex);
    const quint64 e = exponentHex.toULongLong(nullptr, 16);
    const int digitsPerBlock = biHighIndex16(m);   // chunkSize/2
    const int blockBytes = digitsPerBlock * 2;

    while (a.size() % blockBytes != 0)
        a.append(0);

    QString result;
    for (int i = 0; i < a.size(); i += blockBytes) {
        QVector<quint16> d(digitsPerBlock, 0);
        for (int j = 0; j < digitsPerBlock; ++j) {
            const quint32 lo = a.at(i + 2 * j);
            const quint32 hi = a.at(i + 2 * j + 1);
            d[j] = quint16((lo + (hi << 8)) & 0xFFFF);
        }
        const Limbs c = modpow(digitsToLimbs(d), e, m);
        if (!result.isEmpty())
            result += QLatin1Char(' ');
        result += limbsToHex16(c);
    }
    return result;
}

} // namespace

// ─────────────────────────────────────────────────────────
//  单位代码表（取自 ISC-SSO 登录页单位选择列表）
// ─────────────────────────────────────────────────────────
const QVector<QPair<QString, QString>> &SsoLogin::unitList()
{
    static const QVector<QPair<QString, QString>> list = {
        {"fj", "福建电力"}, {"sgcc", "公司总部"},
        {"nc", "华北分部"}, {"hd", "华东分部"}, {"cc", "华中分部"},
        {"ne", "东北分部"}, {"nw", "西北分部"}, {"sw", "西南分部"},
        {"sgid", "国网国际公司"}, {"sdln", "鲁能集团"}, {"sgepri", "南瑞集团"},
        {"cet", "中电装备公司"}, {"sgxy", "国网新源公司"}, {"sgga", "空间技术公司"},
        {"sgm", "国网物资公司"}, {"xjgc", "许继集团"}, {"pg", "平高集团"},
        {"sdee", "山东电工电气"}, {"sgoc", "国网直流中心"}, {"sgdc", "国网直流公司"},
        {"sgac", "国网交流公司"}, {"sgit", "国网信通中心(大数据中心)"},
        {"csc", "国网客服中心"}, {"sgjn", "国网综能集团"}, {"epri", "中国电科院"},
        {"chinasperi", "国网经研院"}, {"sgeri", "国网能源院"}, {"sgri", "国网智研院"},
        {"ydc", "国网英大集团"}, {"cpfc", "中国电财"}, {"ydpic", "英大财险"},
        {"ydthlife", "英大人寿"}, {"mi", "国网党校"}, {"caib", "英大长安"},
        {"yditc", "英大信托"}, {"ydzq", "英大证券"}, {"ydfut", "英大期货"},
        {"unknown", "英大汇通"}, {"indaa", "英大传媒集团"}, {"zxpower", "国网中兴公司"},
        {"atc", "高培中心"}, {"sgtc", "国网技术学院"}, {"sggjyw", "服务分公司"},
        {"sgitg", "信通产业集团"}, {"evs", "国网车网公司"}, {"geig", "全球能源集团"},
        {"sgec", "数科控股公司"}, {"gzkj", "国中康健集团"}, {"sgil", "国网融资租赁"},
        {"sght", "国网海外投资"}, {"sguhv", "国网特高压公司"}, {"sgchip", "智芯公司"},
        {"ei", "国网工研院"},
        {"bj", "北京电力"}, {"tj", "天津电力"}, {"he", "河北电力"},
        {"jb", "冀北电力"}, {"sx", "山西电力"}, {"sd", "山东电力"},
        {"sh", "上海电力"}, {"js", "江苏电力"}, {"zj", "浙江电力"},
        {"ah", "安徽电力"}, {"hb", "湖北电力"}, {"hn", "湖南电力"},
        {"ha", "河南电力"}, {"jx", "江西电力"}, {"sc", "四川电力"},
        {"cq", "重庆电力"}, {"ln", "辽宁电力"}, {"jl", "吉林电力"},
        {"hl", "黑龙江电力"}, {"md", "蒙东电力"}, {"sn", "陕西电力"},
        {"gs", "甘肃电力"}, {"qh", "青海电力"}, {"nx", "宁夏电力"},
        {"xj", "新疆电力"}, {"xzepc", "西藏电力"},
        {"wbdw", "外部单位"},
    };
    return list;
}

// ─────────────────────────────────────────────────────────
//  流程
// ─────────────────────────────────────────────────────────
SsoLogin::SsoLogin(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_jar = m_nam->cookieJar();
    connect(m_nam, &QNetworkAccessManager::finished,
            this, &SsoLogin::onFinished);
}

void SsoLogin::login(const QString &unitCode, const QString &username,
                     const QString &password)
{
    m_unitCode = unitCode;
    m_username = username;
    m_password = password;
    m_hops = 0;
    m_phase = FetchPage;

    m_serviceUrl = QString::fromLatin1(kServicePath);
    m_loginUrl = QString::fromLatin1(kIscssoBase) +
                 QStringLiteral("/isc_sso/login?service=") +
                 QString::fromLatin1(QUrl(m_serviceUrl).toEncoded());

    emit logLine(QStringLiteral("[*] 获取统一权限认证中心登录页…"));
    fetchLoginPage();
}

void SsoLogin::fetchLoginPage()
{
    QNetworkRequest req{QUrl(m_loginUrl)};
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setTransferTimeout(30000);
    m_nam->get(req);
}

void SsoLogin::onFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError && status == 0) {
        fail(QStringLiteral("网络错误: %1").arg(reply->errorString()));
        return;
    }

    // ── 重定向: 手动跟随以便记录每一跳（301/302/303 → GET）──
    const QUrl target = reply->attribute(
        QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (status >= 300 && status < 400 && target.isValid()) {
        if (m_phase == PostForm && (status == 307 || status == 308)) {
            fail(QStringLiteral("服务器返回 %1，登录流程异常终止").arg(status));
            return;
        }
        if (++m_hops > 10) {
            fail(QStringLiteral("重定向次数过多，登录流程异常"));
            return;
        }
        const QUrl next = reply->url().resolved(target);
        emit logLine(QStringLiteral("  [↓] HTTP %1 → %2").arg(status).arg(next.toString()));
        if (m_phase == PostForm)
            m_phase = Follow;
        QNetworkRequest req(next);
        req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
        req.setRawHeader("Referer", reply->url().toString().toUtf8());
        req.setTransferTimeout(60000);
        m_nam->get(req);
        return;
    }

    if (status != 200) {
        fail(QStringLiteral("服务器返回 HTTP %1").arg(status));
        return;
    }

    const QByteArray body = reply->readAll();
    const QString html = QString::fromUtf8(body);

    if (m_phase == FetchPage) {
        // ── 解析登录页 ──
        m_lt         = hiddenInputValue(html, QStringLiteral("lt"));
        m_execution  = hiddenInputValue(html, QStringLiteral("execution"));
        m_smPass     = html.contains(QStringLiteral("var smPass = true"));
        m_rsaPass    = html.contains(QStringLiteral("var rsaPass = true"));

        static const QRegularExpression reKey(
            QStringLiteral("var\\s+encryptionKey\\s*=\\s*\"([^\"]+)\""));
        const auto mk = reKey.match(html);
        m_encryptionKey = mk.hasMatch() ? mk.captured(1) : QString();

        if (m_lt.isEmpty() || m_execution.isEmpty()) {
            fail(QStringLiteral("登录页解析失败（缺少 lt/execution），页面结构可能已变更"));
            return;
        }
        if (m_smPass) {
            fail(QStringLiteral("该认证中心启用了国密(SM2/SM3)密码加密，暂不支持"));
            return;
        }
        if (!m_rsaPass || m_encryptionKey.isEmpty()) {
            fail(QStringLiteral("登录页未下发 RSA 公钥，无法加密密码"));
            return;
        }

        emit logLine(QStringLiteral("[*] 已取得登录票据 lt=%1…").arg(m_lt.left(12)));
        submitForm();
        return;
    }

    // ── 最终 200: PostForm 直接 200 = 登录失败回显；Follow 200 = 落地页 ──
    if (html.contains(QStringLiteral("name=\"lt\"")) ||
        html.contains(QStringLiteral("id=\"fm1\""))) {
        const QString err = extractErrorText(html);
        fail(err.isEmpty()
             ? QStringLiteral("登录失败（回到登录页），请检查单位/账号/密码")
             : QStringLiteral("登录失败: %1").arg(err));
        return;
    }

    const QString cookieHeader = collectCookies();
    if (!cookieHeader.contains(QLatin1String("JSESSIONID2"))) {
        fail(QStringLiteral("登录流程已完成，但未取得 %1 的会话 Cookie (JSESSIONID2)")
                 .arg(QString::fromLatin1(kGwxtHost)));
        return;
    }

    emit logLine(QStringLiteral("[OK] 应用内登录成功，已取得会话 Cookie"));
    emit succeeded(cookieHeader);
}

void SsoLogin::submitForm()
{
    const QStringList kv = m_encryptionKey.split(QLatin1Char('#'));
    const QString modulus  = kv.value(0);
    const QString exponent = kv.value(1, QStringLiteral("010001"));
    if (modulus.isEmpty()) {
        fail(QStringLiteral("RSA 公钥格式异常"));
        return;
    }

    emit logLine(QStringLiteral("[*] RSA 加密密码并提交登录表单…（SSO 校验可能需要十几秒）"));
    const QString encPwd = encryptPassword(m_password, modulus, exponent);

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("wangsheng"), m_unitCode);
    form.addQueryItem(QStringLiteral("username"), m_username);
    form.addQueryItem(QStringLiteral("password"), encPwd);
    form.addQueryItem(QStringLiteral("authModeSerial"), QString());
    form.addQueryItem(QStringLiteral("signature"), QString());
    form.addQueryItem(QStringLiteral("lt"), m_lt);
    form.addQueryItem(QStringLiteral("execution"), m_execution);
    form.addQueryItem(QStringLiteral("messageCode"), QString());
    form.addQueryItem(QStringLiteral("token"), QString());
    form.addQueryItem(QStringLiteral("_eventId"), QStringLiteral("submit"));
    form.addQueryItem(QStringLiteral("checkAcc"), QStringLiteral("on"));

    QNetworkRequest req{QUrl(m_loginUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setRawHeader("Referer", m_loginUrl.toUtf8());
    req.setRawHeader("Origin", QByteArray(kIscssoBase));
    req.setTransferTimeout(60000);

    m_phase = PostForm;
    m_hops = 0;
    m_nam->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
}

// 从 CookieJar 汇总 gwxt / iscsso / 父域三个作用域的 Cookie（按名去重）
QString SsoLogin::collectCookies()
{
    QSet<QString> seen;
    QStringList parts;
    const QList<QUrl> targets = {
        QUrl(QStringLiteral("http://") + QString::fromLatin1(kGwxtHost) + QStringLiteral("/")),
        QUrl(QString::fromLatin1(kIscssoBase) + QStringLiteral("/")),
        QUrl(QStringLiteral("http://sgcc.com.cn/")),
    };
    for (const QUrl &u : targets) {
        const auto cookies = m_jar->cookiesForUrl(u);
        for (const QNetworkCookie &c : cookies) {
            const QString key = QString::fromLatin1(c.name());
            if (seen.contains(key))
                continue;
            seen.insert(key);
            if (key == QLatin1String("CASTGC"))
                emit logLine(QStringLiteral("  [+] CASTGC 已取得（SSO 票据授权票据）"));
            else if (key == QLatin1String("JSESSIONID2"))
                emit logLine(QStringLiteral("  [+] JSESSIONID2 已取得（题库会话）"));
            parts << key + QLatin1Char('=') + QString::fromLatin1(c.value());
        }
    }
    return parts.join(QStringLiteral("; "));
}

void SsoLogin::fail(const QString &msg)
{
    emit logLine(QStringLiteral("[!] %1").arg(msg));
    emit failed(msg);
}

// ─────────────────────────────────────────────────────────
//  RSA 密码加密 — 严格复刻 ISC-SSO_files/RsaUtils.js
//
//  JS 侧: envelope = $.md5(pwd) + getRandomString(8) + pwd
//         RSAUtils.encryptedString(key, envelope)
// ─────────────────────────────────────────────────────────
QString SsoLogin::encryptPassword(const QString &password,
                                  const QString &modulusHex,
                                  const QString &exponentHex)
{
    const QString md5hex = QString::fromLatin1(
        QCryptographicHash::hash(password.toUtf8(),
                                 QCryptographicHash::Md5).toHex());

    static const QString charset =
        QStringLiteral("ABCDEFGHJKMNPQRSTWXYZabcdefhijkmnprstwxyz2345678");
    QString rnd;
    rnd.reserve(8);
    for (int i = 0; i < 8; ++i)
        rnd += charset.at(QRandomGenerator::global()->bounded(charset.size()));

    return encryptEnvelope(md5hex + rnd + password, modulusHex, exponentHex);
}
