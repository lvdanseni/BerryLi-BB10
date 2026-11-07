using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace BiliClient.Views;

/// <summary>
/// 主窗口：左侧 TabbedPane 式导航 + 右侧 Frame 页面栈（对应 BBTube main.qml 的 TabbedPane + NavigationPane）。
/// </summary>
public partial class MainWindow : Window
{
    private readonly Dictionary<string, FrameworkElement> _pages = new();
    private string _currentTab = "home";

    public MainWindow()
    {
        InitializeComponent();
        ContentFrame.Navigated += (_, _) =>
        {
            // 视频页进入后高亮"首页"归属即可（视频页不是导航项）
        };
        NavigateTo("home");
    }

    private void OnNavClick(object sender, RoutedEventArgs e)
    {
        if (sender is Button { Tag: string tag }) NavigateTo(tag);
    }

    private void NavigateTo(string tab)
    {
        _currentTab = tab;
        HighlightNav(tab);
        if (!_pages.TryGetValue(tab, out var page))
        {
            page = tab switch
            {
                "home" => new HomePage(),
                "popular" => new PopularPage(),
                "ranking" => new RankingPage(),
                "region" => new RegionPage(),
                "search" => new SearchPage(),
                "settings" => new SettingsPage(),
                _ => new HomePage()
            };
            _pages[tab] = page;
        }
        ContentFrame.Navigate(page);
    }

    private void HighlightNav(string tab)
    {
        var inactive = (Brush)FindResource("NavInactiveBrush");
        var active = (Brush)FindResource("NavActiveBrush");
        var font = (FontWeight)FontWeights.SemiBold;

        foreach (var btn in new[] { NavHome, NavPopular, NavRanking, NavRegion, NavSearch, NavSettings })
        {
            var isActive = btn.Tag as string == tab;
            btn.Foreground = isActive ? active : inactive;
            btn.FontWeight = isActive ? FontWeights.SemiBold : FontWeights.Normal;
        }
    }

    public void OpenVideo(string bvid)
    {
        var page = new VideoPage { Bvid = bvid };
        ContentFrame.Navigate(page);
    }

    public void GoBack()
    {
        if (ContentFrame.NavigationService?.CanGoBack == true)
        {
            ContentFrame.NavigationService.GoBack();
        }
        else
        {
            NavigateTo(_currentTab);
        }
    }
}
