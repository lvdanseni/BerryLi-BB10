#ifndef VIDEO_STORE_HPP
#define VIDEO_STORE_HPP

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

#include <bb/cascades/ArrayDataModel>

class QTimer;
class QTcpServer;
class QTcpSocket;
class ProxyStreamWorker;
class BbPlayer;
class FeedModel;

/**
 * 本地流式代理 —— 解决"必须整段缓存到本地才能播"的问题。
 *
 * 【为什么需要它】bilibili 的 mp4 直链要校验 UA/Referer，而
 * bb::multimedia::MediaPlayer（QNX mmrenderer 的 http 客户端）不能设置请求头，
 * 直连必然 403。之前的做法是先用 libcurl 把整段下到沙盒再播 —— 于是每次都要等，
 * 而且白占几十 MB 存储。
 *
 * 【做法】在 127.0.0.1 上起一个极简 HTTP 服务，播放器去请求
 * http://127.0.0.1:<port>/v.mp4，代理再用 libcurl 去 CDN 拉数据（带上 UA/Referer），
 * 边拉边往 socket 里写。播放器拿到的是标准 HTTP 流，支持 Range（拖动进度条）。
 *
 * 【线程模型】
 *   - curl 在 ProxyStreamWorker（QThread）里跑，只往一个互斥保护的缓冲区里塞数据；
 *   - QTcpSocket 只在主线程里碰，靠一个 20ms 的 QTimer 把缓冲区里的数据泵出去。
 *   两边不共享 socket，也不用跨线程信号，避免 BB10 上那套信号投递的坑。
 */
class VideoProxy : public QObject {
    Q_OBJECT

public:
    explicit VideoProxy(QObject* parent = 0);
    ~VideoProxy();

    /** 启动代理并返回可直接交给播放器的本地 URL；失败返回空字符串。 */
    Q_INVOKABLE QString start(const QString& sourceUrl, qint64 totalSize);
    Q_INVOKABLE void stop();

signals:
    /**
     * 流式拉取失败（CDN 拒绝 / 连接断了），且一个字节都没送出去过。
     * 上层收到后应退回"整段下载再播"，用户只是慢一点，不会看到错误。
     */
    void streamFailed(const QString& reason);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onPump();
    void onClientGone();

private:
    void handleRequest(const QByteArray& head);
    void abortStream();
    void dropClient();

    QTcpServer* m_server;
    QTcpSocket* m_client;
    QTimer* m_pump;
    ProxyStreamWorker* m_worker;
    QString m_sourceUrl;
    qint64 m_total;
    qint64 m_expectedLen;   // 本次响应承诺的字节数（-1 表示未知）
    qint64 m_sentBytes;     // 已写进 socket 的正文字节数
    bool m_headersSent;
    bool m_reported;      // streamFailed 只发一次
    QByteArray m_requestBuf;
};

/**
 * 视频播放器封装（C++ 版）—— 后端 = BBTube 自带的 libbbplayer.so（OpenMAX AL +
 * 静态 FFmpeg + QNX 视频缓冲队列），通过 bbplayer_wrapper 以 dlopen/dlsym 调用。
 *
 * 为什么这么做：BB10 系统自带的 bb::multimedia::MediaPlayer（mmrenderer）无法给 HTTP
 * 请求加 UA/Referer，连不上 loopback 流式代理，B 站 CDN 一律 403 —— v29~v34 全部失败。
 * 逆向 BBTube 后发现它根本不用 mmrenderer，而是用 libbbplayer 直接吃 CDN 直链、
 * 字节逐块喂解码器、全程不落盘。我们直接复用同一个库。
 *
 * 播放画面仍靠 QML 的 ForeignWindowControl 承载：libbbplayer 在 set_window 后创建屏幕
 * 窗口（windowId 由我们指定、groupId 用应用主窗口组），ForeignWindowControl 用同一个
 * windowId 认领它，视频就"长"在界面里。这一层是 bb.cascades 自带类型，不涉及额外模块。
 */
class VideoPlayer : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString windowId READ windowId CONSTANT)
    /** 视频窗口所在的窗口组（= 应用主窗口组）。libbbplayer 在 set_window 后用它把屏幕窗口
     *  join 进同一组，ForeignWindowControl 必须认领同一组才能出画面。把它暴露给 QML 并显式
     *  设到 FWC 的 windowGroup，保证两侧用【完全相同】的组字符串（绕开 FWC 默认组的歧义）。 */
    Q_PROPERTY(QString groupId READ groupId CONSTANT)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    /** 视频宽高比（w/h）。0 表示还没拿到，QML 侧按 16:9 兜底。
     *  用途：把播放区高度按视频真实比例算，避免竖屏/方形视频被拉伸。 */
    Q_PROPERTY(double videoAspect READ videoAspect NOTIFY videoAspectChanged)
    /** 播放位置/总时长（毫秒）。未开始播放时为 0。 */
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY durationChanged)
    /** 播放倍速（1.0 = 正常）。0.5/1/1.5/2/3 五档。 */
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    /**
     * 【v85】控件层（进度条 + 暂停按钮）是否显示。
     *
     * 为什么要放到 C++：QML 里没有 Timer（Cascades 1.1 不带），做不了"显示 4 秒后
     * 自动隐藏"。而 C++ 的信号又不能可靠地 connect 到 QML 函数（铁律 4），
     * 所以干脆把状态和定时器都放在 C++，QML 只做属性绑定。
     */
    Q_PROPERTY(bool ctrlShown READ ctrlShown NOTIFY ctrlShownChanged)
    /**
     * 【v86】长按左/右半屏的"3 倍速快进/后退"。
     *
     * 【为什么是"跳"而不是真倍速】libbbplayer.so 的导出符号表里**没有任何
     * rate/speed 接口**（全列：create/destroy/set_window/net_config/
     * set_video_enabled/open_url/open_file/open_segments/open_source/play/pause/
     * close/seek/position/duration/state/tags_x/waveform/version），
     * 字符串表里也没有 PlaybackRate / XAPlaybackRateItf 的痕迹。
     * 唯一能动播放位置的就是 bbplayer_seek。
     *
     * 所以这里的做法是：按住期间按 3 倍速**连续 seek**（目标位置由
     * "按下时的位置 + 3 × 按住时长"实时算出，不做累加，避免误差滚雪球）。
     * 观感上是画面一段段往前/往后跳，不是平滑 3 倍速 —— 但这是这个播放后端
     * 能做到的最接近的效果。
     */
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    /** -1 = 后退中，+1 = 快进中，0 = 没在扫。 */
    Q_PROPERTY(int scanDir READ scanDir NOTIFY scanningChanged)
    /** 原始 position/duration ticks（bbplayer_position/duration 直接返回的 64 位值，零缩放）。
     *  用途：调试期定死时间单位。BB10 的 libbbplayer 不暴露单位，rodata 也无标注，
     *  pos=0/dur=0 既可能是"原始=毫秒被 /1000 截断"，也可能是"真冻结=0"。叠加层同时打印
     *  原始值与两种解读，一张截图即可判定。NOTIFY 复用 pollSeqChanged 强制刷新。 */
    Q_PROPERTY(qint64 rawPosition READ rawPosition NOTIFY pollSeqChanged)
    Q_PROPERTY(qint64 rawDuration READ rawDuration NOTIFY pollSeqChanged)
    Q_PROPERTY(int rawState READ rawState NOTIFY pollSeqChanged)
    /** 每轮轮询 +1 的序列号：拖绑定强制重算（否则 pos 恒 0 时绑定永不重绘，raw 也刷不出）。 */
    Q_PROPERTY(int pollSeq READ pollSeq NOTIFY pollSeqChanged)
    /** libbbplayer 事件回调累积日志（type=1 状态变更 / 7 错误 / 8 日志）。
     *  v48 起 bbplayer_create 传了真回调，库内部的错误原文会走这里出来；
     *  NOTIFY 复用 pollSeqChanged 强制刷新。 */
    Q_PROPERTY(QString eventLog READ eventLog NOTIFY pollSeqChanged)
    /** 最近一次 type=7 错误（错误码 + 原文）。空串 = 从未收到错误事件。 */
    Q_PROPERTY(QString playerError READ playerError NOTIFY pollSeqChanged)
    /** 回调里收到的最新状态值（type=1 的 a）。-1 = 还没收到过。 */
    Q_PROPERTY(int cbState READ cbState NOTIFY pollSeqChanged)
    /** 回调被调用的总次数。0 = 回调根本没生效（create 参数错/库未加载）。 */
    Q_PROPERTY(int eventCount READ eventCount NOTIFY pollSeqChanged)
    /** CA 根证书装载状态（v51）：空 = 还没建播放器；"ok <path>" = 装载成功；
     *  "fail ..." = 失败（https 必挂，见 bbplayer_wrapper.hpp 的说明）。 */
    Q_PROPERTY(QString caStatus READ caStatus NOTIFY pollSeqChanged)

public:
    explicit VideoPlayer(QObject* parent = 0);
    ~VideoPlayer();

    /**
     * 提前创建后端句柄（不开始播放、也**不建窗口** —— 窗口要等 openUrl 才建）。
     *
     * 【v63】界面已经不再调用它。原来在 VideoPage 注入播放面时同步调它，是为了
     * "让屏幕窗口先存在，FWC 才认领得到"，但那个前提是错的（窗口本来就在 openUrl
     * 时才创建）。代价是把"装 CA + dlopen 两个大 .so + create 句柄"全压在 nav.push()
     * 之前的 UI 线程上，这才是"点进播放页要等好几秒"的真因。
     * 现在播放器由 play() 内部的 ensurePlayer() 在页面显示之后才创建。
     * 保留本方法只为调试/特殊场景需要提前建句柄时使用，正常流程不应调用。
     */
    Q_INVOKABLE void preparePlayer();

    /**
     * v61：屏幕窗口探针（诊断用）。
     *
     * 直接向 libscreen 查询"本进程的 application context 里到底有哪些窗口/窗口组"，
     * 逐个打印 ID_STRING / GROUP / VISIBLE / ZORDER / RENDER_BUFFER_COUNT / SIZE。
     *
     * 为什么需要它：v57~v60 连续四轮，QML 侧 FWC 的 windowId/windowGroup 与
     * videoPlayer.windowId/groupId **逐字符完全一致**，boundToWindow 依然恒为 false。
     * QML 侧能试的组合已经试尽（显式组/默认组、空串→真实ID跳变、remove/add 重挂、
     * bindWindow 翻转），继续盲试没有信息增量。必须换到"事实层"回答三个问题：
     *   1) libbbplayer 到底有没有把屏幕窗口建出来？
     *   2) 建出来的窗口 ID_STRING 是不是我们传的那个字符串？
     *   3) 它 join 的是哪个窗口组？是不是 mainWindow()->groupId()？
     * 这三问一旦有答案，fwc=N 的根因就唯一确定了。
     *
     * 【v63】界面上的调试叠加层已移除，本方法目前没有调用方，保留作为窗口类问题的
     * 现场诊断工具（必要时在 QML 里 `console.log(videoPlayer.screenProbe())` 即可）。
     * 内部有 1 秒节流缓存，重复调用不会真的反复问 screen。
     */
    Q_INVOKABLE QString screenProbe();

    QString windowId() const { return m_windowId; }
    QString groupId() const { return m_groupId; }
    QString stateText() const { return m_state; }
    double videoAspect() const { return m_aspect; }
    int positionMs() const;
    int durationMs() const;
    double speed() const { return m_speed; }
    bool playing() const { return m_playing; }
    bool ctrlShown() const { return m_ctrlShown; }
    bool scanning() const { return m_scanDir != 0; }
    int scanDir() const { return m_scanDir; }
    qint64 rawPosition() const { return m_rawPos; }
    qint64 rawDuration() const { return m_rawDur; }
    int rawState() const { return m_rawState; }
    int pollSeq() const { return m_pollSeq; }
    QString eventLog() const;
    QString playerError() const;
    int cbState() const;
    int eventCount() const;
    QString caStatus() const;

public slots:
    /**
     * 播放视频：直接把 B 站 CDN 直链交给 libbbplayer 流式播放（不落盘）。
     * 首次调用时才创建后端句柄并配置窗口/网络头，避免拖慢首帧。
     * 用 public slots（而非 Q_INVOKABLE）：这样 applicationui 里
     * connect(videoReadyToPlay, play) 的老式 SIGNAL/SLOT 字符串连接才能可靠匹配。
     * slot 同样可从 QML 直接调用（seekBy/togglePlay/setSpeed 同理）。
     */
    void play(const QString& url);
    void pause();
    void stop();
    /** 播放/暂停切换（界面按钮用）。 */
    void togglePlay();
    /** 相对拖动，单位毫秒（负数后退）。会自动夹在 [0, duration] 内。 */
    void seekBy(int deltaMs);
    /**
     * 【v85】绝对拖动到 ms。进度条抬起时调用。
     *
     * 相比 seekBy 的两个改进：
     *   1) 直接给目标位置，不再依赖"此刻的 positionMs"算差值 ——
     *      原来那个差值是在手指抬起那一刻算的，而 positionMs 可能刚被轮询改过，
     *      差值就偏了，表现是"拖到 50% 却跳到了别处"。
     *   2) 拖完之后开一个 1.5 秒的保护窗口（见 onPoll），期间不采信播放器回报的
     *      位置，避免进度条被旧位置"拽回去"（用户说的"回弹"）。
     */
    void seekTo(int ms);
    /** 【v85】点画面切换控件层显隐（播放中 4 秒后自动隐藏）。 */
    void toggleControls();
    void showControls();
    void hideControls();
    /**
     * 【v86】开始长按扫动。dir = +1 快进，-1 后退，0 等价于 stopScan()。
     *
     * 用 public slots（不用 Q_INVOKABLE）：QML 里 startScan(1) 这种直接调用对
     * slot 和 Q_INVOKABLE 都有效，但 slot 在 Qt 4.8 的元对象里更稳（老式
     * SIGNAL/SLOT 也能连），出问题时排查手段更多。
     */
    void startScan(int dir);
    /** 【v86】结束扫动（手指抬起/滑出/播放页退出都要调，否则会一直跳）。 */
    void stopScan();
    /**
     * 【v86】按下。x = 按下点的局部横坐标，w = 触摸层宽度（用来判左/右半屏）。
     *
     * 长按判定放在 C++ 是因为 **Cascades 1.1 没有 Timer 类型**，QML 侧写不出
     * "按住 500ms 不放"这种延时逻辑；而 C++ 信号连到 QML 函数在 Qt 4.8 上又
     * 静默不连接（铁律 4）。所以这里把「按下 → 500ms 计时 → 判定左/右 → 起扫」
     * 整条链路都收在 C++，QML 只负责转交三个触摸事件。
     */
    void pressAt(double x, double w);
    /** 【v86】移动超过阈值 → 取消这次长按（用户在滑页面，不是要扫动）。 */
    void cancelPress();
    /**
     * 【v86】松手。**返回 true 表示这次是长按**（已触发过扫动），QML 据此决定
     * 是否还要把它当成"点一下切控件"处理 —— 长按松手再切控件会很突兀。
     */
    bool releasePress();
    /** 设置播放倍速（0.5 / 1 / 1.5 / 2 / 3）。调用后立即生效。 */
    void setSpeed(double speed);
    /**
     * 【v74】接收 B 站 view 接口给的视频总时长（**秒**）。
     *
     * 为什么需要它：libbbplayer 的 duration 字段在流式播放时可能是 0（见成员区注释），
     * 而"有没有时长"直接决定进度条和控制按钮显不显示。view 接口的 data.duration
     * 是权威值，用它兜底最稳。
     *
     * 用 public slots 而非 Q_INVOKABLE：applicationui 里要靠老式
     * SIGNAL/SLOT 字符串连接把 VideoStore::apiDurationReady 接进来。
     */
    void setApiDurationSec(int sec);

signals:
    void stateChanged(const QString& text);
    void videoAspectChanged();
    void positionChanged();
    void durationChanged();
    void playingChanged();
    /** 【v85】控件层显隐变化。 */
    void ctrlShownChanged();
    /** 【v86】扫动开始/结束/方向变化。 */
    void scanningChanged();
    /** 倍速变更，供 QML 按钮状态同步。 */
    void speedChanged();
    /** 每轮轮询触发，拖 dbg 叠加层/时间标签强制重算（见 rawPosition 等属性的说明）。 */
    void pollSeqChanged();

private slots:
    /** 定时轮询 libbbplayer 的 position/duration（mmrenderer 的信号在 BB10 上不可靠，改为轮询）。 */
    void onPoll();
    /** 【v86】扫动节拍：每 600ms 按 3 倍速跳一次。 */
    void onScanTick();
    /** 【v86】长按计时到点：按按下点判左/右，起扫。 */
    void onPressHold();

private:
    void ensurePlayer();
    void startPoll();
    void stopPoll();
    void setState(const QString& s);
    /** v55：https 因证书问题失败时，把 scheme 换成 http 重试一次。
     *  目的有二：(1) 立刻验证"除证书外整条链路是否都通"（能出画面就说明
     *  UA/Referer/解码/窗口认领全对）；(2) 给用户一个能看的降级路径。 */
    void tryHttpFallback();

    BbPlayer* m_player;
    /** 后端句柄 + 屏幕窗口是否已经建好。stop() 会把窗口拆掉并复位此标志，
     *  下一次 ensurePlayer() 会重建窗口（同 windowId），保证换片后 FWC 仍能认领。 */
    bool m_initialized;
    QString m_windowId;
    QString m_groupId;
    QString m_state;
    double m_aspect;
    int m_position;
    int m_duration;
    double m_speed;
    bool m_playing;
    /** 【v85】控件层（进度条/暂停按钮）显隐。默认显示，播放后 4 秒自动隐藏。 */
    bool m_ctrlShown;
    QTimer* m_hideTimer;
    /** 【v85】seek 保护窗口。seek 之后解码器要重开，这段时间 positionTicks()
     *  读到的还是旧位置，照常写回就会让进度条"回弹"。isValid()=false 表示还没 seek 过。 */
    QElapsedTimer m_seekClock;
    /* ================= 【v86】长按扫动（左后退 / 右快进） =================
     *
     * 后端没有 rate 接口，只能"连续 seek"。关键设计：目标位置由
     *   m_scanBaseMs + dir * elapsed * 3
     * 实时算出，**绝不做累加** —— 每次 seek 都有几十毫秒误差，累加 10 次就偏出
     * 好几秒，按住久了会明显跑飞。固定基准点则误差恒定在最后一次 tick 之内。
     */
    /** -1 后退 / +1 快进 / 0 空闲。 */
    int m_scanDir;
    /** 按下那一刻的播放位置（毫秒）。整个扫动期间不变。 */
    int m_scanBaseMs;
    QElapsedTimer m_scanClock;
    /** 600ms 一跳（3 倍速 = 每跳 1.8 秒，观感比 1 秒一跳顺）。惰性创建。 */
    QTimer* m_scanTimer;
    /* ---- 长按判定（QML 没有 Timer，只能在这里做） ---- */
    /** 500ms 单次定时器。 */
    QTimer* m_pressTimer;
    /** 按下点横坐标 / 触摸层宽度（判左半屏还是右半屏）。 */
    double m_pressX;
    double m_pressW;
    /** 手指当前是否按着。 */
    bool m_pressHeld;
    /** 这次按下是否已经升级成长按（松手时要靠它区分"点一下"和"长按"）。 */
    bool m_pressFired;
    /* ================= 【v74】时长与进度：不再盲信 libbbplayer =================
     *
     * 真机现象：时间标签恒显示 0:00 / 0:00，进度条不动，连"播放/暂停"按钮都不出现
     * （按钮的 visible 绑在 durationMs > 0 上）。根因有两层：
     *
     *  1) libbbplayer 的 bbplayer_duration 只是 `return *(int64*)(handle + 0x1220)`
     *     （反汇编 0x59314 确认），而流式 HTTP 播放时那个字段可能一直是 0 ——
     *     容器总时长根本没解析出来。这是库的行为，改不了。
     *  2) 就算字段有值，单位也不知道。wrapper 里 TICKS_PER_MS 是**猜的**（1000，
     *     假设微秒）。如果实际是毫秒，播放 10 秒 = 10000 ticks → /1000 = 10ms →
     *     时间标签永远 0:00。这一条能完美解释"进度一直是 0"。
     *
     * 解法是三条独立的保险，任意一条成立界面就正常：
     *   A. 总时长优先用 B 站 view 接口的 data.duration（单位秒，绝对权威），
     *      由 VideoStore 发 apiDurationReady → applicationui 编译期 connect 进来；
     *   B. tick 单位**自校准**：拿 bbplayer_position 的原始值和"墙钟真实经过的毫秒"
     *      求比值，落在 1 / 1000 / 1000000 哪个量级就定哪个。不依赖任何外部数据；
     *   C. 如果连 position 都是恒 0（自校准永远做不成），就退回**墙钟计时**：
     *      从 play() 成功开始算真实经过时间。缓冲会让它略快于画面，但比"永远是 0"
     *      好得多，且 seek 时会把基准重置到目标位置。
     */
    /** view 接口给的总时长（毫秒）。0 = 还不知道。 */
    int m_apiDurMs;
    /** tick 单位自校准结果：0 = 尚未定，1 = 毫秒，1000 = 微秒，1000000 = 纳秒。 */
    int m_ticksPerMs;
    /** 墙钟兜底：播放计时器 + 基准毫秒（seek 时重置到目标位置）。 */
    QElapsedTimer m_clock;
    int m_clockBaseMs;
    bool m_clockOn;
    // 原始 ticks 缓存（onPoll 每轮写入，供调试叠加层读取；零缩放，= bbplayer 直接返回值）
    qint64 m_rawPos;
    qint64 m_rawDur;
    int m_rawState;
    int m_pollSeq;
    QTimer* m_poll;
    /** v55：最近一次 play 的原始 url（http 回退时用它改写 scheme）。 */
    QString m_lastUrl;
    /** v55：https→http 回退是否已试过（只回退一次，避免反复重开句柄）。 */
    bool m_httpFallbackTried;
    /** v55：已经展示过的错误串。lastError() 是累积值不会自动清空，
     *  用它可以避免"回退成功后错误又把状态刷回 error"的死循环。 */
    QString m_lastErrShown;
    /** v61：screenProbe() 的节流缓存（dbgText 每轮轮询都会调用它）。 */
    QString m_probeCache;
    qint64  m_probeStamp;
    /** 【v64】上一次向界面发射进度时显示的"秒"。
     *  轮询仍是 250ms（要及时发现播放结束），但只有跨秒才 emit —— 界面刷新从 4Hz
     *  降到 1Hz，减少时间标签重绘对视频/音频管线的干扰。 */
    int     m_lastEmitSec;
};

/**
 * 视频详情 + 播放流 Store —— 对应 BBTube VideoStore：
 * 拉取 view（标题/UP主/统计/简介/cid）→ playurl（mp4 直链）→ 流式播放（不落盘）→ 评论。
 *
 * 播放为什么现在是"直接把直链交给播放器"：
 * 早期用 bb::multimedia::MediaPlayer（mmrenderer）走 QNX http 客户端，不能设
 * User-Agent / Referer，B 站 CDN 一律 403，v29~v34 全部失败。逆向 BBTube 后发现它
 * 用自带 libbbplayer.so 直接吃 CDN 直链、字节逐块喂解码器、全程不写沙盒。所以这里
 * 拿到 playurl 的直链后直接 emit videoReadyToPlay(url)，由 VideoPlayer 交给 libbbplayer
 * 流式播放；UA/Referer 由 VideoPlayer 通过 bbplayer_net_config 注入。
 */
class VideoStore : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    /** 视频详情（title/statText/ownerName/ownerFace/desc）。QML 直接绑定字段，可靠。 */
    Q_PROPERTY(QVariantMap detail READ detail NOTIFY detailChanged)
    /** 加载/播放失败原因（空字符串表示无错误）。 */
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    /** 下载/缓存进度 0-100（流式代理阶段恒为 0）。 */
    Q_PROPERTY(int dlPercent READ dlPercent NOTIFY dlPercentChanged)
    /** 下载速度/剩余时间文本。 */
    Q_PROPERTY(QString dlSpeed READ dlSpeed NOTIFY dlSpeedChanged)
    /** 评论列表模型（ArrayDataModel），ListView 直接绑定，避免 QML .connect() 不可靠。 */
    Q_PROPERTY(QObject* commentModel READ commentModel CONSTANT)
    /** 评论条数（供标题显示）。 */
    Q_PROPERTY(int commentCount READ commentCount NOTIFY commentCountChanged)
    /**
     * 【v72】相关推荐列表（FeedModel::Related），ListView 直接绑定。
     * 详情加载完成后自动请求 /x/web-interface/archive/related。
     */
    Q_PROPERTY(QObject* relatedModel READ relatedModel CONSTANT)

    // ---- 【v85】互动状态（点赞/投币/收藏/关注）----
    // 全部走 Q_PROPERTY，QML 只做绑定，不 connect 任何 C++ 信号（铁律 4）。
    Q_PROPERTY(bool liked READ liked NOTIFY interactChanged)
    Q_PROPERTY(bool coined READ coined NOTIFY interactChanged)
    Q_PROPERTY(bool favored READ favored NOTIFY interactChanged)
    Q_PROPERTY(bool followed READ followed NOTIFY interactChanged)
    /** 操作反馈（"已点赞" / "投币失败（-111）"），2.5 秒后自动清空。 */
    Q_PROPERTY(QString interactTip READ interactTip NOTIFY interactChanged)
    /**
     * 【v89】下载状态。0 = 没下过，1 = 下载中，2 = 已下载。
     *
     * 为什么用整型而不是 bool：药丸按钮要显示三种文案（"下载"/"下载中 42%"/"已下载"），
     * 两态表达不了。进度本身走已有的 dlPercent。
     */
    Q_PROPERTY(int dlState READ dlState NOTIFY dlStateChanged)

public:
    explicit VideoStore(QObject* parent = 0);

    bool loading() const { return m_loading; }
    QVariantMap detail() const { return m_detail; }
    QString error() const { return m_error; }
    int dlPercent() const { return m_dlPercent; }
    QString dlSpeed() const { return m_dlSpeed; }
    QObject* commentModel() const { return m_commentModel; }
    /**
     * 实现放在 video_store.cpp —— 这里只有 FeedModel 的前向声明，
     * 内联 return 会因为"不知道 FeedModel 继承 QObject"而编译失败
     * （cannot convert 'FeedModel*' to 'QObject*'）。
     */
    QObject* relatedModel() const;
    int commentCount() const { return m_commentCount; }
    bool liked() const { return m_liked; }
    bool coined() const { return m_coinGiven > 0; }
    bool favored() const { return m_favored; }
    bool followed() const { return m_followed; }
    QString interactTip() const { return m_interactTip; }
    int dlState() const { return m_dlState; }

    // ---- 【v85】互动动作。全部是写操作，需要登录 + csrf。 ----
    /** 点赞 / 取消点赞。 */
    Q_INVOKABLE void toggleLike();
    /** 投币（每次 1 枚，同一视频最多 2 枚）。 */
    Q_INVOKABLE void addCoin();
    /** 收藏 / 取消收藏（取消时从所有收藏夹里移除，避免"不知道它在哪个夹"）。 */
    Q_INVOKABLE void toggleFav();
    /** 关注 / 取关当前 UP 主。 */
    Q_INVOKABLE void toggleFollow();
    /** 分享：把视频链接复制到剪贴板。 */
    Q_INVOKABLE void shareVideo();
    /**
     * 【v89】播放页药丸行的"下载"。
     *
     * 和 downloadVideo(url, size) 的区别：这里不传参数 —— 直链和大小在
     * playurl 回来时已经存在 m_streamUrl / m_streamSize 里了，界面不该关心这些。
     * 内部还会处理三种情况：还没拿到直链、已经下过、正在下。
     */
    Q_INVOKABLE void downloadCurrent();

    Q_INVOKABLE void load(const QString& bvid);

    /**
     * 下载 mp4 直链到沙盒（url/size 来自 playurl 的 durl[0]）。
     *
     * 【v90】forPlayback：下载完成后要不要顺手把它塞给播放器。
     *   true  = 流式失败后的降级路径（必须立刻播）；
     *   false = 用户在播放页主动点"下载"（此刻画面正在播，重开播放器会黑一下）。
     */
    Q_INVOKABLE void downloadVideo(const QString& url, qint64 expectedBytes,
                                   bool forPlayback = false);

    /** 是否正在走流式代理（true 表示没有落盘，也没有可回退的本地文件）。 */
    Q_INVOKABLE bool usingProxy() const { return m_usingProxy; }
    /** 关掉流式代理（离开详情页时调用）。 */
    Q_INVOKABLE void stopStream();
    /** 流式播放失败时退回"整段下载再播"。 */
    Q_INVOKABLE void fallbackToDownload();

public slots:
    /**
     * BiliApi::netReply 统一接收端（编译期 signal/slot 连接）。
     * tag 路由：__detail__ / __play__ / __comment__
     */
    void onNetReply(const QString& tag, const QString& json, bool ok);
    /** BiliApi::downloadFinished 接收端。 */
    void onDownloadFinished(const QString& tag, const QString& path, bool ok);
    /** 下载进度轮询（每 500ms 读一次已写字节数）。 */
    void onDownloadTick();
    /** 【v85】操作反馈文案 2.5 秒后自动清空。 */
    void clearTip();
    /** 流式代理失败 → 自动退回整段下载。 */
    void onStreamFailed(const QString& reason);

signals:
    void loadingChanged();
    void detailChanged();
    void errorChanged();
    void dlPercentChanged();
    void dlSpeedChanged();
    void commentCountChanged();
    /** 【v85】互动状态/反馈变化。 */
    void interactChanged();
    /** 【v89】下载状态 0/1/2 变化（进度本身走 dlPercentChanged）。 */
    void dlStateChanged();
    void detailReady(const QVariantMap& detail);
    /** 【v74】view 接口解析出的总时长（秒）。applicationui 用编译期 connect
     *  转给 VideoPlayer::setApiDurationSec，作为 libbbplayer duration 拿不到时的兜底。 */
    void apiDurationReady(int sec);
    void playUrlReady(const QString& playUrl);
    void commentsReady(const QVariantList& comments);
    void loadFailed(const QString& message);
    /** 下载进度 0-100。 */
    void downloadProgress(int percent);
    /** 下载完成，path 为本地 mp4 路径，可直接交给播放器。 */
    void localVideoReady(const QString& path);
    /** 下载失败。 */
    void downloadFailed(const QString& message);
    /** 下载速度/剩余时间文本（"2.4 MB/s · 剩 12s"），供界面显示。 */
    void downloadSpeed(const QString& text);
    /** 流式代理就绪，url 可直接交给播放器（http://127.0.0.1:port/v.mp4）。 */
    void streamReady(const QString& url);
    /** 视频已就绪、可直接播放：proxy 本地 URL 或本地缓存/下载路径。
     *  由 applicationui 用编译期 connect 连到 videoPlayer->play，规避 QML .connect() 不可靠。 */
    void videoReadyToPlay(const QString& url);

private:
    void finishLoading();
    void onDetailReply(const QString& json, bool ok);
    void onPlayReply(const QString& json, bool ok, bool highQuality = false);
    void onCommentReply(const QString& json, bool ok);
    /** 【v85】点赞/投币/收藏/关注/收藏夹目录的统一回复处理。 */
    void onInteractReply(const QString& suffix, const QString& json, bool ok);
    /**
     * 【v89】下载落盘完成：写 sidecar 元数据 + 把 dlState 置为"已下载"。
     * 用槽而不是在各处 emit 前手动调 —— 下载完成有 3 个出口
     * （缓存命中 / 单连接 / 分段拼接），逐个改容易漏。
     */
    void onLocalVideoReady(const QString& path);
    /** 【v89】写 videos/<bvid>.json，给"已下载"板块显示标题/封面/时长/UP 主。 */
    void writeDownloadMeta();
    /** 【v89】下载失败：把 dlState 从"下载中"退回"没下过"，否则按钮永远卡在"下载中"。 */
    void onDownloadFailedReset(const QString& reason);
    void connectNet();
    void startSegmented(const QString& url, const QString& finalPath, qint64 total);
    void startSingle(const QString& url, const QString& finalPath, qint64 total);
    void finishSegmented();
    void abortDownload(const QString& reason);
    /** 起本地代理，成功则发 streamReady，失败直接转 downloadVideo。 */
    void startStream(const QString& url, qint64 total);

    // ---- 【v85】互动内部实现 ----
    /** 设置一行操作反馈，2.5 秒后自动清空（QML 没有 Timer，只能放 C++）。 */
    void setTip(const QString& t);
    /** 把 detail 里某个计数文案改成新的数值（如 likeText）。 */
    void setCountText(const QString& key, qint64 n);
    /** 拉收藏夹目录（只为拿 media_id，收藏接口必须给这个）。 */
    void fetchFavFolders();
    /** 真正调 /x/v3/fav/resource/deal。add=true 收藏，false 取消。 */
    void doFavDeal(bool add);

    /* ================= 【v87】头像缓存 =================
     *
     * 【问题】UP 主头像和评论头像原来是把接口给的 https 直链**直接**塞给
     * `ImageView.imageSource`。而 BB10 的 QtNetwork/QSslSocket HTTPS 握手会永久
     * 挂起（这就是本工程网络层全部换成 libcurl 的原因）—— 于是头像永远转圈/空白，
     * 一个都不显示。视频封面早在 v78 就走 curl 落地了，头像漏掉了。
     *
     * 【做法】和封面同一套：curl 下到沙盒 → 换成本地 file:// 路径喂 ImageView。
     * 已缓存（> 512 字节）直接命中，不重复下载。
     */
    /** URL → 沙盒里的缓存文件绝对路径（文件名 = 去协议后的 URL，够长不截断，
     *  避免两个用户名前缀相同时撞名）。 */
    static QString faceCachePath(const QString& url);
    /** 归一化 + 命中缓存直接返回 file://，否则入队下载并返回空串。
     *  tag 是下载回调的标识（`:face` / `:cface<行号>`）。 */
    QString resolveFace(const QString& url, const QString& tag);
    /** 头像下载完成：回填 detail.ownerFace 或某一行评论的 face。 */
    void onFaceDownloaded(const QString& tag, const QString& path, bool ok);

    QString m_tagPrefix;   // 实例唯一前缀
    QString m_bvid;
    long long m_aid;
    long long m_cid;
    bool m_loading;
    bool m_netConnected;

    // ---- 【v85】互动状态 ----
    bool m_liked;
    int  m_coinGiven;        // 已投币数 0/1/2（同一视频最多 2 枚）
    bool m_favored;
    bool m_followed;
    qint64 m_likeCount;      // 原始计数，操作成功后 ±1 并重算文案
    qint64 m_coinCount;
    qint64 m_favCount;
    QString m_ownerMid;      // UP 主 mid（关注接口的 fid）
    QStringList m_favFolderIds;  // 收藏夹 id 列表（第一个一般是默认夹）
    QString m_pendingFav;    // "add"/"del"：等收藏夹目录回来后再执行
    QString m_interactTip;
    QTimer* m_tipTimer;

    // ---- 【v89】下载状态 ----
    /** 0 = 没下过 / 1 = 下载中 / 2 = 已下载。 */
    int m_dlState;
    /**
     * 这次下载对应的 bvid 与详情快照。
     *
     * 【为什么不能直接用 m_bvid / m_detail】下载要跑几十秒，用户完全可能中途
     * 点开另一个视频 —— 那时 m_bvid 已经变了，回调回来再按它写 sidecar，
     * 就会把 A 视频的文件配上 B 视频的标题。开工时拍个快照，全程只认快照。
     */
    QString m_dlBvid;
    QVariantMap m_dlMeta;
    /** 【v90】下载任务序号。tag 里带上它，新旧两次下载的回调就不会互相干扰
     *  （分段失败回退单连接时，旧分段的 :dl0 和新单连接的 :dl0 曾经同名）。 */
    int m_dlGen;
    /** 【v90】下载完成后是否要立刻交给播放器（见 downloadVideo 的说明）。 */
    bool m_dlForPlayback;
    /** 【v90】用户点了下载但直链还没到手：playurl 回来后自动开工。 */
    bool m_dlWanted;
    /** 【v90】卡死保护：最后一次"已下载字节数增长"的时刻。 */
    qint64 m_dlProgressMs;
    /** 【v90】历史最大已下载字节（worker 完成后 downloadWritten 会归零，不能直接比较）。 */
    qint64 m_dlBestGot;

    // ---- 下载状态（支持分段并行） ----
    QStringList m_dlTags;    // 本次所有分段 tag
    QStringList m_dlParts;   // 分段临时文件
    QString m_dlFinal;       // 最终文件
    QString m_dlUrl;
    qint64 m_dlExpected;     // 总字节
    int m_dlPending;         // 尚未回调的分段数
    bool m_dlSegmented;      // 是否分段模式
    qint64 m_dlLastBytes;    // 上次采样的累计字节（算速度）
    qint64 m_dlLastMs;
    QTimer* m_dlTimer;
    int m_detailRetry;       // view 接口请求失败时的重试计数（最多 2 次）

    // ---- 流式播放 ----
    VideoProxy* m_proxy;
    QString m_streamUrl;     // 当前视频的 CDN 直链
    qint64 m_streamSize;
    bool m_usingProxy;

    // ---- 暴露给 QML 的状态（属性绑定，规避 QML .connect() 不可靠） ----
    QVariantMap m_detail;
    QString m_error;
    int m_dlPercent;
    QString m_dlSpeed;
    bb::cascades::ArrayDataModel* m_commentModel;
    /** 【v72】相关推荐（FeedModel::Related 实例，构造时自建、自动连 netReply）。 */
    FeedModel* m_relatedModel;
    int m_commentCount;

    // ---- 【v87】头像缓存 ----
    // 【v103】原来的 `QHash<QString,int> m_faceRows`（tag → 评论行号）已删除。
    // 评论头像不再走"下载完回填模型行"这条路（BB10 上 replace() 不重绘），
    // 改成模型里存远端 URL + QML 用 biliApi.faceUrlFor(url, coverVersion) 解析。
    /** UP 主头像的远端 URL（用于判断是否需要重新下载）。 */
    QString m_ownerFaceRemote;
};



#endif // VIDEO_STORE_HPP
