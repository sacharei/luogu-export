// src/export/json_scan.cpp
// NDJSON 原始文本的快速预筛（见 include/luogu-export/export/json_scan.h）。
#include "luogu-export/export/json_scan.h"

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

namespace luogu::detail
{
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

TagScanResult raw_tags_scan(std::string_view line,
                            const std::vector<std::string> &names,
                            const std::vector<long> &ids)
{
    TagScanResult res;
    if (names.empty() || names.size() > 64)
    {
        res.may_any = true;
        return res;
    }

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

        while (q < line.size())
        {
            while (q < line.size() && is_json_ws(line[q]))
                ++q;
            if (q >= line.size())
            {
                res.may_any = true; // 数组未闭合，保守
                return res;
            }
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
                {
                    res.may_any = true; // 字符串未闭合，保守
                    return res;
                }
                std::string_view tok = line.substr(tok_start, q - tok_start);
                ++q;
                for (size_t i = 0; i < names.size(); ++i)
                {
                    const int m = json_string_token_match(tok, names[i]);
                    if (m == 1)
                        res.found |= (uint64_t{1} << i);
                    else if (m == -1)
                    {
                        res.may_any = true; // 无法可靠解码：保守
                        return res;
                    }
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
                        res.found |= (uint64_t{1} << i);
            }
            else
            {
                q = skip_json_token(line, q); // 对象等其它元素
            }

            if (q < line.size() && line[q] == ',')
                ++q;
        }
        if (q < line.size() && line[q] == ']')
            ++q;
        pos = q; // 继续找其它 "tags" 键
    }
    return res;
}

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
    const TagScanResult res = raw_tags_scan(line, names, ids);
    if (res.may_any)
        return true;
    return res.found == need;
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
} // namespace luogu::detail
