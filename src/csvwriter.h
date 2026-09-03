#ifndef CSVWRITER_H
#define CSVWRITER_H

#include <QString>
#include <QList>
#include "parser.h"

/// 将题目列表写入 CSV 文件（UTF-8 BOM，Excel 直接打开不乱码）
class CsvWriter {
public:
    /// @return 写入的题目数量；失败返回 -1
    static int write(const QString &filePath, const QList<TopicData> &topics);
};

#endif // CSVWRITER_H
