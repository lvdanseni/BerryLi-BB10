# BerryLi 完整源码整理包

BlackBerry 10 原生 bilibili 客户端（Cascades / QML + C++）

## 目录结构

```
BerryLi_完整源码/
├── .gitignore                # Git 忽略规则（排除编译产物、缓存、系统文件）
├── README.md                 # 本说明文件
├── BiliBar/                  # BB10 NDK 原生源码（C++ + QML，主构建目录）
│   ├── src/                  # C++ 源码（21 个 .cpp/.hpp）
│   ├── assets/               # QML 界面 + 图片资源（21 个 .qml + images/ + components/）
│   ├── bar-descriptor.xml    # BAR 包描述符（应用 ID、名称、版本、权限）
│   ├── build.sh              # 一键构建脚本（6 步：版本号→同步→moc→编译→so→打包）
│   ├── Makefile              # 手工维护的 Makefile（qmake 生成后修正过，路径已参数化）
│   ├── bili_client.pro       # qmake 项目文件
│   ├── icon.png              # 应用图标（114×114，圆角，RGBA）
│   ├── qmlcheck.py           # QML 属性静态核查工具
│   └── *.py                  # 其他辅助脚本（图标生成、导入修复等）
└── BiliClient/               # C# 关联项目源码（WPF/WinForms 客户端）
    ├── Api/
    ├── Controls/
    ├── Stores/
    ├── Themes/
    ├── Views/
    ├── App.xaml / App.xaml.cs
    ├── BiliClient.csproj
    ├── app.manifest
    └── ...                   # 仅源码，已排除 bin/ obj/ 等编译产物
```

## 快速开始

### 1. 克隆仓库

```bash
git clone <你的仓库地址>
cd BerryLi_完整源码
```

### 2. 配置构建环境

本项目基于 **BlackBerry 10 NDK 10.1** 构建。请在本地安装 NDK，并设置环境变量：

| 环境变量 | 说明 | 默认值 |
|---------|------|--------|
| `BBNDK` | NDK 根目录 | `D:/bbndk` |
| `BUILD_DIR` | 构建输出目录（相对 `BiliBar/`） | `../build` |
| `BBTUBE_LIB` | BBTube 逆向素材目录（含 `libbbplayer.so` / `libbbnet.so`） | `../bbtube_extracted/native/lib` |
| `DELIVER_DIR` | 构建产物交付目录 | `..`（项目根目录） |

### 3. 一键构建

```bash
cd BiliBar
bash build.sh
```

构建成功后，产物 `Xiaohongshu.bar` 会输出到 `BUILD_DIR` 并交付到 `DELIVER_DIR`。

### 4. 版本号管理

版本号由 `bar-descriptor.xml` 中的 `<versionNumber>` + `<buildId>` 组成（例如 `1.0.0.104`）。每次迭代只需修改 `buildId`（+1），无需动 C++/QML 代码。

## 关键约定（铁律，修改前必读）

1. **改头文件后必须用 `build.sh`**（强制删 moc 产物并做签名自检）。Makefile moc 目标丢盘符，不重跑会导致 `Q_PROPERTY` 按旧索引派发 → 崩溃。
2. **新增 C++ 依赖必须加到 Makefile `LIBS` 的 `-l` 库**（否则设备 `dlopen` 失败，应用完全打不开）。当前 `LIBS` 已含 `-lbbdata -lbbsystem -lcurl -lbbcascades -lQtDeclarative -lbps -lbbmultimedia`，**无 `-lbbdevice`**，不可用 `bb::device::DisplayInfo`。
3. **QML 不要写 `type: "item"`**（默认空字符串才匹配），否则只显示 `bvid`；自定义 item 需给 `preferredWidth`。
4. **QML 无 `Connections`**，C++ 信号 → QML JS `.connect()` 静默不连接。页面跳转零信号中转、同步直调：`main.qml` 注入 `nav` → 页面 `property variant nav` → `onTriggered` 里 `createObject()` + `nav.push()`。`ComponentDefinition` 必须放 `main.qml` 根节点，`source` 用平级文件名。
5. **QML 属性绑定不能写成块** `{ ... }`（Qt 4.8 把 `{` 当对象字面量），跨行三元/`||` 都压成单行。
6. **`bar-descriptor.xml` `<icon>` 块必须写成单行** `<icon>icon.png</icon>`，不可用 `</image>` 误闭合，否则打包阶段 XML 解析失败。
7. **只保留 `access_internet` + `run_native` 权限**，不要 `access_shared`（未签名包会弹窗→闪退循环）。
8. **版本号固定 `1.0.0`，靠 `buildId` 递增拼成 `1.0.0.N`**；每次迭代只改 `bar-descriptor.xml` 的 `buildId`，不要动 C++/QML。

## 应用 ID（永远不要改）

- 包 ID：`com.doubao.biliclient`
- 改了等于换一个应用：桌面图标变成新的，沙盒目录也换掉，cookie / 下载的视频 / 设置全部读不到。

## 功能范围

推荐 / 关注 / 收藏 / 历史 / 热门 / 排行 / 分区 / 搜索 / 已下载 / 设置，播放页（含评论、相关推荐、进度拖拽、长按 3 倍速扫动），扫码登录，视频下载与离线播放。

## 权限

只保留 `access_internet` + `run_native`，**不要 `access_shared`**。

## 注意事项

- `libbbplayer.so` / `libbbnet.so` 是从 BBTube 逆向复用的流式播放后端，**不包含在本源码包中**。如需编译，请自行准备这两个文件并通过 `BBTUBE_LIB` 环境变量指定路径。
- `cacert.pem` 是公开的 CA 根证书包，可直接使用。
- 构建产物（`*.bar`、`*.exe`、`*.dll`、`bin/`、`obj/` 等）已通过 `.gitignore` 排除，不会提交到 Git 仓库。

## 备注

- 图标已圆角化（四个直角改为透明圆角，半径 28 px）。
- 构建脚本和 Makefile 中的路径已参数化，可通过环境变量适配不同机器。
- 构建需要 BlackBerry 10 真机或模拟器进行部署验证。
