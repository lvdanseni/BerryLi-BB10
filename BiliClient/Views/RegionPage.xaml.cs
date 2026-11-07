using System.Windows;
using System.Windows.Controls;
using BiliClient.Stores;

namespace BiliClient.Views;

public partial class RegionPage : UserControl
{
    private static readonly (int Rid, string Name)[] Regions =
    {
        (1, "动画"), (3, "音乐"), (6, "游戏"), (36, "知识"), (188, "科技"),
        (181, "影视"), (160, "生活"), (119, "鬼畜"), (95, "数码"), (129, "舞蹈"), (211, "美食")
    };

    private RegionStore? _store;
    private int _currentRid = -1;

    public RegionPage()
    {
        InitializeComponent();
        BuildChips();
    }

    private void BuildChips()
    {
        foreach (var (rid, name) in Regions)
        {
            var btn = new Button
            {
                Content = name,
                Style = (Style)FindResource("PillButtonStyle"),
                Margin = new Thickness(0, 0, 10, 0),
                Tag = rid,
                Foreground = (System.Windows.Media.Brush)FindResource("TextSecondaryBrush")
            };
            btn.Click += (_, _) => SelectRegion(rid);
            RegionChips.Children.Add(btn);
        }
        SelectRegion(Regions[0].Rid);
    }

    private void SelectRegion(int rid)
    {
        if (rid == _currentRid) return;
        _currentRid = rid;
        _store = new RegionStore(rid);
        Feed.Store = _store;
    }

    private void OnVideoRequested(string bvid)
        => (Window.GetWindow(this) as MainWindow)?.OpenVideo(bvid);
}
