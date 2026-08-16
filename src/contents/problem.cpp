// src/contents/problem.cpp
#include <vector>
#include <nlohmann/json.hpp>
#include "luogu-export/contents/problem.h"
#include "luogu-export/util/image_util.h"
#include "luogu-export/util/tag_cache.h"

using nlohmann::json;
using problem::Problem;

namespace
{

// 键存在但值为 null 时也返回缺省值（官方数据里 background/hint 等可能为 null）
std::string get_string(const json &j, const char *key)
{
    if (!j.contains(key) || !j[key].is_string())
        return "";
    return j[key].get<std::string>();
}

int get_int(const json &j, const char *key, int def)
{
    if (!j.contains(key) || !j[key].is_number_integer())
        return def;
    return j[key].get<int>();
}

} // namespace

problem::Problem::Problem() { return; }

problem::Problem::Problem(const json &data,
                          const std::unordered_map<int, std::string> *tag_id_to_name)
{
    pid = get_string(data, "pid");
    type = get_string(data, "type");
    difficulty = get_int(data, "difficulty", 0);

    // 标签：批量缓存里一般是中文名；若是数字 ID 则用 tag_id_to_name 翻译
    if (data.contains("tags") && data["tags"].is_array())
    {
        for (const auto &t : data["tags"])
        {
            if (t.is_string())
            {
                tags.push_back(tagcache::strip_bom(t.get<std::string>()));
            }
            else if (t.is_number_integer())
            {
                const int id = t.get<int>();
                if (tag_id_to_name)
                {
                    auto it = tag_id_to_name->find(id);
                    if (it != tag_id_to_name->end())
                    {
                        tags.push_back(it->second);
                        continue;
                    }
                }
                tags.push_back("tag#" + std::to_string(id));
            }
        }
    }

    // 中文题面：优先取 translations.zh-CN（部分题目默认语言是英文，如 P1561），
    // 缺失时回退到顶层字段（批量缓存里中文键名是 title/inputFormat/outputFormat）
    auto zh_field = [&](const char *key) -> std::string {
        if (data.contains("translations") && data["translations"].is_object())
        {
            const json &tr = data["translations"];
            if (tr.contains("zh-CN") && tr["zh-CN"].is_object())
            {
                const std::string zh = get_string(tr["zh-CN"], key);
                if (!zh.empty())
                    return zh;
            }
        }
        return get_string(data, key);
    };
    name = zh_field("title");
    background = zh_field("background");
    description = zh_field("description");
    formatI = zh_field("inputFormat");
    formatO = zh_field("outputFormat");
    hint = zh_field("hint");

    // 样例
    if (data.contains("samples") && data["samples"].is_array())
    {
        for (const auto &sample : data["samples"])
        {
            if (sample.is_array() && sample.size() >= 2 &&
                sample[0].is_string() && sample[1].is_string())
            {
                samples.emplace_back(sample[0].get<std::string>(),
                                     sample[1].get<std::string>());
            }
        }
    }

    // 时空限制
    if (data.contains("limits") && data["limits"].is_object())
    {
        const json &limits = data["limits"];
        if (limits.contains("time") && limits["time"].is_array())
            for (const auto &v : limits["time"])
                if (v.is_number_integer()) time.push_back(v.get<int>());
        if (limits.contains("memory") && limits["memory"].is_array())
            for (const auto &v : limits["memory"])
                if (v.is_number_integer()) memory.push_back(v.get<int>());
    }

    // 多语言题面：只保留 en（zh-CN 与顶层字段重复）
    if (data.contains("translations") && data["translations"].is_object() &&
        data["translations"].contains("en") && data["translations"]["en"].is_object())
    {
        translations = data["translations"]["en"];
    }
}

std::vector<std::string> Problem::image_urls() const
{
    std::string all;
    all.reserve(background.size() + description.size() + formatI.size() +
                formatO.size() + hint.size() + 8);
    all += background;
    all += '\n';
    all += description;
    all += '\n';
    all += formatI;
    all += '\n';
    all += formatO;
    all += '\n';
    all += hint;
    return image_util::extract_urls(all);
}
