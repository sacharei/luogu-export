// include/luogu-export/util/image_util.h
#ifndef LUOGU_EXPORT_IMAGE_UTIL_H
#define LUOGU_EXPORT_IMAGE_UTIL_H

#include <string>
#include <vector>

namespace image_util
{
    // 从 markdown / HTML 文本中提取图片链接（去重，保持出现顺序）。
    // 支持 markdown 语法 ![alt](url) 以及 HTML 的 <img src="url">。
    std::vector<std::string> extract_urls(const std::string &markdown);
}

#endif // LUOGU_EXPORT_IMAGE_UTIL_H
