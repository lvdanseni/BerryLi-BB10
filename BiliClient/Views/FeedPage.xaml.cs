using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Windows;
using System.Windows.Controls;
using BiliClient.Api;

namespace BiliClient.Views;

/// <summary>
/// 通用分页卡片页。对应 BBTube 的 HomePage：同一页面复用不同 store（推荐/热门/排行/分区/搜索）。
/// </summary>
public partial class FeedPage : UserControl
{
    public static readonly DependencyProperty PageTitleProperty =
        DependencyProperty.Register(nameof(PageTitle), typeof(string), typeof(FeedPage), new PropertyMetadata(""));

    public static readonly DependencyProperty StoreProperty =
        DependencyProperty.Register(nameof(Store), typeof(object), typeof(FeedPage), new PropertyMetadata(null, OnStoreChanged));

    public string PageTitle
    {
        get => (string)GetValue(PageTitleProperty);
        set => SetValue(PageTitleProperty, value);
    }

    public object? Store
    {
        get => GetValue(StoreProperty);
        set => SetValue(StoreProperty, value);
    }

    private INotifyCollectionChanged? _bound;

    public FeedPage()
    {
        InitializeComponent();
        Loaded += (_, _) =>
        {
            BindCollection();
            _ = TryLoadAsync();
        };
        DataContext = this;
    }

    private static void OnStoreChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var page = (FeedPage)d;
        page.BindCollection();
    }

    private void BindCollection()
    {
        if (_bound != null) _bound.CollectionChanged -= OnItemsChanged;
        _bound = null;
        if (Store is IPagedStore ps && ps.Items is INotifyCollectionChanged ncc)
        {
            _bound = ncc;
            ncc.CollectionChanged += OnItemsChanged;
        }
        OnItemsChanged();
    }

    private void OnItemsChanged(object? sender = null, NotifyCollectionChangedEventArgs? e = null)
    {
        CardList.ItemsSource = null;
        if (Store is IPagedStore ps) CardList.ItemsSource = ps.Items;
    }

    private async Task TryLoadAsync()
    {
        if (Store is IPagedStore ps && ps.Items.Cast<object?>().Any() == false) await ps.LoadAsync();
    }

    private void OnScrollChanged(object sender, ScrollChangedEventArgs e)
    {
        if (e.VerticalOffset + e.ViewportHeight >= e.ExtentHeight - 80)
        {
            if (Store is IPagedStore ps) _ = ps.LoadMoreAsync();
        }
    }

    private void OnRefreshClick(object sender, RoutedEventArgs e)
    {
        if (Store is IPagedStore ps) _ = ps.LoadAsync();
    }

    private void OnRetryClick(object sender, RoutedEventArgs e) => OnRefreshClick(sender, e);

    private void OnCardClick(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement fe) return;
        var data = fe.DataContext;
        var bvid = data switch
        {
            VideoItem v => v.Bvid,
            SearchVideo s => s.Bvid,
            RegionVideo r => r.Bvid,
            _ => ""
        };
        if (bvid == "") return;
        OpenVideo(bvid);
    }

    public event Action<string>? VideoRequested;

    private void OpenVideo(string bvid) => VideoRequested?.Invoke(bvid);
}
