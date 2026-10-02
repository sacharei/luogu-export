// include/luogu-export/export/latex/image.h
#ifndef LUOGU_EXPORT_LATEX_IMAGE_H
#define LUOGU_EXPORT_LATEX_IMAGE_H

#include <filesystem>
#include <string>

// 图片/视频链接的处理：识别视频、生成可点击链接、准备缓存图片。
namespace latex::detail
{
    // 视频链接判断：洛谷用“图片语法”插入 Bilibili 视频，或常见视频文件后缀
    bool is_video_url(const std::string &url);

    // 洛谷视频语法 bilibili:BVxxx[?page=N] → 标准 B 站视频页地址
    std::string bilibili_https_url(const std::string &url);

    // 视频链接渲染为可点击的超链接
    std::string video_link_latex(const std::string &url);

    // 是否带有可识别的协议（http/https/mailto/bilibili/其它 scheme）。
    // 注意：相对地址（/problem/P1）不算，需用 resolve_url 补全。
    bool looks_like_url(const std::string &url);

    // 把链接目标补全成可点击的绝对地址：
    // - 已带协议（http/https/mailto/bilibili/其它 scheme）的原样返回；
    // - 协议相对地址（//host/...）补上 https:；
    // - 站内相对地址（/problem/P1）补上 https://www.luogu.com.cn；
    // - 其它（如 [](一段说明文字) 里不是链接的文字）返回空串。
    std::string resolve_url(const std::string &url);

    // data:image/...;base64,... 这类内嵌数据 URI：xelatex 无法使用，一律跳过
    bool is_data_uri(const std::string &url);

    // 按文件头魔数识别缓存图片的真实格式
    enum class ImageKind
    {
        kPng,
        kJpeg,
        kGif,
        kWebp,
        kBmp,
        kSvg,
        kIco,
        kPdf,
        kEps,
        kUnknown,
    };

    ImageKind detect_image_kind(const std::filesystem::path &path);

    // 读取图片的像素宽高（PNG/JPEG；其余格式返回 false）。
    bool image_pixel_size(const std::filesystem::path &path, int &width, int &height);

    // 图片按“浏览器/CSS 像素”换算出的自然宽度（pt）：1px = 1/96in = 0.75pt，
    // 与洛谷网页（.lfe-marked img{max-width:100%}，按像素尺寸显示、忽略内嵌 DPI）
    // 保持一致。无法得到像素尺寸（PDF/EPS 等矢量图或解析失败）时返回 0，
    // 由调用方回退到 graphicx 的自然尺寸。
    double image_natural_width_pt(const std::filesystem::path &path);

    // 修正 JPEG 的 JFIF 像素密度（异常密度会让 graphicx 的自然尺寸异常）。
    // 返回 true 表示无需修正或已修正；需要修正但缓存不可写时返回 false。
    // 图片尺寸现由像素宽度显式指定，因此修正失败也不再导致图片被跳过。
    bool fix_jpeg_density(const std::filesystem::path &path);

    // 让缓存图片能被 xelatex 正常加载：不可用格式返回空路径；
    // JPEG 密度异常时先修正；缺少可识别扩展名时生成带正确扩展名的副本。
    std::filesystem::path prepare_cached_image(const std::filesystem::path &cache_path);
}

#endif // LUOGU_EXPORT_LATEX_IMAGE_H
