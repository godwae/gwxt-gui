#include "csvwriter.h"
#include <QFile>
#include <QTextStream>

int CsvWriter::write(const QString &filePath, const QList<TopicData> &topics)
{
    if (topics.isEmpty())
        return 0;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return -1;

    QTextStream out(&file);
    // UTF-8: Qt6 默认 UTF-8; Qt5 需要显式 setCodec
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << QChar(0xFEFF);  // BOM

    // 表头
    out << QStringLiteral("题目,答案,选项A,选项B,选项C,选项D,选项E\n");

    for (const auto &t : topics) {
        const QStringList row = Parser::buildRow(t);
        // CSV 转义：包含逗号或引号的字段用双引号包裹
        QStringList escaped;
        for (const auto &field : row) {
            QString f = field;
            f.replace(QLatin1String("\""), QLatin1String("\"\""));
            if (f.contains(QLatin1Char(',')) || f.contains(QLatin1Char('"'))
                || f.contains(QLatin1Char('\n'))) {
                f = QLatin1Char('"') + f + QLatin1Char('"');
            }
            escaped << f;
        }
        out << escaped.join(QLatin1Char(',')) << QLatin1Char('\n');
    }

    file.close();
    return topics.size();
}
