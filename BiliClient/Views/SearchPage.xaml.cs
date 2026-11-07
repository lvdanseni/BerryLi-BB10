using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using BiliClient.Stores;

namespace BiliClient.Views;

public partial class SearchPage : UserControl
{
    private readonly SearchStore _store = new();
    private readonly List<string> _history = new();

    public SearchPage()
    {
        InitializeComponent();
        Feed.Store = _store;
        Loaded += (_, _) => SearchBox.Focus();
    }

    private void OnSearchTextChanged(object sender, TextChangedEventArgs e)
        => SearchHint.Visibility = SearchBox.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;

    private void OnHintClick(object sender, MouseButtonEventArgs e) => SearchBox.Focus();

    private void OnSearchKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter) RunSearch();
    }

    private void OnSearchClick(object sender, RoutedEventArgs e) => RunSearch();

    private void RunSearch()
    {
        var q = SearchBox.Text.Trim();
        if (q == "") return;
        _store.Keyword = q;
        _ = _store.SearchAsync();
        Remember(q);
    }

    private void Remember(string q)
    {
        _history.Remove(q);
        _history.Insert(0, q);
        if (_history.Count > 6) _history.RemoveAt(_history.Count - 1);
        HistoryChips.Children.Clear();
        foreach (var h in _history)
        {
            var btn = new Button
            {
                Content = h,
                Style = (Style)FindResource("PillButtonStyle"),
                Margin = new Thickness(0, 0, 10, 0),
                Foreground = (System.Windows.Media.Brush)FindResource("TextSecondaryBrush")
            };
            btn.Click += (_, _) =>
            {
                SearchBox.Text = h;
                RunSearch();
            };
            HistoryChips.Children.Add(btn);
        }
        HistoryChips.Visibility = Visibility.Visible;
    }

    private void OnVideoRequested(string bvid)
        => (Window.GetWindow(this) as MainWindow)?.OpenVideo(bvid);
}
