// src/tag_cache.cpp
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/util/tag_cache.h"

using nlohmann::json;

bool tagcache::Cache::load(const std::filesystem::path &path)
{
    id_to_name.clear();
    name_to_id.clear();
    name_to_type.clear();

    FILE *in = std::fopen(path.c_str(), "rb");
    if (!in)
        return false;

    std::string content;
    try
    {
        // 一次性读入内存再解析：比逐字符流式解析更快
        char buffer[65536];
        size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0)
            content.append(buffer, n);
    }
    catch (...)
    {
        std::fclose(in);
        return false;
    }
    std::fclose(in);

    try
    {
        json data = json::parse(content);
        if (!data.is_object())
            return false;

        for (auto it = data.begin(); it != data.end(); ++it)
        {
            int id = 0;
            try
            {
                id = std::stoi(it.key());
            }
            catch (...)
            {
                continue;
            }

            std::string name;
            int type = 0;
            if (it.value().is_string())
            {
                // 旧格式：{"<id>": "<名称>"}
                name = strip_bom(it.value().get<std::string>());
            }
            else if (it.value().is_object())
            {
                // 新格式：{"<id>": {"name": "<名称>", "type": <分类>}}
                if (it.value().contains("name") && it.value()["name"].is_string())
                    name = strip_bom(it.value()["name"].get<std::string>());
                if (it.value().contains("type") && it.value()["type"].is_number_integer())
                    type = it.value()["type"].get<int>();
            }
            else
            {
                continue;
            }

            if (name.empty())
                continue;
            id_to_name[id] = name;
            name_to_id[name] = id;
            name_to_type[name] = type;
        }
    }
    catch (...)
    {
        return false;
    }
    return !id_to_name.empty();
}

bool tagcache::Cache::load_from_cache_dir()
{
    return load(crawler::get_cache_dir() / "tags.json");
}

namespace
{
// 持有共享缓存及加载状态；函数内 static 保证整个进程只构造（加载）一次，
// 且 C++11 起的初始化是线程安全的。
struct SharedCacheHolder
{
    tagcache::Cache cache;
    bool loaded;

    SharedCacheHolder() : loaded(cache.load_from_cache_dir()) {}
};

const SharedCacheHolder &shared_holder()
{
    static const SharedCacheHolder holder;
    return holder;
}
} // namespace

const tagcache::Cache &tagcache::shared_cache()
{
    return shared_holder().cache;
}

bool tagcache::shared_cache_loaded()
{
    return shared_holder().loaded;
}
