#ifndef PARSER_H
#define PARSER_H

#include <QString>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRegularExpression>

/// 题目类型：1=单选, 2=多选, 4=判断
struct TopicData {
    QString basetypeId;   // "1"/"2"/"4"
    QString topic;        // 题目正文
    QString key;          // 答案
    QStringList options;  // 选项列表
};

/// 专业条目（从主页面解析）
struct SubjectEntry {
    QString title;  // 专业名称，如"变电设备检修工"
    QString url;    // 详情页相对路径
};

/// 练习层级条目（从详情页解析）
struct LevelEntry {
    QString name;  // 层级名称，如"初级工"
    QString url;   // topic.htm 链接
};

/// 静态解析工具类 — HTML / JS 文本 → 结构化数据
class Parser {
public:
    /// 从主页面 HTML 中提取专业列表 <div class="txtnr"> → <a href="...exam_detail...">
    static QList<SubjectEntry> extractSubjects(const QString &html);

    /// 从详情页 HTML 中提取练习层级 <div class="dtlx_box"> → <a>
    static QList<LevelEntry> extractLevels(const QString &html);

    /// 从 JS 文本中提取题目数组 topicArray.push({...});
    static QList<TopicData> extractTopics(const QString &jsContent);

    /// 根据题目数据构建一行输出（题目, 答案, 选项A-E）
    static QStringList buildRow(const TopicData &topic);

private:
    /// 解析单条 topicArray.push({...}) 的原始文本
    static TopicData parseTopicRaw(const QString &raw);
};

#endif // PARSER_H
