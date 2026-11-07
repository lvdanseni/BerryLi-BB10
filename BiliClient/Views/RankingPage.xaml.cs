using System.Windows;
using System.Windows.Controls;
using BiliClient.Stores;

namespace BiliClient.Views;

public partial class RankingPage : UserControl
{
    public RankingPage()
    {
        InitializeComponent();
        Feed.Store = new RankingStore();
    }

    private void OnVideoRequested(string bvid)
        => (Window.GetWindow(this) as MainWindow)?.OpenVideo(bvid);
}
