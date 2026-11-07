# BerryLi 开发文档

> BlackBerry 10 原生 bilibili 客户端（Cascades / QML + C++）
> 目标机型：BB10 OS 10.3.x（Passport / Q10 / Z10 / Classic）
> 版本基线：1.0.0.100
>
> 本文档的重点是 **第 4 章：视频硬解**。那一章是按"下次做一个视频类 app 能照着复现"
> 的标准写的，包含完整接口契约、数据流、和每一个真实踩过的坑。

---

## 目录

1. [项目概览](#1-项目概览)
2. [构建环境与一键构建](#2-构建环境与一键构建)
3. [架构分层](#3-架构分层)
4. [**视频硬解：FFmpeg + H.264（重点）**](#4-视频硬解ffmpeg--h264重点)
5. [网络层](#5-网络层)
6. [封面 / 图片加载](#6-封面--图片加载)
7. [QML 层铁律](#7-qml-层铁律)
8. [构建系统坑](#8-构建系统坑)
9. [速查表：现象 → 根因 → 修法](#9-速查表现象--根因--修法)
10. [新视频 App 最小可行清单](#10-新视频-app-最小可行清单)

---

## 1. 项目概览

| 项 | 值 |
|---|---|
| 应用名 | BerryLi |
| 包 ID | `com.doubao.biliclient`（**永远不要改**，见 8.3） |
| 源码 | `E:\PCdesk\桌面\bilibili\BiliBar\` |
| 构建目录 | `D:\bbuild\bilibar\` |
| 产物 | `BerryLi.bar`（复制到 `E:\PCdesk\桌面\bilibili\`） |
| 权限 | `access_internet` + `run_native`（**只这两个**，见 8.4） |

功能范围：推荐 / 关注 / 收藏 / 历史 / 热门 / 排行 / 分区 / 搜索 / 已下载 / 设置，
播放页（含评论、相关推荐、进度拖拽、长按 3 倍速扫动），扫码登录，视频下载与离线播放。

---

## 2. 构建环境与一键构建

### 2.1 路径

```
NDK          D:\bbndk
host tools   D:\bbndk\host_10_1_0_132\win32\x86\usr\bin
             ⚠ 不是 host_10_1_0_1020 —— 那个没有 make.exe
打包器 JDK   D:\bbndk\jre\bin\java.exe   （NDK 自带 JDK 1.6）
             ⚠ 用系统 Java 21 会因 SecurityManager 被移除而直接崩
target       D:\bbndk\target_10_1_0_1020\qnx6
```

### 2.2 一键构建

```bash
cd "E:/PCdesk/桌面/bilibili/BiliBar"
bash build.sh
```

`build.sh` 六个阶段，封装了全部已知坑：

| 阶段 | 做什么 | 解决什么坑 |
|---|---|---|
| `[0/6]` | 从 `bar-descriptor.xml` 生成 `src/app_version.hpp` | 版本号单一数据源；顺带 `rm applicationui.o` 强制重编 |
| `[1/6]` | 同步 `src/` `assets/` 到构建目录 | — |
| `[1b]` | 同步 `libbbplayer.so` / `libbbnet.so` | 硬解后端打包 |
| `[2/6]` | 修正 Makefile 里丢失盘符的 moc 路径 | 见 8.1 |
| `[3/6]` | 强制删除并重建 moc | 见 8.1 |
| `[4/6]` | 编译 + moc 自检 + 未解析符号自检 | 见 8.2 |
| `[5/6]` | 补齐 `.so` 副本（替代 `ln -s`） | Windows 上 symlink 不可用 |
| `[6/6]` | 打包 devMode BAR（NDK JDK 1.6） | 见 2.1 |

**改过头文件之后必须用 `build.sh` 构建，不要直接 `make`。** 原因见 8.1。

---

## 3. 架构分层

```
┌──────────────────────────────────────────────────────────┐
│ QML（bb.cascades 1.1）                                    │
│   main.qml  TabbedPane，10 个 Tab                          │
│   FeedPage / VideoPage / DownloadPage / SettingsPage ...   │
│   ↑ 只做属性绑定，不 connect C++ 信号（铁律 4）              │
├──────────────────────────────────────────────────────────┤
│ C++ 业务层（QObject + Q_PROPERTY）                         │
│   ApplicationUI   版本号、生命周期                          │
│   FeedModel       列表（8 个实例，kind 区分）                │
│   VideoStore      播放页详情 / 评论 / 取流                   │
│   VideoPlayer     播放器控制 + 进度轮询                      │
│   DownloadStore   已下载列表 / 长按删除                      │
│   FavStore        收藏夹                                    │
│   BiliApi         网络单例（curl 子进程 + WBI 签名）         │
├──────────────────────────────────────────────────────────┤
│ 网络 / 媒体                                                │
│   /usr/bin/curl      所有 HTTP(S) 业务请求                  │
│   libbbplayer.so     OpenMAX AL 硬解 + 静态 FFmpeg          │
│   libbbnet.so        curl 流式 reader（libbbplayer 的依赖）  │
├──────────────────────────────────────────────────────────┤
│ 系统                                                       │
│   libscreen.so       视频窗口（ForeignWindow 交给 Cascades） │
│   libOpenMAXAL.so.1  硬件解码引擎                           │
│   libasound.so.2 / libaudio_manager.so.1  音频输出          │
└──────────────────────────────────────────────────────────┘
```

### 为什么网络不用 QtNetwork

BB10 的 `QtNetwork`（`QSslSocket`）**HTTPS 握手会永久挂起**——设备浏览器经 WebKit
自己的网络栈能正常访问，但 QtNetwork 在 https 重定向后无响应。所以业务请求一律走
系统 `curl`（`QProcess` 异步读 stdout JSON），流媒体走 `libbbnet`（同样是 curl）。

### 为什么不用 `bb::multimedia::MediaPlayer`

1. 它**无法自定义请求头**，而 B 站 CDN 必须带 `User-Agent` + `Referer`，缺了直接 403；
2. 它只能吃直链，B 站播放地址需要动态获取；
3. 拿不到解码器状态、错误原文，出问题只能看到黑屏。

---

## 4. 视频硬解：FFmpeg + H.264（重点）

> 这一章是本文档的核心。目标：**下次做一个 BB10 视频类 app，照着这一章就能复现。**

### 4.1 结论先行：谁在解码

很多人以为"FFmpeg + H.264 硬解"是 FFmpeg 负责解码。**不是。**

在这套实现里：

| 角色 | 谁干的 |
|---|---|
| 网络读取 | `libbbnet.so`（静态链接 libcurl） |
| 解封装 / 解析 / H.264 Annex-B 转换 | **FFmpeg**（静态链接进 `libbbplayer.so`） |
| **真正的硬件解码** | **OpenMAX AL**（`libOpenMAXAL.so.1`，BB10 的 VideoCore/BCM 硬解引擎） |
| 画面呈现 | `libscreen.so` 建窗口 → Cascades `ForeignWindowControl` 认领 |
| 音频输出 | `libasound.so.2` + `libaudio_manager.so.1` |
| 软件解码兜底 | FFmpeg 自带 swscale / 软解（日志前缀 `soft:`） |

`libbbplayer.so` 的 `NEEDED` 列表（`ntoarm-readelf -d` 实测）：

```
libOpenMAXAL.so.1      ← 硬解
libaoi.so.1
libscreen.so.1         ← 视频窗口
libasound.so.2         ← 音频
libaudio_manager.so.1  ← 音频设备管理
libbbnet.so            ← curl 流式读取
libz.so.2
libsocket.so.3
libm.so.2
libc.so.3
```

FFmpeg 是**静态链进 `.so` 内部**的（不在 NEEDED 里），从字符串表能直接看到源码路径：

```
src/libavformat/demux.c
src/libavformat/aviobuf.c
src/libavfilter/af_aresample.c
src/libavfilter/af_atempo.c        ← 倍速/重采样
src/libavformat/avformat.c
```

所以：**你不需要自己写 FFmpeg 集成，也不需要自己调 OpenMAX。**
`libbbplayer.so` 已经把这条链路封好了，你只需要正确调它的 C 接口。

### 4.2 整体数据流

```
  B 站 playurl API
        │  fnval=1 & platform=html5 → 单个已合流的 MP4 直链（H.264 + AAC）
        ▼
  bbplayer_open_url(handle, url, 0, NULL)
        │
        ├─ libbbnet：curl 拉流（可注入 UA / Referer / CA）
        ▼
  FFmpeg：解封装 MP4 → 识别 H.264 → h264_mp4toannexb → 送 AVCC→Annex-B
        │  日志："remux: %s - %s: h264%s%s, reorder depth %d, %.0f MB ahead, opened in %.0f ms"
        ├──────────────► 音频轨：AAC → libasound 播放
        │                （"remux: the sound (%s) is none the hardware plays - the picture goes alone"
        │                  说明硬解只认特定音频编码，不认就静音放画面）
        ▼
  OpenMAX AL：硬解 H.264 → 输出到 libscreen 的 window buffer
        │
        ▼
  libscreen window（ID_STRING = 我们传的 windowId）
        │  screen_join_window_group(win, groupId)
        ▼
  Cascades ForeignWindowControl
     windowId  = 同一个 windowId
     windowGroup = 同一个 groupId
        │
        ▼
  屏幕上的画面
```

**关键点：画面不是"画"到 QML 里的，是 QML 把一个原生窗口"认领"过来贴上。**
所以 `ForeignWindowControl` 的属性必须和 `libbbplayer` 建窗时用的字符串**逐字符一致**。

### 4.3 五步复现清单

下面这段就是 `VideoPlayer::ensurePlayer()` + `play()` 的骨架，
**顺序错了任何一步都出不来画面**。

```cpp
// ── 0. 加载库（dlopen，不进链接期依赖）────────────────────────
void* lib = dlopen("libbbplayer.so", RTLD_LAZY | RTLD_GLOBAL);
dlopen("libbbnet.so", RTLD_LAZY | RTLD_GLOBAL);   // 显式再加载一次，保证符号可见

// ── 1. 全局网络配置（必须在 create 之前！）──────────────────────
//    注意：这个函数【没有 handle 参数】，是全局的，三个纯字符串
net_config(ua, referer, caPath);

// ── 2. 创建句柄，第一个参数是【事件回调】，第二个是 userdata ────
void* h = bbplayer_create(&eventTrampoline, this);

// ── 3. 开启视频输出（默认关闭！不开就只有声音）──────────────────
bbplayer_set_video_enabled(h, 1);

// ── 4. 建窗口：参数顺序是 (group, windowId) —— 组在前、ID 在后 ──
bbplayer_set_window(h, groupId, windowId);

// ── 5. 打开 + 播放 ────────────────────────────────────────────
bbplayer_open_url(h, url, 0, NULL);   // 网络流
// bbplayer_open_file(h, path, 0);    // 本地文件（已下载）
bbplayer_play(h);
```

QML 侧：

```qml
ForeignWindowControl {
    windowId: videoPlayer.windowId
    windowGroup: videoPlayer.groupId
    updatedProperties: WindowProperty.Size | WindowProperty.Position | WindowProperty.Visible
    visible: true        // ⚠ 必须 true，不能绑 boundToWindow
}
```

`windowId` 生成方式（每个播放器实例唯一，镜像 BBTube 的 `PlayerController.newWindowId()`）：

```cpp
m_windowId = QString("biliSurface_%1").arg((quintptr)this, 0, 16);
m_groupId  = Application::instance()->mainWindow()->groupId();
```

### 4.4 接口契约（全部由 ARM 反汇编逐条核对）

> 这张表是硬解能跑起来的前提。**任何一个签名写错，表现都是"没画面"或"直接崩"。**

```c
// 事件回调：fn(userdata, type, a, b, c, d, msg)
//   寄存器 r0=userdata r1=type r2=a r3=b；栈 [sp+0]=c [sp+4]=d [sp+8]=msg
typedef void (*PFN_event_cb)(void* userdata, int type, int a, int b,
                             int c, int d, const char* msg);

void* bbplayer_create(void* event_cb, void* userdata);          // 返回 5600 字节句柄
void  bbplayer_destroy(void* handle);

// ⚠ 没有 handle！全局配置，三个字符串：UA(512B槽) / Referer(256B槽) / CA路径(160B槽)
int   bbplayer_net_config(const char* ua, const char* ref, const char* caPath);

// ⚠ 组在前、ID 在后
int   bbplayer_set_window(void* handle, const char* group, const char* windowId);

int   bbplayer_set_video_enabled(void* handle, int on);         // 默认 0，必须显式置 1
int   bbplayer_open_url(void* handle, const char* url, int flags, const char* type);
int   bbplayer_open_file(void* handle, const char* path, int flags);
int   bbplayer_play(void* handle);
int   bbplayer_pause(void* handle);
int   bbplayer_close(void* handle);

// ⚠ position 是 64 位，占 r2,r3；r1 是未使用的对齐填充
int   bbplayer_seek(void* handle, long long position, int flags);

long long bbplayer_position(void* handle);   // 句柄偏移 0x1218
long long bbplayer_duration(void* handle);   // 句柄偏移 0x1220
int       bbplayer_state(void* handle);      // 句柄偏移 0x1210
```

**其它导出接口**（本工程未用，但同一套库里有，做多段/音视频分离时会需要）：

```
bbplayer_open_segments / open_segments_seekable / open_segments_timed
bbplayer_open_source
bbplayer_mux_start / mux_grow / mux_cancel / mux_input_done / mux_destroy
bbplayer_anim_open / anim_open_memory / anim_next / anim_rewind / anim_close
bbplayer_music_create / music_open / music_play / music_seek / music_wave ...
bbplayer_tags_x / bbplayer_waveform / bbplayer_version
```

**回调 type 语义**（实测）：

| type | 含义 | 参数 |
|---|---|---|
| 1 | 状态变更 | `a` = 新 state（0 停止 / 2 缓冲准备 / 3 播放中 / 5 错误） |
| 3 | 库内部周期事件（约 250ms 一次） | 无诊断价值，**必须直接 return** |
| 7 | 错误 | `a` = 错误码，`msg` = 错误原文 |
| 8 | 日志 | `msg` = 日志原文 |

### 4.5 时间与进度

`position` / `duration` / `seek` 的单位是 **tick**，但**库不告诉你 tick 是什么量级**
（rodata 里只有一句 "in microseconds" 的注释，不可靠）。

**不要硬编码 `TICKS_PER_MS`。** 本工程的做法是**运行时自校准**：

```cpp
// 播了 3~20 秒后，拿"墙钟真实经过的毫秒"当尺子，和原始 pos 求比值
if (m_ticksPerMs == 0 && m_clockOn && pos > 0) {
    const qint64 elapsed = m_clock.elapsed();
    if (elapsed >= 3000 && elapsed <= 20000) {
        const double ratio = (double)pos / (double)elapsed;   // 期望 ≈ ticksPerMs
        if      (ratio > 0.3   && ratio < 3.0  ) m_ticksPerMs = 1;        // 毫秒
        else if (ratio > 300.0 && ratio < 3000.0) m_ticksPerMs = 1000;    // 微秒
        else if (ratio > 3.0e5 && ratio < 3.0e6 ) m_ticksPerMs = 1000000; // 纳秒
        // 落在别的量级 = 垃圾值（刚 open 还没起播），不设，下一轮再来
    }
}
```

猜错的后果很直观：若实际是毫秒而你按微秒除，`pos=10000 → 10ms`，
**时间标签永远停在 0:00，进度条一动不动**。

配套两个保护：

- **seek 保护窗口 1.5 秒**：刚 seek 完解码器在重开，`position()` 读到的还是旧位置。
  这段时间改用墙钟推进，否则进度条会"先弹回拖动前、再跳回来"。
- **墙钟兜底**：`m_ticksPerMs` 还没校准时，进度用 `m_clock.elapsed()` 顶上，
  至少让用户看到"在动"。

### 4.6 硬解踩坑与修复（每条都真实付出过代价）

#### 坑 1：`bbplayer_create(0, 0)` —— 错误全被静默丢弃

前两个参数**不是保留参数**，是「事件回调 + userdata」。
反汇编 `bbplayer_create(0x58ca8)` 开头即 `str r0,[r4,#0]; str r1,[r4,#4]`，
之后所有回调派发（跳板 `0x55a34`）都是 `ldr fn,[handle]; ldr ud,[handle,#4]; bx fn`。

传 `(0,0)` → 回调为 NULL → **库的错误消息与状态变更事件全部被丢弃**，
日志里一条 ERROR 都抓不到，state 只能靠轮询句柄字段猜。

> 症状：黑屏，没有任何错误信息，完全无法定位。

#### 坑 2：`bbplayer_net_config` 的签名

它是**全局配置，没有 handle**，三个纯字符串。反汇编 `0x50220`：

```
mov r6,r0 ; mov r5,r1 ; mov r4,r2
if (r6) snprintf(槽A 512B, fmt, r6)      ← UA
if (r5) snprintf(槽B 256B, fmt, r5)      ← Referer
if (r4 && r4[0]) snprintf(槽C 160B, fmt, r4)  ← CA 证书路径
```

写成 `(handle, ua, ref, proxy)` 四个参数的后果：handle 指针被当 UA 打印进 512 槽
（UA 变二进制垃圾），ua 占了 Referer 槽，ref 占了 160 字节的 CA 槽，
真正的 proxy 落在栈上根本没被读。

**而且必须在 `create()` 之前调**——它只写 `.bss` 里的全局快照，
真正读快照的是 create/open 流程，create 之后再设等于白设。

> 症状：CDN 返回 403 / 403 后无响应。

#### 坑 3：`bbplayer_set_window` 参数顺序反了

底层是 `(handle, group, windowId)`，**组在前、ID 在后**。传反的连锁反应：

```
窗口的 SCREEN_PROPERTY_ID_STRING 被设成【主窗口组名】
→ screen_join_window_group 去 join 一个叫 "biliSurface_xxxx" 的【不存在的组】
→ join 失败 → 库内部直接 screen_destroy_window 把窗口销毁
→ 屏幕窗口从来就没建出来过
→ ForeignWindowControl 永远 boundToWindow = false
```

反汇编证据链：

```
bbplayer_set_window(0x58e00):
  0x58e08  mov  r6, r1          ; r6 = arg1
  0x58e0a  mov  r5, r2          ; r5 = arg2
  0x58e20  snprintf(handle+0x1148, 128, "%s", r6)   ; arg1 → +0x1148（组）
  0x58e34  snprintf(handle+0x11c8,  64, "%s", r5)   ; arg2 → +0x11c8（ID）
建窗函数(0x622e4):
  0x6231c  snprintf(struct+0x18, 64, "%s", arg2)    ; arg2 是 ID
  0x623be  screen_set_window_property_cv(win, 20 [ID_STRING], len, struct+0x18)
  0x623c6  screen_join_window_group(win, arg1)      ; arg1 是组
  0x623e6  join 失败 → screen_destroy_window + screen_destroy_context
```

> 症状：**有声音、没画面**，`boundToWindow` 恒为 false。
> 这个坑连续踩了 19 个版本（v43~v61），期间把 QML 侧所有组合都试遍了都没用。

#### 坑 4：忘了 `set_video_enabled(true)`

`bbplayer_create` 默认**不开启视频输出**。不显式置 1，只有声音没有画面。

#### 坑 5：`bbplayer_seek` 的 64 位参数

`position` 占 `r2,r3` 两个寄存器，`flags` 落在栈上，`r1` 是**未使用的对齐填充**。
所以 C 原型必须是 `(handle, long long, int)`。写成 `(handle, int, int)` 参数错位，
**直接崩设备**。

#### 坑 6：回调里只有 type 7/8 的第三个参数才是字符串指针

其它 type 的这个槽位可能是任意值（甚至非指针）。
拿它去做 `QString::fromUtf8()` = **野指针崩溃**。这是接回调最容易踩的坑。

```cpp
const bool hasMsg = (type == 7 || type == 8);
QString m = (hasMsg && msg) ? QString::fromUtf8(msg) : QString();
```

`msg` 由库 `snprintf` 到临时栈缓冲区后传出，**只在回调瞬间有效，必须立即深拷贝**。

#### 坑 7：回调在库线程里，不能做重活

回调运行在 `libbbplayer` 自己的线程，很可能是时间敏感的喂数据/音频线程。
`type=3` 是每 ~250ms 一次的周期事件。如果每条都走完整流程：

```
QString 拼接分配 → m_log 追加 → 超 3000 字符时整段拷贝
```

等于**在播放线程上每 250ms 做一次堆分配 + 3000 字符拷贝**。

> 症状：**声音一卡一下**。节奏和这个 250ms 完全对得上，尤其画面接通后线程负载更高，
> 这点抖动就足以让音频欠载。

修法：只保留 type 1 / 7 / 8，其余只计数不建字符串。

#### 坑 8：`bbplayer_close()` 会销毁窗口，而 FWC 不会回头重扫

BB10 的 `ForeignWindowControl` 在加入场景时**只扫一次**匹配 `windowId` 的窗口，
之后不会回头重扫。

所以"先 open https 失败 → close() → 再 open http"这套自动回退是致命的：
`close()` 把窗口销毁了，重开的窗口 FWC 不会再认领。

> 症状：**有声音、没画面**，`fwc=N`。

修法：**在第一次 `openUrl` 之前就把 scheme 换掉**，窗口只建一次、FWC 只认领一次。

```cpp
QString playUrl = url;
if (!forceHttps && playUrl.startsWith("https://"))
    playUrl.replace(0, 8, "http://");
```

（B 站 CDN 双协议都支持，http 下实测 30fps 解码、29.6 帧实际送出。）

#### 坑 9：`ForeignWindowControl` 的 `visible` 不能绑 `boundToWindow`

```qml
visible: true                      // ✅
visible: surface.boundToWindow     // ❌ 永远绑不上，永远黑屏
```

控件在不可见期间**不去认领**屏幕窗口，于是永远绑不上。

另外三个属性必须写全，否则 Cascades 不会把位置尺寸同步给屏幕窗口，
**表现为视频贴在左上角、大小不对**：

```qml
updatedProperties: WindowProperty.Size | WindowProperty.Position | WindowProperty.Visible
```

#### 坑 10：CA 根证书装载是页面卡顿的主因

`installCaBundle()` 要干的事很重：查 4 个候选路径 → 复制 PEM 到沙盒 →
`dlopen libbbnet` → `bbnet_global_init` → `bbnet_ca_add_file`（解析 121 张 X509）
→ `bbnet_ca_add_system`（再解析 125 张）。在 BB10 的 CPU 上是**几百毫秒级**开销。

v62 及之前同一次 `ensurePlayer` 里它会跑 2~3 遍，全发生在 `nav.push()` 之前。

> 症状：**点进播放页要等好几秒，连转场动画都看不到**，用户只觉得"点下去没反应"。

修法：`installCaBundle` 改成"装成功一次就永不再做"，并且**只在真要打开 https 时才装**
（播放默认降级 http，这条路根本不做 TLS，装根证书纯属白烧 CPU）。

补充：`bbnet_ca_add_file` 的返回值**不是错误码，是成功加入的证书张数**
（本包 = 121 张，`> 0` 才是成功）。`bbnet_ca_add_file` 只把 PEM 解析进**全局内存 store**
（给 cert pinning 用），真正让 curl 句柄信任这些证书的是 `CURLOPT_CAINFO`，
而它的值只能通过 `bbplayer_net_config` 的**第三参数**写进全局 config+16 槽。

#### 坑 11：软硬解自动回退是库内建的

`libbbplayer.so` 内部有两条解码路径，日志前缀能区分：

```
remux: ... h264..., reorder depth %d, %.0f MB ahead, opened in %.0f ms   ← 硬解通路
soft:  %s %dx%d goes through swscale                                       ← 软解兜底
the hardware decoder refused the video of %s                               ← 硬解拒绝 → 转软解
the hardware decoder fell behind, and the software one did not start: %s   ← 硬解掉队
video: %.1f s behind the sound, every frame late ... - the hardware de...  ← 音画不同步诊断
```

**这意味着：如果硬解不支持某个 profile/level，库会自己退回软解，不会黑屏。**
你要做的只是保证输入是 H.264 + AAC 的 MP4。

### 4.7 取流参数：为什么必须选 `fnval=1`

```cpp
QVariantMap pp;
pp.insert("bvid", m_bvid);
pp.insert("cid",  QString::number(m_cid));
pp.insert("qn",   "32");        // 480P（BB10 屏幕最宽 720px，再高纯白下载体积）
pp.insert("fnval", "1");        // ⭐ 单文件 MP4（已合流），H.264 + AAC
pp.insert("platform", "html5"); // upos-*.bilivideo.com 稳定节点
pp.insert("high_quality", "1");
BiliApi::instance()->getSigned("/x/player/playurl", pp, tag);
```

- `fnval=1` → 返回**单个已合流的 MP4**，H.264 + AAC。正好是硬解要的组合，
  而且不需要自己拼音视频轨。
- 如果用了 DASH（`fnval=16`），拿到的是分离的音视频轨，得用
  `bbplayer_open_segments` 自己拼——本工程没走这条路。
- `qn=32` 失败时自动降级重试 `qn=64`。
- **必须用 `getSigned`（WBI 签名）**，否则服务端拒绝 → `playUrl` 拿不到 →
  页面永远停在"正在连接视频…"且无任何提示。

---

## 5. 网络层

### 5.1 curl 子进程

所有业务请求走 `/usr/bin/curl`，`QProcess` 异步读 stdout。

- 必须带 `User-Agent` + `Referer`（缺 UA 直接 403）
- 评论接口（`reply/main`）必须用**标准浏览器 UA**，用 BB10 UA 会被风控返回 `-352`
- 所有请求自动带登录 cookie；扫码轮询例外（带 cookie 会拼出两条重复 SESSDATA）

### 5.2 WBI 签名

`/x/web-interface/wbi/view`（播放页详情）和 `/x/player/playurl` 都需要 WBI 签名。
`getSigned` 在密钥未就绪时自动 fallback 为无签名请求，安全。
**裸 `get` 请求 view 会稳定失败（-412）。**

### 5.3 回调一律用编译期 signal/slot

```cpp
QObject::connect(BiliApi::instance(), SIGNAL(netReply(QString,QString,bool)),
                 this, SLOT(onNetReply(QString,QString,bool)));
```

**不要用 `QMetaObject::invokeMethod` 运行时派发**——BB10 Qt 4.8.5 上它返回 `true`
却不投递。

### 5.4 单例

`BiliApi::instance()` 与 `create()` 必须共用同一个 static 指针
（曾出现双实例导致信号连不上）。

---

## 6. 封面 / 图片加载

### 6.1 为什么不能直连 https

BB10 的 `ImageView` 走 QtNetwork 加载 https 图片会握手失败，显示黑块。
所以**所有网络图片都必须先 curl 落到本地，再喂 `file://` 路径**。

### 6.2 缓存路径必须全工程唯一

```cpp
BiliApi::coverCachePath(rawUrl)   // 全工程唯一入口
```

以前列表页 / 下载 sidecar / 已下载页三处各写一遍路径算法，60 字符截断漏了一处，
算出两个不同文件名 → **"已下载列表永远没有封面"**。

### 6.3 三条必须守住的规则

1. **一个目标路径只允许一个 worker。**
   缓存路径由 URL 决定，所以"同一个视频出现在推荐和热门里"= 同一个目标路径。
   两个 worker 同时 `fopen(path,"wb")` → 互相截断 + 交错写入 →
   **文件头合法、正文乱码** → ImageView 解不出来 → 封面永久空白且永不重下。
   用 `m_dlPathTags`（path → tag 列表）去重，完成时统一分发。

2. **原子落盘。** 先写 `<path>.part`，`CURLE_OK` 才 rename 转正。
   半截文件绝不出现在最终路径上。rename 失败要用 `QFile::copy` 兜底。

3. **缓存校验只能看字节，绝不能依赖解码器。**
   `QImageReader::canRead()` 在 BB10 的 Cascades 应用里可能对所有 jpeg 返回 false
   （图片格式插件不一定被正确加载）→ 每张刚下完就被判成坏图删掉 → **一张都不显示**。
   正确做法：头 + 尾双标记，纯字节比较。
   JPEG 必须 `FF D9` 收尾，PNG 以 `IEND`，WEBP 校验 RIFF 长度，GIF 以 `0x3B`。

### 6.4 封面 tag 不能用行号

`clear()` 之后行号从 0 重来，刷新前没下完的 `:img0` 和刷新后新发的 `:img0`
撞成同一个 tag → 旧 worker 回来把**旧视频的封面贴到新列表第 0 行**。
用单调递增的 `m_imgSeq`，**永不清零**。

### 6.5 并发与超时

- 封面并发 **5**（3 太少跑不完首屏，20~40 会握不完 TLS 全部 CONNECTTIMEOUT）
- 封面超时 **20 秒**，视频超时 **600 秒**——必须分开设
  （共用 600 秒时，3 个连接卡在"已建连但不返回响应头"，`LOW_SPEED` 不触发，
  只能干等 10 分钟，队列永久停摆 = 整页封面空白）
- 队列计数**实时统计**，不要手工 `++`/`--`（漏减一次就永久停摆）

---

## 7. QML 层铁律

### 7.1 `setContextProperty` 只接受 `QObject*`

BB10 的 `QmlDocument::setContextProperty` 只有一个 `QObject*` 重载。
传 `QVariant` / `QString` 直接编译失败。非 QObject 的值必须做成 `Q_PROPERTY`：

```cpp
Q_PROPERTY(QString version READ version CONSTANT)
```
```qml
text: "BerryLi " + app.version
```

### 7.2 没有 `Connections` 元素

`import bb.cascades 1.1` 不带 QtQuick，所以 `Connections` 不存在，
写了直接 `QML PARSE ERROR`。

**`app.someSignal.connect(handler)` 对 C++ 信号在 Qt 4.8 / BB10 上同样不可靠
（静默不连接）。** 数据给 QML 一律走 `Q_PROPERTY` 绑定，NOTIFY 在 QML 引擎内部
驱动，可靠。

### 7.3 多跳 QML 信号转发会静默断链

`A.videoTapped → B.onVideoTapped → B.openVideo → C.onOpenVideo` 在 Qt 4.8 上会断。
**可靠做法：零信号中转，同步直调。**

`main.qml` 把 `NavigationPane` 通过 `nav: homeNav` 注入页面 → 页面
`property variant nav` → `onTriggered` 里直接 `createObject()` + `nav.push()`。
属性绑定和 JS 函数调用是可靠的，信号不是。

### 7.4 `ComponentDefinition` 必须放在 `main.qml` 根节点

`source` 用 assets 根下的平级文件名（`"VideoPage.qml"`）。
放在子组件的 `attachedObjects` 里，或写 `"../VideoPage.qml"`、
`"asset:///VideoPage.qml"`，`createObject()` **都返回 null**（三种写法都实测过）。
建页函数也必须和 `ComponentDefinition` 在同一个文档作用域。

### 7.5 `ListItemComponent` 不要写 `type: "item"`

`ArrayDataModel::itemType` 默认是**空字符串**，`ListItemComponent::type` 也必须为空
才匹配。写了 `type: "item"` → 匹配失败 → 退回默认 delegate → 只显示 QVariantMap 里
**字母序第一个键**的值（表现：整页只有 BV 号，无封面无标题）。

自定义 list item 需要给确定 `preferredWidth`，否则宽度塌陷。

### 7.6 属性绑定不能写成块 `{ ... }`

Qt 4.8 会把 `{` 当对象字面量。写成单行表达式或用 `Qt.binding()`。

### 7.7 长按只能用 `contextActions`

`gestureHandlers` 属性在 NDK 10.1 头文件里**根本不存在**，`LongPressHandler` 挂不上。
长按只能靠 `contextActions`（ListView 原生识别，不和滚动打架）。

`Control::contextActions` 是 `QDeclarativeListProperty<ActionSet>`，
**QML 里必须包一层 `ActionSet`**，不能直接放 `ActionItem`。

### 7.8 `ActionItem.onTriggered` 的作用域陷阱

JS 函数体里 `ListItemData` 和 context property **都不可见**。
直接写 `downloadStore.requestDelete(ListItemData.bvid)` → `ReferenceError` →
**静默失败，界面毫无反应**。

解法：取值全部走属性绑定（`store: ListItemData.store`），JS 里只引用根节点 `id`
（id 是编译期绑定，绝对可靠）。

### 7.9 列表刷新用 `ArrayDataModel::replace`

`Q_SLOT void replace(int index, const QVariant& item)`，用来通知 ListView 重画单行。

---

## 8. 构建系统坑

### 8.1 moc 规则路径丢了盘符（最致命）

Makefile 里 moc 规则目标写成 `/.moc/xxx.cpp`（丢盘符），
依赖方要的是 `../../.moc/xxx.cpp`。两者字符串不等 → make 认为"无规则可生成" →
只要文件存在就复用，**永不检查头文件变更**。

元对象表描述旧类布局 + 实际代码是新布局 → QML 读 `Q_PROPERTY` 时
`qt_metacall` 按旧索引派发 → **崩溃**。

`build.sh` 已强制删除 moc 产物并做签名自检。**改头文件后务必用它构建。**

### 8.2 共享库允许保留未定义符号

新增 C++ 依赖如果 Makefile 的 `LIBS` 里没有对应 `-l`：
**编译零告警、链接成功，但设备上 dlopen 解析失败，应用完全打不开**（连崩溃日志都没有）。

排查：

```bash
ntoarm-nm -u libBiliClient.so | grep <类名>        # 有 U 符号 = 未解析
ntoarm-readelf -d libBiliClient.so | grep NEEDED   # 看有没有对应 .so
```

当前 `LIBS` 含：`-lbbdata -lbbsystem -lcurl -lbbcascades -lQtDeclarative ... -lbps -lbbmultimedia`
**没有 `-lbbdevice`** → 不能用 `bb::device::DisplayInfo`。

需要临时用某个系统库时，**走 `dlopen` + `dlsym`，不要改 Makefile**（v61 的屏幕探针就是
这么做的）。

### 8.3 `<id>` 绝对不能改

`<id>com.doubao.biliclient</id>` 改了等于换应用——沙盒目录换掉，
cookie / 已下载视频 / 设置**全部读不到**。

改应用名只需要改两处：`<name>` 和 `<initialWindow title>`。

### 8.4 权限只要两个

只保留 `access_internet` + `run_native`。
**不要加 `access_shared`**——受限权限会弹授权窗，devMode 未签名包授权后进程重启、
重启再申请，形成"弹窗→闪退"循环。

### 8.5 版本号单一数据源

```xml
<versionNumber>1.0.0</versionNumber>
<buildId>100</buildId>
```

`build.sh` 从 descriptor 生成 `src/app_version.hpp`：

```cpp
#define APP_VERSION_STRING "1.0.0.100"
```

改版本号**只改 descriptor 的 buildId**。同版本覆盖安装会被 BB10 / Sachesi 静默拒绝，
每次必须升 buildId。

### 8.6 应用图标

`bar-descriptor.xml` 的 `<asset path="icon.png">` 指的是**构建目录**，不是 `assets/`。
`build.sh` 里已有 `cp -f "$PROJ_DIR/icon.png" .`，不同步就一直是历史遗留的旧图标。

BB10 标准尺寸 114×114。

### 8.7 C++03 限制（qcc）

- 不允许局部类型当模板参数 → 结构体必须提到文件作用域
- `-Wreorder`：初始化列表顺序必须和头文件声明顺序一致
- `<cstdio>` 的 `::remove` / `::rename` 在 qcc 下可能只在 `std::` 里 → 用 `QFile` 代替

### 8.8 `JsonDataAccess::save()` 返回 void

失败完全静默。对 `QMetaType::LongLong` 支持存疑——`ts` 用 `qint64` 可能导致整个
sidecar 序列化失败，列表里所有视频只剩 BV 号。用 `(int)` 并做写回读验证。

---

## 9. 速查表：现象 → 根因 → 修法

| 现象 | 根因 | 修法 |
|---|---|---|
| 应用完全打不开，无崩溃日志 | 共享库有未解析符号 | 检查 `LIBS` 里的 `-l`，或用 `dlopen` |
| 应用打不开 / 崩溃 | moc 没重跑，元对象表错位 | 用 `build.sh` 构建 |
| 点下去没反应 | QML 信号多跳转发断链 | 改成属性绑定 / 同步直调 |
| 点进去要等好几秒 | CA 证书解析在 `nav.push()` 前跑 2~3 遍 | 只在 https 时按需装，装一次不再装 |
| **有声音、没画面** | `set_window` 参数顺序反了 / `close()` 销毁了窗口 | 组在前 ID 在后；open 前就降级 scheme |
| 黑屏且无任何错误 | `bbplayer_create(0,0)` 回调为 NULL | 传回调 + userdata |
| 只有声音没画面（新建窗口后） | 忘了 `set_video_enabled(true)` | 显式置 1 |
| 视频贴左上角、大小不对 | `updatedProperties` 没写全 | 加 Size + Position + Visible |
| `boundToWindow` 恒 false | FWC `visible` 绑了 `boundToWindow` | 写死 `visible: true` |
| 时间标签永远 0:00，进度不动 | tick 单位猜错（毫秒当微秒除） | 运行时自校准 |
| 进度条 seek 后回弹 | 解码器重开期间读到旧 position | 1.5 秒保护窗口用墙钟 |
| **声音一卡一下** | 回调每 250ms 做堆分配 | type 3 直接 return |
| 播放直接崩设备 | `bbplayer_seek` 原型写成 32 位 | `(handle, long long, int)` |
| 崩溃在回调里 | 非 7/8 的 type 拿去 `fromUtf8` | 只有 7/8 才有 msg 指针 |
| 播放页卡在"正在连接视频…" | playurl 没用 WBI 签名 | 改 `getSigned` |
| 整页只有 BV 号 | `ListItemComponent` 写了 `type:"item"` | 去掉 type |
| 长按菜单点了没反应 | `onTriggered` 里 `ReferenceError` | 取值走属性绑定 |
| 封面只显示一部分 | 同一路径被两个 worker 并发写坏 | `m_dlPathTags` 去重 + 原子落盘 |
| 封面一张都不显示 | `QImageReader::canRead()` 误判 | 改用头+尾字节校验 |
| 已下载没封面 | 三处路径算法不一致 | 统一 `coverCachePath` |
| 已下载没标题 | sidecar 的 `qint64` 导致序列化失败 | 改 `(int)` + 写回读验证 |
| 刷新后封面贴错行 | tag 用行号，`clear()` 后撞号 | 单调递增 `m_imgSeq` |
| 授权弹窗后闪退 | 加了 `access_shared` | 删掉，只留两个权限 |

---

## 10. 新视频 App 最小可行清单

下次做一个 BB10 视频类 app，按这个顺序来：

### 第一步：能编译能跑

- [ ] 装 NDK 10.1，确认 `host_10_1_0_132` 下有 `make.exe`
- [ ] 确认打包用 NDK 自带 JDK 1.6
- [ ] 写 `build.sh`，至少包含：版本号生成、moc 强制重建、`-l` 库自检
- [ ] 权限只要 `access_internet` + `run_native`

### 第二步：网络

- [ ] 用系统 `curl` 子进程，不要用 QtNetwork（https 握手会挂起）
- [ ] 所有请求带 UA + Referer
- [ ] 回调走编译期 `connect`，不用 `invokeMethod`

### 第三步：视频硬解（核心）

- [ ] 拿 `libbbplayer.so` + `libbbnet.so`，`dlopen` 加载，不进链接期依赖
- [ ] 复制 `bbplayer_wrapper.hpp` 的接口契约（4.4 那张表），一个字都别改
- [ ] 严格按 4.3 的五步顺序：`net_config` → `create(cb, this)` →
      `set_video_enabled(1)` → `set_window(group, id)` → `open` + `play`
- [ ] `windowId` 用 `biliSurface_<this指针hex>`，`groupId` 用 `mainWindow()->groupId()`
- [ ] QML 里 `ForeignWindowControl`：`visible: true`、三个 `updatedProperties` 写全
- [ ] 播放前把 https 降级成 http（避开 CA 问题，也避开 `close()` 销毁窗口）
- [ ] 进度用运行时自校准，不要硬编码 `TICKS_PER_MS`

### 第四步：图片

- [ ] 所有网络图先 curl 落盘，再喂 `file://`
- [ ] 缓存路径全工程唯一入口
- [ ] 一个路径一个 worker + 原子落盘 + 头尾字节校验
- [ ] 封面并发 5、超时 20 秒；视频超时单独设 600 秒

### 第五步：QML

- [ ] 数据全部走 `Q_PROPERTY` 绑定，不 connect 信号
- [ ] `ComponentDefinition` 放 `main.qml` 根节点
- [ ] 列表项不写 `type: "item"`
- [ ] 长按用 `contextActions` + `ActionSet`
- [ ] `ActionItem.onTriggered` 里只引用根节点 `id`

### 验证顺序（别跳）

1. `dlopen` 是否成功 → 打印 `dlerror()`
2. 符号是否全部解析 → `loaded()`
3. 回调是否真的被调用 → `eventCount() > 0`
4. 窗口是否建出来 → 用 `libscreen` 探针枚举本进程窗口，比对 ID / GROUP
5. 是否有画面 → `boundToWindow`

**这五步每一层都有独立可观测的信号，不要跳过去直接看"有没有画面"。**
本工程当年就是因为跳过了第 3、4 步，在"参数顺序反了"这个坑上卡了 19 个版本。
