using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace BiliClient;

public abstract class ViewModelBase : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    protected bool Set<T>(ref T field, T value, [CallerMemberName] string? name = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value)) return false;
        field = value;
        OnPropertyChanged(name);
        return true;
    }
}

/// <summary>非泛型分页 store 契约。</summary>
public interface IPagedStore
{
    System.Collections.IEnumerable Items { get; }
    bool IsLoading { get; }
    bool IsLoadingMore { get; }
    bool HasError { get; }
    string Error { get; }
    Task LoadAsync();
    Task LoadMoreAsync();
}

/// <summary>
/// 分页 Feed Store —— 直接移植 BBTube FeedStore 的模式：
/// 首页首次加载大转圈、底部加载更多、错误显示 + 重试、加载中防重入。
/// </summary>
public abstract class PagedStore<T> : ViewModelBase, IPagedStore
{
    private bool _isLoading;
    private bool _isLoadingMore;
    private string _error = "";

    public ObservableCollection<T> Items { get; } = new();

    System.Collections.IEnumerable IPagedStore.Items => Items;

    public bool IsLoading
    {
        get => _isLoading;
        protected set { if (Set(ref _isLoading, value)) OnPropertyChanged(nameof(ShowBigSpinner)); }
    }

    public bool IsLoadingMore
    {
        get => _isLoadingMore;
        protected set { if (Set(ref _isLoadingMore, value)) OnPropertyChanged(nameof(ShowMoreSpinner)); }
    }

    public string Error
    {
        get => _error;
        protected set { if (Set(ref _error, value)) OnPropertyChanged(nameof(HasError)); }
    }

    public bool ShowBigSpinner => IsLoading && Items.Count == 0;
    public bool ShowMoreSpinner => IsLoadingMore;
    public bool HasError => Error != "";

    /// <summary>首次加载 / 下拉刷新。对应 FeedStore.reload()。</summary>
    public async Task LoadAsync()
    {
        if (IsLoading) return;
        IsLoading = true;
        Error = "";
        try
        {
            var items = await FetchPageAsync(0);
            Items.Clear();
            foreach (var it in items) Items.Add(it);
        }
        catch (Exception ex)
        {
            Error = ex.Message;
        }
        finally
        {
            IsLoading = false;
        }
    }

    /// <summary>滚动到底加载下一页。对应 FeedStore.loadMore() 的防重入。</summary>
    public async Task LoadMoreAsync()
    {
        if (IsLoading || IsLoadingMore || !HasMore) return;
        IsLoadingMore = true;
        try
        {
            var items = await FetchPageAsync(Items.Count);
            foreach (var it in items) Items.Add(it);
        }
        catch
        {
            // 触底加载失败静默，滚动可重试
        }
        finally
        {
            IsLoadingMore = false;
        }
    }

    public abstract bool HasMore { get; }

    protected abstract Task<List<T>> FetchPageAsync(int offset);
}
