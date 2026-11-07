using System.Text.Json.Serialization;

namespace BiliClient.Api;

// bilibili API 数据模型（对应 BBTube 中 FeedStore/VideoStore 行数据）

public class ApiResponse<T>
{
    [JsonPropertyName("code")] public int Code { get; set; }
    [JsonPropertyName("message")] public string Message { get; set; } = "";
    [JsonPropertyName("data")] public T? Data { get; set; }
}

public class NavData
{
    [JsonPropertyName("wbi_img")] public WbiImg? WbiImg { get; set; }
    [JsonPropertyName("isLogin")] public bool IsLogin { get; set; }
}

public class WbiImg
{
    [JsonPropertyName("img_url")] public string ImgUrl { get; set; } = "";
    [JsonPropertyName("sub_url")] public string SubUrl { get; set; } = "";
}

public class VideoItem
{
    [JsonPropertyName("bvid")] public string Bvid { get; set; } = "";
    [JsonPropertyName("aid")] public long Aid { get; set; }
    [JsonPropertyName("cid")] public long Cid { get; set; }
    [JsonPropertyName("title")] public string Title { get; set; } = "";
    [JsonPropertyName("pic")] public string Pic { get; set; } = "";
    [JsonPropertyName("duration")] public long Duration { get; set; }
    [JsonPropertyName("pubdate")] public long Pubdate { get; set; }
    [JsonPropertyName("owner")] public Owner? Owner { get; set; }
    [JsonPropertyName("stat")] public VideoStat? Stat { get; set; }
    [JsonPropertyName("typename")] public string TypeName { get; set; } = "";
    [JsonPropertyName("badge")] public string Badge { get; set; } = "";

    // 展示用
    [JsonIgnore] public string DurationText => App.SecondsToText((int)Duration);
    [JsonIgnore] public string ViewsText => Stat is { } s ? App.ViewCountToText(s.View) + "播放" : "";
    [JsonIgnore] public string OwnerText => Owner?.Name ?? "";
    [JsonIgnore] public string PubText => Pubdate > 0 ? TimeAgo(Pubdate) : "";
    [JsonIgnore] public string MetaText => string.Join(" · ", new[] { OwnerText, ViewsText, PubText }.Where(x => x != ""));
    [JsonIgnore] public string PicUrl => Pic.StartsWith("//") ? "https:" + Pic : Pic;

    private static string TimeAgo(long unix)
    {
        var dt = DateTimeOffset.FromUnixTimeSeconds(unix).ToLocalTime();
        var span = DateTime.Now - dt;
        if (span.TotalMinutes < 1) return "刚刚";
        if (span.TotalHours < 1) return $"{(int)span.TotalMinutes}分钟前";
        if (span.TotalDays < 1) return $"{(int)span.TotalHours}小时前";
        if (span.TotalDays < 30) return $"{(int)span.TotalDays}天前";
        if (span.TotalDays < 365) return $"{(int)(span.TotalDays / 30)}个月前";
        return $"{dt:yyyy-MM-dd}";
    }
}

public class Owner
{
    [JsonPropertyName("mid")] public long Mid { get; set; }
    [JsonPropertyName("name")] public string Name { get; set; } = "";
    [JsonPropertyName("face")] public string Face { get; set; } = "";
}

public class VideoStat
{
    [JsonPropertyName("view")] public long View { get; set; }
    [JsonPropertyName("danmaku")] public long Danmaku { get; set; }
    [JsonPropertyName("reply")] public long Reply { get; set; }
    [JsonPropertyName("favorite")] public long Favorite { get; set; }
    [JsonPropertyName("coin")] public long Coin { get; set; }
    [JsonPropertyName("share")] public long Share { get; set; }
    [JsonPropertyName("like")] public long Like { get; set; }
}

public class RcmdData
{
    [JsonPropertyName("item")] public List<VideoItem> Items { get; set; } = new();
}

public class RankingData
{
    [JsonPropertyName("list")] public List<VideoItem> List { get; set; } = new();
}

public class VideoDetailData
{
    [JsonPropertyName("bvid")] public string Bvid { get; set; } = "";
    [JsonPropertyName("aid")] public long Aid { get; set; }
    [JsonPropertyName("cid")] public long Cid { get; set; }
    [JsonPropertyName("title")] public string Title { get; set; } = "";
    [JsonPropertyName("desc")] public string Desc { get; set; } = "";
    [JsonPropertyName("pic")] public string Pic { get; set; } = "";
    [JsonPropertyName("duration")] public long Duration { get; set; }
    [JsonPropertyName("pubdate")] public long Pubdate { get; set; }
    [JsonPropertyName("owner")] public Owner? Owner { get; set; }
    [JsonPropertyName("stat")] public VideoStat? Stat { get; set; }
    [JsonPropertyName("tname")] public string TName { get; set; } = "";
    [JsonPropertyName("pages")] public List<VideoPage> Pages { get; set; } = new();

    public VideoPage? FirstPage => Pages.FirstOrDefault();
}

public class VideoPage
{
    [JsonPropertyName("cid")] public long Cid { get; set; }
    [JsonPropertyName("page")] public int Page { get; set; }
    [JsonPropertyName("part")] public string Part { get; set; } = "";
    [JsonPropertyName("duration")] public long Duration { get; set; }
}

public class SearchResultData
{
    [JsonPropertyName("result")] public List<SearchVideo> Result { get; set; } = new();
    [JsonPropertyName("numResults")] public long NumResults { get; set; }
    [JsonPropertyName("page")] public int Page { get; set; }
}

public class SearchVideo
{
    [JsonPropertyName("bvid")] public string Bvid { get; set; } = "";
    [JsonPropertyName("title")] public string Title { get; set; } = "";
    [JsonPropertyName("pic")] public string Pic { get; set; } = "";
    [JsonPropertyName("duration")] public string Duration { get; set; } = "";
    [JsonPropertyName("author")] public string Author { get; set; } = "";
    [JsonPropertyName("play")] public long Play { get; set; }
    [JsonPropertyName("pubdate")] public long Pubdate { get; set; }
    [JsonPropertyName("typename")] public string TypeName { get; set; } = "";

    [JsonIgnore] public string DurationText => Duration;
    [JsonIgnore] public string ViewsText => App.ViewCountToText(Play) + "播放";
    [JsonIgnore] public string OwnerText => Author;
    [JsonIgnore] public string PubText => Pubdate > 0 ? UnixToTime(Pubdate) : "";
    [JsonIgnore] public string MetaText => string.Join(" · ", new[] { OwnerText, ViewsText, PubText }.Where(x => x != ""));
    [JsonIgnore] public string PicUrl => Pic.StartsWith("//") ? "https:" + Pic : Pic;

    private static string UnixToTime(long unix)
    {
        var dt = DateTimeOffset.FromUnixTimeSeconds(unix).ToLocalTime();
        return $"{dt:yyyy-MM-dd}";
    }
}

public class RegionVideo
{
    [JsonPropertyName("bvid")] public string Bvid { get; set; } = "";
    [JsonPropertyName("title")] public string Title { get; set; } = "";
    [JsonPropertyName("pic")] public string Pic { get; set; } = "";
    [JsonPropertyName("duration")] public long Duration { get; set; }
    [JsonPropertyName("author")] public string Author { get; set; } = "";
    [JsonPropertyName("play")] public long Play { get; set; }
    [JsonPropertyName("pubdate")] public long Pubdate { get; set; }

    [JsonIgnore] public string DurationText => App.SecondsToText((int)Duration);
    [JsonIgnore] public string ViewsText => App.ViewCountToText(Play) + "播放";
    [JsonIgnore] public string OwnerText => Author;
    [JsonIgnore] public string PubText => Pubdate > 0 ? $"{DateTimeOffset.FromUnixTimeSeconds(Pubdate).ToLocalTime():yyyy-MM-dd}" : "";
    [JsonIgnore] public string MetaText => string.Join(" · ", new[] { OwnerText, ViewsText, PubText }.Where(x => x != ""));
    [JsonIgnore] public string PicUrl => Pic.StartsWith("//") ? "https:" + Pic : Pic;
}

public class RegionData
{
    [JsonPropertyName("archives")] public List<RegionVideo> Archives { get; set; } = new();
}

public class CommentData
{
    [JsonPropertyName("replies")] public List<CommentReply> Replies { get; set; } = new();
    [JsonPropertyName("total")] public long Total { get; set; }
}

public class CommentReply
{
    [JsonPropertyName("member")] public CommentMember? Member { get; set; }
    [JsonPropertyName("content")] public CommentContent? Content { get; set; }
    [JsonPropertyName("like")] public long Like { get; set; }
    [JsonPropertyName("ctime")] public long Ctime { get; set; }
    [JsonPropertyName("rcount")] public long Rcount { get; set; }

    [JsonIgnore] public string UserName => Member?.Uname ?? "";
    [JsonIgnore] public string FaceUrl => (Member?.Avatar ?? "").StartsWith("//") ? "https:" + (Member?.Avatar ?? "") : (Member?.Avatar ?? "");
    [JsonIgnore] public string Message => Content?.Message ?? "";
    [JsonIgnore] public string TimeText => $"{DateTimeOffset.FromUnixTimeSeconds(Ctime).ToLocalTime():MM-dd HH:mm}";
}

public class CommentMember
{
    [JsonPropertyName("uname")] public string Uname { get; set; } = "";
    [JsonPropertyName("avatar")] public string Avatar { get; set; } = "";
}

public class CommentContent
{
    [JsonPropertyName("message")] public string Message { get; set; } = "";
}
