#include "mainwindow.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QApplication>
#include <QScrollBar>
#include <QStandardPaths>
#include <QMenu>
#include <QButtonGroup>
#include <QShowEvent>
#include <QSizePolicy>

// ────────────────────────────────────────────
//  辅助函数
// ────────────────────────────────────────────
static QLabel *cardTitle(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("cardTitle");
    return l;
}
static QFrame *makeCard(QWidget *parent = nullptr) {
    auto *f = new QFrame(parent);
    f->setObjectName("card");
    return f;
}

// ═══════════════════════════════════════════════════════════
//  日志着色 — 颜色随主题切换（深浅两套均可读）
//  注意: 失败判断必须在成功之前（"  [x]" 同样以 "  [" 开头）
// ═══════════════════════════════════════════════════════════
static QString logHtml(const QString &ts, const QString &msg)
{
    const bool dark = ThemeManager::instance()->isDark();
    const QString cBase = dark ? QStringLiteral("#C6CEDD") : QStringLiteral("#3A4657");
    const QString cOk   = dark ? QStringLiteral("#4ADE80") : QStringLiteral("#15803D");
    const QString cErr  = dark ? QStringLiteral("#F87171") : QStringLiteral("#D92D20");
    const QString cWarn = dark ? QStringLiteral("#FBBF24") : QStringLiteral("#B45309");
    const QString cInfo = dark ? QStringLiteral("#60A5FA") : QStringLiteral("#2563EB");
    const QString cTs   = dark ? QStringLiteral("#5B6478") : QStringLiteral("#98A2B3");

    QString color = cBase;
    if (msg.startsWith(QStringLiteral("[!]")) || msg.startsWith(QStringLiteral("  [x]")))
        color = cErr;
    else if (msg.startsWith(QStringLiteral("[OK]")) || msg.startsWith(QStringLiteral("  [√]")))
        color = cOk;
    else if (msg.startsWith(QStringLiteral("[*]")) || msg.startsWith(QStringLiteral("  [*]")))
        color = cWarn;
    else if (msg.startsWith(QStringLiteral("  [+]")) || msg.startsWith(QStringLiteral("  [↓]")))
        color = cInfo;

    return QStringLiteral(
        "<span style='color:%1'>[%2]</span> "
        "<span style='color:%3'>%4</span>")
        .arg(cTs, ts, color, msg.toHtmlEscaped());
}

// ────────────────────────────────────────────
//  构造
// ────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_downloader(new DownloadManager(this))
{
    setWindowTitle(QStringLiteral("技能等级评价题库下载器"));
    // 功能收敛后窗口高度可收敛；宽度保留 960 以确保顶部「获取题库列表」
    // 按钮完整显示（高 DPI 缩放/大字体下 860 会挤压顶行按钮）。
    resize(960, 660);
    setMinimumSize(760, 580);

    setupUi();

    connect(m_downloader, &DownloadManager::subjectsReady,
            this, &MainWindow::onSubjectsReady);
    connect(m_downloader, &DownloadManager::progressUpdate,
            this, &MainWindow::onProgressUpdate);
    connect(m_downloader, &DownloadManager::logMessage,
            this, &MainWindow::onLog);
    connect(m_downloader, &DownloadManager::errorOccurred,
            this, &MainWindow::onError);
    connect(m_downloader, &DownloadManager::allFinished,
            this, &MainWindow::onAllFinished);

    // 主题切换 → 日志重着色 + 标题栏换色（QSS 已由 ThemeManager 应用）
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *rootVBox = new QVBoxLayout(central);
    rootVBox->setSpacing(12);
    rootVBox->setContentsMargins(20, 16, 20, 16);

    // ── 标题栏 ──────────────────────────────────────
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(12);

        auto *titleCol = new QVBoxLayout;
        titleCol->setSpacing(2);
        auto *title = new QLabel(QStringLiteral("技能等级评价题库下载"));
        title->setObjectName("appTitle");
        auto *sub = new QLabel(QStringLiteral("批量导出题库为 CSV，Excel 可直接打开"));
        sub->setObjectName("appSub");
        titleCol->addWidget(title);
        titleCol->addWidget(sub);
        row->addLayout(titleCol);

        row->addStretch();

        // ── 主题分段切换: 浅色 / 深色 / 跟随系统 ──
        auto *seg = new QFrame;
        seg->setObjectName("seg");
        auto *segLay = new QHBoxLayout(seg);
        segLay->setContentsMargins(2, 2, 2, 2);
        segLay->setSpacing(2);

        m_segLight  = new QPushButton(QStringLiteral("浅色"));
        m_segDark   = new QPushButton(QStringLiteral("深色"));
        m_segSystem = new QPushButton(QStringLiteral("系统"));
        for (auto *b : { m_segLight, m_segDark, m_segSystem }) {
            b->setProperty("seg", true);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            segLay->addWidget(b);
        }
        m_segSystem->setToolTip(QStringLiteral("跟随系统深浅色设置（自动切换）"));

        switch (ThemeManager::instance()->mode()) {
        case ThemeManager::Light:  m_segLight->setChecked(true);  break;
        case ThemeManager::Dark:   m_segDark->setChecked(true);   break;
        case ThemeManager::System: m_segSystem->setChecked(true); break;
        }

        auto *segGroup = new QButtonGroup(this);
        segGroup->setExclusive(true);
        segGroup->addButton(m_segLight, ThemeManager::Light);
        segGroup->addButton(m_segDark, ThemeManager::Dark);
        segGroup->addButton(m_segSystem, ThemeManager::System);
        connect(segGroup, &QButtonGroup::idClicked, this, [](int id) {
            ThemeManager::instance()->setMode(static_cast<ThemeManager::Mode>(id));
        });

        row->addWidget(seg);
        rootVBox->addLayout(row);
    }

    // ═════════════════════════════════════════════════
    //  1. 登录卡片
    // ═════════════════════════════════════════════════
    {
        auto *card = makeCard();
        auto *vbox = new QVBoxLayout(card);
        vbox->setSpacing(10);
        vbox->addWidget(cardTitle(QStringLiteral("登录")));

        auto *row = new QHBoxLayout;
        row->setSpacing(10);

        auto *loginHint = new QLabel(QStringLiteral("应用内直连统一身份认证"));
        loginHint->setObjectName("hint");
        loginHint->setMinimumWidth(0);
        // 宽度紧张时优先压缩提示文字，避免挤压右侧两个操作按钮
        loginHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->addWidget(loginHint, 1);

        m_accountLoginBtn = new QPushButton(QStringLiteral("账号登录"));
        m_accountLoginBtn->setObjectName("primaryBtn");
        m_accountLoginBtn->setCursor(Qt::PointingHandCursor);
        m_accountLoginBtn->setToolTip(
            QStringLiteral("直连统一权限认证中心完成登录，成功后自动获取题库列表"));

        m_fetchBtn = new QPushButton(QStringLiteral("获取题库列表"));
        m_fetchBtn->setObjectName("secondaryBtn");
        m_fetchBtn->setCursor(Qt::PointingHandCursor);
        m_fetchBtn->setToolTip(
            QStringLiteral("登录后拉取专业列表（登录成功后也会自动执行一次）"));

        row->addWidget(m_accountLoginBtn);
        row->addWidget(m_fetchBtn);
        vbox->addLayout(row);

        connect(m_accountLoginBtn, &QPushButton::clicked, this, &MainWindow::onAccountLogin);
        connect(m_fetchBtn, &QPushButton::clicked, this, &MainWindow::onFetchSubjects);

        rootVBox->addWidget(card);
    }

    // ═════════════════════════════════════════════════
    //  2. 专业列表卡片
    // ═════════════════════════════════════════════════
    {
        auto *card = makeCard();
        auto *vbox = new QVBoxLayout(card);
        vbox->setSpacing(10);

        // 标题行
        auto *headerRow = new QHBoxLayout;
        headerRow->addWidget(cardTitle(QStringLiteral("专业列表")));
        headerRow->addStretch();
        vbox->addLayout(headerRow);

        // 搜索框 — 独占一行
        m_filterEdit = new QLineEdit;
        m_filterEdit->setPlaceholderText(QStringLiteral("输入关键字过滤专业…"));
        m_filterEdit->setClearButtonEnabled(true);
        vbox->addWidget(m_filterEdit);

        // 列表（先创建，后面加到 layout 中工具栏下方）
        m_subjectList = new QListWidget;
        m_subjectList->setSelectionMode(QAbstractItemView::NoSelection);
        m_subjectList->setSpacing(1);
        m_subjectList->setMinimumHeight(140);

        // 工具栏 — 全选复选框 + 操作按钮 + 计数（放在列表上方，不会遮挡列表）
        auto *toolRow = new QHBoxLayout;
        toolRow->setSpacing(8);
        m_selectAllCb = new QCheckBox(QStringLiteral("全选 / 取消全选"));
        m_selectAllCb->setTristate(false);
        toolRow->addWidget(m_selectAllCb);

        toolRow->addSpacing(4);
        m_invertBtn      = new QPushButton(QStringLiteral("反选"));
        m_invertBtn->setObjectName("toolBtn");
        m_invertBtn->setCursor(Qt::PointingHandCursor);
        toolRow->addWidget(m_invertBtn);
        toolRow->addStretch();

        // 实时计数（胶囊样式）
        auto *countLabel = new QLabel;
        countLabel->setObjectName("pill");
        toolRow->addWidget(countLabel);
        connect(m_subjectList, &QListWidget::itemChanged, this, [this, countLabel]() {
            int vis = 0, chk = 0;
            for (int i = 0; i < m_subjectList->count(); ++i) {
                auto *item = m_subjectList->item(i);
                if (!item->isHidden()) { vis++; if (item->checkState() == Qt::Checked) chk++; }
            }
            countLabel->setText(QStringLiteral("已选 %1 / %2").arg(chk).arg(vis));
            m_selectAllCb->blockSignals(true);
            if (vis == 0) m_selectAllCb->setCheckState(Qt::Unchecked);
            else if (chk == vis) m_selectAllCb->setCheckState(Qt::Checked);
            else m_selectAllCb->setCheckState(Qt::PartiallyChecked);
            m_selectAllCb->blockSignals(false);
        });
        vbox->addLayout(toolRow);

        // 列表填满剩余空间（在工具栏下方）
        vbox->addWidget(m_subjectList, 1);

        connect(m_filterEdit, &QLineEdit::textChanged,
                this, &MainWindow::onFilterChanged);
        connect(m_selectAllCb, &QCheckBox::toggled, this, [this](bool c) {
            if (c) onSelectAll(); else onDeselectAll();
        });
        connect(m_invertBtn,      &QPushButton::clicked, this, &MainWindow::onInvertSelection);

        rootVBox->addWidget(card, 2);  // stretch=2: 给列表更多空间避免底部边框被裁剪
    }

    // ═════════════════════════════════════════════════
    //  3. 输出 & 下载行
    // ═════════════════════════════════════════════════
    {
        auto *card = makeCard();
        auto *hbox = new QHBoxLayout(card);
        hbox->setSpacing(10);

        hbox->addWidget(cardTitle(QStringLiteral("输出格式")));
        m_formatCombo = new QComboBox;
        m_formatCombo->addItem(QStringLiteral("CSV (推荐)"), QStringLiteral("csv"));
        hbox->addWidget(m_formatCombo);

        hbox->addSpacing(20);
        hbox->addWidget(cardTitle(QStringLiteral("保存到")));
        m_saveDirEdit = new QLineEdit(
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        hbox->addWidget(m_saveDirEdit, 1);
        m_browseDirBtn = new QPushButton(QStringLiteral("浏览…"));
        m_browseDirBtn->setObjectName("toolBtn");
        m_browseDirBtn->setCursor(Qt::PointingHandCursor);
        hbox->addWidget(m_browseDirBtn);
        connect(m_browseDirBtn, &QPushButton::clicked, this, [this]() {
            const QString dir = QFileDialog::getExistingDirectory(
                this, QStringLiteral("选择保存目录"), m_saveDirEdit->text());
            if (!dir.isEmpty()) m_saveDirEdit->setText(dir);
        });

        hbox->addSpacing(20);
        m_downloadBtn = new QPushButton(QStringLiteral("开始下载"));
        m_downloadBtn->setObjectName("primaryBtn");
        m_downloadBtn->setCursor(Qt::PointingHandCursor);
        m_downloadBtn->setMinimumWidth(130);
        hbox->addWidget(m_downloadBtn);

        m_abortBtn = new QPushButton(QStringLiteral("取消"));
        m_abortBtn->setObjectName("dangerBtn");
        m_abortBtn->setEnabled(false);
        m_abortBtn->setCursor(Qt::PointingHandCursor);
        hbox->addWidget(m_abortBtn);

        connect(m_downloadBtn, &QPushButton::clicked, this, &MainWindow::onStartDownload);
        connect(m_abortBtn,    &QPushButton::clicked, this, &MainWindow::onAbort);

        rootVBox->addWidget(card);
    }

    // ═════════════════════════════════════════════════
    //  4. 进度条 + 状态文字
    // ═════════════════════════════════════════════════
    {
        auto *progRow = new QHBoxLayout;
        progRow->setSpacing(12);
        m_progressBar = new QProgressBar;
        m_progressBar->setTextVisible(false);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        m_progressLabel = new QLabel(QStringLiteral("就绪"));
        m_progressLabel->setObjectName("hint");
        progRow->addWidget(m_progressBar, 1);
        progRow->addWidget(m_progressLabel);
        rootVBox->addLayout(progRow);
    }

    // ═════════════════════════════════════════════════
    //  5. 日志区
    // ═════════════════════════════════════════════════
    m_logView = new QTextEdit;
    m_logView->setObjectName("logView");
    m_logView->setReadOnly(true);
    m_logView->document()->setMaximumBlockCount(2000);
    m_logView->setMinimumHeight(100);
    m_logView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_logView, &QTextEdit::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu *menu = m_logView->createStandardContextMenu();
        menu->addSeparator();
        QAction *clearAction = menu->addAction(QStringLiteral("清除日志"));
        connect(clearAction, &QAction::triggered, this, [this]() {
            m_logLines.clear();
            m_logView->clear();
            onLog(QStringLiteral("日志已清除"));
        });
        menu->exec(m_logView->viewport()->mapToGlobal(pos));
        delete menu;
    });
    rootVBox->addWidget(m_logView, 1);  // stretch=1: 日志区让出空间给专业列表

    onLog(QStringLiteral("就绪 - 点击 [账号登录] 完成统一身份认证，然后 [获取题库列表]"));
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    ThemeManager::applyDarkTitleBar(this, ThemeManager::instance()->isDark());
}

// ────────────────────────────────────────────
//  主题切换响应
// ────────────────────────────────────────────
void MainWindow::onThemeChanged(bool /*dark*/)
{
    // 同步分段控件选中态（主题可能由系统模式自动翻转或程序内部切换）
    switch (ThemeManager::instance()->mode()) {
    case ThemeManager::Light:  m_segLight->setChecked(true);  break;
    case ThemeManager::Dark:   m_segDark->setChecked(true);   break;
    case ThemeManager::System: m_segSystem->setChecked(true); break;
    }

    renderAllLogs();
    ThemeManager::applyDarkTitleBar(this, ThemeManager::instance()->isDark());
}

void MainWindow::renderAllLogs()
{
    QString html;
    html.reserve(m_logLines.size() * 96);
    for (const auto &line : m_logLines) {
        if (!html.isEmpty())
            html += QLatin1String("<br>");
        html += logHtml(line.first, line.second);
    }
    m_logView->setHtml(html);

    auto *sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

// ────────────────────────────────────────────
//  应用内账号密码登录
// ────────────────────────────────────────────
void MainWindow::onAccountLogin()
{
    LoginDialog dlg(this);
    connect(&dlg, &LoginDialog::loginSuccess, this, [this](const QString &cookie) {
        m_sessionCookie = cookie;
        m_downloader->setCookie(m_sessionCookie);
        onLog(QStringLiteral("[OK] 应用内登录成功，已取得会话 Cookie"));
    });
    if (dlg.exec() == QDialog::Accepted)
        onFetchSubjects();  // 登录成功后自动拉取专业列表
}

// ────────────────────────────────────────────
//  获取专业列表
// ────────────────────────────────────────────
void MainWindow::onFetchSubjects()
{
    if (m_sessionCookie.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("尚未登录，请先点击「账号登录」"));
        return;
    }

    // 先高效清空列表（关闭重绘避免逐个 item 触发 repaint）
    m_subjectList->setUpdatesEnabled(false);
    m_subjectList->clear();
    m_allSubjects.clear();
    m_subjectList->setUpdatesEnabled(true);

    // 立刻显示进度条并强制刷新 UI，避免后续网络初始化阻塞事件循环导致
    // 进度条延迟出现（Qt5 首次 QNetworkRequest 可能同步探测代理/DNS）
    m_progressBar->setRange(0, 0);
    m_progressLabel->setText(QStringLiteral("正在获取专业列表…"));
    m_fetchBtn->setEnabled(false);
    m_accountLoginBtn->setEnabled(false);
    QApplication::processEvents();

    m_downloader->setCookie(m_sessionCookie);
    m_downloader->fetchSubjects();
    setUiEnabled(false);
}

void MainWindow::onSubjectsReady(const QList<SubjectEntry> &subjects)
{
    m_allSubjects = subjects;
    m_subjectList->setUpdatesEnabled(false);
    m_subjectList->blockSignals(true);
    m_subjectList->clear();
    for (const auto &s : subjects) {
        auto *item = new QListWidgetItem(s.title);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        m_subjectList->addItem(item);
    }
    m_subjectList->blockSignals(false);
    m_subjectList->setUpdatesEnabled(true);

    // 恢复进度条
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressLabel->setText(QStringLiteral("就绪"));

    m_selectAllCb->setChecked(true);
    m_fetchBtn->setEnabled(true);
    m_accountLoginBtn->setEnabled(true);
    setUiEnabled(true);

    onLog(QStringLiteral("提示: 可用关键字过滤，勾选需要下载的专业后点击 [开始下载]"));
}

// ────────────────────────────────────────────
//  关键字过滤
// ────────────────────────────────────────────
void MainWindow::onFilterChanged(const QString &text)
{
    const QString kw = text.trimmed();
    for (int i = 0; i < m_subjectList->count(); ++i) {
        auto *item = m_subjectList->item(i);
        item->setHidden(kw.isEmpty() ? false
                                     : !item->text().contains(kw, Qt::CaseInsensitive));
    }
}

// ────────────────────────────────────────────
//  全选 / 取消 / 反选（只影响可见项）
// ────────────────────────────────────────────
void MainWindow::onSelectAll()
{
    for (int i = 0; i < m_subjectList->count(); ++i) {
        auto *item = m_subjectList->item(i);
        if (!item->isHidden())
            item->setCheckState(Qt::Checked);
    }
}

void MainWindow::onDeselectAll()
{
    for (int i = 0; i < m_subjectList->count(); ++i) {
        auto *item = m_subjectList->item(i);
        if (!item->isHidden())
            item->setCheckState(Qt::Unchecked);
    }
}

void MainWindow::onInvertSelection()
{
    for (int i = 0; i < m_subjectList->count(); ++i) {
        auto *item = m_subjectList->item(i);
        if (!item->isHidden()) {
            item->setCheckState(item->checkState() == Qt::Checked
                                ? Qt::Unchecked : Qt::Checked);
        }
    }
}

// ────────────────────────────────────────────
//  下载
// ────────────────────────────────────────────
void MainWindow::onStartDownload()
{
    const auto selected = getCheckedSubjects();
    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请至少勾选一个专业"));
        return;
    }

    m_downloader->setCookie(m_sessionCookie);
    m_downloader->setSaveDir(m_saveDirEdit->text().trimmed());
    m_downloader->startDownload(selected);

    setUiEnabled(false);
    m_downloadBtn->setEnabled(false);
    m_accountLoginBtn->setEnabled(false);
    m_abortBtn->setEnabled(true);
}

void MainWindow::onAbort()
{
    m_downloader->abort();
    setUiEnabled(true);
    m_fetchBtn->setEnabled(true);
    m_downloadBtn->setEnabled(true);
    m_accountLoginBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
    m_progressLabel->setText(QStringLiteral("已取消"));
}

QList<SubjectEntry> MainWindow::getCheckedSubjects() const
{
    QList<SubjectEntry> selected;
    for (int i = 0; i < m_subjectList->count(); ++i) {
        auto *item = m_subjectList->item(i);
        if (item->checkState() == Qt::Checked && !item->isHidden())
            selected.append(m_allSubjects.at(i));
    }
    return selected;
}

// ────────────────────────────────────────────
//  进度 & 日志
// ────────────────────────────────────────────
void MainWindow::onProgressUpdate(int current, int total)
{
    m_progressBar->setRange(0, total);
    m_progressBar->setValue(current);
    if (total > 0)
        m_progressLabel->setText(QStringLiteral("%1 / %2").arg(current).arg(total));
}

void MainWindow::onLog(const QString &msg)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    m_logLines.append({ts, msg});
    while (m_logLines.size() > 2000)
        m_logLines.removeFirst();

    m_logView->append(logHtml(ts, msg));

    auto *sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void MainWindow::onError(const QString &err)
{
    onLog(QStringLiteral("[!] 错误: %1").arg(err));
    QMessageBox::critical(this, QStringLiteral("错误"), err);
    setUiEnabled(true);
    m_fetchBtn->setEnabled(true);
    m_downloadBtn->setEnabled(true);
    m_accountLoginBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressLabel->setText(QStringLiteral("就绪"));
}

void MainWindow::onAllFinished()
{
    setUiEnabled(true);
    m_fetchBtn->setEnabled(true);
    m_downloadBtn->setEnabled(true);
    m_accountLoginBtn->setEnabled(true);
    m_abortBtn->setEnabled(false);
    m_progressLabel->setText(QStringLiteral("完成 ✓"));
}

// ────────────────────────────────────────────
//  辅助
// ────────────────────────────────────────────
void MainWindow::setUiEnabled(bool enabled)
{
    m_filterEdit->setEnabled(enabled);
    m_subjectList->setEnabled(enabled);
    m_selectAllCb->setEnabled(enabled);
    m_invertBtn->setEnabled(enabled);
    m_formatCombo->setEnabled(enabled);
    m_saveDirEdit->setEnabled(enabled);
    m_browseDirBtn->setEnabled(enabled);
}
