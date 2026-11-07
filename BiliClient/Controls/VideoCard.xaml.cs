using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace BiliClient.Controls;

/// <summary>
/// 视频卡片（对应 BBTube VideoRow.qml：全宽 16:9 封面 + 时长角标 + 双行标题 + 作者·数据行）。
/// </summary>
public partial class VideoCard : UserControl
{
    public static readonly RoutedEvent ClickEvent =
        EventManager.RegisterRoutedEvent(nameof(Click), RoutingStrategy.Bubble, typeof(RoutedEventHandler), typeof(VideoCard));

    public event RoutedEventHandler Click
    {
        add => AddHandler(ClickEvent, value);
        remove => RemoveHandler(ClickEvent, value);
    }

    public static readonly DependencyProperty BvidProperty =
        DependencyProperty.Register(nameof(Bvid), typeof(string), typeof(VideoCard), new PropertyMetadata(""));
    public static readonly DependencyProperty TitleProperty =
        DependencyProperty.Register(nameof(Title), typeof(string), typeof(VideoCard), new PropertyMetadata(""));
    public static readonly DependencyProperty PicUrlProperty =
        DependencyProperty.Register(nameof(PicUrl), typeof(string), typeof(VideoCard), new PropertyMetadata(""));
    public static readonly DependencyProperty DurationTextProperty =
        DependencyProperty.Register(nameof(DurationText), typeof(string), typeof(VideoCard), new PropertyMetadata(""));
    public static readonly DependencyProperty MetaTextProperty =
        DependencyProperty.Register(nameof(MetaText), typeof(string), typeof(VideoCard), new PropertyMetadata(""));

    public string Bvid { get => (string)GetValue(BvidProperty); set => SetValue(BvidProperty, value); }
    public string Title { get => (string)GetValue(TitleProperty); set => SetValue(TitleProperty, value); }
    public string PicUrl { get => (string)GetValue(PicUrlProperty); set => SetValue(PicUrlProperty, value); }
    public string DurationText { get => (string)GetValue(DurationTextProperty); set => SetValue(DurationTextProperty, value); }
    public string MetaText { get => (string)GetValue(MetaTextProperty); set => SetValue(MetaTextProperty, value); }

    public ICommand ClickCommand { get; }

    public VideoCard()
    {
        InitializeComponent();
        ClickCommand = new RelayCommand(_ => RaiseEvent(new RoutedEventArgs(ClickEvent)));
    }
}

public class RelayCommand : ICommand
{
    private readonly Action<object?> _execute;
    private readonly Predicate<object?>? _canExecute;

    public RelayCommand(Action<object?> execute, Predicate<object?>? canExecute = null)
    {
        _execute = execute;
        _canExecute = canExecute;
    }

    public event EventHandler? CanExecuteChanged;
    public bool CanExecute(object? parameter) => _canExecute?.Invoke(parameter) ?? true;
    public void Execute(object? parameter) => _execute(parameter);
    public void RaiseCanExecuteChanged() => CanExecuteChanged?.Invoke(this, EventArgs.Empty);
}
