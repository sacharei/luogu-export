// src/export/common.cpp
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
#include "luogu-export/util/problem_info.h"
#include "luogu-export/util/tag_cache.h"

using nlohmann::json;

namespace
{

std::string to_lower_ascii(std::string s)
{
    for (auto &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string join_strings(const std::vector<std::string> &v, const std::string &sep)
{
    std::string out;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (i)
            out += sep;
        out += v[i];
    }
    return out;
}

// 按空白拆成多个 token（空 token 忽略）
std::vector<std::string> split_whitespace(const std::string &s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        const size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i > start)
            out.push_back(s.substr(start, i - start));
    }
    return out;
}

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

// ---- 原始文本快速预筛 -------------------------------------------------
// 目标：跳过“确定不可能命中筛选条件”的行，避免为它们构造完整 JSON DOM。
// 原则：只有能严格证明不命中时才跳过；任何不确定情况一律返回“可能命中”，
// 交给后面的完整 JSON 解析与精确筛选，保证筛选结果与原来完全一致。

inline bool is_json_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// 扫描整行的 "key" 键值对：只要任一出现处的整数值命中 allowed[0..allowed_max]
// 就返回 true；键未出现或所有值都不命中返回 false。
// 值带小数点/指数（如 1.0、1e0，nlohmann 不会当作整数）或无法解析时保守返回 true。
bool raw_int_value_match(std::string_view line, const char *key,
                         const bool *allowed, int allowed_max)
{
    const size_t key_len = std::strlen(key);
    size_t pos = 0;
    while ((pos = line.find(key, pos)) != std::string_view::npos)
    {
        size_t q = pos + key_len;
        while (q < line.size() && is_json_ws(line[q]))
            ++q;
        if (q >= line.size() || line[q] != ':')
        {
            pos = q;
            continue;
        }
        ++q;
        while (q < line.size() && is_json_ws(line[q]))
            ++q;
        if (q >= line.size())
            break;

        const bool neg = line[q] == '-';
        if (neg)
            ++q;
        if (q < line.size() && line[q] >= '0' && line[q] <= '9')
        {
            long v = 0;
            while (q < line.size() && line[q] >= '0' && line[q] <= '9')
            {
                v = v * 10 + (line[q] - '0');
                if (v > allowed_max)
                    v = static_cast<long>(allowed_max) + 1; // 超出范围即无需精确值
                ++q;
            }
            // 浮点形式不会被当作整数，但无法可靠判断，保守交给完整解析
            if (q < line.size() && (line[q] == '.' || line[q] == 'e' || line[q] == 'E'))
                return true;
            const long value = neg ? -v : v;
            if (value >= 0 && value <= allowed_max && allowed[static_cast<size_t>(value)])
                return true;
        }
        // null / 字符串等类型或值不在允许集合内：继续找下一个同名字段
        pos = q;
    }
    return false;
}

// 扫描整行的 "key" 键值对：只要任一出现处的字符串值（不区分大小写）命中
// allowed 之一就返回 true；键未出现或所有值都不命中返回 false。
// 值含转义或无法解析时保守返回 true。
bool raw_string_value_match(std::string_view line, const char *key,
                            const char *const *allowed, size_t allowed_count)
{
    const size_t key_len = std::strlen(key);
    size_t pos = 0;
    while ((pos = line.find(key, pos)) != std::string_view::npos)
    {
        size_t q = pos + key_len;
        while (q < line.size() && is_json_ws(line[q]))
            ++q;
        if (q >= line.size() || line[q] != ':')
        {
            pos = q;
            continue;
        }
        ++q;
        while (q < line.size() && is_json_ws(line[q]))
            ++q;
        if (q >= line.size() || line[q] != '"')
        {
            pos = q; // null / 数字等非字符串值：不命中
            continue;
        }
        ++q;
        const size_t value_start = q;
        bool has_escape = false;
        while (q < line.size() && line[q] != '"')
        {
            if (line[q] == '\\')
            {
                has_escape = true;
                ++q;
                if (q < line.size())
                    ++q;
            }
            else
            {
                ++q;
            }
        }
        if (q >= line.size())
            return true; // 字符串未闭合，保守
        const std::string_view value = line.substr(value_start, q - value_start);
        ++q;
        if (has_escape)
            return true; // 含转义无法可靠比较，保守

        for (size_t i = 0; i < allowed_count; ++i)
        {
            const std::string_view want(allowed[i]);
            if (value.size() != want.size())
                continue;
            bool eq = true;
            for (size_t j = 0; j < value.size(); ++j)
            {
                if (std::tolower(static_cast<unsigned char>(value[j])) !=
                    std::tolower(static_cast<unsigned char>(want[j])))
                {
                    eq = false;
                    break;
                }
            }
            if (eq)
                return true;
        }
        pos = q;
    }
    return false;
}

// 跳过从 q 开始的 JSON token（对象/数组/普通值），返回其后的位置。
// 用于在 tags 数组里跳过对象元素，避免误把对象字符串里的 ']' 当成数组结束。
size_t skip_json_token(std::string_view line, size_t q)
{
    if (q >= line.size())
        return q;
    const char open_c = line[q];
    if (open_c == '"')
        return q; // 字符串由调用方处理
    const char close_c = (open_c == '{') ? '}' : ((open_c == '[') ? ']' : '\0');
    if (!close_c)
    {
        while (q < line.size() && line[q] != ',' && line[q] != ']' && line[q] != '}')
            ++q;
        return q;
    }

    ++q; // 跳过开括号
    int depth = 1;
    while (q < line.size() && depth > 0)
    {
        const char c = line[q];
        if (c == '"')
        {
            ++q;
            while (q < line.size())
            {
                if (line[q] == '\\')
                {
                    q += 2;
                    if (q > line.size())
                        q = line.size();
                }
                else if (line[q] == '"')
                {
                    ++q;
                    break;
                }
                else
                {
                    ++q;
                }
            }
            continue;
        }
        if (c == open_c)
            ++depth;
        else if (c == close_c)
            --depth;
        ++q;
    }
    return q;
}

// 把 JSON 字符串 token（引号内的原始字节）解码成 UTF-8 后与 name 比较。
// 返回 1=匹配，0=确定不匹配，-1=含无法可靠解码的内容（调用方应保守放行）。
int json_string_token_match(std::string_view raw, const std::string &name)
{
    static const char kBom[] = "\xEF\xBB\xBF";
    if (raw.find('\\') == std::string_view::npos)
    {
        if (raw.size() >= 3 && std::memcmp(raw.data(), kBom, 3) == 0)
            raw.remove_prefix(3);
        return raw == std::string_view(name) ? 1 : 0;
    }

    std::string decoded;
    decoded.reserve(raw.size());
    size_t i = 0;
    while (i < raw.size())
    {
        const char c = raw[i];
        if (c != '\\')
        {
            decoded += c;
            ++i;
            continue;
        }
        ++i; // 跳过反斜杠
        if (i >= raw.size())
            return -1;
        const char e = raw[i];
        ++i;
        switch (e)
        {
        case '"': decoded += '"'; break;
        case '\\': decoded += '\\'; break;
        case '/': decoded += '/'; break;
        case 'b': decoded += '\b'; break;
        case 'f': decoded += '\f'; break;
        case 'n': decoded += '\n'; break;
        case 'r': decoded += '\r'; break;
        case 't': decoded += '\t'; break;
        case 'u':
        {
            if (i + 4 > raw.size())
                return -1;
            uint32_t cp = 0;
            for (int k = 0; k < 4; ++k)
            {
                const char h = raw[i + static_cast<size_t>(k)];
                cp <<= 4;
                if (h >= '0' && h <= '9')
                    cp |= static_cast<uint32_t>(h - '0');
                else if (h >= 'a' && h <= 'f')
                    cp |= static_cast<uint32_t>(h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                    cp |= static_cast<uint32_t>(h - 'A' + 10);
                else
                    return -1;
            }
            i += 4;

            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
                // 高代理：需后随 \uXXXX 低代理才能组成非 BMP 字符
                if (i + 6 <= raw.size() && raw[i] == '\\' && raw[i + 1] == 'u')
                {
                    uint32_t lo = 0;
                    bool ok = true;
                    for (int k = 0; k < 4; ++k)
                    {
                        const char h = raw[i + 2 + static_cast<size_t>(k)];
                        lo <<= 4;
                        if (h >= '0' && h <= '9')
                            lo |= static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            lo |= static_cast<uint32_t>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            lo |= static_cast<uint32_t>(h - 'A' + 10);
                        else
                        {
                            ok = false;
                            break;
                        }
                    }
                    if (!ok || lo < 0xDC00 || lo > 0xDFFF)
                        return -1;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
                else
                {
                    return -1;
                }
            }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
            {
                return -1; // 孤立的低代理
            }

            if (cp < 0x80)
                decoded += static_cast<char>(cp);
            else if (cp < 0x800)
            {
                decoded += static_cast<char>(0xC0 | (cp >> 6));
                decoded += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                decoded += static_cast<char>(0xE0 | (cp >> 12));
                decoded += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                decoded += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                decoded += static_cast<char>(0xF0 | (cp >> 18));
                decoded += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                decoded += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                decoded += static_cast<char>(0x80 | (cp & 0x3F));
            }
            break;
        }
        default:
            return -1; // 未知转义：不确定
        }
    }

    if (decoded.size() >= 3 && std::memcmp(decoded.data(), kBom, 3) == 0)
        decoded.erase(0, 3);
    return decoded == name ? 1 : 0;
}

// 检查整行里是否存在包含全部 filter_tags 的 "tags" 数组。
// 每个标签在数组里以“名字字符串”（去 BOM 后比较）或“数字 ID”任一种形式
// 出现都算命中；返回 false 表示确定不命中，true 表示可能命中或无法可靠判断。
bool raw_tags_match(std::string_view line,
                    const std::vector<std::string> &names,
                    const std::vector<long> &ids)
{
    if (names.empty())
        return true;
    if (names.size() > 64)
        return true; // 数量过多时保守处理，直接完整解析
    const uint64_t need = (names.size() == 64)
                              ? ~uint64_t{0}
                              : ((uint64_t{1} << names.size()) - 1);

    constexpr size_t kKeyLen = 6; // "\"tags\""
    size_t pos = 0;
    while ((pos = line.find("\"tags\"", pos)) != std::string_view::npos)
    {
        size_t q = pos + kKeyLen;
        while (q < line.size() && is_json_ws(line[q]))
            ++q;
        if (q >= line.size() || line[q] != ':')
        {
            pos = q;
            continue;
        }
        ++q;
        while (q < line.size() && is_json_ws(line[q]))
            ++q;
        if (q >= line.size() || line[q] != '[')
        {
            // tags 不是数组（如 null）：这一处不命中，继续找其它同名键
            pos = q;
            continue;
        }
        ++q; // 进入数组

        uint64_t found = 0;
        while (q < line.size())
        {
            while (q < line.size() && is_json_ws(line[q]))
                ++q;
            if (q >= line.size())
                return true; // 数组未闭合，保守
            if (line[q] == ']')
                break;

            if (line[q] == '"')
            {
                ++q;
                const size_t tok_start = q;
                while (q < line.size() && line[q] != '"')
                {
                    if (line[q] == '\\')
                    {
                        ++q;
                        if (q < line.size())
                            ++q;
                    }
                    else
                    {
                        ++q;
                    }
                }
                if (q >= line.size())
                    return true; // 字符串未闭合，保守
                std::string_view tok = line.substr(tok_start, q - tok_start);
                ++q;
                for (size_t i = 0; i < names.size(); ++i)
                {
                    const int m = json_string_token_match(tok, names[i]);
                    if (m == 1)
                        found |= (uint64_t{1} << i);
                    else if (m == -1)
                        return true; // 无法可靠解码：保守交给完整解析
                }
            }
            else if (line[q] >= '0' && line[q] <= '9')
            {
                long v = 0;
                while (q < line.size() && line[q] >= '0' && line[q] <= '9')
                {
                    v = v * 10 + (line[q] - '0');
                    if (v > 1000000000L)
                        v = 1000000001L;
                    ++q;
                }
                for (size_t i = 0; i < ids.size(); ++i)
                    if (ids[i] >= 0 && v == ids[i])
                        found |= (uint64_t{1} << i);
            }
            else
            {
                q = skip_json_token(line, q); // 对象等其它元素
            }

            if (q < line.size() && line[q] == ',')
                ++q;
        }
        if (found == need)
            return true;
        if (q < line.size() && line[q] == ']')
            ++q;
        pos = q; // 继续找其它 "tags" 键
    }
    return false;
}

// 综合预筛：难度、类型、标签任一条件在原始文本上就确定不满足时返回 false。
bool raw_may_match(std::string_view line,
                   const std::vector<int> &difficulties,
                   const std::vector<std::string> &filter_tags,
                   const std::vector<long> &filter_tag_ids,
                   const std::vector<std::string> &types)
{
    if (!difficulties.empty())
    {
        bool allowed[9] = {false};
        for (int d : difficulties)
            if (d >= 0 && d <= 8)
                allowed[static_cast<size_t>(d)] = true;
        if (!raw_int_value_match(line, "\"difficulty\"", allowed, 8))
            return false;
    }

    if (!types.empty())
    {
        const char *allowed_types[2] = {nullptr, nullptr};
        size_t n = 0;
        for (const auto &t : types)
            if (n < 2)
                allowed_types[n++] = t.c_str();
        if (!raw_string_value_match(line, "\"type\"", allowed_types, n))
            return false;
    }

    if (!filter_tags.empty() && !raw_tags_match(line, filter_tags, filter_tag_ids))
        return false;

    return true;
}

} // namespace

bool luogu::select_problems(const ExportFilter &filter,
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

        for (const auto &tok : split_whitespace(raw))
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
                seen_tags.insert(to_lower_ascii(t));

            // 标签：多个值取“且”
            if (!filter_tags.empty())
            {
                bool all = true;
                for (const auto &wanted : filter_tags)
                {
                    const std::string key = to_lower_ascii(wanted);
                    if (std::find_if(p.tags.begin(), p.tags.end(),
                                     [&key](const std::string &t) { return to_lower_ascii(t) == key; }) ==
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

    // 5. 校验 --tag 名称确实存在于缓存中
    std::vector<std::string> not_found;
    for (const auto &wanted : filter_tags)
    {
        if (!seen_tags.count(to_lower_ascii(wanted)))
            not_found.push_back(wanted);
    }
    if (!not_found.empty())
    {
        error = "以下标签在题目缓存中不存在: " + join_strings(not_found, "、") +
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

std::string luogu::describe_filter(const ExportFilter &filter,
                                   const std::vector<std::string> &resolved_tags)
{
    const bool use_en = (filter.lang == "en");
    const bool show_difficulty = (filter.show.size() >= 2 && filter.show[0] == '1');
    const bool show_tags = (filter.show.size() >= 2 && filter.show[1] == '1');

    std::vector<std::string> conds;
    if (!resolved_tags.empty())
        conds.push_back("标签包含 " + join_strings(resolved_tags, "、"));
    if (!filter.difficulties.empty())
    {
        std::vector<std::string> ds;
        for (int d : filter.difficulties)
            ds.push_back(std::string(luogu::difficulty_label(d)) + "(" + std::to_string(d) + ")");
        conds.push_back("难度为 " + join_strings(ds, " 或 "));
    }
    if (!filter.types.empty())
        conds.push_back("类型为 " + join_strings(filter.types, "、"));
    if (use_en)
        conds.push_back("题面语言为英文（缺失时回退中文）");
    if (!show_difficulty)
        conds.push_back("不显示难度");
    if (!show_tags)
        conds.push_back("不显示算法类标签");
    return join_strings(conds, "；");
}
