#!/usr/bin/env bash
# BiliClient 一键构建脚本（Windows + BB10 NDK 10.1）
#
# 解决的已知环境坑：
#   1. Windows 下 ln -s 创建符号链接失败（需管理员权限）→ 用 cp 复制替代
#   2. ntoarm-objcopy 不在 PATH（strip 步骤）→ 跳过，devMode 包无需 strip
#   3. 系统 Java 21 太新，打包器用了已被移除的 SecurityManager → 用 NDK 自带 JDK 1.6
#   4. 【最致命】qmake 生成的 Makefile 里 moc 规则目标写成 `/.moc/xxx.cpp`（丢了盘符），
#      而依赖方要的是 `../../.moc/xxx.cpp`。两者字符串不等 → make 认为"无规则可生成"，
#      只要文件存在就直接复用，从此永不检查头文件是否变更。
#      后果：改了头文件（加信号/槽/属性）后 moc 仍是旧的，元对象与类布局错位，
#      QML 一读 Q_PROPERTY 就崩溃 —— 表现为"点图标无法打开/闪退"。
#      本脚本每次构建都强制重新生成 moc，并在检测到坏路径时自动修正 Makefile。
#
# 用法：bash build.sh
# 环境变量：
#   BBNDK        NDK 根目录，默认 D:/bbndk
#   BUILD_DIR    构建输出目录，默认 ../build（相对本脚本所在目录）
#   BBTUBE_LIB   BBTube 逆向素材目录（含 libbbplayer.so / libbbnet.so），
#                默认 ../bbtube_extracted/native/lib（相对本脚本所在目录）
#   DELIVER_DIR  构建产物交付目录，默认本脚本所在目录的上级（即项目根目录）

set -u

# 脚本所在目录（即 BiliBar/），所有相对路径以此为基准
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- 可配置路径（通过环境变量覆盖）---
PROJ_DIR="$SCRIPT_DIR"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/../build}"
NDK="${BBNDK:-D:/bbndk}"
QNX_HOST="$NDK/host_10_1_0_132/win32/x86"
QNX_TARGET="$NDK/target_10_1_0_1020/qnx6"
NDK_JAVA="$NDK/jre/bin/java.exe"
LIB_DIR="$QNX_HOST/usr/lib"
MOC_DIR="$BUILD_DIR/../.moc"
OBJ_DIR="$BUILD_DIR/../.obj"
BBTUBE_LIB="${BBTUBE_LIB:-$SCRIPT_DIR/../bbtube_extracted/native/lib}"
DELIVER_DIR="${DELIVER_DIR:-$SCRIPT_DIR/..}"

echo "==> [0/6] 生成版本号头文件（数据源：bar-descriptor.xml）"
# 【为什么在构建时生成，而不是在 C++ 里写死】
# 版本号只在 bar-descriptor.xml 里维护一处，改完 buildId 直接跑本脚本，
# app_version.hpp 自动跟着变 —— 不会出现"设置页显示 1.0.0.88、安装包其实是 .90"。
# 生成之后必须删掉 applicationui.o：Makefile 的依赖列表里没有这个新头文件，
# 不删的话 make 认为 applicationui.cpp 没变，版本号会一直停留在上一次编译的值。
APP_VER=$(sed -n 's:.*<versionNumber>\([^<]*\)</versionNumber>.*:\1:p' "$PROJ_DIR/bar-descriptor.xml" | head -1 | tr -d ' \r')
APP_BID=$(sed -n 's:.*<buildId>\([^<]*\)</buildId>.*:\1:p' "$PROJ_DIR/bar-descriptor.xml" | head -1 | tr -d ' \r')
if [ -z "$APP_VER" ] || [ -z "$APP_BID" ]; then
    echo "!!! 无法从 bar-descriptor.xml 解析版本号（versionNumber='$APP_VER' buildId='$APP_BID'）"
    exit 1
fi
cat > "$PROJ_DIR/src/app_version.hpp" <<EOF
#ifndef APP_VERSION_HPP
#define APP_VERSION_HPP
// 本文件由 build.sh 自动生成，请勿手工编辑。
// 数据源：bar-descriptor.xml 的 <versionNumber> + <buildId>。
#define APP_VERSION_STRING "$APP_VER.$APP_BID"
#endif
EOF
rm -f "$OBJ_DIR/applicationui.o"
echo "    版本号 = $APP_VER.$APP_BID"

echo "==> [1/6] 同步源码"
mkdir -p "$BUILD_DIR" || exit 1
cd "$BUILD_DIR" || exit 1
cp -f "$PROJ_DIR/src/"*.cpp "$PROJ_DIR/src/"*.hpp src/ || exit 1
cp -f "$PROJ_DIR/bar-descriptor.xml" . 2>/dev/null
# Makefile 是手工维护的（qmake 生成后改过），也要同步，否则本机改了 SOURCES/OBJECTS
# （比如新增 bbplayer_wrapper.cpp）不会进构建。注意：下面 [2/6] 会修正 moc 路径，
# 所以这里拷的是 qmake 原样的 /.moc/，[2/6] 会把它改成 ../../.moc/。
cp -f "$PROJ_DIR/Makefile" . 2>/dev/null
# 应用图标：bar-descriptor 里 <asset path="icon.png"> 指的是**构建目录**下的
# icon.png，不是 assets 里的。漏拷这一行的话，改了源图标但包里还是旧图标
# （构建目录里那份是历史遗留的，会一直沿用下去）。
cp -f "$PROJ_DIR/icon.png" . 2>/dev/null
# 资源：含 components 子目录（FeedPage.qml / RegionChip.qml 在这里，漏拷会导致 QML 导入失败）
mkdir -p o.le-v7/assets/components assets/components
# 播放控件图标（v67：从 BBTube 的 bb-shared/media/images 复用 —— disc/play/pause/knob
# 这几个是 BBTube 真机验证过的，96x96 带 alpha，配 filterColor 染色）。
# 漏拷的后果是 ImageView 空白，中央播放按钮变成一个看不见的透明块。
mkdir -p o.le-v7/assets/images assets/images
cp -f "$PROJ_DIR/assets/images/"*.png o.le-v7/assets/images/ 2>/dev/null
# .amd 是九宫格（9-slice）资源描述，缺了它圆角图会被当普通图整张拉伸，
# 圆角就变成"被拉扁的圆角"。文件名必须是 <png名>.amd。
cp -f "$PROJ_DIR/assets/images/"*.amd o.le-v7/assets/images/ 2>/dev/null
cp -f "$PROJ_DIR/assets/"*.qml o.le-v7/assets/ 2>/dev/null
cp -f "$PROJ_DIR/assets/"*.png o.le-v7/assets/ 2>/dev/null
# CA 根证书包：libbbnet 的静态 curl 默认找不到系统 CA 库，必须显式
# bbnet_ca_add_file(<path>) 装载，否则所有 https 请求报
# "Problem with the SSL CA cert (path? access rights?)"（v50 真机实证）。
cp -f "$PROJ_DIR/assets/"*.pem o.le-v7/assets/ 2>/dev/null
cp -f "$PROJ_DIR/assets/components/"*.qml o.le-v7/assets/components/ 2>/dev/null
cp -f "$PROJ_DIR/assets/"*.qml assets/ 2>/dev/null
cp -f "$PROJ_DIR/assets/"*.pem assets/ 2>/dev/null

echo "==> [1b] 同步 BBTube 逆向复用的流式播放后端 .so"
# libbbplayer.so（OpenMAX AL + 静态 FFmpeg）+ libbbnet.so（curl 流式 reader）。
# SONAME 均为 libbbplayer.so / libbnet.so（无版本后缀），与 BBTube.so 的 NEEDED 一致；
# 运行时由 VideoPlayer 通过 dlopen 加载，落到 app/native/lib（LD_LIBRARY_PATH 已含）。
cp -f "$BBTUBE_LIB/libbbplayer.so" o.le-v7/ 2>/dev/null
cp -f "$BBTUBE_LIB/libbbnet.so" o.le-v7/ 2>/dev/null
if [ ! -f o.le-v7/libbbplayer.so ] || [ ! -f o.le-v7/libbbnet.so ]; then
    echo "!!! 未找到 libbbplayer.so / libbnet.so"
    echo "    请设置环境变量 BBTUBE_LIB 指向含这两个 .so 的目录"
    echo "    当前尝试路径：$BBTUBE_LIB"
    exit 1
fi

echo "==> [2/6] 修正 Makefile 中的 moc 路径（防坑 4）"
# 只在 `/ .moc/` 前面不是 `..` 时才替换，否则会把已正确的 ../../.moc/ 改成 ../..../../.moc/
if grep -qE '(^|[[:space:]])/\.moc/' Makefile 2>/dev/null; then
    sed -i -E 's#(^|[[:space:]])/\.moc/#\1../../.moc/#g' Makefile
    echo "    已修正 Makefile 中缺失盘符的 moc 路径"
fi

# 播放器用 bb::multimedia::MediaPlayer（libbbmultimedia.so，系统自带）。
# Makefile 是手工维护的，不会被 qmake 重新生成，所以这里幂等地补 -l。
# 漏掉这条的后果：编译零告警、链接成功，但设备上 dlopen 解析失败 → 应用完全打不开。
if ! grep -q "lbbmultimedia" Makefile 2>/dev/null; then
    sed -i 's/^LIBS\( *\)= \(.*\)$/LIBS\1= \2 -lbbmultimedia/' Makefile
    echo "    已在 Makefile 的 LIBS 中补 -lbbmultimedia"
fi

echo "==> [3/6] 强制重新生成 moc"
rm -f "$MOC_DIR"/moc_*.cpp "$OBJ_DIR"/moc_*.o
echo "    已清理旧 moc，make 将按头文件重新生成"

echo "==> [4/6] 编译（qcc -Vgcc_ntoarmv7le）"
export QNX_HOST QNX_TARGET
export PATH="$QNX_HOST/usr/bin:$PATH"
"$QNX_HOST/usr/bin/make.exe" -j2 > make_log.txt 2>&1

# 真实编译错误（排除已知无害的 ln -s / ntoarm-objcopy）
REAL_ERR=$(grep -E "error:" make_log.txt | grep -v "ntoarm-objcopy" | head -20)
if [ -n "$REAL_ERR" ]; then
    echo "!!! 编译失败："
    echo "$REAL_ERR"
    exit 1
fi
if [ ! -f libBiliClient.so.1.0.0 ]; then
    echo "!!! 未生成 libBiliClient.so.1.0.0"
    tail -20 make_log.txt
    exit 1
fi
WARN=$(grep -cE "warning:" make_log.txt)
echo "    编译完成，告警数=$WARN"

# 关键自检：moc 必须真的重新生成过，否则元对象可能是旧的
if [ ! -f "$MOC_DIR/moc_bili_api.cpp" ] || [ ! -f "$MOC_DIR/moc_feed_model.cpp" ]; then
    echo "!!! moc 未生成，元对象会缺失，应用将无法启动"
    exit 1
fi
if ! grep -q "netReply(QString,QString,bool)" "$MOC_DIR/moc_bili_api.cpp"; then
    echo "!!! moc_bili_api.cpp 缺少 netReply 信号（moc 未更新）"
    exit 1
fi
if ! grep -q "onNetReply(QString,QString,bool)" "$MOC_DIR/moc_feed_model.cpp"; then
    echo "!!! moc_feed_model.cpp 缺少 onNetReply 槽（moc 未更新）"
    exit 1
fi
echo "    moc 自检通过"

# 关键自检 2：未解析的 bb:: 符号。
# ELF 共享库允许把符号留到运行期解析 —— 编译零告警、链接成功、打包成功，
# 但设备上 dlopen 找不到这些符号，应用完全打不开，连崩溃日志都没有。
# Makefile 的 LIBS 里只有 -lbbdata -lbbsystem -lbps（没有 -lbbdevice / -lbbplatform），
# 所以只要出现这些命名空间的 U 符号，就必然是坑。
UNRESOLVED=$(ntoarm-nm.exe -u libBiliClient.so.1.0.0 2>/dev/null \
             | grep -E "_ZN2bb6device|_ZN2bb8platform|_ZN2bb7network|_ZN2bb11permissions|_ZN2bb10multimedia")
if [ -n "$UNRESOLVED" ]; then
    echo "!!! 发现未解析的 bb:: 符号（LIBS 未链接对应库）："
    echo "$UNRESOLVED"
    echo "    这类符号编译不报错，但设备上 dlopen 失败 → 应用完全打不开。"
    echo "    解决：改用纯 QML 实现，或在 Makefile 的 LIBS 里补 -l<库名>"
    exit 1
fi
echo "    符号自检通过（无未链接的 bb:: 依赖）"

echo "==> [5/6] 补齐 so 副本（替代 ln -s）"
for v in libBiliClient.so libBiliClient.so.1 libBiliClient.so.1.0; do
    cp -f libBiliClient.so.1.0.0 "$v"
done
cp -f libBiliClient.so.1.0.0 o.le-v7/BiliClient.so

echo "==> [6/6] 打包 devMode BAR（NDK JDK 1.6）"
# 【v90】产物名跟着应用名走：BiliClient.bar → Xiaohongshu.bar。
# 注意 libBiliClient.so / o.le-v7/BiliClient.so 不改名 —— 那是 descriptor 里
# <asset entry="true"> 指定的入口 so 名，改了要同步改 descriptor 和 Makefile 的 TARGET。
BAR_NAME="Xiaohongshu.bar"
rm -f "$BAR_NAME" BiliClient.bar
"$NDK_JAVA" -Djava.awt.headless=true -Xmx512M \
    -cp "$LIB_DIR/EccpressoJDK15ECC.jar;$LIB_DIR/EccpressoAll.jar;$LIB_DIR/TrustpointAll.jar;$LIB_DIR/TrustpointJDK15.jar;$LIB_DIR/TrustpointProviders.jar;$LIB_DIR/BarPackager.jar;$LIB_DIR/BarSigner.jar;$LIB_DIR/BarDeploy.jar;$LIB_DIR/BarAir.jar" \
    com.qnx.bbt.nativepackager.BarNativePackager \
    -devMode -package "$BAR_NAME" bar-descriptor.xml 2>&1 | tail -5

if [ -f "$BAR_NAME" ]; then
    ls -la "$BAR_NAME" libBiliClient.so.1.0.0
    echo "OK: $BUILD_DIR/$BAR_NAME"
    # 交付产物到 DELIVER_DIR
    cp -f "$BAR_NAME" "$DELIVER_DIR/$BAR_NAME" && echo "delivered: $DELIVER_DIR/$BAR_NAME"
else
    echo "!!! 打包失败"
    exit 1
fi
