using System.Security.Cryptography;
using System.Text;
using System.Web;

namespace BiliClient.Api;

/// <summary>
/// bilibili WBI 签名（对应 BBTube 中 InnerTube 客户端对 youtubei 请求的签名/上下文处理）。
/// 算法：nav 接口取 img_key/sub_key → 按 mixinKeyEncTab 重排取 32 位混合键 →
/// 参数按字典序拼接 + wts 时间戳 → md5(query + mixin_key) 得 w_rid。
/// </summary>
public static class WbiSigner
{
    private static readonly int[] MixinKeyEncTab =
    {
        46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13,37,48,7,16,24,
        55,40,61,26,17,0,1,60,51,30,4,22,25,54,21,56,59,6,63,57,62,11,36,20,34,44,52
    };

    private static readonly SemaphoreSlim Lock = new(1, 1);
    private static string? _imgKey;
    private static string? _subKey;

    /// <summary>获取（并缓存）WBI 密钥。失败时返回 false，调用方可选择不签名继续请求。</summary>
    public static async Task<bool> EnsureKeysAsync(BiliApi api)
    {
        if (_imgKey != null && _subKey != null) return true;
        await Lock.WaitAsync();
        try
        {
            if (_imgKey != null && _subKey != null) return true;
            var nav = await api.GetNavAsync();
            if (nav?.Data?.WbiImg is not { } img || string.IsNullOrEmpty(img.ImgUrl) || string.IsNullOrEmpty(img.SubUrl))
                return false;
            _imgKey = KeyOf(img.ImgUrl);
            _subKey = KeyOf(img.SubUrl);
            return true;
        }
        finally
        {
            Lock.Release();
        }
    }

    private static string KeyOf(string url)
    {
        var name = url.Substring(url.LastIndexOf('/') + 1);
        var dot = name.IndexOf('.');
        return dot > 0 ? name.Substring(0, dot) : name;
    }

    private static string MixinKey(string imgKey, string subKey)
    {
        var s = imgKey + subKey;
        var sb = new StringBuilder(32);
        for (var i = 0; i < 32 && i < MixinKeyEncTab.Length; i++)
        {
            var idx = MixinKeyEncTab[i];
            if (idx < s.Length) sb.Append(s[idx]);
        }
        return sb.ToString();
    }

    /// <summary>对参数字典做 WBI 签名，返回可直接拼接到 URL 上的 query 串。</summary>
    public static string Sign(Dictionary<string, string> rawParams)
    {
        var p = new Dictionary<string, string>(rawParams)
        {
            ["wts"] = DateTimeOffset.UtcNow.ToUnixTimeSeconds().ToString()
        };
        var ordered = p.OrderBy(kv => kv.Key, StringComparer.Ordinal).ToList();
        var sb = new StringBuilder();
        for (var i = 0; i < ordered.Count; i++)
        {
            if (i > 0) sb.Append('&');
            sb.Append(HttpUtility.UrlEncode(ordered[i].Key));
            sb.Append('=');
            sb.Append(HttpUtility.UrlEncode(ordered[i].Value));
        }
        var query = sb.ToString();
        var mixin = MixinKey(_imgKey!, _subKey!);
        var wrid = Convert.ToHexString(MD5.HashData(Encoding.UTF8.GetBytes(query + mixin))).ToLowerInvariant();
        return query + "&w_rid=" + wrid;
    }
}
