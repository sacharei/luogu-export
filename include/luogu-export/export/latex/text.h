// include/luogu-export/export/latex/text.h
#ifndef LUOGU_EXPORT_LATEX_TEXT_H
#define LUOGU_EXPORT_LATEX_TEXT_H

#include <cstddef>
#include <functional>
#include <regex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// LaTeX 导出实现内部使用的文本/转义/正则工具。
// 这些函数原来都在 latex.cpp 的匿名命名空间中，拆分后统一放入
// latex::detail，便于各实现文件共享。
namespace latex::detail
{
    // 去掉首尾空格与制表符
    std::string trim(const std::string &s);

    // 用正则逐个替换，convert(match) 返回替换文本
    std::string regex_transform(const std::string &s, const std::regex &re,
                                const std::function<std::string(const std::smatch &)> &convert);

    // 转义普通文本中的 LaTeX 特殊字符（数学/代码已先用占位符保护）
    std::string escape_latex(std::string s);

    // 去掉字符串中的控制字符（制表符/回车等，ASCII < 0x20）。
    // 保留 \x01/\x02 占位符哨兵。
    std::string strip_control_chars(std::string s);

    // \includegraphics 的路径：转义空格与反斜线
    std::string escape_path(std::string s);

    // 从 JSON 对象取字符串；键缺失或值为 null 时返回空串
    std::string safe_string(const nlohmann::json &j, const char *key);

    // 代码围栏的语言标记 → listings 的语言名；未知语言返回空串
    std::string fence_to_listings_lang(std::string tag);

    // 把超过 limit 字符的行拆成多行（listings 的 breaklines 无法处理超长行）
    std::string split_long_line(std::string line, size_t limit = 2500, size_t chunk = 1000);

    // 按 '\n' 把字符串拆成行（不保留行尾换行；结尾换行不产生多余空行）
    std::vector<std::string> split_lines(const std::string &content);

    // 逐行处理多行内容（用于样例输入/输出）
    std::string split_long_lines(const std::string &content);
}

#endif // LUOGU_EXPORT_LATEX_TEXT_H
