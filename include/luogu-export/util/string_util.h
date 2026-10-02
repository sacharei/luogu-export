// include/luogu-export/util/string_util.h
#ifndef LUOGU_EXPORT_UTIL_STRING_UTIL_H
#define LUOGU_EXPORT_UTIL_STRING_UTIL_H

#include <string>
#include <vector>

// 项目内多处用到的基础字符串工具（原先在 common.cpp / markdown.cpp /
// main.cpp / latex 模块里各有一份副本，现统一到这里）。
namespace util
{
    // ASCII 转小写（不改动非 ASCII 字节）
    std::string to_lower_ascii(std::string s);

    // 用 sep 连接字符串
    std::string join_strings(const std::vector<std::string> &v, const std::string &sep);

    // 按空白拆成多个 token（空 token 忽略）
    std::vector<std::string> split_whitespace(const std::string &s);
}

#endif // LUOGU_EXPORT_UTIL_STRING_UTIL_H
