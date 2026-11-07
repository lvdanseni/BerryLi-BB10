#ifndef APPLICATIONUI_HPP
#define APPLICATIONUI_HPP

#include <QObject>

namespace bb { namespace cascades { class Page; } }

class NoteModel;
class NoteStore;
class CollectStore;
class HistoryStore;
class FollowStore;
class LocalDb;
class VideoPlayer;

/**
 * QML 侧 `ui.du(...)` 的实现。
 *
 * 【为什么需要它】Cascades QML 里所有尺寸属性（preferredWidth / padding / margin …）
 * 本来就以 du（design unit）为单位，框架自己会按屏幕换算，所以 du(n) 就应该返回 n。
 *
 * 但工程里所有 QML 都写了 `ui.du(...)`，而 `ui` 从来没有被定义过（既没有 QML 单例，
 * 也没有 setContextProperty）。结果是每个 `ui.du(...)` 都在抛
 *   ReferenceError: Can't find variable: ui
 * 属性绑定因此得到 undefined，静默塌陷成 0 或"按内容自适应"——
 * 这正是详情页播放区只有一条细缝、文字贴着屏幕左边缘、评论列表高度不对的总根因。
 *
 * 之前把它误判成 ListItemComponent 的作用域问题（v15），其实两者无关。
 */
class UiHelper : public QObject {
    Q_OBJECT

public:
    explicit UiHelper(QObject* parent = 0) : QObject(parent) {}

    Q_INVOKABLE double du(double value) const { return value; }
};


/**
 * 应用入口：创建主题、各笔记模型/Store，注入 QML 上下文并加载 main.qml。
 * 对应 BBTube 的 applicationui（C++ 核心装配 + QML 场景）。
 *
 * 【从 BerryLi 到 Xiaohongshu 的主体变更】
 *   - FeedModel*（rcmd/popular/ranking/region/search/follow/history） → NoteModel*
 *     统一笔记流模型，按 Kind 区分推荐/发现/搜索/关注/历史。
 *   - FavStore* / DownloadStore* → CollectStore* / HistoryStore*
 *     收藏与历史改为独立 Store，由 LocalDb 持久化。
 *   - 新增 LocalDb*（SQLite 本地数据库 xiaohongshu.db，沙箱根目录）。
 *   - 新增 NoteStore*（笔记详情 + 播放流）。
 */
class ApplicationUI : public QObject {
    Q_OBJECT

    /**
     * 【v90】版本号给 QML 用（设置页显示 "Xiaohongshu 1.0.0.N"）。
     *
     * 为什么不用 setContextProperty("appVersion", QString)：
     * BB10 的 QmlDocument::setContextProperty 只有 QObject* 一个重载，
     * 传 QVariant/QString 直接编译失败（no matching function）。
     * 而 documentContext()->setContextProperty(...) 用的是另一个 context 对象，
     * 与 createRootObject 实际使用的那个是否一致无法确认 —— 不确定的路径不用。
     * 做成 Q_PROPERTY 最稳：`app` 这个 context property 早就在了，
     * 属性绑定在 QML 引擎内部驱动，是本工程唯一验证过 100% 可靠的 C++→QML 通道。
     *
     * 值来自 build.sh 生成的 app_version.hpp，源头是 bar-descriptor.xml，
     * 所以显示的和安装包里的是同一个号。
     */
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit ApplicationUI(QObject* parent = 0);
    virtual ~ApplicationUI();

    QString version() const;

    Q_INVOKABLE void openVideo(const QString& bvid);

    /**
     * 返回启动面包屑日志内容（沙箱 xiaohongshu_boot.log）。
     * 设置页直接展示，方便在设备上肉眼看到启动卡在哪一步，无需取文件。
     */
    Q_INVOKABLE QString bootLogText() const;

    /** 网络层 + 各模型诊断汇总（curl 自检 / 请求计数 / connect 结果 / 最后一次回复）。 */
    Q_INVOKABLE QString diagText() const;

    /**
     * 返回进程内捕获到的 QML 告警/报错文本。
     * 用途：ComponentDefinition 的 createObject() 返回 null 时，QML 文件里
     * 到底是哪一行、哪个元素不被支持，全在这段文字里。
     */
    Q_INVOKABLE QString qmlLogText() const;

    /**
     * 返回 libbbplayer 的 stderr 日志尾部（含 "ERROR: ..." 等真实错误）。
     * 【为什么需要】v46 真机确认 raw=0/0、state=5（=库内部 ERROR），但错误原文只写进
     * stderr/slog2，设备上取不到。构造期已把 fd1/fd2 dup2 到沙箱 bbplayer.log，
     * libbbplayer 链接了 stdio（fprintf/vfprintf/write），其错误日志因此可被读到并显示。
     */
    Q_INVOKABLE QString playerLogText() const;

    /**
     * 代理相关环境变量的当前值（no_proxy / http_proxy / all_proxy）。
     * 【用途】v50 在 main() 里设了 no_proxy=* 来绕开 libbbnet 写死的默认代理，
     * 这个接口把实际生效的值显示到播放页叠加层，一眼确认修复是否真的落地
     * （若显示 np=(null) 说明 setenv 没生效，就得换别的路子）。
     */
    Q_INVOKABLE QString proxyEnvDiag() const;

signals:
    void videoRequested(const QString& bvid);

private slots:
    /** 场景就绪后延迟启动网络层（QTimer::singleShot(0) 触发），保证首帧先渲染。 */
    void startNetworkDeferred();
    /** 应用退出时清理资源：停止播放器、流式代理，避免进程残留导致下次启动失败。 */
    void cleanup();

    /**
     * 【v70】NavigationPane 弹出页面完成时调用。
     *
     * 为什么需要它：播放页现在照 BBTube 去掉了 TitleBar，返回只剩 BB10 的
     * 系统边缘手势。那条路径**不会**触发 QML 里的 back 信号，所以必须在这里
     * 停掉播放、释放 CDN 连接，否则退出播放页后视频还在后台播、流也不断。
     *
     * 用 C++ 侧的编译期 connect（不是 QML 的 .connect()）—— 后者对 C++ 信号
     * 在本工程不可靠（见项目铁律 4）。
     */
    void onPagePopped(bb::cascades::Page* page);

private:
    void createScene();
    void setFallbackScene(const QString& message);

    /** 笔记流模型（替代原多 FeedModel，按 Kind 分推荐/发现/搜索/关注/历史）。 */
    NoteModel* m_noteModel;
    /** 搜索笔记流模型（Kind=Search）。 */
    NoteModel* m_searchModel;
    /** 笔记详情 + 播放流 Store。 */
    NoteStore* m_noteStore;
    /** 收藏夹 + 笔记收藏 Store（需登录态）。 */
    CollectStore* m_collectStore;
    /** 观看历史 Store（需登录态，持久化到 LocalDb）。 */
    HistoryStore* m_historyStore;
    /** 关注动态 Store（需登录态）。 */
    FollowStore* m_followStore;
    /** SQLite 本地数据库（xiaohongshu.db，沙箱根目录）。 */
    LocalDb* m_localDb;
    VideoPlayer* m_player;
    UiHelper* m_ui;
    QObject* m_theme;
    /** 构造期 dup2 的 stderr/stdout 日志文件路径（沙箱 bbplayer.log）。 */
    QString m_playerLogPath;
};

#endif // APPLICATIONUI_HPP
