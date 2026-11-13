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

namespace
{
// 会自己开启 display 数学结构的顶层环境。这类环境不能再套在 \[ \] 里，
// 否则 amsmath 会报 "Erroneous nesting of equation structures"
// （如洛谷题面里常见的 $$\begin{align*}...\end{align*}$$）。
bool is_display_math_env(const std::string &env)
{
    static const std::set<std::string> kEnvs = {
        "align",     "align*",    "alignat",  "alignat*", "flalign",
        "flalign*",  "gather",    "gather*",  "multline", "multline*",
        "equation",  "equation*", "eqnarray", "eqnarray*",
    };
    return kEnvs.count(env) > 0;
}

// body 整体就是一个 display 数学环境时返回 true，环境名写入 env
bool is_standalone_math_env(const std::string &body, std::string &env)
{
    const std::string text = trim(body);
    if (text.rfind("\\begin{", 0) != 0)
        return false;
    const size_t close = text.find('}', 7);
    if (close == std::string::npos)
        return false;
    env = text.substr(7, close - 7);
    if (!is_display_math_env(env))
        return false;
    const std::string end = "\\end{" + env + "}";
    return text.size() >= end.size() &&
           text.compare(text.size() - end.size(), end.size(), end) == 0;
}
} // namespace

std::string markdown_to_latex(const std::string &markdown)
{
    std::vector<std::string> lines;
    lines = split_lines(markdown);
    lines.emplace_back(); // 末尾哨兵，简化处理

    std::string out;
    std::vector<std::string> env_stack; // 自定义块环境栈（quote/center/flushright/折叠框）
    // 当前是否位于折叠框内（含被拍平成 quote 的嵌套折叠框）
    auto inside_callout = [&env_stack]() {
        for (const std::string &e : env_stack)
            if (e.rfind("luogucallout", 0) == 0)
                return true;
        return false;
    };
    // ::cute-table{...} 只作用于紧随其后的那一张表；未遇到表格时保持默认样式
    TableStyle pending_table_style;

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
                // 先把代码拼好，顺便统计物理行数（split_long_line 可能把一行拆成多行，
                // 这个行数决定行号的最大位数）
                std::string code_body;
                size_t listing_lines = 0;
                for (const auto &cl : code_lines)
                {
                    const std::string l = split_long_line(cl);
                    listing_lines += 1 + static_cast<size_t>(std::count(l.begin(), l.end(), '\n'));
                    code_body += l + "\n";
                }
                std::string opts = "breaklines=true";
                if (!lang.empty())
                    opts = "language=" + lang + "," + opts;
                // 折叠框里的代码框：listings 的行号画在代码框左侧的版心外，
                // 默认会伸进折叠框的彩色竖线里（多位数行号尤其明显）。
                // 按行号位数把整段 listing 右移，给行号留出净空；
                // xleftmargin 会同时移动代码框和行号，因此嵌套折叠框里也有效。
                if (inside_callout())
                {
                    int digits = 1;
                    for (size_t n = listing_lines; n >= 10; n /= 10)
                        ++digits;
                    opts += ",xleftmargin=" + std::to_string(8 + 4 * digits) + "pt";
                }
                out += "\\begin{lstlisting}[" + opts + "]\n" + code_body + "\\end{lstlisting}\n\n";
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

        // ::cute-table 的样式只作用于紧随其后的表格；若下一块不是表格则丢弃
        if (pending_table_style.kind != TableStyle::Plain && !is_table_row(line))
            pending_table_style = TableStyle{};

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
            const std::string body = sanitize_math(util::join_strings(ml, "\n"));
            // 本身已是 align* 之类的 display 环境时直接输出，不能再包 \[ \]
            std::string env;
            if (is_standalone_math_env(body, env))
                out += body + "\n\n";
            else
                out += "\\[\n" + body + "\n\\]\n\n";
            continue;
        }

        // ---- Luogu 扩展块：anti-ai / cute-table / align / epigraph / info 等 ----
        // 其中，anti-ai 是神秘防 AI，直接不显示
        if (line.rfind("::anti-ai", 0) == 0)
        {
            ++i;
            continue;
        }
        // ::cute-table{tuack} / ::cute-table{tuack=N} / ::cute-table{three}
        // 只改变紧随其后的那张表格的画法（见 TableStyle），标记行本身不输出
        if (line.rfind("::cute-table", 0) == 0)
        {
            const size_t lb = line.find('{');
            const size_t rb = (lb == std::string::npos)
                                  ? std::string::npos
                                  : line.find('}', lb + 1);
            const std::string arg =
                (lb != std::string::npos && rb != std::string::npos && rb > lb)
                    ? trim(line.substr(lb + 1, rb - lb - 1))
                    : std::string();
            if (arg.find("three") != std::string::npos)
            {
                pending_table_style = TableStyle{TableStyle::Three, 0};
            }
            else
            {
                // tuack=N：每 N 列插一条粗竖线分组，缺省 0 表示不分组
                int group = 0;
                const size_t eq = arg.find('=');
                if (eq != std::string::npos)
                {
                    try
                    {
                        group = std::max(0, std::stoi(trim(arg.substr(eq + 1))));
                    }
                    catch (...)
                    {
                        group = 0;
                    }
                }
                pending_table_style = TableStyle{TableStyle::Tuack, group};
            }
            ++i;
            continue;
        }
        if (line.size() >= 2 && line[0] == ':' && line[1] == ':')
        {
            static const std::regex kCloser(R"(^\s*:+$)", std::regex::icase);
            static const std::regex kCustom(
                R"(^\s*:+\s*(align\{(center|right)\}|epigraph(?:\[[^\]]*\])?|(info|success|warning|error)(?:\[[^\]]*\])?(?:\{[^}]*\})?)\s*$)",
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
                {
                    title = tm[1].str();
                    // 标题最终放在 \textbf{...} 这样的水平盒子里，块级公式
                    // $$...$$ 会被 LaTeX 拒绝（如 :::success[$$\sum...$$]），
                    // 统一降级为行内公式 $...$
                    for (size_t p = title.find("$$"); p != std::string::npos;
                         p = title.find("$$", p + 1))
                        title.erase(p, 1);
                }

                // 折叠框（info / success / warning / error）：无论是否带 {open}
                // 一律展开输出，用带彩色左竖线的 luogucallout* 环境（见 preamble）
                const std::string callout = util::to_lower_ascii(m[3].str());
                // 最外层折叠框用 framed 的 luogucallout*（逐页画彩色竖线）；
                // 嵌套在另一个折叠框里的折叠框改用 quote，只保留缩进和彩色标题。
                // 原因：framed 的 \MakeFramed 嵌套且跨页时会耗尽输入栈
                // （TeX capacity exceeded），拍平嵌套层级可以避免这个限制，
                // 外层折叠框的竖线仍然覆盖整段内容。
                const bool nested_in_callout = inside_callout();
                std::string env = "quote";
                if (!callout.empty())
                {
                    if (!nested_in_callout)
                        env = "luogucallout" + callout;
                }
                else if (spec.rfind("align{center}", 0) == 0)
                    env = "center";
                else if (spec.rfind("align{right}", 0) == 0)
                    env = "flushright";
                env_stack.push_back(env);
                out += "\\begin{" + env + "}\n";
                if (!title.empty())
                {
                    const std::string t = inline_to_latex(title);
                    if (!callout.empty())
                        // 标题用与竖线相同的颜色；\par 结束标题段，
                        // 避免标题是折叠框最后内容时 \\ 报 "no line here to end"；
                        // \noindent 让标题与竖线/正文左缘对齐
                        out += "\\noindent\\textbf{\\luogufontcallouttitle{}\\textcolor{luogu" + callout + "}{" + t + "}}\\par\n";
                    else
                        out += "\\textbf{\\luogufontcallouttitle{}" + t + "}\\\\\n";
                }
                if (!callout.empty())
                    out += "\\luogufontcalloutbody\n";
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
                const std::string &cur_env = env_stack.empty() ? std::string() : env_stack.back();
                const bool in_quote_like = (cur_env == "quote" ||
                                            cur_env == "center" ||
                                            cur_env == "flushright" ||
                                            cur_env.rfind("luogucallout", 0) == 0);
                static const char *kCmds[] = {"section*", "subsection*", "subsubsection*",
                                              "paragraph*", "subparagraph*", "paragraph*"};
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

            out += "\\begin{quote}\\luogufontquote\n";
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
                    out += ordered ? "\\luogufontlist\\begin{enumerate}\n"
                                   : "\\luogufontlist\\begin{itemize}\n";
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
                {
                    emit_table(rows, out, pending_table_style);
                    pending_table_style = TableStyle{}; // 样式只对这张表生效
                }
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
    if(!tagsfrom.empty() || !tagsdata.empty() || !tagsarea.empty() || !tagsspec.empty()) out += "\\hspace*{5.78pt}标签：";
    for(auto &tag : tagsfrom) tag = "\\textcolor{white}{\\colorbox[HTML]{13c2c2}{\\luogufonttag\\small\\vphantom{草}" + tag + "}}";
    for(auto &tag : tagsdata) tag = "\\textcolor{white}{\\colorbox[HTML]{3498db}{\\luogufonttag\\small\\vphantom{草}" + tag + "}}";
    for(auto &tag : tagsarea) tag = "\\textcolor{white}{\\colorbox[HTML]{53c41a}{\\luogufonttag\\small\\vphantom{草}" + tag + "}}";
    for(auto &tag : tagsspec) tag = "\\textcolor{white}{\\colorbox[HTML]{f39c11}{\\luogufonttag\\small\\vphantom{草}" + tag + "}}";
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
