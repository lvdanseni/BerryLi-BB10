using System.Windows;
using System.Windows.Controls;
using BiliClient.Stores;

namespace BiliClient.Views;

public partial class PopularPage : UserControl
{
    public PopularPage()
    {
        InitializeComponent();
        Feed.Store = new PopularStore();
    }

    private void OnVideoRequested(string bvid)
        => (Window.GetWindow(this) as MainWindow)?.OpenVideo(bvid);
}
