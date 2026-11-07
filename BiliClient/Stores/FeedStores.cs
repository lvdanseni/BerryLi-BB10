using System.Collections.ObjectModel;
using BiliClient.Api;

namespace BiliClient.Stores;

/// <summary>首页推荐（对应 BBTube FEwhat_to_watch FeedStore）。</summary>
public class RcmdStore : PagedStore<VideoItem>
{
    private const int PageSize = 30;
    private int _freshIdx;

    public override bool HasMore => !Error.StartsWith("已到底") && _freshIdx < 300;

    protected override async Task<List<VideoItem>> FetchPageAsync(int offset)
    {
        var items = await ApiHolder.Api.GetRcmdAsync(_freshIdx, PageSize);
        if (items.Count < PageSize) _freshIdx = int.MaxValue / 2; // 触底
        _freshIdx += items.Count;
        return items;
    }
}

/// <summary>热门（对应 BBTube 中的热门 browse）。</summary>
public class PopularStore : PagedStore<VideoItem>
{
    private int _pn;

    public override bool HasMore => _pn < 50;

    protected override async Task<List<VideoItem>> FetchPageAsync(int offset)
    {
        _pn++;
        return await ApiHolder.Api.GetPopularAsync(_pn, 30);
    }
}

/// <summary>排行榜（单页 100 条）。</summary>
public class RankingStore : PagedStore<VideoItem>
{
    private bool _loaded;

    public override bool HasMore => !_loaded;

    protected override async Task<List<VideoItem>> FetchPageAsync(int offset)
    {
        var items = await ApiHolder.Api.GetRankingAsync();
        _loaded = true;
        return items;
    }
}

/// <summary>分区排行。</summary>
public class RegionStore : PagedStore<RegionVideo>
{
    private readonly int _rid;
    private int _pn;

    public RegionStore(int rid)
    {
        _rid = rid;
    }

    public override bool HasMore => _pn < 20;

    protected override async Task<List<RegionVideo>> FetchPageAsync(int offset)
    {
        _pn++;
        return await ApiHolder.Api.GetRegionAsync(_rid, _pn, 30);
    }
}

/// <summary>搜索。</summary>
public class SearchStore : PagedStore<SearchVideo>
{
    private string _keyword = "";
    private int _page;
    private long _total;

    public string Keyword
    {
        get => _keyword;
        set
        {
            _keyword = value.Trim();
            _page = 0;
            _total = 0;
            Items.Clear();
        }
    }

    public override bool HasMore => Items.Count < _total;

    public async Task SearchAsync()
    {
        if (Keyword == "") return;
        _page = 0;
        await LoadAsync();
    }

    protected override async Task<List<SearchVideo>> FetchPageAsync(int offset)
    {
        _page++;
        var (items, total) = await ApiHolder.Api.SearchAsync(Keyword, _page);
        _total = total;
        return items;
    }
}

public static class ApiHolder
{
    public static BiliApi Api { get; } = new();
}
