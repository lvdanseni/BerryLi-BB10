using System.Windows;
using System.Windows.Controls;
using BiliClient.Stores;

namespace BiliClient.Views;

public partial class HomePage : UserControl
{
    public HomePage()
    {
        InitializeComponent();
        Feed.Store = new RcmdStore();
    }

    private void OnVideoRequested(string bvid)
        => (Window.GetWindow(this) as MainWindow)?.OpenVideo(bvid);
}
