import bb.cascades 1.1

// 设置/关于页（含账号登录）
//
// 【为什么登录界面直接内联在这一页，而不是 push 一个 LoginPage】
// 本工程踩过太多次 createObject() 返回 null 的坑（ComponentDefinition 必须放在
// main.qml 根节点、source 只能写 assets 根下的平级文件名…）。登录界面只是一张
// 二维码 + 一行提示 + 一个按钮，内联在 ScrollView 里零跳转、零 createObject、
// 零失败点。扫码状态全部走 biliApi 的 Q_PROPERTY 绑定（属性绑定可靠，
// C++ 信号 → QML 函数的 .connect() 在 Qt 4.8 上会静默不连接）。
Page {
    id: settingsPage

    titleBar: TitleBar {
        title: "设置"
    }

    content: ScrollView {
        scrollRole: ScrollRole.Main
        horizontalAlignment: HorizontalAlignment.Fill
        verticalAlignment: VerticalAlignment.Fill

        Container {
            // 【v87】显式铺页面底色。圆形头像靠 avatar_mask.png 把四个角"涂掉"，
            // 那张遮罩的填充色就是 #17181A（theme.bg）—— 底色不确定的话，
            // 头像四角会露出和页面不一致的方块。整个应用其它页也都是这个色。
            background: Color.create("#17181A")
            leftPadding: ui.du(20)
            rightPadding: ui.du(20)
            topPadding: ui.du(20)

            // ================= 账号 =================
            Label {
                text: "账号"
                textStyle.color: theme.accent
                textStyle.fontSize: FontSize.Medium
            }

            // ---- 已登录：头像 + 昵称 + UID + 退出 ----
            Container {
                visible: biliApi.loggedIn
                topMargin: ui.du(10)
                layout: StackLayout {}

                // 圆形头像：Cascades 没有 clip，靠「方图铺满 + 中心透明四周填底色的
                // 遮罩图叠在上层」模拟。遮罩后声明 = 在上层。
                Container {
                    preferredWidth: ui.du(48)
                    preferredHeight: ui.du(48)
                    verticalAlignment: VerticalAlignment.Center
                    layout: DockLayout {}
                    // 【v87】垫一层深灰：头像还没下完/下载失败时看到的是一个
                    // 灰色圆位，而不是一个和背景融为一体的洞。
                    background: Color.create("#26272B")

                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        imageSource: biliApi.userFace
                        scalingMethod: ScalingMethod.AspectFill
                    }
                    ImageView {
                        horizontalAlignment: HorizontalAlignment.Fill
                        verticalAlignment: VerticalAlignment.Fill
                        imageSource: "asset:///images/avatar_mask.png"
                    }
                }

                Container {
                    leftMargin: ui.du(12)
                    verticalAlignment: VerticalAlignment.Center
                    layoutProperties: StackLayoutProperties {
                        spaceQuota: 1
                    }

                    Label {
                        text: biliApi.userName.length > 0 ? biliApi.userName : "已登录"
                        textStyle.color: theme.textPrimary
                        textStyle.fontSize: FontSize.Large
                        textFormat: TextFormat.Plain
                    }
                    Label {
                        text: biliApi.userMid.length > 0 ? ("UID " + biliApi.userMid) : ""
                        visible: biliApi.userMid.length > 0
                        textStyle.color: theme.textSecondary
                        textStyle.fontSize: FontSize.Small
                    }
                }
            }

            Button {
                visible: biliApi.loggedIn
                text: "退出登录"
                topMargin: ui.du(12)
                horizontalAlignment: HorizontalAlignment.Left
                onClicked: {
                    biliApi.logout()
                }
            }

            // ---- 未登录：扫码 ----
            Container {
                visible: !biliApi.loggedIn
                topMargin: ui.du(10)

                Label {
                    // loginTip 由 C++ 按状态给出（等待扫码 / 已扫码待确认 / 已失效 …）
                    text: biliApi.loginTip.length > 0
                          ? biliApi.loginTip
                          : "登录后可同步收藏、点赞，并提升体验。"
                    textStyle.color: theme.textSecondary
                    multiline: true
                    textFormat: TextFormat.Plain
                }

                // 只在 0（空闲）和 4/5（失效/出错）时出现 —— 扫码过程中不该有按钮
                Button {
                    visible: biliApi.loginState == 0 || biliApi.loginState >= 4
                    text: biliApi.loginState == 0 ? "扫码登录" : "重新获取二维码"
                    topMargin: ui.du(10)
                    horizontalAlignment: HorizontalAlignment.Left
                    onClicked: {
                        biliApi.loginStart()
                    }
                }

                // 二维码本体：C++ 已经把它画成 PNG 落到沙盒，这里用 file:// 显示。
                ImageView {
                    horizontalAlignment: HorizontalAlignment.Center
                    topMargin: ui.du(12)
                    imageSource: biliApi.qrImageUrl
                    visible: biliApi.qrImageUrl.length > 0
                             && biliApi.loginState >= 1 && biliApi.loginState <= 3
                }

                ActivityIndicator {
                    horizontalAlignment: HorizontalAlignment.Center
                    topMargin: ui.du(6)
                    running: biliApi.loginState == 1 || biliApi.loginState == 2
                    visible: biliApi.loginState == 1 || biliApi.loginState == 2
                }

                Label {
                    text: "打开手机「小红书」→ 我的 → 扫一扫"
                    visible: biliApi.loginState == 1 || biliApi.loginState == 2
                    topMargin: ui.du(6)
                    horizontalAlignment: HorizontalAlignment.Center
                    textStyle.color: theme.textMuted
                    textStyle.fontSize: FontSize.XSmall
                }
            }

            Divider {
            }

            // ================= 关于 =================
            // 【v90】版本号不再写死。app.version 是 ApplicationUI 的 Q_PROPERTY，
            // 值由 build.sh 从 bar-descriptor.xml 生成 ——
            // 以后每次迭代只改 descriptor 里的 buildId，这里自动跟着变。
            Label {
                text: "Xiaohongshu " + app.version
                textStyle.color: theme.textPrimary
                textStyle.fontSize: FontSize.Large
                textStyle.fontWeight: FontWeight.Normal
            }
            Label {
                text: "小红书原生客户端（BlackBerry 10 / Cascades）"
                textStyle.color: theme.textSecondary
                textStyle.fontSize: FontSize.Small
            }
            // 【v100】封面诊断，可见。排查"封面不显示"时靠这一行区分：
            //   0/20   → 一张都没下到（网络/队列问题）
            //   20/20  → 全下好了，问题在界面刷新或图片解码
            //   12/20  → 部分失败，看是不是并发写坏/超时
            // 绑定 Q_PROPERTY coverStats（带 NOTIFY），会随下载实时跳动。
            Label {
                text: "封面 " + noteModel.coverStats
                textStyle.color: theme.textSecondary
                textStyle.fontSize: FontSize.XSmall
                topMargin: ui.du(4)
            }

            Divider {
            }

            Label {
                text: "架构（逆向自小红书原生客户端）"
                textStyle.color: theme.accent
                textStyle.fontSize: FontSize.Medium
                topMargin: ui.du(10)
            }
            Label {
                text: "· UI 层：Cascades QML（TabbedPane + ListView，深色主题 主色 #FB7299）"
                textStyle.color: theme.textSecondary
            }
            Label {
                text: "· 数据层：C++ BiliApi（WBI 签名 + cookie 登录态）+ NoteModel 分页 Store"
                textStyle.color: theme.textSecondary
            }
            Label {
                text: "· 存储层：SQLite 本地数据库 xiaohongshu.db（沙箱根目录）"
                textStyle.color: theme.textSecondary
            }
            Label {
                text: "· 播放层：libbbplayer（OpenMAX AL + FFmpeg）流式播放 MP4 直链"
                textStyle.color: theme.textSecondary
            }

            Divider {
            }

            Label {
                text: "说明"
                textStyle.color: theme.accent
                textStyle.fontSize: FontSize.Medium
                topMargin: ui.du(10)
            }
            Label {
                text: "· 数据来自小红书公开 Web API，未登录也可匿名浏览\n· 登录凭证只保存在本机沙盒（xiaohongshu_cookie.txt），不会外传\n· 仅供学习研究，请遵守小红书服务条款"
                textStyle.color: theme.textMuted
                multiline: true
                textFormat: TextFormat.Plain
            }

            Divider {
            }

            Label {
                text: "运行诊断"
                textStyle.color: theme.accent
                textStyle.fontSize: FontSize.Medium
                topMargin: ui.du(10)
            }
            Button {
                text: "刷新诊断信息"
                horizontalAlignment: HorizontalAlignment.Left
                onClicked: {
                    diagLabel.text = "网络/模型：\n" + app.diagText()
                        + "\n\n登录：" + (biliApi.loggedIn ? "已登录 " + biliApi.userName : "未登录")
                        + " cookie=" + biliApi.cookie().length + "B state=" + biliApi.loginState
                        + "\n\n代理环境：\n" + app.proxyEnvDiag()
                        + "\n\n播放层：\n" + app.playerLogText()
                        + "\n\n启动日志：\n" + app.bootLogText()
                        + "\n\nQML 报错：\n" + app.qmlLogText()
                }
            }
            Label {
                id: diagLabel
                text: "点上方按钮获取。若页面一直转圈，把这里的内容反馈即可定位问题。"
                textStyle.color: theme.textMuted
                textStyle.fontSize: FontSize.XSmall
                multiline: true
                textFormat: TextFormat.Plain
            }

            bottomPadding: ui.du(20)
        }
    }
}
