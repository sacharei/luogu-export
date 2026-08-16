// src/problem_info.cpp
#include "luogu-export/util/problem_info.h"
#include "luogu-export/util/tag_cache.h"

std::vector<std::string> luogu::filter_display_tags(const std::vector<std::string> &tags, bool show_all)
{
    if (show_all)
        return tags;

    // 复用进程内共享缓存：tags.json 只读取/解析一次
    const tagcache::Cache &cache = tagcache::shared_cache();
    std::vector<std::string> shown;
    for (const auto &t : tags)
    {
        auto it = cache.name_to_type.find(t);
        if (it == cache.name_to_type.end() || it->second != 2)
            shown.push_back(t);
    }
    return shown;
}

std::vector<std::string> luogu::filter_tags_by_type(const std::vector<std::string> &tags, int type)
{
    const tagcache::Cache &cache = tagcache::shared_cache();
    std::vector<std::string> out;
    out.reserve(tags.size());
    for (const auto &t : tags)
    {
        auto it = cache.name_to_type.find(t);
        if (it != cache.name_to_type.end() && it->second == type)
            out.push_back(t);
    }
    return out;
}
