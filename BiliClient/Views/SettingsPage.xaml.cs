using System.Windows;
using System.Windows.Controls;

namespace BiliClient.Views;

public partial class SettingsPage : UserControl
{
    public SettingsPage()
    {
        InitializeComponent();
    }

    private async void OnTestApi(object sender, RoutedEventArgs e)
    {
        TestResult.Text = "正在测试…";
        var sb = new System.Text.StringBuilder();
        try
        {
            var api = Stores.ApiHolder.Api;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var rcmd = await api.GetRcmdAsync(0, 5);
            sw.Stop();
            sb.AppendLine($"✓ 首页推荐（WBI 签名）: {rcmd.Count} 条, {sw.ElapsedMilliseconds}ms");
        }
        catch (Exception ex)
        {
            sb.AppendLine("✗ 首页推荐: " + ex.Message);
        }
        try
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var detail = await Stores.ApiHolder.Api.GetVideoDetailAsync("BV1GJ411x7h7");
            sw.Stop();
            sb.AppendLine($"✓ 视频详情: {detail.Title} · {sw.ElapsedMilliseconds}ms");
        }
        catch (Exception ex)
        {
            sb.AppendLine("✗ 视频详情: " + ex.Message);
        }
        TestResult.Text = sb.ToString();
    }
}
