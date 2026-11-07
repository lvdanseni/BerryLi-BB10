#include "applicationui.hpp"
#include "app_version.hpp"     // build.sh 从 bar-descriptor.xml 自动生成
#include "bili_api.hpp"
#include "note_model.hpp"
#include "note_store.hpp"
#include "collect_store.hpp"
#include "history_store.hpp"
#include "follow_store.hpp"
#include "local_db.hpp"
#include "video_store.hpp"

#include <bb/cascades/Application>
#include <bb/cascades/QmlDocument>
#include <bb/cascades/AbstractPane>
#include <bb/cascades/Page>
#include <bb/cascades/NavigationPane>
#include <bb/cascades/Container>
#include <bb/cascades/Label>
#include <bb/cascades/Color>
#include <QtDeclarative/QDeclarativeError>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QDateTime>
#include <QCoreApplication>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

using namespace bb::cascades;

/**
 * 启动面包屑日志：只写应用自己的沙箱（tmp / 沙箱根）。
 *
 * 重要：不要写 /accounts/1000/shared —— 那需要 access_shared 权限，BB10 会在首次
 * 启动时弹出权限授权窗口，而 devMode 未签名包在授权后进程会被重启、重启再申请，
 * 形成"弹权限窗 → 闪退"的循环。诊断信息宁可难取，也不能妨碍应用启动。
 */
static void bootLog(const QString& s)
{
    const QString line = QDateTime::currentDateTime().toString("hh:mm:ss.zzz") + "  " + s;
    QStringList dirs;
    dirs << QDir::tempPath()   // 应用私有 tmp
         << QDir::homePath();  // 沙箱根 /accounts/1000/appdata/<pkg>
    for (int i = 0; i < dirs.size(); ++i) {
        if (dirs.at(i).isEmpty()) continue;
        QDir d(dirs.at(i));
        QFile f(d.filePath("xiaohongshu_boot.log"));
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << line << "\n";
            f.close();
            return;
        }
    }
}

// ---- QML 报错捕获 ----------------------------------------------------------
// 设备上取不到 slog2 日志，所以把 Qt 的告警捕获到内存，由 qmlLogText() 暴露给 QML 显示。
// 用途：ComponentDefinition 的 createObject() 返回 null 时（组件加载失败），
// Qt 一定会打出具体的 QML 错误行（哪个文件、第几行、什么错误），
// 这段文字就是唯一可靠的定位依据，比反复猜"哪个元素不被支持"高效得多。
static QString g_qmlLog;
static const int kQmlLogMax = 6000;

static void qmlMsgHandler(QtMsgType type, const char* msg)
{
    Q_UNUSED(type)
    if (!msg) return;
    const QString s = QString::fromUtf8(msg);
    // 只留 QML 相关的行，避免被 BiliApi 的 qDebug 冲掉（那些每秒都有）
    if (s.contains("QDeclarative") || s.contains("Cannot assign")
        || s.contains("Unable to assign") || s.contains("is not a type")
        || s.contains("Component is not ready") || s.contains(".qml")
        || s.contains("Unknown") || s.contains("Unable to create")) {
        g_qmlLog += s + "\n";
        if (g_qmlLog.size() > kQmlLogMax) {
            g_qmlLog = g_qmlLog.right(kQmlLogMax);
        }
    }
    // 同时仍输出到 stderr（= slog2），有 SSH 时照旧可见
    fprintf(stderr, "%s\n", msg);
}

ApplicationUI::ApplicationUI(QObject* parent)
    : QObject(parent)
    , m_noteModel(0)
    , m_noteStore(0)
    , m_collectStore(0)
    , m_historyStore(0)
    , m_followStore(0)
    , m_localDb(0)
    , m_player(0)
    , m_ui(0)
    , m_theme(0)
{
    // 【v47 诊断】最早把 stdout/stderr 重定向到沙箱文件，捕获 libbbplayer 的 "ERROR: ..." 日志。
    // libbbplayer 链接 stdio（fprintf/vfprintf/fputs/fwrite/write），其内部错误经 fd2 输出；
    // BB10 设备上取不到 slog2，dup2 到文件后由 playerLogText() 读出显示，定位流错误真因。
    // 必须在任何 libbbplayer 调用（play）之前完成——它在本 ctor 里只是惰性创建，play 更晚。
    {
        m_playerLogPath = QDir(QDir::homePath()).filePath("bbplayer.log");
        int fd = ::open(m_playerLogPath.toUtf8().constData(),
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            ::dup2(fd, 1);
            ::dup2(fd, 2);
            if (fd > 2) ::close(fd);
        }
    }
    // 必须最早安装：QML 组件加载失败的告警要在 createScene 之前就能被捕获
    qInstallMsgHandler(qmlMsgHandler);
    bootLog("=== ApplicationUI ctor begin ===");

    BiliApi::create(this);
    bootLog("BiliApi created");

    // 本地数据库：先创建，各 Store 可能在构造期就要用到它（例如历史记录的读取）。
    m_localDb = new LocalDb(this);
    bootLog("LocalDb created");

    // 笔记流模型（替代原多 FeedModel，按 Kind 分推荐/发现/搜索/关注/历史）。
    m_noteModel = new NoteModel(NoteModel::Rcmd, this);
    bootLog("NoteModel created");

    // 搜索笔记流模型（Kind=Search），注入 QML 为 searchModel
    m_searchModel = new NoteModel(NoteModel::Search, this);
    bootLog("Search NoteModel created");

    // 笔记详情 + 播放流 Store。
    m_noteStore = new NoteStore(this);
    bootLog("NoteStore created");

    // 收藏 Store（需登录态）。
    m_collectStore = new CollectStore(this);
    bootLog("CollectStore created");

    // 历史 Store（需登录态，持久化到 LocalDb）。
    m_historyStore = new HistoryStore(this);
    bootLog("HistoryStore created");

    // 关注动态 Store（需登录态）。
    m_followStore = new FollowStore(this);
    bootLog("FollowStore created");

    m_player = new VideoPlayer(this);
    bootLog("VideoPlayer created");

    // 播放触发走编译期信号槽，可靠（不依赖 QML .connect()）。
    // videoReadyToPlay 在 note_store 内部拿到 CDN 直链时发射，这里统一转给播放器。
    // ⚠️ 关键：connect 必须在 m_player 创建【之后】。v29~v35 这里顺序写反了——
    // 当时 m_player 还是构造初始值 0（null），Qt 对空接收者 connect 静默失败，
    // videoReadyToPlay 永远到不了 play()，播放页因此永远卡在"正在连接视频…"。
    QObject::connect(m_noteStore, SIGNAL(videoReadyToPlay(QString)),
                     m_player, SLOT(play(QString)));

    // 【v74】总时长：view 接口解析出 data.duration（秒）后直接喂给播放器，
    // 作为 libbbplayer duration 字段为 0 时的兜底。同样是编译期 connect ——
    // 不能走 QML 转发（Qt 4.8 上 C++ 信号 → QML 处理函数会静默不连接）。
    QObject::connect(m_noteStore, SIGNAL(apiDurationReady(int)),
                     m_player, SLOT(setApiDurationSec(int)));

    // QML 里到处在用 ui.du(...)，必须注入，否则每个尺寸绑定都抛 ReferenceError
    m_ui = new UiHelper(this);
    bootLog("UiHelper created");

    m_theme = new QObject(this);
    m_theme->setProperty("bg", QColor("#17181A"));
    m_theme->setProperty("cardBg", QColor("#1F2124"));
    m_theme->setProperty("border", QColor("#2A2C30"));
    m_theme->setProperty("textPrimary", QColor("#F2F3F5"));
    m_theme->setProperty("textSecondary", QColor("#9CA0A6"));
    m_theme->setProperty("textMuted", QColor("#6E7278"));
    m_theme->setProperty("accent", QColor("#FB7299"));
    m_theme->setProperty("accentDark", QColor("#E5497A"));
    m_theme->setProperty("thumbBg", QColor("#26272B"));
    bootLog("theme set");

    createScene();
    bootLog("=== ApplicationUI ctor end ===");
}

ApplicationUI::~ApplicationUI()
{
}

void ApplicationUI::openVideo(const QString& bvid)
{
    // 由 QML 侧监听本信号并 push VideoPage
    emit videoRequested(bvid);
}

QString ApplicationUI::version() const
{
    // APP_VERSION_STRING 由 build.sh 从 bar-descriptor.xml 的
    // <versionNumber> + <buildId> 拼出来，形如 "1.0.0.104"。
    return QString::fromLatin1(APP_VERSION_STRING);
}

QString ApplicationUI::bootLogText() const
{
    QStringList dirs;
    dirs << QDir::tempPath() << QDir::homePath();
    for (int i = 0; i < dirs.size(); ++i) {
        if (dirs.at(i).isEmpty()) continue;
        QFile f(QDir(dirs.at(i)).filePath("xiaohongshu_boot.log"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString all = QString::fromUtf8(f.readAll());
            f.close();
            // 只保留最后 30 行，避免撑爆界面
            QStringList lines = all.split('\n', QString::SkipEmptyParts);
            if (lines.size() > 30) lines = lines.mid(lines.size() - 30);
            return lines.join("\n");
        }
    }
    return QString::fromUtf8("(无启动日志)");
}

QString ApplicationUI::qmlLogText() const
{
    if (g_qmlLog.isEmpty()) {
        return QString::fromUtf8("(未捕获到 QML 错误)");
    }
    return g_qmlLog;
}

QString ApplicationUI::playerLogText() const
{
    if (m_playerLogPath.isEmpty()) return QString::fromUtf8("(无日志路径)");
    QFile f(m_playerLogPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8("(打不开 bbplayer.log)");
    }
    QByteArray all = f.readAll();
    f.close();
    if (all.isEmpty()) return QString::fromUtf8("(bbplayer.log 为空)");
    // 只取尾部，避免文件很大时卡。放宽到 32KB：QML 报错会把日志刷得很大，
    // 真正的库错误往往被推到很后面（v47 就是因为只取 4KB + 尾部 8 行，被 ReferenceError 淹了）。
    if (all.size() > 32768) all = all.right(32768);
    QString s = QString::fromUtf8(all);
    QStringList lines = s.split('\n', QString::SkipEmptyParts);

    // v48：先剔除 QML 噪声，否则成片的
    //   asset:///VideoPage.qml:189: ReferenceError: Can't find variable: ui
    // 会把 libbbplayer / FFmpeg 的真实输出彻底盖住。
    QStringList clean;
    for (int i = 0; i < lines.size(); ++i) {
        QString l = lines.at(i).trimmed();
        if (l.isEmpty()) continue;
        if (l.contains("asset:///") || l.contains("ReferenceError")
            || l.contains("TypeError") || l.contains("is not a type")
            || l.contains("Cannot assign to non-existent property")
            || l.contains("Image loading aborted") || l.contains("Unsupported scheme")
            || l.startsWith("qml:") || l.startsWith("BiliClient")) {
            continue;
        }
        clean << l;
    }
    if (clean.isEmpty()) return QString::fromUtf8("(无库日志)");

    // 优先保留含错误/播放关键字的行（libbbplayer 与 FFmpeg 的标记），否则退回尾部几行。
    QStringList keep;
    for (int i = 0; i < clean.size(); ++i) {
        const QString& l = clean.at(i);
        if (l.contains("ERROR") || l.contains("error") || l.contains("fail")
            || l.contains("mux") || l.contains("open") || l.contains("http")
            || l.contains("scheme") || l.contains("codec") || l.contains("demux")
            || l.contains("av_") || l.contains("segment")) {
            keep << l;
        }
    }
    if (keep.isEmpty()) {
        int from = clean.size() - 5; if (from < 0) from = 0;
        for (int i = from; i < clean.size(); ++i) keep << clean.at(i);
    }
    // 最多 4 行，避免撑爆叠加层
    if (keep.size() > 4) keep = keep.mid(keep.size() - 4);
    QString out = keep.join("\n");
    if (out.size() > 400) out = out.right(400);
    return out;
}

QString ApplicationUI::proxyEnvDiag() const
{
    const char* np = ::getenv("no_proxy");
    const char* hp = ::getenv("http_proxy");
    const char* ap = ::getenv("all_proxy");
    return QString("np=%1 hp=%2 ap=%3")
        .arg(np ? QString::fromLatin1(np) : QString("(null)"))
        .arg(hp ? QString::fromLatin1(hp) : QString("(null)"))
        .arg(ap ? QString::fromLatin1(ap) : QString("(null)"));
}

QString ApplicationUI::diagText() const
{
    QStringList out;
    out << "net: " + BiliApi::instance()->diagResult();
    out << "biz: " + BiliApi::instance()->bizDiag();
    if (m_noteModel)    out << "noteModel: " + m_noteModel->diagLast();
    if (m_noteStore)    out << "noteStore: " + m_noteStore->diagLast();
    if (m_collectStore) out << "collectStore: " + m_collectStore->diagLast();
    if (m_historyStore) out << "historyStore: " + m_historyStore->diagLast();
    if (m_followStore)  out << "followStore: " + m_followStore->diagLast();

    // 【v96】封面缓存实况。排查"封面只显示一部分"时，这一行最关键：
    //   files 少 + bad=0  → 根本没下下来（网络/队列问题）
    //   bad 不为 0        → 缓存里躺着一堆 HTML 劫持页，解码必然失败
    //   files 多但界面还空 → QML 绑定/刷新问题，不是下载问题
    {
        QDir imgDir(QDir::homePath() + "/images");
        int total = 0, bad = 0;
        qint64 bytes = 0;
        if (imgDir.exists()) {
            const QStringList fs = imgDir.entryList(QStringList() << "*.jpg", QDir::Files);
            total = fs.size();
            for (int i = 0; i < fs.size(); ++i) {
                const QString p = imgDir.absoluteFilePath(fs.at(i));
                bytes += QFileInfo(p).size();
                if (!BiliApi::isImageFile(p)) bad++;
            }
        }
        out << QString("cover: %1 files, %2 bad, %3 KB")
                   .arg(total).arg(bad).arg(bytes / 1024);
    }
    return out.join("\n");
}

void ApplicationUI::createScene()
{
    QmlDocument* qml = QmlDocument::create("asset:///main.qml").parent(this);
    if (!qml) {
        bootLog("QmlDocument::create FAILED");
        qDebug() << "Xiaohongshu: QmlDocument::create failed";
        setFallbackScene("QML DOC LOAD FAILED: asset:///main.qml");
        return;
    }
    if (qml->hasErrors()) {
        QString detail;
        QList<QDeclarativeError> errs = qml->errors();
        qDebug() << "Xiaohongshu: QML errors:" << errs.size();
        for (int i = 0; i < errs.size(); ++i) {
            const QDeclarativeError& e = errs.at(i);
            qDebug() << "  " << e.url().toString() << "line" << e.line() << "col" << e.column()
                     << e.description();
            if (i < 5) {
                detail += QString::number(e.line()) + ":" + QString::number(e.column()) + " " +
                          e.description() + "\n";
            }
        }
        bootLog("QML PARSE ERROR: " + detail);
        setFallbackScene("QML PARSE ERROR (main.qml):\n" + detail);
        return;
    }
    qml->setContextProperty("theme", m_theme);
    qml->setContextProperty("noteModel", m_noteModel);
    qml->setContextProperty("searchModel", m_searchModel);
    qml->setContextProperty("noteStore", m_noteStore);
    qml->setContextProperty("collectStore", m_collectStore);
    qml->setContextProperty("historyStore", m_historyStore);
    qml->setContextProperty("followStore", m_followStore);
    qml->setContextProperty("localDb", m_localDb);
    qml->setContextProperty("videoStore", m_noteStore);
    qml->setContextProperty("videoPlayer", m_player);
    qml->setContextProperty("ui", m_ui);
    // 登录态/扫码状态全部是 Q_PROPERTY，QML 侧直接读属性绑定即可。
    // 不要把 BiliApi 的信号 connect 到 QML 函数上 —— Qt 4.8 上会静默不连接。
    qml->setContextProperty("biliApi", BiliApi::instance());
    qml->setContextProperty("app", this);

    AbstractPane* root = qml->createRootObject<AbstractPane>();
    if (!root) {
        bootLog("createRootObject returned NULL");
        qDebug() << "Xiaohongshu: createRootObject returned NULL";
        setFallbackScene("QML ROOT CREATE FAILED (main.qml)");
        return;
    }
    qDebug() << "Xiaohongshu: root created OK, scene set";
    Application::instance()->setScene(root);
    bootLog("scene set OK");

    // 【v70】播放页没有 TitleBar 了，返回只剩系统边缘手势。那条路径不触发 QML 的
    // back 信号，所以在这里挂一个编译期连接：页面 pop 完成时停播放、断流。
    // qobject_cast 失败（root 不是 NavigationPane）就跳过，不影响启动。
    if (NavigationPane* nav = qobject_cast<NavigationPane*>(root)) {
        QObject::connect(nav, SIGNAL(popTransitionEnded(bb::cascades::Page*)),
                         this, SLOT(onPagePopped(bb::cascades::Page*)));
        bootLog("nav popTransitionEnded connected");
    }

    // 应用退出时清理资源：停止播放器和流式代理，避免 curl 线程 /
    // MediaPlayer / QTcpServer 残留导致进程无法正常结束，下次启动失败。
    QObject::connect(QCoreApplication::instance(), SIGNAL(aboutToQuit()),
                     this, SLOT(cleanup()));

    // 网络层延迟到场景就绪后启动：先让首帧渲染出来，再起 TLS 线程。
    // 这样即使网络层有问题，应用也能正常启动并显示错误，而不是"点图标没反应"。
    QTimer::singleShot(0, this, SLOT(startNetworkDeferred()));
}

void ApplicationUI::startNetworkDeferred()
{
    bootLog("startNetwork begin");
    BiliApi::instance()->startNetwork();
    bootLog("startNetwork done -> " + BiliApi::instance()->diagResult());
}

void ApplicationUI::onPagePopped(bb::cascades::Page* page)
{
    // 只对播放页生效。QML 侧给 VideoPage 的根节点写了 objectName: "VideoPage"，
    // 因为 C++ 拿不到 main.qml 里 createObject 出来的那个引用。
    if (!page || page->objectName() != QLatin1String("VideoPage")) {
        return;
    }
    bootLog("video page popped -> stop playback");
    if (m_player)     m_player->stop();
    if (m_noteStore) m_noteStore->stopStream();
}

void ApplicationUI::cleanup()
{
    bootLog("cleanup: begin");
    if (m_player) {
        m_player->stop();
        bootLog("cleanup: videoPlayer stopped");
    }
    if (m_noteStore) {
        m_noteStore->stopStream();
        bootLog("cleanup: noteStore stream stopped");
    }
    bootLog("cleanup: end");
}

void ApplicationUI::setFallbackScene(const QString& message)
{
    // 兜底页：任何 QML 失败都不再黑屏，直接显示错误信息
    Page* page = new Page();
    Container* root = new Container();
    root->setHorizontalAlignment(HorizontalAlignment::Fill);
    root->setVerticalAlignment(VerticalAlignment::Fill);
    root->setTopPadding(32);
    root->setLeftPadding(16);
    root->setRightPadding(16);
    Label* label = new Label();
    label->setText(message);
    label->setMultiline(true);
    label->textStyle()->setColor(Color::fromRGBA(0xF2, 0xF3, 0xF5, 0xFF));
    root->add(label);
    page->setContent(root);
    Application::instance()->setScene(page);
}
