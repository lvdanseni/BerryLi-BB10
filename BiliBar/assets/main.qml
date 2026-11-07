import bb.cascades 1.1

import "."
// 全局导航：TabbedPane 根（Xiaohongshu 原生客户端），每个 Tab 内嵌一个 NavigationPane
//
// 【点击跳转的实现方式】
// 不用 C++ 信号、不用 Connections（Cascades 1.1 里根本没有 Connections 这个类型）、
// 也不做多跳 QML 信号转发（Qt 4.8 的 QML 里信号命名参数注入不可靠，会静默断链）。
// 做法是把每个 Tab 的 NavigationPane 通过 nav 属性直接注入给页面，
// 由 NotePage 在 onTriggered 里同步完成 createObject + push。
TabbedPane {
    id: tabs
    activeTab: homeTab

    /**
     * 【v83】切 Tab 时的按需加载。
     *
     * 用 tabs.activeTab 这个**属性**判断，不要用信号参数 activeTab ——
     * 参数名和属性名同名，在 Qt 4.8 的 QML 里会被遮蔽（铁律 4 那类坑）。
     * 关注流依赖登录态，必须在切过去的时候才拉：启动时用户多半还没登录。
     */
    onActiveTabChanged: {
        if (tabs.activeTab === followTab) {
            followStore.reloadIfStale();
        } else if (tabs.activeTab === collectTab) {
            collectStore.reload();
        } else if (tabs.activeTab === historyTab) {
            // 历史列表非空时 reloadIfStale 不会重拉 —— 但这里就是要它"看过的才准"，
            // 用户刚看完一个笔记回来，列表里应该立刻有它。所以每次切过来都重拉。
            historyStore.reload();
        }
    }

    /** 供页面的"去登录"按钮跳转到设置页（JS 函数调用，可靠）。 */
    function openSettings() {
        tabs.activeTab = settingsTab;
    }

    // 【v90】每个 Tab 都要显式给 imageSource。
    // 不给的话 Cascades 会统一塞一个系统默认齿轮图标 —— 多个齿轮长得一模一样，
    // 完全分不清哪个是哪个板块。图标由 make_tab_icons.py 生成（96x96 线条图）。
    Tab {
        id: homeTab
        title: "首页"
        imageSource: "asset:///images/tab_home.png"
        NavigationPane {
            id: homeNav
            // 页面被 pop（含底部返回箭头、系统手势）时停掉播放器。
            // MediaPlayer 是 C++ 单例，页面销毁不会连带停播，不停会"返回后还在放声音"。
            onPopTransitionEnded: {
                videoPlayer.stop();
                noteStore.stopStream();
            }
            HomePage {
                nav: homeNav
                host: tabs
            }
        }
    }
    Tab {
        id: discoverTab
        title: "发现"
        imageSource: "asset:///images/tab_discover.png"
        NavigationPane {
            id: discoverNav
            onPopTransitionEnded: {
                videoPlayer.stop();
                noteStore.stopStream();
            }
            DiscoverPage {
                nav: discoverNav
                host: tabs
            }
        }
    }
    Tab {
        id: noteTab
        title: "笔记"
        imageSource: "asset:///images/tab_note.png"
        NavigationPane {
            id: noteNav
            onPopTransitionEnded: {
                videoPlayer.stop();
                noteStore.stopStream();
            }
            NotePage {
                nav: noteNav
                host: tabs
            }
        }
    }
    Tab {
        id: collectTab
        title: "收藏"
        imageSource: "asset:///images/tab_collect.png"
        NavigationPane {
            id: collectNav
            onPopTransitionEnded: {
                videoPlayer.stop();
                noteStore.stopStream();
            }
            CollectPage {
                nav: collectNav
                host: tabs
            }
        }
    }
    Tab {
        id: historyTab
        title: "历史"
        imageSource: "asset:///images/tab_history.png"
        NavigationPane {
            id: historyNav
            onPopTransitionEnded: {
                videoPlayer.stop();
                noteStore.stopStream();
            }
            HistoryPage {
                nav: historyNav
                host: tabs
            }
        }
    }
    Tab {
        id: followTab
        title: "关注"
        imageSource: "asset:///images/tab_follow.png"
        NavigationPane {
            id: followNav
            onPopTransitionEnded: {
                videoPlayer.stop();
                noteStore.stopStream();
            }
            FollowPage {
                nav: followNav
                host: tabs
            }
        }
    }
    Tab {
        id: settingsTab
        title: "设置"
        imageSource: "asset:///images/tab_settings.png"
        NavigationPane {
            id: settingsNav
            SettingsPage {
            }
        }
    }

    // 视频/笔记详情页工厂 + 建页 push，全部放在根节点完成 —— 这是 BBTube 唯一验证过的位置
    // （ComponentDefinition 与调用 createObject 的函数必须在同一个文档作用域内）。
    // source 用 assets 根目录下的平级文件名，不要写 ../，也不要用 asset:///。
    attachedObjects: [
        ComponentDefinition {
            id: notePageDef
            source: "NotePage.qml"
        },
        // 【诊断探针】最小页面，区分「路径/机制问题」与「NotePage 内容有错」
        ComponentDefinition {
            id: probePageDef
            source: "ProbePage.qml"
        }
    ]

    // 在指定 NavigationPane 上 push 笔记详情页。返回字符串结果供诊断显示。
    function pushNote(nav, noteId) {
        if (!nav || !noteId || noteId.length == 0) {
            return "bad-args";
        }
        if (!notePageDef) {
            return "def-obj-null";
        }
        var page = notePageDef.createObject();
        if (!page) {
            return "def-null";
        }
        page.noteId = noteId;
        // back 是 NotePage 自己声明的 QML 信号，对 QML 信号 .connect(匿名函数) 可靠
        page.back.connect(function() {
            videoPlayer.stop();
            // 流式代理占着一条到 CDN 的连接，页面退出必须掐掉，
            // 否则会在后台继续吃掉几十 KB/s 的流量。
            noteStore.stopStream();
            nav.pop();
        });
        nav.push(page);
        return "pushed";
    }

    // 探针：probe-ok = 路径与机制正常，失败在 NotePage 内容；probe-fail = 路径问题
    function probe() {
        if (!probePageDef) {
            return "probe-nodef";
        }
        var p = probePageDef.createObject();
        return p ? "probe-ok" : "probe-fail";
    }
}
