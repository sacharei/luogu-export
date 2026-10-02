// src/export/common.cpp
// -M / -L 共用的题目筛选：解析筛选条件并在缓存 latest.ndjson 上精确过滤。
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/export/common.h"
#include "luogu-export/export/json_scan.h"
#include "luogu-export/util/problem_info.h"
#include "luogu-export/util/string_util.h"
#include "luogu-export/util/tag_cache.h"

using nlohmann::json;

namespace luogu
{
namespace
{

// 把 --tag 参数规范成标签名：数字 ID 优先按 tags.json 翻译，其余按名称原样处理
// 返回 false 仅当输入是数字 ID 但缺少 tags.json 无法翻译
bool resolve_tag(const std::string &raw, bool has_tag_map,
                 const tagcache::Cache &cache, std::string &out)
{
    const bool numeric = !raw.empty() &&
        std::all_of(raw.begin(), raw.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c));
        });
    if (!numeric)
    {
        out = tagcache::strip_bom(raw);
        return true;
    }
    if (!has_tag_map)
        return false; // 数字 ID 需要 tags.json 才能翻译

    int id = 0;
    try
    {
        id = std::stoi(raw);
    }
    catch (...)
    {
        out = tagcache::strip_bom(raw);
        return true;
    }

    auto it = cache.id_to_name.find(id);
    if (it != cache.id_to_name.end())
    {
        out = it->second;
        return true;
    }
    // 数字也可能是年份等标签名（如 "1997"）
    out = tagcache::strip_bom(raw);
    return true;
}

// 题号数字部分（用于按题号从小到大排序）
long pid_number(const std::string &pid)
{
    long n = 0;
    bool any = false;
    for (char c : pid)
    {
        if (c >= '0' && c <= '9')
        {
            n = n * 10 + (c - '0');
            any = true;
        }
    }
    return any ? n : -1;
}

} // namespace

using namespace detail;

bool select_problems(const ExportFilter &filter,
                            std::vector<problem::Problem> &problems,
                            std::vector<std::string> *resolved_tags,
                            std::string &error)
{
    error.clear();
    problems.clear();
    if (resolved_tags)
        resolved_tags->clear();

    // 1. 标签缓存（-U 生成：ID <-> 名称，以及标签分类 type）
    //    复用进程内共享缓存，tags.json 只读取一次
    const tagcache::Cache &tag_cache = tagcache::shared_cache();
    const bool has_tag_map = tagcache::shared_cache_loaded();

    // 2. 解析 --tag 参数
    std::vector<std::string> filter_tags;
    for (const auto &raw : filter.tags)
    {
        // 含空格的参数先整体匹配已知标签名（如 "NOIP 普及组"）：命中就按一个标签，
        // 否则按空格拆成多个标签（如 --tag "模拟 贪心"），保持原有写法。
        const std::string raw_stripped = tagcache::strip_bom(raw);
        if (raw_stripped.find_first_of(" \t") != std::string::npos &&
            has_tag_map &&
            tag_cache.name_to_id.find(raw_stripped) != tag_cache.name_to_id.end())
        {
            filter_tags.push_back(raw_stripped);
            continue;
        }

        for (const auto &tok : util::split_whitespace(raw))
        {
            std::string name;
            if (!resolve_tag(tok, has_tag_map, tag_cache, name))
            {
                error = "缺少 tags.json，无法把标签 ID 翻译成名称，请先运行 -U: " + tok;
                return false;
            }
            filter_tags.push_back(name);
        }
    }
    if (resolved_tags)
        *resolved_tags = filter_tags;

    // 3. 打开题目缓存
    std::filesystem::path ndjson_path = crawler::get_cache_dir() / "latest.ndjson";
    FILE *in = std::fopen(ndjson_path.c_str(), "rb");
    if (!in)
    {
        error = "找不到题目缓存 '" + ndjson_path.string() + "'，请先运行 -U 更新缓存";
        return false;
    }

    // 4. 逐行扫描并筛选（统一用 Problem 结构承载题目）
    std::set<std::string> seen_tags; // 缓存中出现的全部标签名（小写化），用于校验 --tag 拼写

    // 预计算每个 --tag 名字对应的数字 ID，供原始文本快速预筛使用（-1 表示查不到）
    std::vector<long> filter_tag_ids;
    filter_tag_ids.reserve(filter_tags.size());
    for (const auto &name : filter_tags)
    {
        const auto it = tag_cache.name_to_id.find(name);
        filter_tag_ids.push_back(it != tag_cache.name_to_id.end()
                                     ? static_cast<long>(it->second)
                                     : -1L);
    }

    char *line_buf = nullptr;
    size_t line_cap = 0;
    long line_len = 0;
    while ((line_len = getline(&line_buf, &line_cap, in)) != -1)
    {
        size_t content_len = static_cast<size_t>(line_len);
        if (content_len > 0 && line_buf[content_len - 1] == '\n')
            --content_len;
        if (content_len == 0)
            continue;

        // 快速预筛：原始文本上就确定不可能命中的行，跳过 JSON 解析
        if (!raw_may_match(std::string_view(line_buf, content_len),
                           filter.difficulties, filter_tags, filter_tag_ids,
                           filter.types))
            continue;

        json data;
        try
        {
            data = json::parse(line_buf, line_buf + content_len);
        }
        catch (...)
        {
            continue; // 跳过损坏行
        }

        try
        {
            // 难度：多个值取“或”
            if (!filter.difficulties.empty())
            {
                if (!data.contains("difficulty") || !data["difficulty"].is_number_integer())
                    continue;
                const int difficulty = data["difficulty"].get<int>();
                if (std::find(filter.difficulties.begin(), filter.difficulties.end(), difficulty) ==
                    filter.difficulties.end())
                    continue;
            }

            // 类型：多个值取“或”（B / P，不区分大小写）
            if (!filter.types.empty())
            {
                std::string ptype = data.value("type", "");
                for (auto &c : ptype)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (std::find(filter.types.begin(), filter.types.end(), ptype) == filter.types.end())
                    continue;
            }

            // 构造 Problem（标签名称、题面、样例、时空限制、多语言都在这里解析）
            problem::Problem p(data, &tag_cache.id_to_name);
            for (const auto &t : p.tags)
                seen_tags.insert(util::to_lower_ascii(t));

            // 标签：多个值取“且”
            if (!filter_tags.empty())
            {
                bool all = true;
                for (const auto &wanted : filter_tags)
                {
                    const std::string key = util::to_lower_ascii(wanted);
                    if (std::find_if(p.tags.begin(), p.tags.end(),
                                     [&key](const std::string &t) { return util::to_lower_ascii(t) == key; }) ==
                        p.tags.end())
                    {
                        all = false;
                        break;
                    }
                }
                if (!all)
                    continue;
            }

            problems.push_back(std::move(p));
        }
        catch (...)
        {
            continue; // 字段类型异常时跳过该题
        }
    }
    std::free(line_buf);
    std::fclose(in);

    // 5. 校验 --tag 名称确实存在于缓存中。
    //    注意：seen_tags 只收集了“通过预筛（难度/类型/标签）的行”里的标签，
    //    当筛选组合本身没有命中任何题目时，seen_tags 会缺失这些标签，
    //    但标签本身可能在缓存里是存在的 —— 此时不能误报“标签不存在”。
    //    因此对未能在 seen_tags 中确认的标签，再依次检查：
    //    官方标签表（tags.json）和整份题目缓存（一次原始文本扫描）。
    std::vector<std::string> not_found;
    std::vector<std::string> verify_missing;
    for (const auto &wanted : filter_tags)
    {
        if (seen_tags.count(util::to_lower_ascii(wanted)))
            continue;
        verify_missing.push_back(wanted);
    }
    if (!verify_missing.empty())
    {
        // 2a. 官方标签表（tags.json）里有该名称即视为存在
        std::vector<std::string> still_missing;
        for (const auto &wanted : verify_missing)
        {
            if (tag_cache.name_to_id.find(wanted) != tag_cache.name_to_id.end())
                continue;
            still_missing.push_back(wanted);
        }
        // 2b. 整份题目缓存扫描：任一题目带该标签即视为存在
        if (!still_missing.empty())
        {
            std::vector<long> check_ids;
            check_ids.reserve(still_missing.size());
            for (const auto &name : still_missing)
            {
                const auto it = tag_cache.name_to_id.find(name);
                check_ids.push_back(it != tag_cache.name_to_id.end()
                                        ? static_cast<long>(it->second)
                                        : -1L);
            }
            const uint64_t need_all = (still_missing.size() == 64)
                                          ? ~uint64_t{0}
                                          : ((uint64_t{1} << still_missing.size()) - 1);
            uint64_t found_any = 0;

            FILE *scan_in = std::fopen(ndjson_path.c_str(), "rb");
            if (scan_in)
            {
                char *scan_buf = nullptr;
                size_t scan_cap = 0;
                long scan_len = 0;
                while ((scan_len = getline(&scan_buf, &scan_cap, scan_in)) != -1)
                {
                    size_t n = static_cast<size_t>(scan_len);
                    if (n > 0 && scan_buf[n - 1] == '\n')
                        --n;
                    if (n == 0)
                        continue;
                    const TagScanResult r = raw_tags_scan(
                        std::string_view(scan_buf, n), still_missing, check_ids);
                    if (r.may_any)
                    {
                        found_any = need_all; // 无法可靠判断 → 全部视为存在
                        break;
                    }
                    found_any |= r.found;
                    if (found_any == need_all)
                        break;
                }
                std::free(scan_buf);
                std::fclose(scan_in);
            }

            for (size_t i = 0; i < still_missing.size(); ++i)
                if (!(found_any & (uint64_t{1} << i)))
                    not_found.push_back(still_missing[i]);
        }
    }
    if (!not_found.empty())
    {
        error = "以下标签在题目缓存中不存在: " + util::join_strings(not_found, "、") +
                "（请先运行 -U 更新缓存）";
        return false;
    }

    // 6. 排序：按题号从小到大（先按数字部分升序，前缀字母作为次级排序）
    std::sort(problems.begin(), problems.end(), [](const problem::Problem &a, const problem::Problem &b) {
        const long na = pid_number(a.pid);
        const long nb = pid_number(b.pid);
        if (na != nb)
            return na < nb;
        return a.pid < b.pid;
    });
    return true;
}

std::string describe_filter(const ExportFilter &filter,
                                   const std::vector<std::string> &resolved_tags)
{
    const bool use_en = (filter.lang == "en");
    const bool show_difficulty = (filter.show.size() >= 2 && filter.show[0] == '1');
    const bool show_tags = (filter.show.size() >= 2 && filter.show[1] == '1');

    std::vector<std::string> conds;
    if (!resolved_tags.empty())
        conds.push_back("标签包含 " + util::join_strings(resolved_tags, "、"));
    if (!filter.difficulties.empty())
    {
        std::vector<std::string> ds;
        for (int d : filter.difficulties)
            ds.push_back(std::string(luogu::difficulty_label(d)) + "(" + std::to_string(d) + ")");
        conds.push_back("难度为 " + util::join_strings(ds, " 或 "));
    }
    if (!filter.types.empty())
        conds.push_back("类型为 " + util::join_strings(filter.types, "、"));
    if (use_en)
        conds.push_back("题面语言为英文（缺失时回退中文）");
    if (!show_difficulty)
        conds.push_back("不显示难度");
    if (!show_tags)
        conds.push_back("不显示算法类标签");
    return util::join_strings(conds, "；");
}
} // namespace luogu
