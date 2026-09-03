#include "parser.h"
#include <QRegularExpressionMatch>
#include <QHash>

// ═══════════════════════════════════════════════════════════════
//  辅助函数：解码 HTML 实体
//  Python 版 BeautifulSoup 自动解码，C++ 需手动处理
//  关键：详情页 URL 中的 &amp; 必须解码为 &，否则 id 参数无法传递
// ═══════════════════════════════════════════════════════════════
static QString decodeHtmlEntities(const QString &text)
{
    QString result = text;

    // 常见命名实体（必须先处理 &amp; 避免二次转义，先替换其余）
    static const QHash<QString, QChar> namedEntities = {
        {QStringLiteral("&quot;"), QChar('"')},
        {QStringLiteral("&apos;"), QChar('\'')},
        {QStringLiteral("&#39;"),  QChar('\'')},
        {QStringLiteral("&lt;"),   QChar('<')},
        {QStringLiteral("&gt;"),   QChar('>')},
    };

    for (auto it = namedEntities.begin(); it != namedEntities.end(); ++it) {
        result.replace(it.key(), QString(it.value()));
    }

    // 数字实体 &#nnn; 和 &#xhhh;
    {
        static const QRegularExpression numRe(
            QStringLiteral(R"(&#(\d+);)"));
        QRegularExpressionMatch m;
        while ((m = numRe.match(result)).hasMatch()) {
            const int cp = m.captured(1).toInt();
            result.replace(m.capturedStart(), m.capturedLength(),
                           cp > 0 && cp <= 0x10FFFF ? QString(QChar(cp)) : QString());
        }
    }
    {
        static const QRegularExpression hexRe(
            QStringLiteral(R"(&#x([0-9a-fA-F]+);)"));
        QRegularExpressionMatch m;
        while ((m = hexRe.match(result)).hasMatch()) {
            bool ok;
            const int cp = m.captured(1).toInt(&ok, 16);
            result.replace(m.capturedStart(), m.capturedLength(),
                           ok && cp > 0 && cp <= 0x10FFFF ? QString(QChar(cp)) : QString());
        }
    }

    // &amp; 必须最后处理，避免与数字实体冲突
    result.replace(QStringLiteral("&amp;"), QStringLiteral("&"));

    return result;
}

// ═══════════════════════════════════════════════════════════════
//  辅助函数：提取指定 CSS class 的 div 内部内容
//  正确处理嵌套 div 和多 class 属性（如 class="dtlx_box active"）
//  \b 确保匹配完整 class 名而非子串
// ═══════════════════════════════════════════════════════════════
static QString extractDivByClass(const QString &html, const QString &className)
{
    // 匹配开标签: <div ... class="...className..." ...>
    const QString pattern = QStringLiteral(
        R"(<div[^>]*\bclass\s*=\s*["'][^"']*\b%1\b[^"']*["'][^>]*>)")
        .arg(className);

    const QRegularExpression openRe(pattern, QRegularExpression::CaseInsensitiveOption);
    const auto m = openRe.match(html);
    if (!m.hasMatch())
        return {};

    const int contentStart = m.capturedEnd();  // 紧接在开标签 > 之后
    int pos = contentStart;
    int depth = 1;

    // 分别用两个简单正则匹配开标签和闭标签，避免一个复杂正则的歧义
    // 静态编译一次，避免每次调用重新构造
    static const QRegularExpression openTagRe(
        QStringLiteral(R"(<div[\s>])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression closeTagRe(
        QStringLiteral(R"(</div\s*>)"),
        QRegularExpression::CaseInsensitiveOption);

    while (depth > 0 && pos < html.length()) {
        const auto openM  = openTagRe.match(html, pos);
        const auto closeM = closeTagRe.match(html, pos);

        if (!closeM.hasMatch())
            break;  // HTML 不完整

        if (openM.hasMatch() && openM.capturedStart() < closeM.capturedStart()) {
            ++depth;
            pos = openM.capturedEnd();
        } else {
            --depth;
            if (depth == 0) {
                // 找到匹配的闭标签 → 返回夹在中间的内容
                return html.mid(contentStart, closeM.capturedStart() - contentStart);
            }
            pos = closeM.capturedEnd();
        }
    }

    return {};  // 未找到匹配的闭合标签
}


// ═══════════════════════════════════════════════════════════════
//  主页面 → 专业列表
//  目标 HTML: <div class="txtnr"> … <a href="…exam_detail…">专业名</a> … </div>
// ═══════════════════════════════════════════════════════════════
QList<SubjectEntry> Parser::extractSubjects(const QString &html)
{
    QList<SubjectEntry> subjects;

    // 使用 div 深度计数提取 txtnr 区域（正确处理嵌套 div 和多 class）
    const QString block = extractDivByClass(html, QStringLiteral("txtnr"));
    if (block.isEmpty())
        return subjects;

    // 提取所有 <a> 标签，要求 href 含 exam_detail
    static const QRegularExpression linkRe(
        QStringLiteral(R"(<a[^>]*href\s*=\s*["']([^"']*exam_detail[^"']*)["'][^>]*>(.*?)</a>)"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption
    );

    auto it = linkRe.globalMatch(block);
    while (it.hasNext()) {
        auto lm = it.next();
        SubjectEntry sub;
        sub.url   = decodeHtmlEntities(lm.captured(1));
        sub.title = lm.captured(2).trimmed();
        if (!sub.title.isEmpty())
            subjects.append(sub);
    }

    return subjects;
}


// ═══════════════════════════════════════════════════════════════
//  详情页 → 练习层级列表
//  目标 HTML: <div class="dtlx_box"> … <a href="…id=…">层级名</a> … </div>
// ═══════════════════════════════════════════════════════════════
QList<LevelEntry> Parser::extractLevels(const QString &html)
{
    QList<LevelEntry> levels;

    // 使用 div 深度计数提取 dtlx_box 区域
    const QString block = extractDivByClass(html, QStringLiteral("dtlx_box"));
    if (block.isEmpty())
        return levels;

    // 匹配所有 <a href="...">text</a>
    static const QRegularExpression linkRe(
        QStringLiteral(R"(<a[^>]*href\s*=\s*["']([^"']*)["'][^>]*>(.*?)</a>)"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption
    );

    auto it = linkRe.globalMatch(block);
    while (it.hasNext()) {
        auto lm = it.next();
        LevelEntry lev;
        lev.url  = decodeHtmlEntities(lm.captured(1));
        lev.name = lm.captured(2).trimmed();
        if (!lev.name.isEmpty() && !lev.url.isEmpty())
            levels.append(lev);
    }

    return levels;
}


// ═══════════════════════════════════════════════════════════════
//  JS 文本 → 题目列表
//  目标片段: topicArray.push({basetypeId:"1", topic:"…", topicOption:"…", topicKey:"…"});
// ═══════════════════════════════════════════════════════════════
QList<TopicData> Parser::extractTopics(const QString &jsContent)
{
    QList<TopicData> topics;

    static const QRegularExpression pushRe(
        QStringLiteral(R"(topicArray\.push\(\{(.*?)\}\)\s*;)"),
        QRegularExpression::DotMatchesEverythingOption
    );

    auto it = pushRe.globalMatch(jsContent);
    while (it.hasNext()) {
        auto m = it.next();
        TopicData t = parseTopicRaw(m.captured(1));
        if (!t.topic.isEmpty())
            topics.append(t);
    }

    return topics;
}


// ═══════════════════════════════════════════════════════════════
//  解析单条 push 参数中的键值对
// ═══════════════════════════════════════════════════════════════
TopicData Parser::parseTopicRaw(const QString &raw)
{
    TopicData d;

    // basetypeId:"1" / basetypeId:"2" / basetypeId:"4"
    static const QRegularExpression typeRe(
        QStringLiteral("basetypeId\\s*:\\s*\"(\\d+)\""));
    auto tm = typeRe.match(raw);
    if (tm.hasMatch())
        d.basetypeId = tm.captured(1);

    // topic:"..."  (允许转义引号 \" )
    static const QRegularExpression topicRe(
        QStringLiteral("topic\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    auto tpm = topicRe.match(raw);
    if (tpm.hasMatch()) {
        d.topic = tpm.captured(1);
        d.topic.replace(QLatin1String("\\\""), QLatin1String("\""));
    }

    // topicOption:"..." — 仅单选/多选有；分隔符 "$;$"
    static const QRegularExpression optRe(
        QStringLiteral("topicOption\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    auto om = optRe.match(raw);
    if (om.hasMatch() && d.basetypeId != QLatin1String("4")) {
        QString optsRaw = om.captured(1);
        d.options = optsRaw.split(QStringLiteral("$;$"));
    }

    // topicKey:"..."
    static const QRegularExpression keyRe(
        QStringLiteral("topicKey\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    auto km = keyRe.match(raw);
    if (km.hasMatch()) {
        d.key = km.captured(1);
        d.key.replace(QLatin1String("\\\""), QLatin1String("\""));
    }

    return d;
}


// ═══════════════════════════════════════════════════════════════
//  题目 → CSV 行: [题目, 答案, 选项A, 选项B, 选项C, 选项D, 选项E]
// ═══════════════════════════════════════════════════════════════
QStringList Parser::buildRow(const TopicData &topic)
{
    QStringList row;
    row << topic.topic << topic.key;

    if (topic.basetypeId == QLatin1String("4")) {
        // 判断题：固定"正确""错误"
        row << QStringLiteral("正确")
            << QStringLiteral("错误")
            << QString()
            << QString()
            << QString();
    } else {
        for (int i = 0; i < 5; ++i) {
            row << (i < topic.options.size() ? topic.options.at(i) : QString());
        }
    }

    return row;
}
