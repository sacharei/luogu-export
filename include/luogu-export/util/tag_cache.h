// include/luogu-export/util/tag_cache.h
#ifndef LUOGU_EXPORT_TAG_CACHE_H
#define LUOGU_EXPORT_TAG_CACHE_H

#include <filesystem>
#include <string>
#include <unordered_map>

namespace tagcache
{
    // 去掉 UTF-8 BOM（\xEF\xBB\xBF），官方数据里个别名称带该字符
    inline std::string strip_bom(std::string s)
    {
        const std::string bom = "\xEF\xBB\xBF";
        size_t pos;
        while ((pos = s.find(bom)) != std::string::npos)
            s.erase(pos, bom.size());
        return s;
    }

    // 标签缓存（由 -U 生成的 tags.json 加载而来）
    // 兼容旧格式 {"<id>": "<名称>"} 与新格式 {"<id>": {"name": ..., "type": ...}}
    struct Cache
    {
        std::unordered_map<int, std::string> id_to_name;     // 标签 ID -> 中文名
        std::unordered_map<std::string, int> name_to_id;     // 中文名 -> 标签 ID
        std::unordered_map<std::string, int> name_to_type;   // 中文名 -> 官方分类 type

        // 从指定路径加载；成功且至少有一条记录时返回 true
        bool load(const std::filesystem::path &path);

        // 从程序缓存目录下的 tags.json 加载
        bool load_from_cache_dir();
    };

    // 进程内共享的标签缓存：第一次调用时从缓存目录读取并解析一次 tags.json，
    // 之后所有调用直接复用同一份数据，避免每处理一道题就重复读文件。
    const Cache &shared_cache();

    // tags.json 是否成功加载过（用于区分“缓存缺失”与“空缓存”）
    bool shared_cache_loaded();
}

#endif // LUOGU_EXPORT_TAG_CACHE_H
