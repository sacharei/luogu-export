// src/export/latex/document.cpp
#include "luogu-export/export/latex.h"
#include "luogu-export/contents/article.h"
#include "luogu-export/contents/problem.h"
#include "luogu-export/export/latex/image.h"
#include "luogu-export/export/latex/inline.h"
#include "luogu-export/export/latex/math.h"
#include "luogu-export/export/latex/table.h"
#include "luogu-export/export/latex/text.h"
#include "luogu-export/util/problem_info.h"
#include "luogu-export/util/string_util.h"

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

namespace latex
{
using namespace detail;

std::string markdown_to_latex(const std::string &markdown)
{
    std::vector<std::string> lines;
    lines = split_lines(markdown);
    lines.emplace_back(); // 末尾哨兵，简化处理

    std::string out;
    std::vector<std::string> env_stack; // 自定义块环境栈（quote/center/flushright）

    char fence = 0;        // 当前代码围栏字符（` 或 ~），0 表示不在代码块内
    size_t fence_len = 0;
    std::string fence_lang; // 围栏语言标记（```cpp 里的 cpp）
    std::vector<std::string> code_lines;

    size_t i = 0;
    while (i < lines.size())
    {
        const std::string raw = lines[i];
        const std::string line = trim(raw);

        // ---- 代码块内部 ----
        if (fence)
        {
            if (line.size() >= fence_len &&
                std::string(line.begin(), line.begin() + fence_len) == std::string(fence_len, fence))
            {
                // lstlisting + breaklines：超长行（如几千个括号）会生成
                // 极宽的 hbox，XeTeX 会直接崩溃；开启自动换行后不再超宽
                const std::string lang = fence_to_listings_lang(fence_lang);
                out += "\\begin{lstlisting}";
                if (!lang.empty())
                    out += "[language=" + lang + ",breaklines=true]";
                else
                    out += "[breaklines=true]";
                out += "\n";
                for (const auto &cl : code_lines)
                    out += split_long_line(cl) + "\n";
                out += "\\end{lstlisting}\n\n";
                fence = 0;
                fence_len = 0;
                fence_lang.clear();
                code_lines.clear();
            }
            else
            {
                code_lines.push_back(raw);
            }
            ++i;
            continue;
        }

        if (line.empty())
        {
            ++i;
            continue;
        }

        // ---- 打开代码块（``` 或 ~~~）----
        if (line.size() >= 3 && (line[0] == '`' || line[0] == '~'))
        {
            size_t n = 0;
            while (n < line.size() && line[n] == line[0])
                ++n;
            if (n >= 3)
            {
                fence = line[0];
                fence_len = n;
                fence_lang = trim(line.substr(n));
                code_lines.clear();
                ++i;
                continue;
            }
        }

        // ---- 块级数学 $$...$$ ----
        if (line.rfind("$$", 0) == 0)
        {
            std::string math = line.substr(2);
            ++i; // 消费当前行（单行公式或起始行）
            // 单行公式：行内还有 $$ 即闭合（允许后面再跟文字，如 $$...$$。）
            const size_t close = math.find("$$");
            if (close != std::string::npos)
            {
                math = math.substr(0, close);
            }
            else
            {
                while (i < lines.size())
                {
                    const std::string l = trim(lines[i]);
                    // % 开头的行是 LaTeX 注释（常用来注释掉 \def 等），直接丢弃；
                    // 否则 \% 转义会让注释内容真正执行
                    if (!l.empty() && l[0] == '%')
                    {
                        ++i;
                        continue;
                    }
                    const size_t lc = l.find("$$");
                    if (lc != std::string::npos)
                    {
                        math += "\n" + l.substr(0, lc);
                        ++i; // 消费闭合行
                        break;
                    }
                    math += "\n" + lines[i];
                    ++i;
                }
            }

            // 过滤空行，避免显示公式里出现空行触发 "Missing $ inserted"
            std::vector<std::string> ml;
            for (const auto &mline : split_lines(math))
                if (!trim(mline).empty())
                    ml.push_back(trim(mline));
            out += "\\[\n" + sanitize_math(util::join_strings(ml, "\n")) + "\n\\]\n\n";
            continue;
        }

        // ---- Luogu 扩展块：anti-ai / cute-table / align / epigraph / info 等 ----
        // 其中，anti-ai 是神秘防 AI，直接不显示
        if (line.rfind("::cute-table", 0) == 0 || line.rfind("::anti-ai", 0) == 0)
        {
            ++i;
            continue;
        }
        if (line.size() >= 2 && line[0] == ':' && line[1] == ':')
        {
            static const std::regex kCloser(R"(^\s*:+$)", std::regex::icase);
            static const std::regex kCustom(
                R"(^\s*:+\s*(align\{(center|right)\}|epigraph(?:\[[^\]]*\])?|(?:info|success|warning|error)(?:\[[^\]]*\])?(?:\{[^}]*\})?)\s*$)",
                std::regex::icase);
            static const std::regex kTitle(R"(\[([^\]]*)\])");

            std::smatch m;
            if (std::regex_match(line, m, kCloser) && line.size() >= 3)
            {
                if (!env_stack.empty())
                {
                    out += "\\end{" + env_stack.back() + "}\n\n";
                    env_stack.pop_back();
                }
                ++i;
                continue;
            }
            if (std::regex_match(line, m, kCustom))
            {
                const std::string spec = m[1].str();
                std::string title;
                std::smatch tm;
                if (std::regex_search(spec, tm, kTitle))
                    title = tm[1].str();

                std::string env = "quote";
                if (spec.rfind("align{center}", 0) == 0)
                    env = "center";
                else if (spec.rfind("align{right}", 0) == 0)
                    env = "flushright";
                env_stack.push_back(env);
                out += "\\begin{" + env + "}\n";
                if (!title.empty())
                    out += "\\textbf{" + inline_to_latex(title) + "}\\\\\n";
                ++i;
                continue;
            }
        }

        // ---- 标题 ----
        if (line[0] == '#')
        {
            size_t n = 0;
            while (n < line.size() && line[n] == '#')
                ++n;
            if (n == line.size() || line[n] == ' ')
            {
                const std::string title = inline_to_latex(trim(line.substr(n)));
                const bool in_quote_like = (!env_stack.empty() &&
                                            (env_stack.back() == "quote" ||
                                             env_stack.back() == "center" ||
                                             env_stack.back() == "flushright"));
                static const char *kCmds[] = {"section*", "subsection*", "subsubsection*",
                                              "paragraph*", "subparagraph*"};
                if (in_quote_like || n > 5)
                    out += "\\textbf{" + title + "}\n\n";
                else
                {
                    out += "\\" + std::string(kCmds[n - 1]) + "{" + title + "}\n";
                    // paragraph*（4 个 #）/ subparagraph*（5 个 #）默认是 run-in 标题
                    // （与后续内容同一行），会导致后面的表格/段落被并到标题行而溢出；
                    // \hspace*{0pt}\par 把标题独立成段
                    if (n >= 4)
                        out += "\\hspace*{0pt}\\par\n";
                    out += "\n";
                }
                ++i;
                continue;
            }
        }

        // ---- 分隔线 ----
        {
            static const std::regex kHr(R"(^([-*_])(\s*\1){2,}\s*$)");
            if (std::regex_match(line, kHr))
            {
                out += "\\bigskip\n{\\color{gray} \\hrule}\n\\medskip\n\n";
                ++i;
                continue;
            }
        }

        // ---- 区块引用 ----
        if (line[0] == '>')
        {
            std::vector<std::vector<ParaPart>> groups; // 空行分段
            std::vector<ParaPart> cur;
            while (i < lines.size())
            {
                const std::string l = trim(lines[i]);
                if (l.empty() || l[0] != '>')
                    break;
                size_t pos = 0;
                while (pos < l.size() && l[pos] == '>')
                    ++pos;
                if (pos < l.size() && l[pos] == ' ')
                    ++pos;
                std::string body = l.substr(pos);
                bool hard = false;
                if (body.size() >= 2 && body.back() == ' ' && body[body.size() - 2] == ' ')
                {
                    hard = true;
                    body = body.substr(0, body.size() - 2);
                }
                else if (!body.empty() && body.back() == ' ')
                {
                    body.pop_back();
                }

                if (trim(body).empty())
                {
                    if (!cur.empty())
                    {
                        groups.push_back(cur);
                        cur.clear();
                    }
                }
                else if (body[0] == '#')
                {
                    if (!cur.empty())
                    {
                        groups.push_back(cur);
                        cur.clear();
                    }
                    size_t n = 0;
                    while (n < body.size() && body[n] == '#')
                        ++n;
                    groups.push_back({ParaPart{"\\textbf{" + inline_to_latex(trim(body.substr(n))) + "}", false, false}});
                }
                else
                {
                    if (!cur.empty() &&
                        (has_unclosed_paren_or_bracket(cur.back().text) ||
                         has_unclosed_math(cur.back().text)))
                    {
                        // 上一行链接未闭合（如 [![](img)]( 换行 url），并入同一片段
                        cur.back().text += " " + body;
                        cur.back().hard = cur.back().hard || hard;
                    }
                    else
                    {
                        cur.emplace_back(ParaPart{body, hard, false});
                    }
                }
                ++i;
            }
            if (!cur.empty())
                groups.push_back(cur);

            out += "\\begin{quote}\n";
            for (const auto &g : groups)
                emit_paragraph_parts(g, out);
            out += "\\end{quote}\n\n";
            continue;
        }

        // ---- 列表 ----
        {
            static const std::regex kItem(R"(^(\s*)([-+*]|\d+\.)\s+(.*)$)");
            std::smatch m;
            if (std::regex_match(raw, m, kItem))
            {
                struct Frame
                {
                    int indent;
                    bool ordered;
                };
                std::vector<Frame> stack;
                auto open_env = [&](bool ordered) {
                    out += ordered ? "\\begin{enumerate}\n" : "\\begin{itemize}\n";
                };
                auto close_env = [&]() {
                    out += stack.back().ordered ? "\\end{enumerate}\n" : "\\end{itemize}\n";
                    stack.pop_back();
                };

                while (i < lines.size())
                {
                    std::smatch im;
                    if (!std::regex_match(lines[i], im, kItem))
                        break;
                    const int indent = static_cast<int>(im[1].str().size());
                    const std::string marker = im[2].str();
                    const bool ordered = std::isdigit(static_cast<unsigned char>(marker[0]));
                    std::string content = im[3].str();

                    if (stack.empty())
                    {
                        open_env(ordered);
                        stack.push_back({indent, ordered});
                    }
                    else if (indent > stack.back().indent)
                    {
                        // LaTeX 的 itemize/enumerate 最多嵌套 4 层，
                        // 更深的层级归入最内层列表，避免 "Too deeply nested"
                        if (stack.size() < 4)
                        {
                            open_env(ordered);
                            stack.push_back({indent, ordered});
                        }
                    }
                    else
                    {
                        while (!stack.empty() && indent < stack.back().indent)
                            close_env();
                        if (stack.empty() || ordered != stack.back().ordered)
                        {
                            if (!stack.empty())
                                close_env();
                            open_env(ordered);
                            stack.push_back({indent, ordered});
                        }
                    }

                    // 任务列表
                    std::string prefix;
                    if (content.rfind("[ ]", 0) == 0)
                    {
                        prefix = "$\\square$ ";
                        content = content.substr(3);
                    }
                    else if (content.rfind("[x]", 0) == 0 || content.rfind("[X]", 0) == 0)
                    {
                        prefix = "$\\boxtimes$ ";
                        content = content.substr(3);
                    }

                    std::string item = prefix + inline_to_latex(content);
                    // \item 内容以 [ 开头会被当作可选参数，用花括号包住
                    if (!item.empty() && item[0] == '[')
                        item = "{" + item + "}";
                    out += "\\item " + item + "\n";
                    ++i;
                }
                while (!stack.empty())
                    close_env();
                out += "\n";
                continue;
            }
        }

        // ---- 表格（支持有无首尾竖线两种写法）----
        if (is_table_row(line))
        {
            // 下一行是分隔行才按表格处理，避免误判普通含 | 的文本
            size_t j = i + 1;
            while (j < lines.size() && trim(lines[j]).empty())
                ++j;
            if (j < lines.size() && is_table_separator_row(lines[j]))
            {
                std::vector<std::string> rows;
                while (i < lines.size())
                {
                    const std::string t = trim(lines[i]);
                    if (t.empty() || !is_table_row(t))
                        break;
                    rows.push_back(t);
                    ++i;
                }
                if (rows.size() >= 2)
                    emit_table(rows, out);
                else
                    for (const auto &r : rows)
                        out += inline_to_latex(r) + "\n\n";
                continue;
            }
            // 不是表格，落入普通段落处理
        }

        // ---- 普通段落 ----
        std::vector<ParaPart> parts;
        size_t collected = 0;
        while (i < lines.size())
        {
            const std::string r = lines[i];
            const std::string t = trim(r);
            // 首行即使像“块语法”也照常当段落收集，保证外层循环总能前进，
            // 避免单反引号、无空格标题等未被块处理器识别的行造成死循环
            if (t.empty() || (collected > 0 && is_block_start(t)))
                break;
            bool hard = false;
            std::string body = r;
            if (body.size() >= 2 && body[body.size() - 1] == ' ' && body[body.size() - 2] == ' ')
            {
                hard = true;
                body = body.substr(0, body.size() - 2);
            }
            else if (!body.empty() && body.back() == ' ')
            {
                body.pop_back(); // 单个尾部空格按普通空格处理
            }
            if (!parts.empty() &&
                (has_unclosed_paren_or_bracket(parts.back().text) ||
                 has_unclosed_math(parts.back().text)))
            {
                // 上一行链接未闭合（如 [![](img)]( 换行 url），或
                // $ 未闭合（多行 $...$ 数学块），并入同一片段
                parts.back().text += " " + body;
                parts.back().hard = parts.back().hard || hard;
            }
            else
            {
                parts.emplace_back(ParaPart{body, hard, false});
            }
            ++collected;
            ++i;
        }
        if (!parts.empty())
        {
            emit_paragraph_parts(parts, out);
            continue;
        }
        // 理论上到不了这里；保险起见直接前进，避免死循环
        ++i;
    }

    // 收尾：关闭未闭合的块环境
    while (!env_stack.empty())
    {
        out += "\\end{" + env_stack.back() + "}\n\n";
        env_stack.pop_back();
    }
    return out;
}

std::string problem_to_latex(const problem::Problem &p, const Options &opt)
{
    const bool use_en = (opt.lang == "en");

    auto field = [&](const char *key, const std::string &zh) -> std::string {
        if (!use_en)
            return zh;
        const std::string en = safe_string(p.translations, key);
        return en.empty() ? zh : en;
    };

    std::string out;
    out += "\\section{" + inline_to_latex(strip_control_chars(p.pid + " " + field("title", p.name))) + "}\n\n";

    // 标签 / 时空限制
    out += "\\begin{center}\n\\begin{tabularx}{\\textwidth}{XX}\n";
    const auto limits = luogu::format_limits(p.time, p.memory);
    out += "时间限制: " + limits.first + " & 内存限制: " + limits.second + " \\\\\n";
    out += "\\end{tabularx}\n\\end{center}\n";
    auto tagsfrom = luogu::filter_tags_by_type(p.tags, 3);
    auto tagsdata = luogu::filter_tags_by_type(p.tags, 4);
    auto tagsarea = luogu::filter_tags_by_type(p.tags, 1);
    auto tagsspec = luogu::filter_tags_by_type(p.tags, 5);
    if(!tagsfrom.empty() || !tagsdata.empty() || !tagsarea.empty() || !tagsspec.empty()) out += "\\hspace{5.78pt}标签：";
    for(auto &tag : tagsfrom) tag = "\\textcolor{white}{\\colorbox[HTML]{13c2c2}{\\tagsfonts\\small\\vphantom{草}" + tag + "}}";
    for(auto &tag : tagsdata) tag = "\\textcolor{white}{\\colorbox[HTML]{3498db}{\\tagsfonts\\small\\vphantom{草}" + tag + "}}";
    for(auto &tag : tagsarea) tag = "\\textcolor{white}{\\colorbox[HTML]{53c41a}{\\tagsfonts\\small\\vphantom{草}" + tag + "}}";
    for(auto &tag : tagsspec) tag = "\\textcolor{white}{\\colorbox[HTML]{f39c11}{\\tagsfonts\\small\\vphantom{草}" + tag + "}}";
    if(!tagsfrom.empty()) out += util::join_strings(tagsfrom, " \\ ") + " \\ ";
    if(!tagsdata.empty()) out += util::join_strings(tagsdata, " \\ ") + " \\ ";
    if(!tagsarea.empty()) out += util::join_strings(tagsarea, " \\ ") + " \\ ";
    if(!tagsspec.empty()) out += util::join_strings(tagsspec, " \\ ") + " \\ ";

    const std::string background = field("background", p.background);
    const std::string description = field("description", p.description);
    const std::string formatI = field("inputFormat", p.formatI);
    const std::string formatO = field("outputFormat", p.formatO);
    const std::string hint = field("hint", p.hint);

    if (!background.empty())
        out += "\\subsection*{题目背景}\n\n" + markdown_to_latex(background) + "\n";
    if (!description.empty())
        out += "\\subsection*{题目描述}\n\n" + markdown_to_latex(description) + "\n";
    if (!formatI.empty())
        out += "\\subsection*{输入格式}\n\n" + markdown_to_latex(formatI) + "\n";
    if (!formatO.empty())
        out += "\\subsection*{输出格式}\n\n" + markdown_to_latex(formatO) + "\n";

    int sample_no = 1;
    for (const auto &s : p.samples)
    {
        out += "\\subsection*{输入输出样例 \\#" + std::to_string(sample_no) + "}\n\n";
        out += "\\subsubsection*{输入 \\#" + std::to_string(sample_no) + "}\n\n";
        out += "\\begin{lstlisting}\n" +
               split_long_lines(s.first) +
               "\n\\end{lstlisting}\n\n";
        out += "\\subsubsection*{输出 \\#" + std::to_string(sample_no) + "}\n\n";
        out += "\\begin{lstlisting}\n" +
               split_long_lines(s.second) +
               "\n\\end{lstlisting}\n\n";
        ++sample_no;
    }

    if (!hint.empty())
        out += "\\subsection*{说明/提示}\n\n" + markdown_to_latex(hint) + "\n";

    return out;
}

std::string article_to_latex(const article::Article &a)
{
    std::string out;
    out += "\\section{" + inline_to_latex(strip_control_chars(a.title)) + "}\n\n";
    if (!a.author_name.empty())
        out += "作者：" + a.author_name + " \\\\\n\n";
    out += markdown_to_latex(a.content) + "\n";
    return out;
}
} // namespace latex
