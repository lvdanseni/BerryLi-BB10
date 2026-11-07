using System.Collections.Concurrent;
using System.Net.Http;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace BiliClient.Controls;

/// <summary>
/// 异步远程图片（对应 BBTube RemoteImage.qml + AdvancedImageClient）：
/// 内存缓存已解码帧（冻结），按 URL 去重下载。
/// </summary>
public partial class RemoteImage : UserControl
{
    private static readonly ConcurrentDictionary<string, BitmapSource> Cache = new();
    private static readonly HttpClient Client = CreateClient();

    private static HttpClient CreateClient()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };
        c.DefaultRequestHeaders.UserAgent.ParseAdd("Mozilla/5.0 (Windows NT 10.0; Win64; x64) BiliClient/1.0");
        return c;
    }

    public static readonly DependencyProperty SourceUrlProperty =
        DependencyProperty.Register(nameof(SourceUrl), typeof(string), typeof(RemoteImage),
            new PropertyMetadata("", OnSourceUrlChanged));

    public string SourceUrl
    {
        get => (string)GetValue(SourceUrlProperty);
        set => SetValue(SourceUrlProperty, value);
    }

    public RemoteImage()
    {
        InitializeComponent();
        ClipToBounds = true;
    }

    private static async void OnSourceUrlChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        var ctrl = (RemoteImage)d;
        var url = e.NewValue as string ?? "";
        ctrl.Img.Visibility = Visibility.Collapsed;
        ctrl.Placeholder.Visibility = Visibility.Visible;
        ctrl.Spinner.Visibility = Visibility.Collapsed;
        if (string.IsNullOrWhiteSpace(url)) return;

        if (Cache.TryGetValue(url, out var cached))
        {
            ctrl.Apply(cached);
            return;
        }
        ctrl.Spinner.Visibility = Visibility.Visible;
        try
        {
            using var resp = await Client.GetAsync(url, HttpCompletionOption.ResponseHeadersRead);
            resp.EnsureSuccessStatusCode();
            var data = await resp.Content.ReadAsByteArrayAsync();
            var bmp = new BitmapImage();
            bmp.BeginInit();
            bmp.CacheOption = BitmapCacheOption.OnLoad;
            bmp.StreamSource = new System.IO.MemoryStream(data);
            bmp.EndInit();
            bmp.Freeze();
            Cache.TryAdd(url, bmp);
            ctrl.Apply(bmp);
        }
        catch
        {
            ctrl.Spinner.Visibility = Visibility.Collapsed;
        }
    }

    private void Apply(BitmapSource bmp)
    {
        Spinner.Visibility = Visibility.Collapsed;
        Placeholder.Visibility = Visibility.Collapsed;
        Img.Source = bmp;
        Img.Visibility = Visibility.Visible;
    }
}
