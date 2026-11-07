using System.Windows;
using System.Windows.Media;

namespace BiliClient;

public partial class App : Application
{
    public static readonly Brush Accent = Create("#FB7299");
    public static readonly Brush AccentDark = Create("#E5497A");
    public static readonly Brush Bg = Create("#17181A");
    public static readonly Brush CardBg = Create("#1F2124");
    public static readonly Brush BorderBrush = Create("#2A2C30");
    public static readonly Brush TextPrimary = Create("#F2F3F5");
    public static readonly Brush TextSecondary = Create("#9CA0A6");
    public static readonly Brush TextMuted = Create("#6E7278");
    public static readonly Brush ThumbBg = Create("#26272B");

    public static string SecondsToText(int sec)
    {
        if (sec < 60) return $"{sec}秒";
        var m = sec / 60;
        var s = sec % 60;
        if (m < 60) return $"{m:D2}:{s:D2}";
        var h = m / 60;
        m %= 60;
        return $"{h}:{m:D2}:{s:D2}";
    }

    public static string ViewCountToText(long n)
    {
        if (n >= 100_000_000) return (n / 100_000_000.0).ToString("0.0") + "亿";
        if (n >= 10_000) return (n / 10_000.0).ToString("0.0") + "万";
        return n.ToString();
    }

    private static SolidColorBrush Create(string hex)
    {
        return new SolidColorBrush((Color)ColorConverter.ConvertFromString(hex));
    }
}
