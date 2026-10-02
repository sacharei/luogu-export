// src/export/latex/text.cpp
#include "luogu-export/export/latex/text.h"
#include "luogu-export/util/string_util.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace latex::detail
{
std::string trim(const std::string &s)
{
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return "";
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// 转义普通文本中的 LaTeX 特殊字符（数学/代码已先用占位符保护）
std::string escape_latex(std::string s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        switch (c)
        {
        case '\\': out += "\\textbackslash{}"; break;
        case '{': out += "\\{"; break;
        case '}': out += "\\}"; break;
        case '#': out += "\\#"; break;
        case '%': out += "\\%"; break;
        case '&': out += "\\&"; break;
        case '_': out += "\\_"; break;
        case '~': out += "\\textasciitilde{}"; break;
        case '^': out += "\\textasciicircum{}"; break;
        case '$': out += "\\$"; break;
        case '<': out += "\\textless{}"; break;
        case '>': out += "\\textgreater{}"; break;
        default: out += c;
        }
    }
    return out;
}

// 去掉字符串中的控制字符（制表符/回车等，ASCII < 0x20）。
// 用于章节标题：控制字符会污染 hyperref 书签（.out 文件），
// 导致后续编译报 "File ended while scanning use of \@@BOOKMARK"。
// 注意：\x01/\x02 是占位符哨兵（inline_to_latex 内部使用），不可清除，
// 因此该函数只在标题等“尚未进入占位符机制”的文本上使用。
std::string strip_control_chars(std::string s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (static_cast<unsigned char>(c) < 0x20 && c != '\x01' && c != '\x02')
        {
            out += ' ';
            continue;
        }
        out += c;
    }
    return out;
}

// \includegraphics 的路径：转义空格与反斜线
std::string escape_path(std::string s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if (c == ' ')
            out += "\\ ";
        else if (c == '\\')
            out += "\\textbackslash{}";
        else
            out += c;
    }
    return out;
}

// 用正则逐个替换，convert(match) 返回替换文本
std::string regex_transform(const std::string &s, const std::regex &re,
                            const std::function<std::string(const std::smatch &)> &convert)
{
    std::string out;
    size_t last = 0;
    for (std::sregex_iterator it(s.begin(), s.end(), re), end; it != end; ++it)
    {
        out += s.substr(last, it->position() - last);
        out += convert(*it);
        last = it->position() + it->length();
    }
    out += s.substr(last);
    return out;
}

// 键存在但为 null 时按缺省处理（多语言字段）
std::string safe_string(const nlohmann::json &j, const char *key)
{
    if (!j.contains(key) || !j[key].is_string())
        return "";
    return j[key].get<std::string>();
}

// 代码围栏的语言标记 → listings 的语言名。
// 未知语言返回空串（不高亮），listings 内置语言有限，其余按纯文本处理
std::string fence_to_listings_lang(std::string tag)
{
    tag = util::to_lower_ascii(trim(tag));
    if (tag.empty() || tag == "text" || tag == "plain" || tag == "none" ||
        tag == "txt" || tag == "console" || tag == "output" ||
        tag == "input" || tag == "markdown" || tag == "md" ||
        tag == "json" || tag == "yaml" || tag == "yml" || tag == "toml" ||
        tag == "diff" || tag == "ini" || tag == "csv" || tag == "dockerfile" ||
        tag == "gitignore" || tag == "log")
        return "";
    if (tag == "c" || tag == "c11" || tag == "c17")
        return "C";
    if (tag == "cpp" || tag == "c++" || tag == "cxx" || tag == "cc" ||
        tag == "c++11" || tag == "c++14" || tag == "c++17" || tag == "c++20")
        return "C++";
    if (tag == "c#" || tag == "csharp")
        return "CSharp"; // listings 语言名不能含 #，用自定义的 CSharp
    if (tag == "python" || tag == "py" || tag == "py3" || tag == "python3")
        return "Python";
    if (tag == "java")
        return "Java";
    if (tag == "pascal" || tag == "pas")
        return "Pascal";
    if (tag == "php")
        return "PHP";
    if (tag == "ruby" || tag == "rb")
        return "Ruby";
    if (tag == "go" || tag == "golang")
        return "Go";
    if (tag == "rust" || tag == "rs")
        return "Rust";
    if (tag == "javascript" || tag == "js" || tag == "node" ||
        tag == "nodejs" || tag == "jsx")
        return "JavaScript";
    if (tag == "typescript" || tag == "ts")
        return "TypeScript";
    if (tag == "html" || tag == "htm")
        return "HTML";
    if (tag == "xml" || tag == "svg")
        return "XML";
    if (tag == "css")
        return "CSS";
    if (tag == "bash" || tag == "sh" || tag == "shell" || tag == "zsh" ||
        tag == "bashrc" || tag == "console")
        return "bash";
    if (tag == "sql")
        return "SQL";
    if (tag == "matlab")
        return "Matlab";
    if (tag == "octave")
        return "Octave";
    if (tag == "perl" || tag == "pl")
        return "Perl";
    if (tag == "lua")
        return "Lua";
    if (tag == "haskell" || tag == "hs")
        return "Haskell";
    if (tag == "lisp" || tag == "scheme" || tag == "elisp" ||
        tag == "clisp" || tag == "racket")
        return "Lisp";
    if (tag == "fortran" || tag == "f90" || tag == "f95" || tag == "f")
        return "Fortran";
    if (tag == "vb" || tag == "vbnet" || tag == "visualbasic" ||
        tag == "basic" || tag == "vba")
        return "VBScript";
    if (tag == "r" || tag == "rscript")
        return "R";
    if (tag == "makefile" || tag == "make" || tag == "gnumake")
        return "make";
    // Objective-C 是 C 的超集，用 C 高亮即可
    if (tag == "objective-c" || tag == "objc" || tag == "objectivec" ||
        tag == "m")
        return "C";
    if (tag == "erlang" || tag == "erl")
        return "Erlang";
    if (tag == "delphi" || tag == "pascal")
        return "Delphi";
    if (tag == "prolog")
        return "Prolog";
    if (tag == "verilog" || tag == "v")
        return "Verilog";
    if (tag == "vhdl")
        return "VHDL";
    if (tag == "latex" || tag == "tex")
        return "TeX";
    if (tag == "ada")
        return "Ada";
    if (tag == "awk")
        return "Awk";
    if (tag == "tcl" || tag == "tk")
        return "tcl";
    return "";
}

// 把超过 limit 字符的行拆成多行：listings 的 breaklines 会先测量整行宽度，
// 超长行（如几千位数字）总宽会超过 TeX 的 \maxdimen（~16383pt），
// 报 "Dimension too large"；插入空格也没用（测量发生在断行之前），
// 只能物理拆行。仅影响极少数病态长行，正常代码/样例不受影响
std::string split_long_line(std::string line,
                            size_t limit,
                            size_t chunk)
{
    if (line.size() <= limit)
        return line;
    std::string out;
    out.reserve(line.size() + line.size() / chunk + 1);
    size_t i = 0;
    while (i < line.size())
    {
        if (i)
            out += '\n';
        out += line.substr(i, chunk);
        i += chunk;
    }
    return out;
}

// 按 '\n' 把字符串拆成行（不保留行尾换行；结尾换行不产生多余空行）
std::vector<std::string> split_lines(const std::string &content)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < content.size())
    {
        const size_t nl = content.find('\n', start);
        lines.push_back(nl == std::string::npos
                            ? content.substr(start)
                            : content.substr(start, nl - start));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    return lines;
}

// 逐行处理多行内容（用于样例输入/输出）
std::string split_long_lines(const std::string &content)
{
    std::string out;
    bool first = true;
    for (const auto &line : split_lines(content))
    {
        if (!first)
            out += '\n';
        first = false;
        out += split_long_line(line);
    }
    return out;
}
} // namespace latex::detail
