// src/export/latex/image.cpp
#include "luogu-export/export/latex/image.h"
#include "luogu-export/export/latex/text.h"
#include "luogu-export/util/string_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace latex::detail
{
// 视频链接判断：洛谷用“图片语法”插入 Bilibili 视频，或常见视频文件后缀
bool is_video_url(const std::string &url)
{
    // 洛谷的 Bilibili 视频专用语法：![](bilibili:BVxxx?page=N)
    if (url.rfind("bilibili:", 0) == 0)
        return true;
    if (url.find("bilibili.com/video/") != std::string::npos ||
        url.find("player.bilibili.com") != std::string::npos)
        return true;

    std::string path = url;
    const size_t q = path.find_first_of("?#");
    if (q != std::string::npos)
        path.resize(q);
    const std::string lower = util::to_lower_ascii(path);
    static const char *kExts[] = {".mp4", ".webm", ".ogv", ".m4v", ".mov", ".m3u8"};
    for (const char *e : kExts)
    {
        const size_t n = std::strlen(e);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, e) == 0)
            return true;
    }
    return false;
}

// 洛谷视频语法 bilibili:BVxxx[?page=N] → 标准 B 站视频页地址
// https://www.bilibili.com/video/BVxxx[?p=N]（B 站多 P 参数是 p 而不是 page）。
// 非 bilibili: 协议的原样返回。
std::string bilibili_https_url(const std::string &url)
{
    if (url.rfind("bilibili:", 0) != 0)
        return url;
    std::string rest = url.substr(9); // 去掉 "bilibili:" 前缀
    std::string path = rest;
    std::string query;
    const size_t q = rest.find('?');
    if (q != std::string::npos)
    {
        path = rest.substr(0, q);
        query = rest.substr(q + 1);
    }
    const size_t pq = query.find("page=");
    if (pq != std::string::npos)
        query.replace(pq, 5, "p="); // "page=" 5 字符 → "p=" 2 字符
    std::string out = "https://www.bilibili.com/video/" + path;
    if (!query.empty())
        out += "?" + query;
    return out;
}

// 视频链接渲染为可点击的超链接：
// bilibili:BVxxx → \href{https://www.bilibili.com/video/BVxxx}{BVxxx}
std::string video_link_latex(const std::string &url)
{
    // 协议相对地址（//www.bilibili.com/...）先补全协议，否则 \href 目标无效
    const std::string href = bilibili_https_url(resolve_url(url));
    std::string display;
    if (url.rfind("bilibili:", 0) == 0)
    {
        // 显示文本取 BV 号（去掉 ?page= 等参数）
        std::string path = url.substr(9);
        const size_t q = path.find('?');
        if (q != std::string::npos)
            path.resize(q);
        display = path.empty() ? "Bilibili 视频" : path;
    }
    else
    {
        display = url; // 其它视频地址原样显示
    }
    return "\\href{" + escape_latex(href) + "}{" + escape_latex(display) + "}";
}

// 是否看起来像真正的链接目标（防止题面里 [](一段文字) 这种写法被当成链接）
bool looks_like_url(const std::string &url)
{
    return url.rfind("http://", 0) == 0 ||
           url.rfind("https://", 0) == 0 ||
           url.rfind("mailto:", 0) == 0 ||
           url.rfind("bilibili:", 0) == 0 ||
           url.find("://") != std::string::npos;
}

std::string resolve_url(const std::string &url)
{
    if (looks_like_url(url))
        return url;
    // 协议相对地址 //host/path
    if (url.rfind("//", 0) == 0)
        return "https:" + url;
    // 洛谷站内相对地址，如 /problem/P8294、/article/xxxx、/user/1
    if (url.rfind("/", 0) == 0)
        return "https://www.luogu.com.cn" + url;
    return {};
}

// data:image/...;base64,... 这类内嵌数据 URI：xelatex 无法使用，
// 而且 base64 内容是一整串无空格文本，会让 TeX 段落排版出问题
// （甚至段错误/卡死），一律跳过
bool is_data_uri(const std::string &url)
{
    return url.rfind("data:", 0) == 0;
}

ImageKind detect_image_kind(const std::filesystem::path &path)
{
    FILE *in = std::fopen(path.c_str(), "rb");
    if (!in)
        return ImageKind::kUnknown;
    unsigned char head[16] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), in);
    std::fclose(in);

    if (n >= 8 && std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0)
        return ImageKind::kPng;
    if (n >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF)
        return ImageKind::kJpeg;
    if (n >= 6 && std::memcmp(head, "GIF8", 4) == 0)
        return ImageKind::kGif;
    if (n >= 12 && std::memcmp(head, "RIFF", 4) == 0 &&
        std::memcmp(head + 8, "WEBP", 4) == 0)
        return ImageKind::kWebp;
    if (n >= 2 && head[0] == 'B' && head[1] == 'M')
        return ImageKind::kBmp;
    if (n >= 4 && (std::memcmp(head, "<svg", 4) == 0 ||
                   std::memcmp(head, "<?xm", 4) == 0))
        return ImageKind::kSvg;
    if (n >= 4 && head[0] == 0x00 && head[1] == 0x00 &&
        head[2] == 0x01 && head[3] == 0x00)
        return ImageKind::kIco;
    if (n >= 5 && std::memcmp(head, "%PDF-", 5) == 0)
        return ImageKind::kPdf;
    if (n >= 2 && head[0] == '%' && head[1] == '!')
        return ImageKind::kEps;
    return ImageKind::kUnknown;
}

namespace
{
// PNG：签名之后的第一个 chunk 必须是 IHDR，宽高为大端 32 位整数
bool png_pixel_size(FILE *in, int &width, int &height)
{
    unsigned char head[24] = {0};
    if (std::fread(head, 1, sizeof(head), in) != sizeof(head))
        return false;
    if (std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        std::memcmp(head + 12, "IHDR", 4) != 0)
        return false;
    width = (head[16] << 24) | (head[17] << 16) | (head[18] << 8) | head[19];
    height = (head[20] << 24) | (head[21] << 16) | (head[22] << 8) | head[23];
    return width > 0 && height > 0;
}

// JPEG：在 SOFn 段中读取像素宽高（SOF0~SOF15，排除 DHT/JPG/DAC）
bool jpeg_pixel_size(FILE *in, int &width, int &height)
{
    int a = std::fgetc(in);
    int b = std::fgetc(in);
    if (a != 0xFF || b != 0xD8) // SOI
        return false;

    for (;;)
    {
        int marker;
        // 跳到下一个标记（0xFF 后可能有多个填充 0xFF）
        do
        {
            marker = std::fgetc(in);
        } while (marker != EOF && marker != 0xFF);
        do
        {
            marker = std::fgetc(in);
        } while (marker == 0xFF);
        if (marker == EOF)
            return false;
        // 无长度字段的独立标记：TEM、RSTn、SOI、EOI
        if (marker == 0x00 || marker == 0x01 ||
            (marker >= 0xD0 && marker <= 0xD9))
            continue;

        const int len_hi = std::fgetc(in);
        const int len_lo = std::fgetc(in);
        if (len_hi == EOF || len_lo == EOF)
            return false;
        const int len = (len_hi << 8) | len_lo;
        if (len < 2)
            return false;

        if (marker >= 0xC0 && marker <= 0xCF &&
            marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
        {
            // SOFn：长度(2) 精度(1) 高(2) 宽(2)
            unsigned char seg[5] = {0};
            if (std::fread(seg, 1, sizeof(seg), in) != sizeof(seg))
                return false;
            height = (seg[1] << 8) | seg[2];
            width = (seg[3] << 8) | seg[4];
            return width > 0 && height > 0;
        }
        if (std::fseek(in, len - 2, SEEK_CUR) != 0)
            return false;
    }
}

} // namespace

bool image_pixel_size(const std::filesystem::path &path, int &width, int &height)
{
    width = 0;
    height = 0;
    FILE *in = std::fopen(path.c_str(), "rb");
    if (!in)
        return false;

    bool ok = false;
    switch (detect_image_kind(path))
    {
    case ImageKind::kPng: ok = png_pixel_size(in, width, height); break;
    case ImageKind::kJpeg: ok = jpeg_pixel_size(in, width, height); break;
    default: break; // GIF/WebP/BMP 等不会被 xelatex 使用，此处无需支持
    }
    std::fclose(in);
    return ok;
}

double image_natural_width_pt(const std::filesystem::path &path)
{
    int width = 0, height = 0;
    if (!image_pixel_size(path, width, height))
        return 0.0;
    // 与浏览器一致：按像素尺寸显示，忽略图片内嵌的 DPI 信息
    return static_cast<double>(width) * 72.0 / 96.0;
}

// 修正 JPEG 的 JFIF 像素密度。洛谷个别老图密度被写成 1 dpi，
// XeTeX 会把 405×256px 的图片按 405×256 英寸排版，超过 TeX 的
// 19 英尺上限而报 "Dimension too large"。密度明显异常（< 72 dpi）
// 时就地改写为 72 dpi（幂等，可重复执行）。
// 返回 true 表示无需修正或已修正；若需要修正但缓存不可写则返回 false。
// 注意：先以只读方式检查，仅在确实需要改写时才申请写权限，
// 这样只读的图片缓存（如 CI/沙箱环境）不会导致无需修正的图片被丢弃。
bool fix_jpeg_density(const std::filesystem::path &path)
{
    FILE *in = std::fopen(path.c_str(), "rb");
    if (!in)
        return false;

    unsigned char head[24] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), in);
    std::fclose(in);
    const bool is_jfif = n >= 18 && head[0] == 0xFF && head[1] == 0xD8 &&
                         head[2] == 0xFF && head[3] == 0xE0 &&
                         std::memcmp(head + 6, "JFIF", 4) == 0 && head[10] == 0;
    if (!is_jfif)
        return true; // 无需修正

    const unsigned units = head[13];
    const unsigned xd = (head[14] << 8) | head[15];
    const unsigned yd = (head[16] << 8) | head[17];
    if (!(units == 1 && (xd < 72 || yd < 72)))
        return true; // 密度正常，无需修正

    // 确实需要改写时才以写模式打开
    FILE *out = std::fopen(path.c_str(), "r+b");
    if (!out)
        return false; // 缓存不可写，无法修正，调用方跳过该图

    const unsigned char k72[] = {1, 0, 72, 0, 72};
    std::fseek(out, 13, SEEK_SET);
    const bool wrote = std::fwrite(k72, 1, sizeof(k72), out) == sizeof(k72);
    std::fclose(out);
    return wrote;
}

// 让缓存图片能被 xelatex 正常加载：
// - GIF/WebP/BMP/SVG/ICO 以及无法识别的内容返回空路径（调用方跳过该图）；
// - JPEG 密度异常时先就地修正；
// - PNG/JPEG/PDF/EPS 内容若文件名没有可识别的扩展名（如无扩展名、或
//   扩展名其实是 URL 的残余），在缓存目录生成带正确扩展名的副本。
std::filesystem::path prepare_cached_image(const std::filesystem::path &cache_path)
{
    if (!std::filesystem::exists(cache_path))
        return {};

    const ImageKind kind = detect_image_kind(cache_path);
    switch (kind)
    {
        case ImageKind::kPng:
        case ImageKind::kJpeg:
        case ImageKind::kPdf:
        case ImageKind::kEps:
            break;
        default:
            return {}; // xelatex 无法加载的格式，直接跳过
    }

    // JPEG 密度异常时仍就地修正（幂等），但不再因此丢弃图片：
    // 现在由 \luoguimg 显式指定像素尺寸，密度不会再导致 "Dimension too large"，
    // 只读缓存下修正失败也可以正常使用。
    if (kind == ImageKind::kJpeg)
        fix_jpeg_density(cache_path);

    const char *want_ext = nullptr;
    switch (kind)
    {
        case ImageKind::kPng: want_ext = ".png"; break;
        case ImageKind::kJpeg: want_ext = ".jpg"; break;
        case ImageKind::kPdf: want_ext = ".pdf"; break;
        case ImageKind::kEps: want_ext = ".eps"; break;
        default: break;
    }

    // XeTeX 按文件内容解码，扩展名只用于选择解析器；
    // 已带可识别扩展名（无论内容是否匹配，如 .png 里的 JPEG）可直接使用
    const std::string name = util::to_lower_ascii(cache_path.filename().string());
    static const char *kRecognized[] = {".png", ".jpg", ".jpeg", ".pdf", ".eps"};
    for (const char *e : kRecognized)
    {
        const size_t m = std::strlen(e);
        if (name.size() >= m && name.compare(name.size() - m, m, e) == 0)
            return cache_path;
    }

    // 生成带正确扩展名的副本，避免与已有缓存文件冲突
    for (int i = 0; i < 128; ++i)
    {
        std::filesystem::path copy = cache_path;
        if (i == 0)
            copy += want_ext;
        else
            copy += "_" + std::to_string(i) + want_ext;
        if (!std::filesystem::exists(copy))
        {
            std::error_code ec;
            std::filesystem::copy_file(cache_path, copy,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            return ec ? std::filesystem::path() : copy;
        }
    }
    return {};
}
} // namespace latex::detail
