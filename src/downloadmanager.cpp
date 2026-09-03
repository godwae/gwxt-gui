#include "downloadmanager.h"
#include "csvwriter.h"
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QStandardPaths>
#include <QFile>
#include <QDebug>

static const QString BASE_URL     = QStringLiteral("http://gwxt.sgcc.com.cn");
static const QString MAIN_PAGE    = QStringLiteral(
    "http://gwxt.sgcc.com.cn/www/command/SkillLevelControlZ"
    "?flag=news_deatil&id=8a84d28c9cd2d572019d4df4305c6d56");
static const QString USER_AGENT   = QStringLiteral(
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36");

// DEBUG ONLY: 开启后把主页面/详情页的原始响应落盘，并打印请求/响应诊断日志，
// 用于排查"抓不到专业列表"或"302 到登录页"等问题。
// 路径 = <saveDir>/_debug_<name>.html。
// 注意: 开启时每个详情页都会写一次磁盘，会明显拖慢批量下载，排查完请改回 false。
static const bool g_debugDump = false;

static void debugDump(const QString &saveDir, const QString &name,
                      const QByteArray &data)
{
    if (!g_debugDump)
        return;
    const QString dir = saveDir;
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + QStringLiteral("_debug_") + name + QStringLiteral(".html");
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
        qInfo() << "[DEBUG] dumped" << path << "(" << data.size() << "bytes)";
    } else {
        qWarning() << "[DEBUG] failed to dump" << path;
    }
}
// 自定义请求属性：分发时用来判断回复属于哪一类、对应哪个任务索引
static const QNetworkRequest::Attribute AttrKind =
    static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 1);
static const QNetworkRequest::Attribute AttrIndex =
    static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 2);

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_saveDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
{
    // 按请求属性分发，而不是靠 m_state 猜测 —— 并发后同时有多个在途请求，
    // 单一状态位已无法区分回来的 reply 属于哪一类。
    connect(m_nam, &QNetworkAccessManager::finished,
            this, [this](QNetworkReply *reply) {
        reply->deleteLater();

        const int kind = reply->request().attribute(AttrKind).toInt();
        if (kind == static_cast<int>(ReqKind::Main))
            onMainPageReply(reply);
        else if (kind == static_cast<int>(ReqKind::Detail))
            onDetailPageReply(reply);
        else if (kind == static_cast<int>(ReqKind::Topic))
            onTopicReply(reply);
    });
}

// 统一的请求头设置
QNetworkRequest DownloadManager::buildRequest(const QUrl &url, const QString &referer,
                                              ReqKind kind, int index) const
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", USER_AGENT.toUtf8());
    req.setRawHeader("Referer", referer.toUtf8());
    if (!m_cookie.isEmpty())
        req.setRawHeader("Cookie", m_cookie.toUtf8());
    req.setAttribute(AttrKind, static_cast<int>(kind));
    req.setAttribute(AttrIndex, index);
    return req;
}

// 进度：两阶段各自 0→100%。
// 若合成一条总进度，阶段一的分子分母都是专业数，会先走到 100%，
// 进入阶段二后分母突然加上几百个层级任务，进度条又会倒退，观感很差。
// 拆成"解析专业"和"下载题目"两段各自完整的进度更直观。
void DownloadManager::emitCombinedProgress()
{
    if (m_topicPhase)
        emit progressUpdate(m_doneTopics, m_topicTasks.size());
    else
        emit progressUpdate(m_doneDetails, m_detailTasks.size());
}

void DownloadManager::setCookie(const QString &cookie)
{
    m_cookie = cookie;
}

void DownloadManager::setSaveDir(const QString &dir)
{
    m_saveDir = dir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
        : dir;
}

// ─────────────────────────────────────────────────────────
//  第一步 — GET 主页面
// ─────────────────────────────────────────────────────────
void DownloadManager::fetchSubjects()
{
    m_state = DownloadState::FetchingSubjects;
    emit logMessage(QStringLiteral("[*] 正在获取专业列表…"));

    static const QString referer =
        QStringLiteral("http://gwxt.sgcc.com.cn/www/command/SkillLevelControlZ?flag=news_list");

    // DEBUG: log what we're actually about to send (cookie prefix only,
    // not the full value, to keep the log readable).
    if (g_debugDump) {
        emit logMessage(QStringLiteral("  [DBG] URL: %1").arg(MAIN_PAGE));
        emit logMessage(QStringLiteral("  [DBG] Cookie len=%1, prefix='%2'")
            .arg(m_cookie.length())
            .arg(m_cookie.left(40)));
        emit logMessage(QStringLiteral("  [DBG] Referer: %1").arg(referer));
    }

    m_nam->get(buildRequest(QUrl(MAIN_PAGE), referer, ReqKind::Main, -1));
}

void DownloadManager::onMainPageReply(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("获取主页面失败: %1").arg(reply->errorString()));
        m_state = DownloadState::Error;
        return;
    }

    const QByteArray data = reply->readAll();
    const QString html = QString::fromUtf8(data);

    // DEBUG: 落盘主页面原始响应（排查用，详见文件顶部 g_debugDump）
    debugDump(m_saveDir, QStringLiteral("main"), data);

    // DEBUG: dump raw response headers too (for redirect target, set-cookie, etc.)
    if (g_debugDump) {
        const QList<QPair<QByteArray,QByteArray>> hdrs = reply->rawHeaderPairs();
        for (const auto &h : hdrs)
            emit logMessage(QStringLiteral("  [DBG] R: %1: %2")
                .arg(QString::fromLatin1(h.first),
                     QString::fromLatin1(h.second.left(200))));
        emit logMessage(QStringLiteral("  [DBG] status=%1 finalURL=%2")
            .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
            .arg(reply->url().toString()));
    }

    // ── HTTP 3xx 重定向 → ISC 统一认证 ──────────────────────
    // 实测证据: status=302, Location=.../isc_sso/login?service=...
    // 含义是服务器端认定当前会话"未认证"。注意这不等价于 Cookie 过期：
    // 也可能缺了 SSO 票据，或 JSESSIONID2 与服务端 session 实例不匹配
    // （WebLogic 的 JSESSIONID2 形如 <id>!<serverid>，缺 ! 后缀即无效）。
    const int statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode == 301 || statusCode == 302 || statusCode == 303
        || statusCode == 307 || statusCode == 308) {
        const QString loc = QString::fromUtf8(reply->rawHeader("Location"));
        emit errorOccurred(
            QStringLiteral("服务器返回 HTTP %1 重定向，会话未通过统一认证(SSO)。\n"
                           "跳转目标: %2\n\n"
                           "请依次排查:\n"
                           " 1. 在浏览器打开题库页面，确认当前是【已登录】状态；\n"
                           " 2. 按 F12 -> Network -> 选中该请求 -> Request Headers\n"
                           "    -> Cookie，复制【完整一行】粘贴到上方输入框；\n"
                           " 3. 对比日志里 [DBG] Cookie len 与浏览器中的长度是否一致；\n"
                           " 4. 若仍失败，请把日志区 [DBG] 开头的全部内容发来排查。")
                .arg(statusCode)
                .arg(loc.isEmpty() ? QStringLiteral("(无 Location 头)") : loc));
        m_state = DownloadState::Error;
        return;
    }

    // 检测登录失效
    if (reply->url().toString().contains(QStringLiteral("login"), Qt::CaseInsensitive)
        || html.contains(QStringLiteral("<title>登录"))) {
        emit errorOccurred(QStringLiteral("Cookie 已失效或登录过期，请重新输入 Cookie"));
        m_state = DownloadState::Error;
        return;
    }

    // 检测网关返回的跳转桩页：体积很小、title 形如 "302 Moved Temporarily"、
    // 正文中含 login。这是"会话未通过认证"的典型表现（Cookie 不完整）。
    // 注意: 旧版只查 "<title>登录"，而跳转桩页的 title 是英文，故漏判。
    if (html.contains(QStringLiteral("Moved Temporarily"), Qt::CaseInsensitive)
        || html.contains(QStringLiteral("302 Found"), Qt::CaseInsensitive)
        || (html.length() < 2048 && html.contains(QStringLiteral("login"), Qt::CaseInsensitive))) {
        emit errorOccurred(
            QStringLiteral("服务器返回跳转页(%1 字节)，会话未通过认证。\n"
                           "Cookie 不完整或已过期。请在浏览器中重新登录题库页面，\n"
                           "按 F12 -> Network -> 请求头 -> Cookie，复制完整 Cookie 粘贴到输入框。")
                .arg(html.length()));
        m_state = DownloadState::Error;
        return;
    }

    const auto subjects = Parser::extractSubjects(html);
    if (subjects.isEmpty()) {
        // Diagnostics: tell "incomplete cookie" apart from "page layout changed".
        // NOTE: kept pure ASCII on purpose. MinGW on Chinese Windows may decode
        // sources with a non-UTF-8 codepage, and UTF-8 CJK inside a literal can
        // then turn into a stray quote/semicolon and break the build.
        emit logMessage(QStringLiteral("  [!] Diag: response %1 bytes, final URL: %2")
            .arg(html.length()).arg(reply->url().toString()));
        emit logMessage(QStringLiteral("  [!] Diag: has [txtnr]=%1, has [exam_detail]=%2, has [login]=%3")
            .arg(html.contains(QStringLiteral("txtnr")))
            .arg(html.contains(QStringLiteral("exam_detail")))
            .arg(html.contains(QStringLiteral("login"))));
        int tStart = html.indexOf(QLatin1String("<title>"));
        int tEnd   = html.indexOf(QLatin1String("</title>"));
        if (tStart >= 0 && tEnd > tStart)
            emit logMessage(QStringLiteral("  [!] Diag: page title = %1")
                .arg(html.mid(tStart + 7, tEnd - tStart - 7).trimmed()));
        emit errorOccurred(QStringLiteral("未能从页面中提取到专业列表，请检查 Cookie 或网络"));
        m_state = DownloadState::Error;
        return;
    }

    m_state = DownloadState::Idle;
    emit logMessage(QStringLiteral("[√] 获取到 %1 个专业").arg(subjects.size()));
    emit subjectsReady(subjects);
}

// ─────────────────────────────────────────────────────────
//  下载启动 — 两阶段并发
// ─────────────────────────────────────────────────────────
void DownloadManager::startDownload(const QList<SubjectEntry> &selectedSubjects)
{
    if (selectedSubjects.isEmpty()) {
        emit errorOccurred(QStringLiteral("未选择任何专业"));
        return;
    }

    // 重置全部状态
    m_detailTasks.clear();
    m_topicTasks.clear();
    m_detailRunning = 0;
    m_topicRunning  = 0;
    m_doneDetails   = 0;
    m_doneTopics    = 0;
    m_topicPhase    = false;
    m_sessionExpired = false;
    m_totalSubjects = selectedSubjects.size();
    m_state         = DownloadState::FetchingDetail;

    m_detailTasks.reserve(selectedSubjects.size());
    for (const auto &s : selectedSubjects) {
        DetailTask t;
        t.subject = s;
        QString safeTitle = s.title;
        safeTitle.remove(QRegularExpression(QStringLiteral(R"([\\/*?:"<>|])")));
        t.dir = m_saveDir + QLatin1Char('/') + safeTitle;
        m_detailTasks.append(t);
    }

    emit logMessage(QStringLiteral("\n======== 开始下载 %1 个专业（%2 路并发）========")
                    .arg(m_totalSubjects).arg(CONCURRENCY));
    emit progressUpdate(0, m_totalSubjects);

    QDir().mkpath(m_saveDir);
    pumpDetails();
}

// ── 阶段一：并发拉取详情页 ──────────────────────────────
void DownloadManager::pumpDetails()
{
    if (m_sessionExpired) {
        if (m_detailRunning == 0)
            m_state = DownloadState::Error;
        return;
    }

    while (m_detailRunning < CONCURRENCY) {
        int idx = -1;
        for (int i = 0; i < m_detailTasks.size(); ++i) {
            if (!m_detailTasks.at(i).started) { idx = i; break; }
        }
        if (idx < 0)
            break;   // 没有未启动的任务

        DetailTask &t = m_detailTasks[idx];
        t.started = true;
        m_detailRunning++;

        QString fullUrl = t.subject.url;
        if (!fullUrl.startsWith(QStringLiteral("http")))
            fullUrl = BASE_URL + fullUrl;

        m_nam->get(buildRequest(QUrl(fullUrl), MAIN_PAGE, ReqKind::Detail, idx));
    }

    // 所有详情页都已返回 → 进入阶段二
    if (m_detailRunning == 0 && m_doneDetails >= m_detailTasks.size()
        && !m_topicPhase && !m_detailTasks.isEmpty()) {
        startTopicPhase();
    }
}

void DownloadManager::onDetailPageReply(QNetworkReply *reply)
{
    const int idx = reply->request().attribute(AttrIndex).toInt();
    if (idx < 0 || idx >= m_detailTasks.size())
        return;
    const DetailTask t = m_detailTasks.at(idx);
    m_detailTasks[idx].done = true;
    m_doneDetails++;
    m_detailRunning--;

    auto finishOne = [this]() {
        emitCombinedProgress();
        pumpDetails();
    };

    if (reply->error() != QNetworkReply::NoError) {
        emit logMessage(QStringLiteral("  [!] 详情页失败: %1 (%2)")
                        .arg(t.subject.title, reply->errorString()));
        finishOne();
        return;
    }

    const QByteArray detailData = reply->readAll();
    const QString html = QString::fromUtf8(detailData);
    debugDump(m_saveDir, QStringLiteral("detail"), detailData);

    if (reply->url().toString().contains(QStringLiteral("login"), Qt::CaseInsensitive)
        || html.contains(QStringLiteral("<title>登录"))
        || html.contains(QStringLiteral("isc_sso"), Qt::CaseInsensitive)) {
        if (!m_sessionExpired) {
            m_sessionExpired = true;
            m_state = DownloadState::Error;
            emit errorOccurred(QStringLiteral("会话已失效，服务器返回登录页。\n"
                                              "请重新点击 [账号登录] 后再试。"));
        }
        finishOne();
        return;
    }

    const auto levels = Parser::extractLevels(html);
    if (levels.isEmpty()) {
        emit logMessage(QStringLiteral("  [-] %1: 无单题练习模块").arg(t.subject.title));
        finishOne();
        return;
    }

    QDir().mkpath(t.dir);

    // 展开为阶段二的任务
    static const QRegularExpression idRe(QStringLiteral(R"(id=([^&]+))"));
    for (const auto &lev : levels) {
        const auto m = idRe.match(lev.url);
        if (!m.hasMatch()) {
            emit logMessage(QStringLiteral("      [!] 无法提取ID，跳过: %1").arg(lev.name));
            continue;
        }
        TopicTask tt;
        tt.subjectTitle = t.subject.title;
        tt.subjectDir   = t.dir;
        tt.levelName    = lev.name;
        tt.jsUrl = QStringLiteral(
            "http://gwxt.sgcc.com.cn/www/resource/uploadfiles/static/topic/%1.js?_=%2")
            .arg(m.captured(1))
            .arg(QDateTime::currentMSecsSinceEpoch());
        m_topicTasks.append(tt);
    }

    emit logMessage(QStringLiteral("  [+] %1: %2 个层级")
                    .arg(t.subject.title).arg(levels.size()));
    finishOne();
}

// ── 阶段二：并发下载 JS ─────────────────────────────────
void DownloadManager::startTopicPhase()
{
    m_topicPhase = true;
    if (m_topicTasks.isEmpty()) {
        emit logMessage(QStringLiteral("[!] 未发现任何可下载的层级"));
        processFinished();
        return;
    }
    emit logMessage(QStringLiteral("\n---- 共 %1 个层级文件，开始并发下载 ----")
                    .arg(m_topicTasks.size()));
    emitCombinedProgress();
    pumpTopics();
}

void DownloadManager::pumpTopics()
{
    if (m_sessionExpired) {
        if (m_topicRunning == 0)
            m_state = DownloadState::Error;
        return;
    }

    while (m_topicRunning < CONCURRENCY) {
        int idx = -1;
        for (int i = 0; i < m_topicTasks.size(); ++i) {
            if (!m_topicTasks.at(i).started) { idx = i; break; }
        }
        if (idx < 0)
            break;
        m_topicTasks[idx].started = true;
        m_topicRunning++;
        startTopic(idx);
    }

    if (m_topicRunning == 0 && m_doneTopics >= m_topicTasks.size() && m_topicPhase)
        processFinished();
}

void DownloadManager::startTopic(int index)
{
    const TopicTask t = m_topicTasks.at(index);
    m_nam->get(buildRequest(QUrl(t.jsUrl), BASE_URL, ReqKind::Topic, index));
}

void DownloadManager::onTopicReply(QNetworkReply *reply)
{
    const int idx = reply->request().attribute(AttrIndex).toInt();
    if (idx < 0 || idx >= m_topicTasks.size())
        return;
    TopicTask &t = m_topicTasks[idx];
    const QString subjectDir = t.subjectDir;
    const QString levelName  = t.levelName;

    auto completeOne = [this, idx]() {
        m_topicTasks[idx].done = true;
        m_doneTopics++;
        emitCombinedProgress();
        pumpTopics();
    };

    // ── 失败重试（指数退避 1s/2s/4s）──
    // 重试期间【不释放】并发槽：m_topicRunning 保持不变，pumpTopics 就不会
    // 往窗口里塞新任务，退避期真正起到降速作用。槽一直占到重试请求返回为止。
    if (reply->error() != QNetworkReply::NoError) {
        if (t.retry < MAX_RETRIES) {
            t.retry++;
            const int wait = 1000 * (1 << (t.retry - 1));
            emit logMessage(QStringLiteral("      [!] 重试(%1/%2) %3，%4秒后…")
                            .arg(t.retry).arg(MAX_RETRIES).arg(levelName).arg(wait / 1000));
            QTimer::singleShot(wait, this, [this, idx]() {
                if (idx < 0 || idx >= m_topicTasks.size())
                    return;
                startTopic(idx);   // 槽仍被占用，直接重发
            });
            return;
        }
        emit logMessage(QStringLiteral("      [x] 放弃: %1 (%2)")
                        .arg(levelName, reply->errorString()));
        m_topicRunning--;
        completeOne();
        return;
    }

    m_topicRunning--;

    const QByteArray data = reply->readAll();
    const QString jsContent = QString::fromUtf8(data);

    if (jsContent.isEmpty()) {
        emit logMessage(QStringLiteral("      [x] 空响应: %1").arg(levelName));
        completeOne();
        return;
    }

    // 会话失效熔断：JS 接口被重定向到登录页时会返回 HTML 而非 JS。
    // 不熔断的话，并发下每个剩余任务都会各自重试 3 次才放弃。
    const QString head = jsContent.left(512).trimmed();
    if (head.startsWith(QLatin1Char('<')) || head.contains(QStringLiteral("isc_sso"))) {
        if (!m_sessionExpired) {
            m_sessionExpired = true;
            m_state = DownloadState::Error;
            emit logMessage(QStringLiteral("  [!] 收到 HTML 而非 JS，登录态已失效"));
            emit errorOccurred(QStringLiteral("会话已失效，服务器返回登录页而非题目数据。\n"
                                              "请重新点击 [账号登录] 后再试。"));
        }
        completeOne();
        return;
    }

    const auto topics = Parser::extractTopics(jsContent);
    if (topics.isEmpty()) {
        emit logMessage(QStringLiteral("      [!] 无题目数据: %1").arg(levelName));
        completeOne();
        return;
    }

    QString safeName = levelName;
    safeName.remove(QRegularExpression(QStringLiteral(R"([\\/*?:"<>|])")));
    const QString csvPath = subjectDir + QLatin1Char('/') + safeName + QStringLiteral(".csv");

    if (QFileInfo::exists(csvPath)) {
        emit logMessage(QStringLiteral("      [*] 已存在，跳过: %1.csv").arg(safeName));
        completeOne();
        return;
    }

    const int count = CsvWriter::write(csvPath, topics);
    if (count > 0) {
        emit logMessage(QStringLiteral("      [√] %1.csv (共 %2 题)").arg(safeName).arg(count));
        emit fileSaved(csvPath, count);
    } else {
        emit logMessage(QStringLiteral("      [!] 写入失败: %1.csv").arg(safeName));
    }

    completeOne();
}

// ─────────────────────────────────────────────────────────
//  收尾
// ─────────────────────────────────────────────────────────
void DownloadManager::processFinished()
{
    m_state = DownloadState::Finished;
    emitCombinedProgress();
    emit logMessage(QStringLiteral("\n🎉 全部下载完成！共 %1 个专业、%2 个层级文件")
                    .arg(m_doneDetails).arg(m_doneTopics));
    emit allFinished();
}

void DownloadManager::abort()
{
    // 标记全部任务为已启动，阻止 pump 再发起新请求；在途 reply 由
    // finished() 回收，其回调会因 done 标记而不再推进流程。
    for (auto &t : m_detailTasks) t.started = true;
    for (auto &t : m_topicTasks)  t.started = true;
    m_state = DownloadState::Idle;
    emit logMessage(QStringLiteral("[!] 已取消"));
}
