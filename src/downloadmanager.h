#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QList>
#include <QQueue>
#include <QVector>
#include "parser.h"

/// 请求类型 — 用于区分 finished() 分发到哪个处理函数
enum class ReqKind { Main = 0, Detail = 1, Topic = 2 };

/// 下载任务状态
enum class DownloadState {
    Idle,
    FetchingSubjects,   // 正在获取专业列表
    FetchingDetail,     // 正在获取某个专业的详情页
    FetchingTopic,      // 正在下载某个层级的 JS 文件
    Finished,
    Error
};

/// 核心下载引擎 — 管理三步爬取流程，全程异步，通过信号汇报进度
///
/// 流程:
///   1. fetchSubjects()        → GET 主页面 → parse → emit subjectsReady()
///   2. startDownload(list)    → 阶段一: 并发 GET 各专业详情页 → 收集层级任务
///                             → 阶段二: 并发 GET 各层级 JS → parse → 写 CSV
///
/// 两个阶段均按 CONCURRENCY 路并发。原先是完全串行且每条 JS 之间固定
/// sleep 200ms，65 个专业 × 若干层级会产生数百次串行往返，实测明显偏慢。
class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);

    /// 设置 Cookie 字符串（每次请求前调用）
    void setCookie(const QString &cookie);

    /// 设置保存目录
    void setSaveDir(const QString &dir);

    /// 第一步：拉取主页面 → 解析专业列表
    void fetchSubjects();

    /// 第二步：对勾选的专业启动下载
    void startDownload(const QList<SubjectEntry> &selectedSubjects);

    /// 取消当前任务
    void abort();

signals:
    /// 专业列表已就绪
    void subjectsReady(const QList<SubjectEntry> &subjects);

    /// 总体进度: current/total (专业维度)
    void progressUpdate(int current, int total);

    /// 详细日志
    void logMessage(const QString &msg);

    /// 单个文件保存完成
    void fileSaved(const QString &path, int topicCount);

    /// 全部完成
    void allFinished();

    /// 发生错误（可恢复）
    void errorOccurred(const QString &err);

private slots:
    void onMainPageReply(QNetworkReply *reply);
    void onDetailPageReply(QNetworkReply *reply);
    void onTopicReply(QNetworkReply *reply);

private:
    struct DetailTask {
        SubjectEntry subject;
        QString      dir;        // 该专业的输出目录
        bool         started = false;
        bool         done    = false;
    };
    struct TopicTask {
        QString subjectTitle;
        QString subjectDir;
        QString levelName;
        QString jsUrl;
        int     retry   = 0;
        bool    started = false;
        bool    done    = false;
    };

    QNetworkRequest buildRequest(const QUrl &url, const QString &referer,
                                 ReqKind kind, int index) const;
    void pumpDetails();       // 阶段一: 填满并发窗口
    void pumpTopics();        // 阶段二: 填满并发窗口
    void startTopicPhase();   // 详情页全部完成后切换到阶段二
    void startTopic(int index);
    void processFinished();
    void emitCombinedProgress();

    QNetworkAccessManager *m_nam;
    QString m_cookie;
    QString m_saveDir;
    DownloadState m_state = DownloadState::Idle;

    QVector<DetailTask> m_detailTasks;
    QVector<TopicTask>  m_topicTasks;
    int m_detailRunning = 0;   // 阶段一在途请求数
    int m_topicRunning  = 0;   // 阶段二在途请求数
    int m_doneDetails   = 0;
    int m_doneTopics    = 0;
    bool m_topicPhase   = false;

    /// 会话失效熔断标志。并发模式下若 Cookie 过期，不熔断的话每个任务都会
    /// 各自白跑 3 次重试再放弃，几百个任务就是上千次无效请求。
    bool m_sessionExpired = false;

    int m_totalSubjects = 0;

    /// 每阶段并发请求数。取 5 是权衡：Qt 对单主机默认上限 6 条连接，
    /// 再高不会更快；同时避免对内网网关形成突发压力触发风控。
    static const int CONCURRENCY  = 5;
    static const int MAX_RETRIES  = 3;
};

#endif // DOWNLOADMANAGER_H
