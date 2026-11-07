using System.Net.Http;
using System.Net.Http.Headers;
using System.Text.Json;

namespace BiliClient.Api;

/// <summary>
/// bilibili Web API 客户端。对应 BBTube 的 InnerTube 层：所有数据请求集中在这里，
/// 统一 UA/Referer，签名失败时降级为匿名请求。
/// </summary>
public class BiliApi
{
    private const string Base = "https://api.bilibili.com";
    private readonly HttpClient _http;

    public BiliApi()
    {
        _http = new HttpClient(new HttpClientHandler
        {
            AutomaticDecompression = System.Net.DecompressionMethods.All,
            UseCookies = true
        });
        _http.Timeout = TimeSpan.FromSeconds(20);
        _http.DefaultRequestHeaders.UserAgent.ParseAdd(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        _http.DefaultRequestHeaders.Referrer = new Uri("https://www.bilibili.com/");
        _http.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
    }

    private static readonly JsonSerializerOptions JsonOpts = new()
    {
        PropertyNameCaseInsensitive = true
    };

    private async Task<T> GetAsync<T>(string pathAndQuery, CancellationToken ct = default)
    {
        var resp = await _http.GetAsync(Base + pathAndQuery, ct);
        resp.EnsureSuccessStatusCode();
        var bytes = await resp.Content.ReadAsByteArrayAsync(ct);
        var doc = JsonSerializer.Deserialize<ApiResponse<T>>(bytes, JsonOpts)
                  ?? throw new InvalidOperationException("bad response");
        if (doc.Code != 0)
            throw new BiliApiException(doc.Code, doc.Message);
        return doc.Data!;
    }

    public async Task<ApiResponse<NavData>?> GetNavAsync(CancellationToken ct = default)
    {
        var resp = await _http.GetAsync(Base + "/x/web-interface/nav", ct);
        if (!resp.IsSuccessStatusCode) return null;
        var bytes = await resp.Content.ReadAsByteArrayAsync(ct);
        return JsonSerializer.Deserialize<ApiResponse<NavData>>(bytes, JsonOpts);
    }

    /// <summary>首页推荐（需要 WBI 签名）。对应 BBTube 的 FEwhat_to_watch browse。</summary>
    public async Task<List<VideoItem>> GetRcmdAsync(int freshIdx, int ps = 30, CancellationToken ct = default)
    {
        var p = new Dictionary<string, string>
        {
            ["fresh_type"] = "3",
            ["ps"] = ps.ToString(),
            ["fresh_idx"] = freshIdx.ToString(),
            ["fresh_idx_1h"] = freshIdx.ToString(),
            ["fresh_idx_1d"] = freshIdx.ToString(),
            ["fresh_idx_2d"] = freshIdx.ToString(),
            ["plat"] = "1",
            ["web_location"] = "1430650"
        };
        if (await WbiSigner.EnsureKeysAsync(this))
            p["w_rid"] = "";
        var q = WbiSigner.Sign(p);
        var data = await GetAsync<RcmdData>("/x/web-interface/wbi/index/top/rcmd?" + q, ct);
        return data.Items;
    }

    /// <summary>热门（匿名可用）。</summary>
    public async Task<List<VideoItem>> GetPopularAsync(int pn, int ps = 30, CancellationToken ct = default)
    {
        var data = await GetAsync<RankingData>($"/x/web-interface/popular?pn={pn}&ps={ps}", ct);
        return data.List;
    }

    /// <summary>排行榜。</summary>
    public async Task<List<VideoItem>> GetRankingAsync(CancellationToken ct = default)
    {
        var data = await GetAsync<RankingData>("/x/web-interface/ranking/v2", ct);
        return data.List;
    }

    /// <summary>分区排行。</summary>
    public async Task<List<RegionVideo>> GetRegionAsync(int rid, int pn = 1, int ps = 30, CancellationToken ct = default)
    {
        var data = await GetAsync<RegionData>($"/x/web-interface/ranking/region?rid={rid}&pn={pn}&ps={ps}", ct);
        return data.Archives;
    }

    /// <summary>视频详情。</summary>
    public async Task<VideoDetailData> GetVideoDetailAsync(string bvid, CancellationToken ct = default)
    {
        return await GetAsync<VideoDetailData>($"/x/web-interface/wbi/view?bvid={bvid}", ct);
    }

    /// <summary>搜索（需要 WBI 签名）。</summary>
    public async Task<(List<SearchVideo> Items, long Total)> SearchAsync(string keyword, int page, CancellationToken ct = default)
    {
        var p = new Dictionary<string, string>
        {
            ["search_type"] = "video",
            ["keyword"] = keyword,
            ["page"] = page.ToString(),
            ["page_size"] = "30"
        };
        if (await WbiSigner.EnsureKeysAsync(this))
            p["w_rid"] = "";
        var q = WbiSigner.Sign(p);
        var data = await GetAsync<SearchResultData>("/x/web-interface/wbi/search/type?" + q, ct);
        return (data.Result, data.NumResults);
    }

    /// <summary>评论列表。</summary>
    public async Task<List<CommentReply>> GetCommentsAsync(long aid, int pn = 1, int ps = 20, CancellationToken ct = default)
    {
        var data = await GetAsync<CommentData>($"/x/v2/reply?type=1&oid={aid}&pn={pn}&ps={ps}", ct);
        return data.Replies ?? new();
    }

    /// <summary>播放页 URL（官方内嵌播放器，对应 BBTube 的 ytengine 播放引擎角色）。</summary>
    public static string BuildPlayerUrl(string bvid, long cid, int page = 1)
    {
        return $"https://player.bilibili.com/player.html?bvid={bvid}&cid={cid}&page={page}&high_quality=1&danmaku=1&autoplay=1";
    }

    public static string BuildWatchUrl(string bvid) => $"https://www.bilibili.com/video/{bvid}";
}

public class BiliApiException : Exception
{
    public int Code { get; }
    public BiliApiException(int code, string message) : base(message)
    {
        Code = code;
    }
}
