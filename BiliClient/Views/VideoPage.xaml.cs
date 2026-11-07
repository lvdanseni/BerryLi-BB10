using System.Windows;
using System.Windows.Controls;
using BiliClient.Api;
using BiliClient.Stores;

namespace BiliClient.Views;

/// <summary>
/// 视频详情 + 播放页。对应 BBTube VideoPage.qml：
/// 原生 UI（标题/数据/药丸操作/UP 主/简介/评论）+ WebView2 播放引擎（对应 ytengine）。
/// </summary>
public partial class VideoPage : UserControl
{
    public static readonly DependencyProperty BvidProperty =
        DependencyProperty.Register(nameof(Bvid), typeof(string), typeof(VideoPage), new PropertyMetadata("", OnBvidChanged));

    public static readonly DependencyProperty OwnerFaceProperty =
        DependencyProperty.Register(nameof(OwnerFace), typeof(string), typeof(VideoPage), new PropertyMetadata(""));

    public string Bvid
    {
        get => (string)GetValue(BvidProperty);
        set => SetValue(BvidProperty, value);
    }

    public string OwnerFace
    {
        get => (string)GetValue(OwnerFaceProperty);
        set => SetValue(OwnerFaceProperty, value);
    }

    private VideoDetailData? _detail;

    public VideoPage()
    {
        InitializeComponent();
        Loaded += async (_, _) =>
        {
            if (_detail == null && Bvid != "")
            {
                await EnsureWebViewAsync();
                await LoadDetailAsync();
            }
        };
        SizeChanged += (_, _) => RecalcPlayerBox();
        DataContext = this;
    }

    private static void OnBvidChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var page = (VideoPage)d;
        page._detail = null;
        _ = page.LoadDetailAsync();
    }

    private async Task EnsureWebViewAsync()
    {
        if (Player.CoreWebView2 != null) return;
        try
        {
            var env = await Microsoft.Web.WebView2.Core.CoreWebView2Environment.CreateAsync(null, null,
                new Microsoft.Web.WebView2.Core.CoreWebView2EnvironmentOptions { AreBrowserExtensionsEnabled = false });
            await Player.EnsureCoreWebView2Async(env);
        }
        catch (Exception ex)
        {
            ShowPlayerError("WebView2 初始化失败：" + ex.Message);
        }
    }

    private void RecalcPlayerBox()
    {
        var w = Math.Max(320, ActualWidth - 56);
        PlayerBorder.Height = w * 9.0 / 16.0;
    }

    private async Task LoadDetailAsync()
    {
        if (Bvid == "") return;
        DetailLoading.Visibility = Visibility.Visible;
        DetailError.Visibility = Visibility.Collapsed;
        try
        {
            _detail = await ApiHolder.Api.GetVideoDetailAsync(Bvid);
            if (_detail is null) throw new InvalidOperationException("视频不存在");

            TitleText.Text = _detail.Title;
            OwnerFace = _detail.Owner?.Face ?? "";
            OwnerName.Text = _detail.Owner?.Name ?? "";
            OwnerMid.Text = _detail.Owner != null ? "UID: " + _detail.Owner.Mid : "";
            DescText.Text = string.IsNullOrWhiteSpace(_detail.Desc) ? "（暂无简介）" : _detail.Desc;

            var st = _detail.Stat;
            var parts = new List<string>();
            if (st != null)
            {
                parts.Add(App.ViewCountToText(st.View) + "播放");
                parts.Add(App.ViewCountToText(st.Danmaku) + "弹幕");
                parts.Add(App.ViewCountToText(st.Like) + "点赞");
                parts.Add(App.ViewCountToText(st.Coin) + "投币");
                parts.Add(App.ViewCountToText(st.Favorite) + "收藏");
                parts.Add(App.ViewCountToText(st.Share) + "分享");
            }
            parts.Add($"{DateTimeOffset.FromUnixTimeSeconds(_detail.Pubdate).ToLocalTime():yyyy-MM-dd}");
            if (_detail.TName != "") parts.Add(_detail.TName);
            StatText.Text = string.Join(" · ", parts);

            var cid = _detail.FirstPage?.Cid ?? _detail.Cid;
            Play(bvid: _detail.Bvid, cid: cid, page: _detail.FirstPage?.Page ?? 1);

            _ = LoadCommentsAsync(_detail.Aid);
        }
        catch (Exception ex)
        {
            DetailLoading.Visibility = Visibility.Collapsed;
            DetailError.Visibility = Visibility.Visible;
            DetailErrorMsg.Text = ex.Message;
            return;
        }
        DetailLoading.Visibility = Visibility.Collapsed;
    }

    private void Play(string bvid, long cid, int page)
    {
        if (Player.CoreWebView2 == null) return;
        var url = BiliApi.BuildPlayerUrl(bvid, cid, page);
        Player.Source = new Uri(url);
        Player.Visibility = Visibility.Visible;
        PlayerLoading.Visibility = Visibility.Collapsed;
    }

    private async Task LoadCommentsAsync(long aid)
    {
        try
        {
            var comments = await ApiHolder.Api.GetCommentsAsync(aid);
            CommentHeader.Text = "评论 (" + comments.Count + "+)";
            CommentList.ItemsSource = comments;
        }
        catch
        {
            CommentHeader.Text = "评论";
        }
    }

    private void ShowPlayerError(string msg)
    {
        PlayerLoading.Visibility = Visibility.Collapsed;
        PlayerError.Visibility = Visibility.Visible;
        PlayerErrorMsg.Text = msg;
    }

    private void OnBackClick(object sender, RoutedEventArgs e)
    {
        if (Window.GetWindow(this) is MainWindow mw) mw.GoBack();
    }

    private void OnCopyLink(object sender, RoutedEventArgs e)
    {
        try
        {
            System.Windows.Clipboard.SetText(BiliApi.BuildWatchUrl(Bvid));
            StatusToast("链接已复制");
        }
        catch
        {
            // clipboard busy
        }
    }

    private void OnOpenInBrowser(object sender, RoutedEventArgs e)
    {
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(BiliApi.BuildWatchUrl(Bvid))
        {
            UseShellExecute = true
        });
    }

    private void OnReloadPlayer(object sender, RoutedEventArgs e)
    {
        if (_detail == null) return;
        Player.Visibility = Visibility.Collapsed;
        PlayerLoading.Visibility = Visibility.Visible;
        PlayerError.Visibility = Visibility.Collapsed;
        var cid = _detail.FirstPage?.Cid ?? _detail.Cid;
        Play(_detail.Bvid, cid, _detail.FirstPage?.Page ?? 1);
    }

    private void OnRetryDetail(object sender, RoutedEventArgs e) => _ = LoadDetailAsync();

    private void StatusToast(string text)
    {
        var toast = new System.Windows.Controls.Primitives.Popup
        {
            Placement = System.Windows.Controls.Primitives.PlacementMode.Center,
            AllowsTransparency = true,
            IsHitTestVisible = false
        };
        var border = new Border
        {
            Background = App.CardBg,
            CornerRadius = new CornerRadius(8),
            Padding = new Thickness(16, 8, 16, 8),
            Child = new TextBlock { Text = text, Foreground = App.TextPrimary, FontSize = 13 }
        };
        toast.Child = border;
        toast.IsOpen = true;
        var t = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromSeconds(1.6) };
        t.Tick += (_, _) => { toast.IsOpen = false; t.Stop(); };
        t.Start();
    }
}
