// src/latex.cpp
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <functional>
#include <regex>
#include <set>
#include <string>
#include <vector>
#include "luogu-export/contents/article.h"
#include "luogu-export/contents/problem.h"
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/export/common.h"
#include "luogu-export/export/latex.h"
#include "luogu-export/util/problem_info.h"

namespace
{

std::string trim(const std::string &s)
{
    const size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return "";
    const size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::string to_lower_ascii(std::string s)
{
    for (auto &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// 洛谷题面常在公式里用 \newcommand/\renewcommand 自定义命令，
// 标准 LaTeX 中若与已有命令同名会报 "already defined"；统一转成 \def（允许重复定义）
// 对齐环境行归一化：洛谷题面里 \begin{array}{c} 等常有多余/缺少的 &，
// 导致 "Extra alignment tab"；把每行统一到目标列数（array 按 spec，矩阵按最大行宽）
std::string regex_transform(const std::string &s, const std::regex &re,
                            const std::function<std::string(const std::smatch &)> &convert);

std::string normalize_alignment(std::string s)
{
    static const std::set<std::string> kEnvs = {
        "array", "matrix", "pmatrix", "bmatrix", "vmatrix", "Vmatrix",
        "smallmatrix", "aligned", "alignedat", "gathered", "cases", "dcases",
        "rcases", "split", "subarray", "matrix*", "pmatrix*", "bmatrix*",
        "cases*",
    };
    // 这些环境不接受 &（如 gathered），行内多余的 & 只能去掉
    static const std::set<std::string> kNoAmpEnvs = {"gathered"};

    std::string out;
    size_t p = 0;
    while (p < s.size())
    {
        if (s.compare(p, 7, "\\begin{") != 0)
        {
            out += s[p];
            ++p;
            continue;
        }

        const size_t name_end = s.find('}', p + 7);
        if (name_end == std::string::npos)
        {
            out += s[p];
            ++p;
            continue;
        }
        const std::string name = s.substr(p + 7, name_end - p - 7);
        if (!kEnvs.count(name))
        {
            out += s[p];
            ++p;
            continue;
        }

        size_t body_start = name_end + 1;
        size_t spec_cols = 0;
        if (name == "array" || name == "subarray")
        {
            if (body_start < s.size() && s[body_start] == '{')
            {
                const size_t spec_end = s.find('}', body_start);
                if (spec_end != std::string::npos)
                {
                    const std::string spec = s.substr(body_start + 1, spec_end - body_start - 1);
                    for (char c : spec)
                        if (std::isalpha(static_cast<unsigned char>(c)))
                            ++spec_cols;
                    body_start = spec_end + 1;
                }
            }
        }

        const std::string endtag = "\\end{" + name + "}";
        // 用同名环境深度找匹配的 \end{name}（洛谷题面有嵌套同名环境）
        size_t end_pos = std::string::npos;
        {
            size_t q = body_start;
            int name_depth = 1;
            while (q < s.size())
            {
                if (s.compare(q, 7, "\\begin{") == 0)
                {
                    const size_t ne = s.find('}', q + 7);
                    if (ne != std::string::npos &&
                        s.substr(q + 7, ne - q - 7) == name)
                        ++name_depth;
                    q = (ne != std::string::npos) ? ne + 1 : q + 7;
                    continue;
                }
                if (s.compare(q, endtag.size(), endtag) == 0)
                {
                    --name_depth;
                    if (name_depth == 0)
                    {
                        end_pos = q;
                        break;
                    }
                    q += endtag.size();
                    continue;
                }
                ++q;
            }
        }
        if (end_pos == std::string::npos)
        {
            out += s[p];
            ++p;
            continue;
        }
        // 先递归处理嵌套的对齐环境（内层 array 列规格、行内 & 等）
        std::string body = normalize_alignment(
            s.substr(body_start, end_pos - body_start));

        // 去掉多余的 &&（洛谷题面常见写法，LaTeX 会报 Extra alignment tab）；
        // 只处理本层（跳过嵌套环境内部）
        {
            std::string t;
            int d = 0;
            int ed = 0;
            size_t k = 0;
            while (k < body.size())
            {
                if (body.compare(k, 7, "\\begin{") == 0)
                {
                    ++ed;
                    t += body.substr(k, 7);
                    k += 7;
                    continue;
                }
                if (body.compare(k, 5, "\\end{") == 0)
                {
                    if (ed > 0)
                        --ed;
                    t += body.substr(k, 5);
                    k += 5;
                    continue;
                }
                if (body[k] == '{') ++d;
                else if (body[k] == '}' && d > 0) --d;
                if (body[k] == '&' && d == 0 && ed == 0 &&
                    k + 1 < body.size() && body[k + 1] == '&')
                {
                    ++k; // 合并连续 &&：跳过第一个，保留第二个
                    continue;
                }
                t += body[k];
                ++k;
            }
            body = std::move(t);
        }

        // 按行拆分（\\ 或 \cr）：忽略花括号内和嵌套环境内部，
        // 否则内层 aligned/array 的 \\ 会被误当成外层换行
        std::vector<std::string> rows;
        std::string cur;
        int depth = 0;
        int env_depth = 0;
        size_t k = 0;
        while (k < body.size())
        {
            if (body.compare(k, 7, "\\begin{") == 0)
            {
                ++env_depth;
                cur += body.substr(k, 7);
                k += 7;
                continue;
            }
            if (body.compare(k, 5, "\\end{") == 0)
            {
                if (env_depth > 0)
                    --env_depth;
                cur += body.substr(k, 5);
                k += 5;
                continue;
            }
            if (body[k] == '{') ++depth;
            else if (body[k] == '}') --depth;
            if (depth <= 0 && env_depth == 0)
            {
                if (body.compare(k, 2, "\\\\") == 0)
                {
                    rows.push_back(cur);
                    cur.clear();
                    k += 2;
                    // 跳过 \\ 的可选间距参数 [-..pt]
                    if (k < body.size() && body[k] == '[')
                    {
                        const size_t close = body.find(']', k);
                        if (close != std::string::npos)
                            k = close + 1;
                    }
                    continue;
                }
                if (body.compare(k, 3, "\\cr") == 0 &&
                    (k + 3 >= body.size() || !std::isalpha(static_cast<unsigned char>(body[k + 3]))))
                {
                    rows.push_back(cur);
                    cur.clear();
                    k += 3;
                    continue;
                }
            }
            cur += body[k];
            ++k;
        }
        if (!trim(cur).empty() || rows.empty())
            rows.push_back(cur);

        // 统计每行单元格数（忽略行首 \hline；嵌套环境内部不算）
        auto count_cells = [](const std::string &row) {
            size_t n = 1;
            int d = 0;
            int ed = 0;
            size_t i = 0;
            while (i < row.size())
            {
                if (row.compare(i, 7, "\\begin{") == 0)
                {
                    ++ed;
                    i += 7;
                    continue;
                }
                if (row.compare(i, 5, "\\end{") == 0)
                {
                    if (ed > 0)
                        --ed;
                    i += 5;
                    continue;
                }
                if (row[i] == '{') ++d;
                else if (row[i] == '}' && d > 0) --d;
                else if (row[i] == '&' && (i == 0 || row[i - 1] != '\\') &&
                         d == 0 && ed == 0) ++n;
                ++i;
            }
            return n;
        };
        // 目标列数：
        // - array/subarray：以显式列规格为准（多余的 & 截掉）
        // - cases 系列：固定 2 列
        // - matrix/aligned 等：按最宽的一行
        size_t target;
        if (name == "array" || name == "subarray")
        {
            target = spec_cols;
            if (target == 0)
                for (const auto &r : rows)
                    target = std::max(target, count_cells(r));
        }
        else if (name == "cases" || name == "dcases" || name == "rcases" ||
                 name == "cases*")
        {
            target = 2;
        }
        else
        {
            target = 1;
            for (const auto &r : rows)
                target = std::max(target, count_cells(r));
        }

        // 逐行归一化：截断多余单元格、补齐缺失单元格
        std::string new_body;
        for (size_t ri = 0; ri < rows.size(); ++ri)
        {
            if (ri)
                new_body += "\\\\";

            std::string row = rows[ri];
            std::string hline;
            size_t start = 0;
            // 连续多个 \hline / \noalign{\hline} 都要作为行前缀取走，
            // 否则第二个 \hline 会变成单元格内容触发 Misplaced \noalign
            while (true)
            {
                if (row.compare(start, 6, "\\hline") == 0 &&
                    (start + 6 >= row.size() ||
                     !std::isalpha(static_cast<unsigned char>(row[start + 6]))))
                {
                    hline += "\\hline";
                    start += 6;
                    continue;
                }
                if (row.compare(start, 16, "\\noalign{\\hline}") == 0)
                {
                    hline += "\\noalign{\\hline}";
                    start += 16;
                    continue;
                }
                break;
            }

            std::vector<std::string> cells;
            std::string cell;
            int d = 0;
            int ed = 0;
            size_t i = start;
            while (i < row.size())
            {
                if (row.compare(i, 7, "\\begin{") == 0)
                {
                    ++ed;
                    cell += row.substr(i, 7);
                    i += 7;
                    continue;
                }
                if (row.compare(i, 5, "\\end{") == 0)
                {
                    if (ed > 0)
                        --ed;
                    cell += row.substr(i, 5);
                    i += 5;
                    continue;
                }
                if (row[i] == '{') ++d;
                else if (row[i] == '}' && d > 0) --d;
                if (row[i] == '&' && (i == 0 || row[i - 1] != '\\') &&
                    d == 0 && ed == 0)
                {
                    cells.push_back(cell);
                    cell.clear();
                    ++i;
                }
                else
                {
                    cell += row[i];
                    ++i;
                }
            }
            cells.push_back(cell);

            if (cells.size() > target)
                cells.resize(target);
            while (cells.size() < target)
                cells.push_back("");

            new_body += hline;
            for (size_t ci = 0; ci < cells.size(); ++ci)
            {
                if (ci && !kNoAmpEnvs.count(name))
                    new_body += " & ";
                else if (ci)
                    new_body += " "; // gathered 等环境不接受 &
                if (!kNoAmpEnvs.count(name) || !cells[ci].empty())
                    new_body += cells[ci];
            }
        }

        // amsmath 的 matrix 环境最多 10 列，超过时改用 array
        const bool matrix_family = (name != "array" && name != "cases" &&
                                    name != "dcases" && name != "rcases");
        if (name == "array" || (matrix_family && target > 10))
        {
            // 重建列规格：取 target 列（统一用 c，保证能编译）
            out += "\\begin{array}{" + std::string(target, 'c') + "}" + new_body +
                   "\\end{array}";
        }
        else
        {
            out += "\\begin{" + name + "}" + new_body + endtag;
        }
        p = end_pos + endtag.size();
    }
    return out;
}

// 数学公式里是否有“顶层”（不在任何 \begin 环境、也不在花括号内）的 & 或 \\ / \cr。
// 洛谷题面常把两段矩阵用顶层 & 和 \\ 直接拼在一行（KaTeX 能渲染），
// 标准 LaTeX 必须包进一个对齐环境才能编译
bool has_top_level_align(const std::string &s)
{
    int brace = 0;
    int env = 0;
    size_t i = 0;
    while (i < s.size())
    {
        if (s.compare(i, 7, "\\begin{") == 0)
        {
            ++env;
            i += 7;
            continue;
        }
        if (s.compare(i, 5, "\\end{") == 0)
        {
            if (env > 0)
                --env;
            i += 5;
            continue;
        }
        if (s[i] == '{')
        {
            ++brace;
            ++i;
            continue;
        }
        if (s[i] == '}')
        {
            if (brace > 0)
                --brace;
            ++i;
            continue;
        }
        if (brace == 0 && env == 0)
        {
            if (s[i] == '&' && (i == 0 || s[i - 1] != '\\'))
                return true;
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\\')
                return true;
            if (s.compare(i, 3, "\\cr") == 0 &&
                (i + 3 >= s.size() ||
                 !std::isalpha(static_cast<unsigned char>(s[i + 3]))))
                return true;
        }
        ++i;
    }
    return false;
}

std::string sanitize_math(std::string s)
{
    // \verb 内容是字面文本：先整体保护起来（占位符），
    // 等所有转换结束后再按文本模式转义还原，
    // 避免中间的 \color / px / % # 等转换污染 verb 内容
    std::vector<std::string> verb_raws;
    auto verb_placeholder = [&](size_t i) {
        return std::string("\x02V") + std::to_string(i) + "\x02";
    };
    {
        static const std::regex kVerb(R"(\\verb(.)(.*?)\1)");
        s = regex_transform(s, kVerb, [&](const std::smatch &m) {
            verb_raws.push_back(m[2].str());
            return "{\\texttt{" + verb_placeholder(verb_raws.size() - 1) + "}}";
        });
    }

    // 源数据里的 \text{\\}（KaTeX 允许文本内换行）在 LaTeX 的表格/矩阵里
    // 会触发 Misplaced \cr；\newline 在文本模式任何位置都合法
    static const std::regex kTextNewline(R"(\\text\{\s*\\\\\s*\})");
    s = std::regex_replace(s, kTextNewline, "\\text{\\newline}");

    // Unicode 数学符号（∑ 等）是普通字符，\limits 要求数学算子，
    // 转成对应的 LaTeX 命令
    static const std::map<std::string, std::string> kUnicodeMath = {
        {"\u2211", "\\sum"}, {"\u220f", "\\prod"}, {"\u222b", "\\int"},
        {"\u222e", "\\oint"}, {"\u221e", "\\infty"}, {"\u2264", "\\le"},
        {"\u2265", "\\ge"}, {"\u2260", "\\neq"}, {"\u00d7", "\\times"},
        {"\u00f7", "\\div"}, {"\u2200", "\\forall"}, {"\u2203", "\\exists"},
        {"\u2208", "\\in"}, {"\u2209", "\\notin"}, {"\u2229", "\\cap"},
        {"\u222a", "\\cup"}, {"\u2286", "\\subseteq"},
        {"\u2287", "\\supseteq"}, {"\u2295", "\\oplus"},
        {"\u2297", "\\otimes"}, {"\u2192", "\\rightarrow"},
        {"\u2190", "\\leftarrow"}, {"\u21d2", "\\Rightarrow"},
        {"\u21d0", "\\Leftarrow"}, {"\u21d4", "\\Leftrightarrow"},
        {"\u221a", "\\sqrt"}, {"\u00b1", "\\pm"}, {"\u2213", "\\mp"},
        {"\u22c5", "\\cdot"}, {"\u223c", "\\sim"}, {"\u2248", "\\approx"},
        {"\u2261", "\\equiv"}, {"\u2202", "\\partial"}, {"\u2207", "\\nabla"},
        {"\u2220", "\\angle"}, {"\u22a5", "\\bot"}, {"\u2227", "\\wedge"},
        {"\u2228", "\\vee"}, {"\u230a", "\\lfloor"}, {"\u230b", "\\rfloor"},
        {"\u2308", "\\lceil"}, {"\u2309", "\\rceil"}, {"\u2225", "\\parallel"},
        {"\u2223", "\\mid"},
    };
    for (const auto &kv : kUnicodeMath)
    {
        std::string t;
        size_t p = 0;
        const std::string &u = kv.first;
        const std::string &rep = kv.second;
        while ((p = s.find(u, p)) != std::string::npos)
        {
            s.replace(p, u.size(), rep);
            p += rep.size();
        }
    }

    // 公式末尾悬空的 ^ / _（如“……则省略 ^”）：没有指数/下标参数，
    // 直接当成符号输出，避免 Missing { inserted（已转义的 \_ 不受影响）
    static const std::regex kTrailingCaret(R"((^|[^\\])[\^_](?=\s*\$?\s*$))");
    s = regex_transform(s, kTrailingCaret, [&](const std::smatch &m) {
        return m[1].str() + "\\wedge";
    });

    // KaTeX 兼容：\colorbox{#hex} / \textcolor{#hex} / \color{#hex}
    // → xcolor 的 HTML 颜色模型
    // 3 位十六进制色值（如 #fff）补齐成 6 位（xcolor HTML 模型要求）
    auto hex_pad = [](const std::string &h) {
        if (h.size() == 3)
            return std::string() + h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
        return h;
    };
    static const std::regex kColorBox(R"(\\colorbox\{#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})\})");
    static const std::regex kTextColor(R"(\\textcolor\{#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})\})");
    static const std::regex kColor(R"(\\color\{#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})\})");
    s = regex_transform(s, kColorBox, [&](const std::smatch &m) {
        return "\\colorbox[HTML]{" + hex_pad(m[1].str()) + "}";
    });
    s = regex_transform(s, kTextColor, [&](const std::smatch &m) {
        return "\\textcolor[HTML]{" + hex_pad(m[1].str()) + "}";
    });
    s = regex_transform(s, kColor, [&](const std::smatch &m) {
        return "\\color[HTML]{" + hex_pad(m[1].str()) + "}";
    });

    // 2 位十六进制（洛谷题面里的 \color{ff}）按白色处理，避免 Undefined color
    static const std::regex kColor2Hex(R"(\\color\{([0-9a-fA-F]{2})\})");
    s = std::regex_replace(s, kColor2Hex, "\\color{white}");

    // \fcolorbox{frame}{bg}{...}：任一参数是十六进制时转成 HTML 模型
    static const std::regex kFColorBox(R"(\\fcolorbox\{([^}]*)\}\{([^}]*)\}\{)");
    static const std::map<std::string, std::string> kNamedHex = {
        {"black", "000000"}, {"white", "FFFFFF"}, {"red", "FF0000"},
        {"green", "00FF00"}, {"blue", "0000FF"}, {"yellow", "FFFF00"},
        {"cyan", "00FFFF"}, {"magenta", "FF00FF"}, {"orange", "FFA500"},
        {"purple", "800080"}, {"gray", "808080"}, {"grey", "808080"},
        {"brown", "A52A2A"}, {"pink", "FFC0CB"}, {"teal", "008080"},
        {"violet", "EE82EE"}, {"lime", "00FF00"}, {"olive", "808000"},
        {"gold", "FFD700"},
    };
    auto is_hex = [](const std::string &c) {
        return c.size() == 3 || c.size() == 6;
    };
    s = regex_transform(s, kFColorBox, [&](const std::smatch &m) {
        auto to_hex = [&](const std::string &c) -> std::string {
            std::string x = c;
            if (!x.empty() && x[0] == '#')
                x = x.substr(1);
            if (is_hex(x) &&
                std::all_of(x.begin(), x.end(), [](char ch) {
                    return std::isxdigit(static_cast<unsigned char>(ch));
                }))
                return hex_pad(x);
            const auto it = kNamedHex.find(to_lower_ascii(x));
            return it != kNamedHex.end() ? it->second : std::string();
        };
        const std::string f = to_hex(m[1].str());
        const std::string b = to_hex(m[2].str());
        if (!f.empty() && !b.empty())
            return "\\fcolorbox[HTML]{" + f + "}{" + b + "}{";
        return m[0].str();
    });

    // 常见笔误：$k^[th}$ → $k^{th}$
    static const std::regex kCaretBracket(R"(\^\[)");
    s = std::regex_replace(s, kCaretBracket, "^{");

    // KaTeX 支持 px 单位，LaTeX 不支持；统一转成 pt
    static const std::regex kPx(R"((\d+(?:\.\d+)?)px)");
    s = std::regex_replace(s, kPx, "$1pt");

    // \hspace 不接受 mu（数学单位），必须用 \mkern；\hspace{3mu} → \mkern3mu
    static const std::regex kHspaceMu(R"(\\hspace\{(\d+(?:\.\d+)?)mu\})");
    s = std::regex_replace(s, kHspaceMu, "\\mkern$1mu");

    // 洛谷题面常见笔误 \\end{cases} / \\\\end{cases}（多写/少写反斜杠）：
    // 行分隔符 \\ 会吃掉 \end 的反斜杠，统一还原成单个 \end{
    static const std::regex kRowEnd(R"(\\+end\{)");
    s = std::regex_replace(s, kRowEnd, "\\end{");

    // \overline\texttt{ab} 这类“重音命令直接跟另一个命令”的写法：
    // 重音命令需要花括号参数，把后面的命令连同参数一起包进 {} 
    {
        static const std::set<std::string> kAccents = {
            "overline", "underline", "overbrace", "underbrace", "widehat",
            "widetilde", "overrightarrow", "overleftarrow",
            "overleftrightarrow",
        };
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            bool matched = false;
            if (s[p] == '\\')
            {
                size_t w = p + 1;
                while (w < s.size() &&
                       std::isalpha(static_cast<unsigned char>(s[w])))
                    ++w;
                const std::string name = s.substr(p + 1, w - p - 1);
                if (kAccents.count(name) && w < s.size() && s[w] == '\\')
                {
                    size_t q = w;
                    size_t w2 = q + 1;
                    while (w2 < s.size() &&
                           std::isalpha(static_cast<unsigned char>(s[w2])))
                        ++w2;
                    size_t end = w2;
                    while (end < s.size() && s[end] == '{')
                    {
                        size_t d = 1;
                        size_t m = end + 1;
                        while (m < s.size() && d > 0)
                        {
                            if (s[m] == '{')
                                ++d;
                            else if (s[m] == '}')
                                --d;
                            ++m;
                        }
                        if (d != 0)
                            break;
                        end = m;
                    }
                    if (end > w2)
                    {
                        t += s.substr(p, w - p); // \overline
                        t += "{";
                        t += s.substr(w, end - w); // \texttt{ab}
                        t += "}";
                        p = end;
                        matched = true;
                    }
                }
            }
            if (!matched)
            {
                t += s[p];
                ++p;
            }
        }
        s = std::move(t);
    }

    // $90^\degree$ 这类写法会变成双重上标，直接展开
    static const std::regex kCaretDegree(R"(\^\\degree)");
    s = std::regex_replace(s, kCaretDegree, "^{\\circ}");

    // 旧字体命令 \tt{...} → \texttt{...}；\tt 后跟数字/字母串也转换
    static const std::regex kTT(R"(\\tt\{)");
    static const std::regex kTTPlain(R"(\\tt\s+([A-Za-z0-9]+))");
    s = std::regex_replace(s, kTT, "\\texttt{");
    s = std::regex_replace(s, kTTPlain, "\\texttt{$1}");

    // 裸 \texttt（后面没跟 {，如 \texttt \\_）在数学模式会吞掉下一个
    // 命令当参数，补一个空花括号
    static const std::regex kTTBare(R"(\\texttt(?![{]))");
    s = std::regex_replace(s, kTTBare, "\\texttt{}");

    // \kern{...} 不接受花括号参数（TeX 原语），转成 \hspace{...}
    static const std::regex kKern(R"(\\kern\{)");
    s = std::regex_replace(s, kKern, "\\hspace{");

    // \space 后紧跟中文字符在 xelatex 会报 Undefined control sequence，
    // 转成控制空格（数学/文本模式都可用）
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 6, "\\space") == 0 &&
                (p + 6 >= s.size() || !std::isalpha(static_cast<unsigned char>(s[p + 6]))))
            {
                t += "\\ ";
                p += 6;
                continue;
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // \LaTeX 是文本命令，在数学模式会触发 spacefactor 错误 → 用 \text 包裹
    static const std::regex kLaTeX(R"(\\LaTeX)");
    s = std::regex_replace(s, kLaTeX, "\\text{\\LaTeX}");

    // \text 后面直接跟中文字符（无花括号）时补空花括号，避免把中文当参数
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 5, "\\text") == 0)
            {
                const size_t after = p + 5;
                if (after >= s.size() ||
                    (!std::isalpha(static_cast<unsigned char>(s[after])) && s[after] != '{'))
                {
                    t += "\\text{}";
                    p = after;
                    continue;
                }
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // \texttt{...} 在数学模式里，命令（\textcolor、\textbackslash 等）直接保留
    // （LaTeX 数学模式 texttt 能正常执行）；只需转义裸特殊字符 _ # % & ^ ~ { }
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 8, "\\texttt{") == 0)
            {
                size_t q = p + 8;
                int depth = 1;
                std::string content;
                while (q < s.size() && depth > 0)
                {
                    if (s[q] == '\\' && q + 1 < s.size() &&
                        !std::isalpha(static_cast<unsigned char>(s[q + 1])))
                    {
                        content += s[q];
                        content += s[q + 1]; // 转义对原样保留
                        q += 2;
                        continue;
                    }
                    if (s[q] == '\\' && q + 1 < s.size() &&
                        std::isalpha(static_cast<unsigned char>(s[q + 1])))
                    {
                        // 控制词（如 \textcolor）整体保留
                        size_t w = q + 1;
                        while (w < s.size() &&
                               std::isalpha(static_cast<unsigned char>(s[w])))
                            ++w;
                        content += s.substr(q, w - q);
                        q = w;
                        continue;
                    }
                    if (s[q] == '{')
                        ++depth;
                    else if (s[q] == '}')
                    {
                        --depth;
                        if (depth == 0)
                            break;
                    }
                    content += s[q];
                    ++q;
                }
                if (depth == 0)
                {
                    // 数学符号在 texttt（文本模式）里未定义，需包 $...$ 显示；
                    // 字号命令（\small 等）在数学模式未定义，直接去掉
                    static const std::set<std::string> kMathSymbols = {
                        "sim", "times", "le", "ge", "leq", "geq", "neq", "ne",
                        "in", "notin", "pm", "mp", "cdot", "div", "oplus",
                        "ominus", "otimes", "circ", "mid", "nmid", "to",
                        "rightarrow", "leftarrow", "Rightarrow", "Leftarrow",
                        "Leftrightarrow", "mapsto", "dots", "cdots", "ldots",
                        "infty", "forall", "exists", "partial", "nabla",
                        "approx", "equiv", "propto", "subset", "subseteq",
                        "supset", "supseteq", "cup", "cap", "setminus", "sqrt",
                        "sum", "prod", "int", "max", "min", "mod", "bmod",
                        "pmod", "argmax", "argmin", "lvert", "rvert",
                        "lVert", "rVert", "angle", "bot", "top", "wedge",
                        "vee", "land", "lor", "not", "bigcup", "bigcap",
                    };
                    static const std::set<std::string> kSizeCmds = {
                        "tiny", "scriptsize", "footnotesize", "small",
                        "normalsize", "large", "Large", "LARGE", "huge",
                        "Huge",
                    };
                    std::string esc;
                    for (size_t k = 0; k < content.size(); ++k)
                    {
                        const char c = content[k];
                        if (c == '\\' && k + 1 < content.size())
                        {
                            if (!std::isalpha(static_cast<unsigned char>(content[k + 1])) &&
                                (content[k + 1] == '^' || content[k + 1] == '~'))
                            {
                                // \^ \~ 是重音命令，在 texttt 里需转成文本符号
                                esc += (content[k + 1] == '^')
                                           ? "\\textasciicircum{}"
                                           : "\\textasciitilde{}";
                                k += 1;
                                continue;
                            }
                            if (std::isalpha(static_cast<unsigned char>(content[k + 1])))
                            {
                                // 控制词（\textcolor、\textbackslash 等）连同其
                                // 花括号参数（如 \textcolor{red}、\textbackslash{}）
                                // 原样保留，否则转义参数里的 { } 会破坏命令
                                size_t j = k + 1;
                                while (j < content.size() &&
                                       std::isalpha(static_cast<unsigned char>(content[j])))
                                    ++j;
                                while (j < content.size() && content[j] == '{')
                                {
                                    size_t d = 1;
                                    size_t m = j + 1;
                                    while (m < content.size() && d > 0)
                                    {
                                        if (content[m] == '{')
                                            ++d;
                                        else if (content[m] == '}')
                                            --d;
                                        ++m;
                                    }
                                    if (d != 0)
                                        break;
                                    j = m;
                                }
                                const std::string word =
                                    content.substr(k, j - k);
                                const size_t word_end = word.find_first_of("{ ");
                                const std::string name = word.substr(
                                    1, word_end == std::string::npos
                                           ? word.size() - 1
                                           : word_end - 1);
                                if (kSizeCmds.count(name))
                                {
                                    // 字号命令去掉（内容保留，被循环继续处理）
                                    k = j - 1;
                                    continue;
                                }
                                if (kMathSymbols.count(name))
                                {
                                    // 数学符号连同参数包上 $...$（如 \sqrt{2}）
                                    esc += "$" + word + "$";
                                    k = j - 1;
                                    continue;
                                }
                                esc += content.substr(k, j - k);
                                k = j - 1;
                            }
                            else
                            {
                                // 转义对（\{ \} \_ 等）原样保留
                                esc += c;
                                esc += content[k + 1];
                                ++k;
                            }
                            continue;
                        }
                        if (c == '\\') // 结尾悬空的 \ → \textbackslash{}
                        {
                            esc += "\\textbackslash{}";
                            continue;
                        }
                        switch (c)
                        {
                        case '_': esc += "\\_"; break;
                        case '#': esc += "\\#"; break;
                        case '%': esc += "\\%"; break;
                        case '&': esc += "\\&"; break;
                        case '~': esc += "\\textasciitilde{}"; break;
                        case '^': esc += "\\textasciicircum{}"; break;
                        case '{': esc += "\\{"; break;
                        case '}': esc += "\\}"; break;
                        default: esc += c;
                        }
                    }
                    t += "\\texttt{" + esc + "}";
                    p = q + 1;
                    continue;
                }
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // \operatorname{...} 参数里含 \color 时，\limits 会报
    // "Limit controls must follow a math operator"，把参数整体包一层花括号
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 14, "\\operatorname{") == 0)
            {
                size_t q = p + 14;
                int depth = 1;
                while (q < s.size() && depth > 0)
                {
                    if (s[q] == '{')
                        ++depth;
                    else if (s[q] == '}')
                        --depth;
                    ++q;
                }
                if (depth == 0)
                {
                    const std::string inner =
                        s.substr(p + 14, q - p - 14 - 1);
                    if (inner.find("\\color") != std::string::npos)
                    {
                        t += "\\operatorname{{" + inner + "}}";
                        p = q;
                        continue;
                    }
                }
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // \text{...} 里的数学符号（\le、\ldots 等）在文本模式未定义，
    // 包上 $...$；\text{ 与 \texttt{ 区分开（\texttt 已单独处理）
    {
        static const std::set<std::string> kTextMathSymbols = {
            "le", "leq", "ge", "geq", "ne", "neq", "sim", "times", "div",
            "pm", "mp", "cdot", "oplus", "ominus", "otimes", "circ", "mid",
            "nmid", "to", "rightarrow", "leftarrow", "Rightarrow",
            "Leftarrow", "Leftrightarrow", "mapsto", "dots", "cdots",
            "ldots", "infty", "forall", "exists", "partial", "nabla",
            "approx", "equiv", "propto", "subset", "subseteq", "supset",
            "supseteq", "cup", "cap", "setminus", "sqrt", "sum", "prod",
            "int", "max", "min", "mod", "bmod", "pmod", "lvert", "rvert",
            "angle", "bot", "top", "wedge", "vee", "land", "lor", "not",
            "bigcup", "bigcap", "in", "notin", "ni", "lfloor", "rfloor",
            "lceil", "rceil", "vert", "Vert", "langle", "rangle",
        };
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 6, "\\text{") == 0)
            {
                size_t q = p + 6;
                int depth = 1;
                while (q < s.size() && depth > 0)
                {
                    if (s[q] == '{')
                        ++depth;
                    else if (s[q] == '}')
                        --depth;
                    ++q;
                }
                if (depth == 0)
                {
                    std::string inner = s.substr(p + 6, q - p - 6 - 1);
                    std::string esc;
                    size_t k = 0;
                    bool in_math_span = false;
                    while (k < inner.size())
                    {
                        if (inner[k] == '$')
                        {
                            esc += '$';
                            in_math_span = !in_math_span;
                            ++k;
                            continue;
                        }
                        if (inner[k] == '\\' && k + 1 < inner.size() &&
                            std::isalpha(static_cast<unsigned char>(inner[k + 1])) &&
                            !in_math_span)
                        {
                            size_t w = k + 1;
                            while (w < inner.size() &&
                                   std::isalpha(static_cast<unsigned char>(inner[w])))
                                ++w;
                            size_t end = w;
                            while (end < inner.size() && inner[end] == '{')
                            {
                                size_t d = 1;
                                size_t m = end + 1;
                                while (m < inner.size() && d > 0)
                                {
                                    if (inner[m] == '{')
                                        ++d;
                                    else if (inner[m] == '}')
                                        --d;
                                    ++m;
                                }
                                if (d != 0)
                                    break;
                                end = m;
                            }
                            const std::string name = inner.substr(k + 1, w - k - 1);
                            if (kTextMathSymbols.count(name))
                            {
                                esc += "$" + inner.substr(k, end - k) + "$";
                                k = end;
                                continue;
                            }
                        }
                        esc += inner[k];
                        ++k;
                    }
                    t += "\\text{" + esc + "}";
                    p = q;
                    continue;
                }
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // 裸 \sout（未跟 {）会吞掉后续命令作为参数（如 \sout\text{...}），
    // 直接删掉，保留正文
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 7, "\\sout{") == 0)
            {
                t += "\\sout{";
                p += 7;
                continue;
            }
            if (s.compare(p, 5, "\\sout") == 0)
            {
                p += 5; // 裸 \sout 删掉
                continue;
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }


    // bm 包无法处理 \bm{...\color...} / \boldsymbol{...\color...}，
    // 内容含 color 时额外包一层花括号；\bm 的参数里 ~ 会触发
    // Missing number，转成数学空格
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            const bool is_bm = s.compare(p, 4, "\\bm{") == 0;
            const bool is_bsym = s.compare(p, 12, "\\boldsymbol{") == 0;
            if (is_bm || is_bsym)
            {
                const size_t open_len = is_bm ? 4 : 12;
                const std::string prefix = is_bm ? "\\bm" : "\\boldsymbol";
                size_t depth = 1;
                size_t q = p + open_len;
                while (q < s.size() && depth > 0)
                {
                    if (s[q] == '{')
                        ++depth;
                    else if (s[q] == '}')
                        --depth;
                    ++q;
                }
                if (depth == 0)
                {
                    std::string inner = s.substr(p + open_len, q - p - open_len - 1);
                    bool need_brace = false;
                    {
                        std::string fixed;
                        for (char ch : inner)
                        {
                            if (ch == '~')
                            {
                                fixed += "\\ ";
                                need_brace = true;
                            }
                            else
                                fixed += ch;
                        }
                        inner = std::move(fixed);
                    }
                    static const char *kColorCmds[] = {
                        "\\color", "\\textcolor", "\\red", "\\blue",
                        "\\green", "\\pink", "\\orange", "\\purple",
                        "\\brown", "\\gray", "\\cyan", "\\teal",
                        "\\magenta", "\\yellow", "\\violet",
                    };
                    for (const char *cc : kColorCmds)
                        if (inner.find(cc) != std::string::npos)
                            need_brace = true;
                    if (need_brace)
                    {
                        t += prefix + "{{" + inner + "}}";
                        p = q;
                        continue;
                    }
                }
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // 对齐环境行归一化（多余/缺少 &、数组列规格不匹配）
    s = normalize_alignment(s);

    // 顶层 & / \\：有 \begin 环境时把整段包进 aligned（如两段矩阵用 & 直接拼接），
    // 没有环境时按普通字符转义（$&@$、样例输入换行等）
    {
        const bool in_env = s.find("\\begin{") != std::string::npos;
        if (in_env && has_top_level_align(s))
        {
            s = "\\begin{aligned}\n" + s + "\n\\end{aligned}";
        }
        else if (!in_env)
        {
            std::string t;
            size_t k = 0;
            while (k < s.size())
            {
                const char c = s[k];
                if (c == '&' && (k == 0 || s[k - 1] != '\\'))
                {
                    t += "\\&";
                    ++k;
                }
                else if (c == '\\' && k + 1 < s.size() && s[k + 1] == '\\')
                {
                    // \text{\\} 在表格/矩阵里会触发 Misplaced \cr，
                    // \newline 在文本模式里任何位置都合法
                    t += "\\text{\\newline}";
                    k += 2;
                }
                else
                {
                    t += c;
                    ++k;
                }
            }
            s = std::move(t);
        }
    }

    // \newcommand → \def（先于 %/# 转义，这样定义体内的 #1 参数引用
    // 在转义阶段已被识别为 \def 宏参数而保留）
    static const std::regex kNewCommandBraced(R"(\\(?:re)?newcommand\s*\{([^}]*)\})");
    static const std::regex kNewCommandPlain(R"(\\(?:re)?newcommand\s+([A-Za-z@]+))");
    s = std::regex_replace(s, kNewCommandBraced, "\\def$1");
    s = std::regex_replace(s, kNewCommandPlain, "\\def$1");

    // \newcommand 的 [N] 参数个数写法（\def\cases[1]{...}）对 \def 无效，
    // 转成标准的参数形式 \def\cases#1{...}
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 5, "\\def\\") == 0)
            {
                size_t w = p + 5;
                while (w < s.size() &&
                       std::isalpha(static_cast<unsigned char>(s[w])))
                    ++w;
                if (w < s.size() && s[w] == '[')
                {
                    size_t e = w + 1;
                    while (e < s.size() &&
                           std::isdigit(static_cast<unsigned char>(s[e])))
                        ++e;
                    if (e < s.size() && s[e] == ']' && e > w + 1)
                    {
                        const int n =
                            std::stoi(s.substr(w + 1, e - w - 1));
                        std::string params;
                        for (int k = 1; k <= n; ++k)
                            params += "#" + std::to_string(k);
                        t += s.substr(p, w - p) + params;
                        p = e + 1;
                        continue;
                    }
                }
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // 裸 % 和 # 在 LaTeX（含数学模式）里是特殊字符，需转义；
    // 已转义的 \% / \# 先保护起来，避免二次转义
    auto escape_special = [](std::string t, char c, const std::string &escaped) {
        const std::string esc_placeholder = "\x01P\x02";
        size_t pos = 0;
        while ((pos = t.find(escaped, pos)) != std::string::npos)
        {
            t.replace(pos, 2, esc_placeholder);
            pos += esc_placeholder.size();
        }
        std::string out;
        out.reserve(t.size());
        for (size_t k = 0; k < t.size(); ++k)
        {
            const char ch = t[k];
            // 宏参数（#1、#2...）不能转义，否则 \def\c#1{...} 会被破坏；
            // \def\<名字>#1 的参数表，以及 \def 定义体内对参数的引用都要保留
            if (ch == '#' && k + 1 < t.size() &&
                std::isdigit(static_cast<unsigned char>(t[k + 1])))
            {
                bool protected_hash = false;
                size_t b = k;
                while (b > 0 && std::isalpha(static_cast<unsigned char>(t[b - 1])))
                    --b;
                if (b >= 5 && t.compare(b - 5, 5, "\\def\\") == 0)
                {
                    protected_hash = true;
                }
                // \def 的参数表（\def\foo#1#2{...}）和定义体内的参数引用：
                // 往回找最近的 \def，若当前 # 位于其参数表或 {body} 内则保留
                if (!protected_hash)
                {
                    for (size_t d = k; d-- > 0;)
                    {
                        if (t.compare(d, 4, "\\def") == 0 &&
                            (d + 4 >= t.size() ||
                             !std::isalpha(static_cast<unsigned char>(t[d + 4]))))
                        {
                            const size_t open = t.find('{', d + 4);
                            if (open == std::string::npos)
                                break;
                            if (k < open)
                            {
                                // 位于 \def\<名字> 与 body 之间的参数表
                                protected_hash = true;
                            }
                            else
                            {
                                int depth = 1;
                                size_t e = open + 1;
                                while (e < t.size() && depth > 0)
                                {
                                    if (t[e] == '{')
                                        ++depth;
                                    else if (t[e] == '}')
                                        --depth;
                                    ++e;
                                }
                                if (depth == 0 && e > k)
                                    protected_hash = true;
                            }
                            break; // 最近的 \def 不包含当前 #，不再往前找
                        }
                    }
                }
                if (protected_hash)
                {
                    out += '#';
                    continue;
                }
            }
            out += (ch == c) ? ("\\" + std::string(1, c)) : std::string(1, ch);
        }
        pos = 0;
        while ((pos = out.find(esc_placeholder)) != std::string::npos)
            out.replace(pos, esc_placeholder.size(), escaped);
        return out;
    };
    s = escape_special(s, '%', "\\%");
    s = escape_special(s, '#', "\\#");

    // 洛谷题面常用 \def\c#1{...} 这类单字母自定义宏，与 LaTeX 内部命令
    // （\c \t \b \s \r 等重音命令）冲突；统一重命名为 \lgoX 前缀。
    // 只在单遍内处理单字母宏，避免 \def\bg 等多字母宏被误改或重复改名
    {
        std::set<char> names;
        for (size_t q = 0; q + 6 <= s.size(); ++q)
        {
            if (s.compare(q, 5, "\\def\\") == 0 &&
                std::isalpha(static_cast<unsigned char>(s[q + 5])))
            {
                const char x = s[q + 5];
                const size_t after = q + 6;
                if (after >= s.size() || !std::isalpha(static_cast<unsigned char>(s[after])))
                    names.insert(x); // 单字母宏
            }
        }

        std::string tmp;
        size_t p = 0;
        while (p < s.size())
        {
            bool matched = false;
            for (char x : names)
            {
                const std::string def = "\\def\\" + std::string(1, x);
                if (s.compare(p, def.size(), def) == 0)
                {
                    const size_t after = p + def.size();
                    if (after >= s.size() || !std::isalpha(static_cast<unsigned char>(s[after])))
                    {
                        tmp += "\\def\\lgo" + std::string(1, x);
                        p = after;
                        matched = true;
                        break;
                    }
                }
                // 用法 \X（后跟 { / 空格 / 标点 等非小写字母，避免误伤 \color 这类长命令）
                if (s[p] == '\\' && p + 1 < s.size() && s[p + 1] == x &&
                    (p + 2 >= s.size() ||
                     !std::islower(static_cast<unsigned char>(s[p + 2]))))
                {
                    tmp += "\\lgo" + std::string(1, x);
                    p += 2;
                    matched = true;
                    break;
                }
            }
            if (!matched)
            {
                tmp += s[p];
                ++p;
            }
        }
        s = std::move(tmp);
    }

    // 控制词后紧跟字母（含 CJK，XeTeX 里都是 catcode 11）时，会被并进
    // 控制词（\qquad第 → 未定义命令 \qquad第；\leN → 未定义命令 \leN）；
    // 按最长已知命令前缀拆开并补空组。放在单字母宏改名之后，
    // 这样 \lgowN 这类改名产物也能被处理
    {
        static const std::set<std::string> kKnownPrefixes = {
            // 关系符（最常被后面直接跟变量名吸收）
            "le", "leq", "ge", "geq", "ne", "neq", "sim", "simeq", "approx",
            "equiv", "propto", "lt", "gt", "times", "div", "pm", "mp",
            "cdot", "ast", "circ", "oplus", "ominus", "otimes", "oslash",
            "cup", "cap", "subset", "supset", "subseteq", "supseteq",
            "in", "notin", "ni", "mid", "nmid", "parallel", "perp", "bot",
            "top", "to", "gets", "mapsto", "rightarrow", "leftarrow",
            "Rightarrow", "Leftarrow", "Leftrightarrow", "iff", "implies",
            "uparrow", "downarrow", "Uparrow", "Downarrow", "updownarrow",
            "dots", "cdots", "ldots", "vdots", "ddots", "quad", "qquad",
            "land", "lor", "wedge", "vee", "lnot", "neg",
            // 常用算子/函数
            "max", "min", "log", "ln", "lg", "gcd", "lcm", "mod", "bmod",
            "pmod", "sum", "prod", "int", "iint", "iiint", "oint", "lim",
            "limsup", "liminf", "sup", "inf", "det", "dim", "exp", "deg",
            "arg", "ker", "hom", "Pr", "rank", "sin", "cos", "tan", "cot",
            "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh",
            "tanh", "coth", "argmax", "argmin",
            // 常见字体/命令（长命令本身也要先放进来，完整匹配时优先）
            "mathrm", "mathbf", "mathit", "mathtt", "mathsf", "mathcal",
            "mathbb", "mathfrak", "mathscr", "boldsymbol", "bm", "text",
            "texttt", "textbf", "textit", "textrm", "textsf",
            "operatorname", "operatornamewithlimits", "textstyle",
            "displaystyle", "scriptstyle", "scriptscriptstyle", "frac",
            "dfrac", "tfrac", "binom", "dbinom", "tbinom", "sqrt",
            "overline", "underline", "overbrace", "underbrace", "widehat",
            "widetilde", "overrightarrow", "overleftarrow", "vec", "bar",
            "hat", "dot", "ddot", "tilde", "check", "acute", "grave",
            "breve", "mathring", "cancel", "bcancel", "xcancel", "sout",
            "not", "xlongequal", "xrightarrow", "xleftarrow", "xmapsto",
            "xleftrightarrow", "raisebox", "hspace", "hfill", "vspace",
            "kern", "mkern", "mskip", "limits", "nolimits",
            "newline",
            "left", "right", "big", "Big", "bigg", "Bigg", "bigl", "bigr",
            "Bigl", "Bigr", "biggl", "biggr", "Biggl", "Biggr",
            "lvert", "rvert", "lVert", "rVert", "langle", "rangle",
            "lfloor", "rfloor", "lceil", "rceil", "lbrace", "rbrace",
            "lgroup", "rgroup", "Vert", "vert", "aleph", "hbar", "ell",
            "imath", "jmath", "Re", "Im", "partial", "nabla", "forall",
            "exists", "nexists", "infty", "emptyset", "varnothing",
            "triangle", "square", "Box", "Diamond", "clubsuit", "diamondsuit",
            "heartsuit", "spadesuit", "checkmark", "dagger", "ddagger",
            "star", "bullet", "degree", "copyright",
            // 希腊字母
            "Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta",
            "Theta", "Iota", "Kappa", "Lambda", "Mu", "Nu", "Xi", "Omicron",
            "Pi", "Rho", "Sigma", "Tau", "Upsilon", "Phi", "Chi", "Psi",
            "Omega", "varTheta", "varSigma", "varPhi", "varOmega",
            "alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta",
            "theta", "iota", "kappa", "lambda", "mu", "nu", "xi", "omicron",
            "pi", "rho", "sigma", "tau", "upsilon", "phi", "chi", "psi",
            "omega", "varepsilon", "vartheta", "varrho", "varsigma",
            "varphi", "digamma", "R", "N", "Z", "Q", "C",
            // 单字母宏改名产物 \lgoX
            "lgoa", "lgob", "lgoc", "lgod", "lgof", "lgog", "lgoh", "lgol",
            "lgom", "lgon", "lgop", "lgoq", "lgos", "lgot", "lgou", "lgov",
            "lgow", "lgox", "lgoy",
        };
        auto is_known = [&](const std::string &w) {
            return kKnownPrefixes.count(w) != 0 ||
                   (w.size() > 3 && w.compare(0, 3, "lgo") == 0);
        };
        // 只有这些“短命令”允许被拆开（\leN → \le{}N）。
        // \textcolor 这类长命令即使含已知前缀也绝不拆，避免破坏命令
        static const std::set<std::string> kSafeSplit = {
            "le", "leq", "ge", "geq", "ne", "neq", "sim", "simeq", "approx",
            "equiv", "propto", "lt", "gt", "times", "div", "pm", "mp",
            "cdot", "ast", "circ", "oplus", "ominus", "otimes", "oslash",
            "cup", "cap", "subset", "supset", "subseteq", "supseteq",
            "in", "notin", "ni", "mid", "nmid", "parallel", "perp", "bot",
            "top", "to", "gets", "mapsto", "rightarrow", "leftarrow",
            "Rightarrow", "Leftarrow", "Leftrightarrow", "iff", "implies",
            "uparrow", "downarrow", "updownarrow", "dots", "cdots", "ldots",
            "vdots", "ddots", "quad", "qquad", "land", "lor", "wedge", "vee",
            "lnot", "neg", "lfloor", "rfloor", "lceil", "rceil", "lbrace",
            "rbrace", "langle", "rangle", "lvert", "rvert", "lVert", "rVert",
            "vert", "Vert", "newline", "max", "min", "log", "ln", "lg", "gcd", "lcm",
            "mod", "bmod", "pmod", "sum", "prod", "int", "iint", "iiint",
            "oint", "lim", "limsup", "liminf", "sup", "inf", "det", "dim",
            "exp", "deg", "arg", "ker", "hom", "Pr", "rank", "sin", "cos",
            "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan",
            "sinh", "cosh", "tanh", "coth", "argmax", "argmin",
        };
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s[p] == '\\' && p + 1 < s.size() &&
                std::isalpha(static_cast<unsigned char>(s[p + 1])))
            {
                size_t w = p + 1;
                while (w < s.size() &&
                       std::isalpha(static_cast<unsigned char>(s[w])))
                    ++w;
                const std::string word = s.substr(p + 1, w - p - 1);
                // 整词不是已知命令时（\leN、\qquad第），按最长已知前缀拆开，
                // 在命令后补空组，避免后续字母被并入命令名；
                // 整词是已知命令（\operatornamewithlimits、\frac12 等）则不动
                if (!is_known(word))
                {
                    size_t best = std::string::npos;
                    for (size_t len = 1; len < word.size(); ++len)
                        if (is_known(word.substr(0, len)))
                            best = len;
                    if (best != std::string::npos &&
                        kSafeSplit.count(word.substr(0, best)))
                    {
                        t += "\\" + word.substr(0, best) + "{}" +
                             word.substr(best);
                        p = w;
                        continue;
                    }
                }
                // CJK 等非 ASCII 字符不是 isalpha，单词扫描会停在它前面；
                // 若上面没能拆开（如 \qquad第），在整词后补空组
                if (w < s.size() &&
                    static_cast<unsigned char>(s[w]) >= 0x80)
                {
                    t += s.substr(p, w - p);
                    t += "{}";
                    p = w;
                    continue;
                }
                t += s.substr(p, w - p);
                p = w;
                continue;
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // \def\or{...} 会重定义 LaTeX 数组前导里的内部命令 \or，
    // 导致 "in array arg"；统一改名为 \lgooor
    {
        std::string t;
        size_t p = 0;
        while (p < s.size())
        {
            if (s.compare(p, 3, "\\or") == 0 &&
                (p + 3 >= s.size() ||
                 !std::isalpha(static_cast<unsigned char>(s[p + 3]))))
            {
                t += "\\lgooor";
                p += 3;
                continue;
            }
            t += s[p];
            ++p;
        }
        s = std::move(t);
    }

    // 洛谷题面常见的 $^$（表示“二进制异或”）没有底数，LaTeX 编译报错；
    // 裸上/下标（$^1$、$^*$ 等脚注标记）补空底数 ${}^1$；
    // 末尾悬空的 ^ / _（如“……则省略 ^”）直接输出 \wedge。
    // 放在最后处理：前面 \texttt{...}\\ 等转换会改变 ^ / _ 的相邻字符
    {
        static const std::regex kBareCaret(R"(\$[\^_]\$)");
        s = std::regex_replace(s, kBareCaret, "$\\wedge$");

        static const std::regex kNoBaseCaret(R"((?:^|\$)[\^_])");
        s = regex_transform(s, kNoBaseCaret, [&](const std::smatch &m) {
            const std::string pre = m[0].str();
            return pre.substr(0, pre.size() - 1) + "{}" + pre.back();
        });

        static const std::regex kTrailingCaret(R"((^|[^\\])[\^_](?=\s*\$?\s*$))");
        s = regex_transform(s, kTrailingCaret, [&](const std::smatch &m) {
            return m[1].str() + "\\wedge";
        });
    }

    // 还原 \verb 内容（此时所有转换已完成，按文本模式转义即可）
    for (size_t vi = 0; vi < verb_raws.size(); ++vi)
    {
        std::string esc;
        for (char c : verb_raws[vi])
        {
            switch (c)
            {
            case '\\': esc += "\\textbackslash{}"; break;
            case '{': esc += "\\{"; break;
            case '}': esc += "\\}"; break;
            case '_': esc += "\\_"; break;
            case '#': esc += "\\#"; break;
            case '%': esc += "\\%"; break;
            case '&': esc += "\\&"; break;
            case '~': esc += "\\textasciitilde{}"; break;
            case '^': esc += "\\textasciicircum{}"; break;
            case '$': esc += "\\$"; break;
            default: esc += c;
            }
        }
        const std::string ph = verb_placeholder(vi);
        size_t pos = 0;
        while ((pos = s.find(ph, pos)) != std::string::npos)
        {
            s.replace(pos, ph.size(), esc);
            pos += esc.size();
        }
    }
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

// 视频链接判断：洛谷用“图片语法”插入 Bilibili 视频，或常见视频文件后缀
bool is_video_url(const std::string &url)
{
    // 洛谷的 Bilibili 视频专用语法：![](bilibili:BVxxx?page=N)
    if (url.rfind("bilibili:", 0) == 0)
        return true;
    if (url.find("bilibili.com/video/") != std::string::npos ||
        url.find("player.bilibili.com") != std::string::npos)
        return true;

    std::string path = url;
    const size_t q = path.find_first_of("?#");
    if (q != std::string::npos)
        path.resize(q);
    const std::string lower = to_lower_ascii(path);
    static const char *kExts[] = {".mp4", ".webm", ".ogv", ".m4v", ".mov", ".m3u8"};
    for (const char *e : kExts)
    {
        const size_t n = std::strlen(e);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, e) == 0)
            return true;
    }
    return false;
}

// 是否看起来像真正的链接目标（防止题面里 [](一段文字) 这种写法被当成链接）
bool looks_like_url(const std::string &url)
{
    return url.rfind("http://", 0) == 0 ||
           url.rfind("https://", 0) == 0 ||
           url.rfind("mailto:", 0) == 0 ||
           url.rfind("bilibili:", 0) == 0 ||
           url.find("://") != std::string::npos;
}

// data:image/...;base64,... 这类内嵌数据 URI：xelatex 无法使用，
// 而且 base64 内容是一整串无空格文本，会让 TeX 段落排版出问题
// （甚至段错误/卡死），一律跳过
bool is_data_uri(const std::string &url)
{
    return url.rfind("data:", 0) == 0;
}

// 按文件头魔数识别缓存图片的真实格式
enum class ImageKind
{
    kPng,
    kJpeg,
    kGif,
    kWebp,
    kBmp,
    kSvg,
    kIco,
    kPdf,
    kEps,
    kUnknown,
};

ImageKind detect_image_kind(const std::filesystem::path &path)
{
    FILE *in = std::fopen(path.c_str(), "rb");
    if (!in)
        return ImageKind::kUnknown;
    unsigned char head[16] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), in);
    std::fclose(in);

    if (n >= 8 && std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0)
        return ImageKind::kPng;
    if (n >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF)
        return ImageKind::kJpeg;
    if (n >= 6 && std::memcmp(head, "GIF8", 4) == 0)
        return ImageKind::kGif;
    if (n >= 12 && std::memcmp(head, "RIFF", 4) == 0 &&
        std::memcmp(head + 8, "WEBP", 4) == 0)
        return ImageKind::kWebp;
    if (n >= 2 && head[0] == 'B' && head[1] == 'M')
        return ImageKind::kBmp;
    if (n >= 4 && (std::memcmp(head, "<svg", 4) == 0 ||
                   std::memcmp(head, "<?xm", 4) == 0))
        return ImageKind::kSvg;
    if (n >= 4 && head[0] == 0x00 && head[1] == 0x00 &&
        head[2] == 0x01 && head[3] == 0x00)
        return ImageKind::kIco;
    if (n >= 5 && std::memcmp(head, "%PDF-", 5) == 0)
        return ImageKind::kPdf;
    if (n >= 2 && head[0] == '%' && head[1] == '!')
        return ImageKind::kEps;
    return ImageKind::kUnknown;
}

// 修正 JPEG 的 JFIF 像素密度。洛谷个别老图密度被写成 1 dpi，
// XeTeX 会把 405×256px 的图片按 405×256 英寸排版，超过 TeX 的
// 19 英尺上限而报 "Dimension too large"。密度明显异常（< 72 dpi）
// 时就地改写为 72 dpi（幂等，可重复执行）。
// 返回 true 表示无需修正或已修正；若需要修正但缓存不可写则返回 false。
bool fix_jpeg_density(const std::filesystem::path &path)
{
    FILE *in = std::fopen(path.c_str(), "r+b");
    if (!in)
        return false;

    unsigned char head[24] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), in);
    const bool is_jfif = n >= 18 && head[0] == 0xFF && head[1] == 0xD8 &&
                         head[2] == 0xFF && head[3] == 0xE0 &&
                         std::memcmp(head + 6, "JFIF", 4) == 0 && head[10] == 0;
    if (is_jfif)
    {
        const unsigned units = head[13];
        const unsigned xd = (head[14] << 8) | head[15];
        const unsigned yd = (head[16] << 8) | head[17];
        if (units == 1 && (xd < 72 || yd < 72))
        {
            const unsigned char k72[] = {1, 0, 72, 0, 72};
            std::fseek(in, 13, SEEK_SET);
            const bool wrote = std::fwrite(k72, 1, sizeof(k72), in) == sizeof(k72);
            std::fclose(in);
            return wrote;
        }
    }
    std::fclose(in);
    return true;
}

// 让缓存图片能被 xelatex 正常加载：
// - GIF/WebP/BMP/SVG/ICO 以及无法识别的内容返回空路径（调用方跳过该图）；
// - JPEG 密度异常时先就地修正；
// - PNG/JPEG/PDF/EPS 内容若文件名没有可识别的扩展名（如无扩展名、或
//   扩展名其实是 URL 的残余），在缓存目录生成带正确扩展名的副本。
std::filesystem::path prepare_cached_image(const std::filesystem::path &cache_path)
{
    if (!std::filesystem::exists(cache_path))
        return {};

    const ImageKind kind = detect_image_kind(cache_path);
    switch (kind)
    {
        case ImageKind::kPng:
        case ImageKind::kJpeg:
        case ImageKind::kPdf:
        case ImageKind::kEps:
            break;
        default:
            return {}; // xelatex 无法加载的格式，直接跳过
    }

    if (kind == ImageKind::kJpeg && !fix_jpeg_density(cache_path))
        return {}; // 需要修正密度但缓存不可写，跳过避免编译报错

    const char *want_ext = nullptr;
    switch (kind)
    {
        case ImageKind::kPng: want_ext = ".png"; break;
        case ImageKind::kJpeg: want_ext = ".jpg"; break;
        case ImageKind::kPdf: want_ext = ".pdf"; break;
        case ImageKind::kEps: want_ext = ".eps"; break;
        default: break;
    }

    // XeTeX 按文件内容解码，扩展名只用于选择解析器；
    // 已带可识别扩展名（无论内容是否匹配，如 .png 里的 JPEG）可直接使用
    const std::string name = to_lower_ascii(cache_path.filename().string());
    static const char *kRecognized[] = {".png", ".jpg", ".jpeg", ".pdf", ".eps"};
    for (const char *e : kRecognized)
    {
        const size_t m = std::strlen(e);
        if (name.size() >= m && name.compare(name.size() - m, m, e) == 0)
            return cache_path;
    }

    // 生成带正确扩展名的副本，避免与已有缓存文件冲突
    for (int i = 0; i < 128; ++i)
    {
        std::filesystem::path copy = cache_path;
        if (i == 0)
            copy += want_ext;
        else
            copy += "_" + std::to_string(i) + want_ext;
        if (!std::filesystem::exists(copy))
        {
            std::error_code ec;
            std::filesystem::copy_file(cache_path, copy,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            return ec ? std::filesystem::path() : copy;
        }
    }
    return {};
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

std::string inline_to_latex(const std::string &text);
std::string inline_to_latex_impl(const std::string &text, std::vector<std::string> &raws,
                                 std::vector<bool> &is_math);

// 占位符：\x01R<n>\x02（注意用字符串拼接构造，避免 \x01R 被当作十六进制转义）
std::string placeholder(size_t index)
{
    return std::string(1, '\x01') + "R" + std::to_string(index) + std::string(1, '\x02');
}

// 恢复 \x01R<n>\x02 占位符
std::string restore_placeholders(const std::string &s, const std::vector<std::string> &raws)
{
    std::string out;
    size_t i = 0;
    while (i < s.size())
    {
        if (s[i] == '\x01' && i + 1 < s.size() && s[i + 1] == 'R')
        {
            size_t j = i + 2;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
                ++j;
            if (j < s.size() && s[j] == '\x02')
            {
                const int idx = std::stoi(s.substr(i + 2, j - i - 2));
                if (idx >= 0 && idx < static_cast<int>(raws.size()))
                    out += raws[static_cast<size_t>(idx)];
                i = j + 1;
                continue;
            }
        }
        out += s[i];
        ++i;
    }
    return out;
}

// 合并相邻的数学占位符：洛谷题面里 \$$ 等畸形写法会把一个公式拆成多段
// （段间可能夹着多余的 $ 和空白），这里把它们拼成一个，避免输出残缺公式
void merge_adjacent_math(std::string &s,
                         std::vector<std::string> &raws,
                         const std::vector<bool> &is_math)
{
    auto parse_placeholder = [&s](size_t p, size_t &idx, size_t &end) -> bool {
        if (p + 2 >= s.size() || s[p] != '\x01' || s[p + 1] != 'R')
            return false;
        size_t j = p + 2;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
            ++j;
        if (j >= s.size() || s[j] != '\x02')
            return false;
        idx = static_cast<size_t>(std::stoul(s.substr(p + 2, j - p - 2)));
        end = j + 1;
        return true;
    };

    std::string out;
    size_t i = 0;
    while (i < s.size())
    {
        size_t idx = 0, end = 0;
        if (!parse_placeholder(i, idx, end) || idx >= is_math.size() || !is_math[idx])
        {
            out += s[i];
            ++i;
            continue;
        }

        // 块级公式 $$...$$ 不参与合并
        if (raws[idx].rfind("$$", 0) == 0)
        {
            out += s.substr(i, end - i);
            i = end;
            continue;
        }
        // 收集相邻的数学片段并合并：仅当间隙里含多余的 $ 才合并
        // （纯空白间隔的两个公式是独立的；如 $$...$$ 后跟 $...$ 不应合并）
        std::string merged = raws[idx];
        size_t run_end = end;
        while (run_end < s.size())
        {
            size_t gap_start = run_end;
            size_t gap_end = gap_start;
            bool gap_has_dollar = false;
            while (gap_end < s.size() &&
                   (s[gap_end] == '$' || std::isspace(static_cast<unsigned char>(s[gap_end]))))
            {
                if (s[gap_end] == '$')
                    gap_has_dollar = true;
                ++gap_end;
            }
            // 严格相邻（空间隙）也合并；只有非空且无 $ 的间隙（如 "$$...$$ 后跟 $...$"）才断开
            if (!gap_has_dollar && gap_end > gap_start)
                break;
            size_t nidx = 0, nend = 0;
            if (gap_end >= s.size() || !parse_placeholder(gap_end, nidx, nend) ||
                nidx >= is_math.size() || !is_math[nidx])
                break;
            if (raws[nidx].rfind("$$", 0) == 0)
                break;
            if (!merged.empty() && merged.back() == '$')
                merged.pop_back();
            for (size_t k = gap_start; k < gap_end; ++k)
                if (s[k] != '$')
                    merged += s[k]; // 保留空白，去掉多余的 $
            if (!raws[nidx].empty() && raws[nidx].front() == '$')
                merged += raws[nidx].substr(1);
            else
                merged += raws[nidx];
            run_end = nend;
        }
        raws.push_back(std::move(merged));
        out += "\x01R" + std::to_string(raws.size() - 1) + "\x02";
        i = run_end;
    }
    s = std::move(out);
}

std::string inline_to_latex_impl(const std::string &text, std::vector<std::string> &raws,
                                 std::vector<bool> &is_math)
{
    auto protect = [&](std::string latex) {
        raws.push_back(std::move(latex));
        is_math.push_back(false);
        return placeholder(raws.size() - 1);
    };

    std::string s = text;

    // 1. 把 \$（转义美元符）保护起来，避免数学提取时把它的 $ 当成公式分隔符
    {
        const std::string dollar_sentinel = "\x01D\x02";
        std::string t;
        size_t p = 0, last = 0;
        while ((p = s.find("\\$", p)) != std::string::npos)
        {
            // \\$ 是“行分隔符 + 公式结束符”，不是转义美元符
            if (p > 0 && s[p - 1] == '\\')
            {
                p += 2;
                continue;
            }
            t += s.substr(last, p - last) + dollar_sentinel;
            p += 2;
            last = p;
        }
        t += s.substr(last);
        s = std::move(t);
    }
    // 2. 数学公式（原样保留）
    {
        static const std::regex re("(\\$\\$[^$]+\\$\\$|\\$[^$]+\\$)");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            raws.push_back(sanitize_math(m[1].str()));
            is_math.push_back(true);
            return placeholder(raws.size() - 1);
        });
    }
    // 2.5 合并相邻的数学片段（源数据畸形时一个公式可能被拆成多段）
    merge_adjacent_math(s, raws, is_math);
    // 3. 行内代码（放在数学之后：数学里的反引号是字面量，不应被当成代码分隔符）
    {
        // 支持 1~N 个反引号包裹的代码段（CommonMark 规则），
        // 避免 ``code`` 这类写法留下多余反引号
        static const std::regex re("`+([^`]+?)`+");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\texttt{" + escape_latex(m[1].str()) + "}");
        });
    }
    // 4. 图片包在链接里：[![](img)](url) → \href{url}{图片}；
    //     必须先于普通链接处理，否则内层 ] 会破坏链接解析
    {
        static const std::regex re(
            R"(\[!\[[^\]]*\]\s*\(\s*([^\s)]+)(?:\s+["'][^"']*["'])?\s*\)\s*\]\s*\(\s*([^\s)]+)(?:\s+["'][^"']*["'])?\s*\))");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            const std::string img_url = m[1].str();
            const std::string link_url = m[2].str();
            if (is_data_uri(img_url) || is_data_uri(link_url))
                return std::string(); // data URI 直接丢弃
            if (!looks_like_url(link_url) || !looks_like_url(img_url))
                return m[0].str();
            if (is_video_url(link_url) || is_video_url(img_url))
                return protect("\\url{" + link_url + "}");
            // 按缓存文件真实内容判断能否加载，扩展名与内容不符的图片
            // 先经 prepare_cached_image 归一化（修正密度/补扩展名）
            const std::filesystem::path usable =
                prepare_cached_image(crawler::image_cache_path(img_url));
            if (usable.empty())
                return std::string(); // xelatex 无法加载的格式，直接跳过
            const std::string path = escape_path(usable.string());
            return protect("\\href{" + escape_latex(link_url) + "}{"
                           "\\IfFileExists{" + path + "}{"
                           "{\\setbox0=\\hbox{\\includegraphics{" + path + "}}"
                           "\\ifdim\\wd0>\\linewidth"
                           "\\includegraphics[width=\\linewidth]{" + path + "}"
                           "\\else\\includegraphics{" + path + "}\\fi}}"
                           "{\\mbox{}}}");
        });
    }
    // 4.5 图片 → 缓存文件；视频 → 仅链接
    {
        static const std::regex re("!\\[[^\\]]*\\]\\s*\\(\\s*([^\\s)]+)(?:\\s+[\"'][^\"']*[\"'])?\\s*\\)");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            const std::string url = m[1].str();
            if (is_data_uri(url))
                return std::string(); // data URI 直接丢弃
            if (!looks_like_url(url))
                return m[0].str(); // 不是真正的图片链接，保留原文（转义阶段处理）
            if (is_video_url(url))
                return protect("\\url{" + url + "}");
            // 按缓存文件真实内容判断能否加载：GIF/WebP/SVG/BMP/ICO 等
            // xelatex 无法加载的格式直接跳过；扩展名与内容不符的图片
            // 先经 prepare_cached_image 归一化（修正密度/补扩展名）
            const std::filesystem::path usable =
                prepare_cached_image(crawler::image_cache_path(url));
            if (usable.empty())
                return std::string();
            const std::string path = escape_path(usable.string());
            return protect("\\IfFileExists{" + path + "}{"
                           "{\\setbox0=\\hbox{\\includegraphics{" + path + "}}"
                           "\\ifdim\\wd0>\\linewidth"
                           "\\includegraphics[width=\\linewidth]{" + path + "}"
                           "\\else\\includegraphics{" + path + "}\\fi}}"
                           "{\\mbox{}}");
        });
    }
    // 5. 链接
    {
        static const std::regex re("\\[([^\\]]*)\\]\\s*\\(\\s*([^\\s)]+)(?:\\s+[\"'][^\"']*[\"'])?\\s*\\)");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            if (is_data_uri(m[2].str()))
                return m[1].str(); // data URI 链接：只保留链接文字
            if (!looks_like_url(m[2].str()))
                return m[0].str(); // 不是真正的链接目标，保留原文（转义阶段处理）
            return protect("\\href{" + escape_latex(m[2].str()) + "}{" +
                           inline_to_latex_impl(m[1].str(), raws, is_math) + "}");
        });
    }
    // 6. 自动链接 <https://...>
    {
        static const std::regex re("<(https?://[^>]+)>");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\url{" + m[1].str() + "}");
        });
    }
    // 7. 粗体
    {
        static const std::regex re("\\*\\*([^*]+)\\*\\*");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\textbf{" + inline_to_latex_impl(m[1].str(), raws, is_math) + "}");
        });
    }
    // 8. 斜体（下划线形式的斜体不转换，避免误伤标识符中的 _）
    {
        static const std::regex re("\\*([^*]+)\\*");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\textit{" + inline_to_latex_impl(m[1].str(), raws, is_math) + "}");
        });
    }
    // 9. 删除线
    {
        static const std::regex re("~~([^~]+)~~");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\sout{" + inline_to_latex_impl(m[1].str(), raws, is_math) + "}");
        });
    }
    // 10. 转义剩余特殊字符
    s = escape_latex(s);
    // 11. 恢复占位符。链接/粗体等递归生成的片段内部可能还嵌着占位符，
    //     需要迭代还原直到不再出现 \x01
    std::string result = restore_placeholders(s, raws);
    while (result.find('\x01') != std::string::npos)
        result = restore_placeholders(result, raws);
    // 还原被保护的 \$（转义美元符）
    {
        const std::string dollar_sentinel = "\x01D\x02";
        size_t p = 0;
        while ((p = result.find(dollar_sentinel, p)) != std::string::npos)
        {
            result.replace(p, dollar_sentinel.size(), "\\$");
            p += 2;
        }
    }
    return result;
}

std::string inline_to_latex(const std::string &text)
{
    // 占位符池只建一次；粗体/链接等递归调用共用同一池，
    // 否则嵌套占位符在递归中无法还原会死循环
    std::vector<std::string> raws;
    std::vector<bool> is_math;
    return inline_to_latex_impl(text, raws, is_math);
}

// 是否以某种“块级”语法开头（用于结束普通段落）
bool is_block_start(const std::string &t)
{
    if (t.empty())
        return false;
    if (t[0] == '#' || t[0] == '>' || t[0] == '|')
        return true;
    if (t[0] == '`' || t[0] == '~')
        return true;
    if (t.rfind("$$", 0) == 0 || t.rfind("::", 0) == 0)
        return true;
    static const std::regex kHr(R"(^([-*_])(\s*\1){2,}\s*$)");
    static const std::regex kItem(R"(^[-+*]\s+|\d+\.\s+)");
    return std::regex_match(t, kHr) || std::regex_search(t, kItem);
}

// 是否为表格分隔行（|:---|:---:| 或 :-:|:-: 等，单元格只能由 - 和 : 组成）
bool is_table_separator_row(const std::string &line)
{
    std::string r = trim(line);
    if (r.empty() || r.find('|') == std::string::npos)
        return false; // 必须有 |，避免把分隔线 --- 误判成单列表格分隔行
    if (r.front() == '|')
        r.erase(r.begin());
    if (!r.empty() && r.back() == '|')
        r.pop_back();
    if (r.empty())
        return false;

    std::string cell;
    auto cell_ok = [](const std::string &c) {
        return !c.empty() &&
               std::all_of(c.begin(), c.end(), [](char x) { return x == '-' || x == ':'; });
    };
    for (char c : r)
    {
        if (c == '|')
        {
            if (!cell_ok(trim(cell)))
                return false;
            cell.clear();
        }
        else
        {
            cell += c;
        }
    }
    return cell_ok(trim(cell));
}

// 一行是否可能是表格行（包含 |）
bool is_table_row(const std::string &line)
{
    return trim(line).find('|') != std::string::npos;
}

// 把多个行内片段拼成一个段落：硬换行用 \\\\，丢弃转换后为空的片段
// （缺失图片会变成空），\\\\ 后紧跟 [ 时补 {} 防止被当作可选参数
std::string join_inline_parts(const std::vector<std::pair<std::string, bool>> &parts)
{
    std::vector<std::pair<std::string, bool>> out;
    for (const auto &rp : parts)
    {
        const std::string c = inline_to_latex(rp.first);
        if (!c.empty())
            out.emplace_back(c, rp.second);
    }
    std::string para;
    for (size_t k = 0; k < out.size(); ++k)
    {
        if (k)
        {
            // 硬换行前补 {}：前一段可能是缺失图片（编译期为空），
            // 没有 {} 的话 \\ 前无内容会报 "There's no line here to end"
            para += out[k - 1].second ? " {}\\\\ " : " ";
            if (out[k].first[0] == '[')
                para += "{}";
        }
        para += out[k].first;
    }
    return para;
}

// 一行文本是否有未闭合的括号/方括号（链接可能跨行：
// [![](img)]( 换行 url)，需要把下一行并入同一片段才能被链接正则匹配）
bool has_unclosed_paren_or_bracket(const std::string &s)
{
    int paren = 0;
    int brack = 0;
    for (char c : s)
    {
        if (c == '(')
            ++paren;
        else if (c == ')')
        {
            if (paren > 0)
                --paren;
        }
        else if (c == '[')
            ++brack;
        else if (c == ']')
        {
            if (brack > 0)
                --brack;
        }
    }
    return paren > 0 || brack > 0;
}

// 表格：rows[0] 表头，rows[1] 对齐行，其余为内容行
void emit_table(const std::vector<std::string> &rows, std::string &out)
{
    auto split_cells = [](const std::string &row) {
        std::string r = trim(row);
        if (!r.empty() && r.front() == '|')
            r.erase(r.begin());
        if (!r.empty() && r.back() == '|')
            r.pop_back();
        std::vector<std::string> cells;
        std::string cur;
        for (char c : r)
        {
            if (c == '|')
            {
                cells.push_back(cur);
                cur.clear();
            }
            else
            {
                cur += c;
            }
        }
        cells.push_back(cur);
        // 去掉末尾的空单元格（源数据里常见 "||" 多出的空列）
        while (!cells.empty() && trim(cells.back()).empty())
            cells.pop_back();
        return cells;
    };

    const std::vector<std::string> align_row = split_cells(rows[1]);
    std::string spec = "|";
    for (const auto &a : align_row)
    {
        const std::string t = trim(a);
        if (t.size() >= 3 && t.front() == ':' && t.back() == ':')
            spec += "c|";
        else if (!t.empty() && t.front() == ':')
            spec += "l|";
        else if (!t.empty() && t.back() == ':')
            spec += "r|";
        else
            spec += "l|";
    }

    auto emit_row = [&](const std::vector<std::string> &cells) {
        for (size_t k = 0; k < cells.size(); ++k)
        {
            if (k)
                out += " & ";
            const std::string cell = trim(cells[k]);
            if (cell == "^") // 表格合并标记，暂时忽略
                continue;
            out += inline_to_latex(cell);
        }
        out += " \\\\\n\\hline\n";
    };

    out += "\\begin{tabular}{" + spec + "}\n\\hline\n";
    const size_t col_count = align_row.size();
    auto bounded = [&](const std::vector<std::string> &cells) {
        std::vector<std::string> v = cells;
        if (v.size() > col_count)
            v.resize(col_count);
        return v;
    };
    emit_row(bounded(split_cells(rows[0])));
    for (size_t r = 2; r < rows.size(); ++r)
        emit_row(bounded(split_cells(rows[r])));
    out += "\\end{tabular}\n\n";
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
    tag = to_lower_ascii(trim(tag));
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
                            size_t limit = 2500,
                            size_t chunk = 1000)
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

} // namespace

std::string latex::markdown_to_latex(const std::string &markdown)
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
            out += "\\[\n" + sanitize_math(join_strings(ml, "\n")) + "\n\\]\n\n";
            continue;
        }

        // ---- Luogu 扩展块：cute-table / align / epigraph / info 等 ----
        if (line.rfind("::cute-table", 0) == 0)
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
                    out += "\\" + std::string(kCmds[n - 1]) + "{" + title + "}\n\n";
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
            using Part = std::pair<std::string, bool>;
            std::vector<std::vector<Part>> groups; // 空行分段
            std::vector<Part> cur;
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
                    groups.push_back({{"\\textbf{" + inline_to_latex(trim(body.substr(n))) + "}", false}});
                }
                else
                {
                    if (!cur.empty() &&
                        has_unclosed_paren_or_bracket(cur.back().first))
                    {
                        // 上一行链接未闭合（如 [![](img)]( 换行 url），并入同一片段
                        cur.back().first += " " + body;
                        cur.back().second = cur.back().second || hard;
                    }
                    else
                    {
                        cur.emplace_back(body, hard);
                    }
                }
                ++i;
            }
            if (!cur.empty())
                groups.push_back(cur);

            out += "\\begin{quote}\n";
            for (const auto &g : groups)
                out += join_inline_parts(g) + "\n\n";
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
        std::vector<std::pair<std::string, bool>> parts; // (文本, 是否硬换行)
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
                has_unclosed_paren_or_bracket(parts.back().first))
            {
                // 上一行链接未闭合（如 [![](img)]( 换行 url），并入同一片段
                parts.back().first += " " + body;
                parts.back().second = parts.back().second || hard;
            }
            else
            {
                parts.emplace_back(body, hard);
            }
            ++collected;
            ++i;
        }
        if (!parts.empty())
        {
            const std::string para = join_inline_parts(parts);
            if (!para.empty())
                out += para + "\n\n";
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

std::string latex::problem_to_latex(const problem::Problem &p, const Options &opt)
{
    const bool use_en = (opt.lang == "en");

    auto field = [&](const char *key, const std::string &zh) -> std::string {
        if (!use_en)
            return zh;
        const std::string en = safe_string(p.translations, key);
        return en.empty() ? zh : en;
    };

    std::string out;
    out += "\\section{" + inline_to_latex(p.pid + " " + field("title", p.name)) + "}\n\n";

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
    if(!tagsfrom.empty()) out += join_strings(tagsfrom, " \\ ") + " \\ ";
    if(!tagsdata.empty()) out += join_strings(tagsdata, " \\ ") + " \\ ";
    if(!tagsarea.empty()) out += join_strings(tagsarea, " \\ ") + " \\ ";
    if(!tagsspec.empty()) out += join_strings(tagsspec, " \\ ") + " \\ ";

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

std::string latex::article_to_latex(const article::Article &a)
{
    std::string out;
    out += "\\section{" + inline_to_latex(a.title) + "}\n\n";
    if (!a.author_name.empty())
        out += "作者：" + a.author_name + " \\\\\n\n";
    out += markdown_to_latex(a.content) + "\n";
    return out;
}

bool latex::export_latex(const luogu::ExportFilter &filter,
                         const std::filesystem::path &output_path,
                         std::string &error)
{
    error.clear();

    // 筛选（-M / -L 共用），结果已按题号排序
    std::vector<problem::Problem> problems;
    std::vector<std::string> resolved_tags;
    if (!luogu::select_problems(filter, problems, &resolved_tags, error))
        return false;

    // 检查图片是否都已下载到缓存；缺失时在终端用英文询问是否下载
    std::vector<std::string> missing;
    for (const auto &p : problems)
    {
        for (const auto &url : p.image_urls())
        {
            // 视频等非图片链接不算“未下载的图片”
            if (!looks_like_url(url) || is_video_url(url))
                continue;
            if (!std::filesystem::exists(crawler::image_cache_path(url)))
                missing.push_back(url);
        }
    }
    if (!missing.empty())
    {
        std::printf("%zu image(s) referenced by the selected problems are not downloaded yet.\nDownload them now? [y/N] ", missing.size());
        fflush(stdout);
        char answer_buf[16];
        if (!fgets(answer_buf, sizeof(answer_buf), stdin))
            answer_buf[0] = '\0';
        std::string answer(answer_buf);
        if (!answer.empty() && (answer[0] == 'y' || answer[0] == 'Y'))
        {
            crawler::download_images(missing);
        }
        else
        {
            std::printf("Skipped. Missing images will be skipped during compilation (\\IfFileExists).\n");
        }
    }

    Options opt;
    opt.lang = filter.lang;
    // opt.show 保持默认 "00"：-L 不再支持 --show，默认不显示难度和标签

    FILE *out = std::fopen(output_path.c_str(), "w");
    if (!out)
    {
        error = "无法打开输出文件 '" + output_path.string() + "'";
        return false;
    }

    std::fputs("\\documentclass{book}\n", out);
    std::fputs("\\usepackage[UTF8]{ctex}\n", out);
    std::fputs("\\usepackage{graphicx}\n", out);
    std::fputs("\\usepackage{titlesec}\n", out);
    std::fputs("\\usepackage{fancyhdr}\n", out);
    std::fputs("\\usepackage[hidelinks]{hyperref}\n", out);
    std::fputs("\\usepackage[normalem]{ulem}\n", out);
    std::fputs("\\usepackage{amsmath,amssymb}\n", out);
    std::fputs("\\usepackage{mathtools}\n", out);
    std::fputs("\\usepackage{bm}\n", out);
    std::fputs("\\usepackage{mathrsfs}\n", out);
    std::fputs("\\usepackage{xcolor}\n", out);
    std::fputs("\\usepackage{listings}\n", out);
    std::fputs("\\usepackage{cancel}\n", out);
    std::fputs("\\usepackage{geometry}\n", out);
    std::fputs("\\usepackage{tabularx}\n", out);
    std::fputs("\\geometry{margin=2cm}\n", out);
        // 去掉所有章节序号（\section 等）：目录和正文都不显示数字
    std::fputs("\\setcounter{secnumdepth}{-1}\n", out);

    std::fputs("\\pagestyle{fancy}\n", out);
    std::fputs("\\fancyhf{}\n", out);
    std::fputs("\\fancyhead[LE]{\\thepage}\n", out);
    std::fputs("\\fancyhead[RE]{\\nouppercase{\\normalfont \\rightmark}}\n", out);
    std::fputs("\\fancyhead[LO]{\\nouppercase{\\normalfont \\rightmark}}\n", out);
    std::fputs("\\fancyhead[RO]{\\thepage}\n", out);

    std::fputs("\\titleformat{\\section}\n", out);
    std::fputs("{\\ttfamily\\Large}\n", out);
    std::fputs("{}\n", out);
    std::fputs("{0em}{}\n", out);
    std::fputs("\\titleformat{\\subsection}\n", out);
    std::fputs("{\\ttfamily\\large}\n", out);
    std::fputs("{}\n", out);
    std::fputs("{0em}{}\n", out);
    std::fputs("\\titleformat{\\subsubsection}\n", out);
    std::fputs("{\\ttfamily\\color{gray}}\n", out);
    std::fputs("{}\n", out);
    std::fputs("{1em}{}\n", out);

    std::fputs("\\lstset{\n", out);
    std::fputs("    breaklines=true,\n", out);
    std::fputs("    breakatwhitespace=false,\n", out);
    std::fputs("    keepspaces=true,\n", out);
    std::fputs("    showstringspaces=false,\n", out);
    std::fputs("    tabsize=4,\n", out);
    std::fputs("    keywordstyle=\\color{blue}\\bfseries,\n", out);
    std::fputs("    commentstyle=\\color{green!50!black},\n", out);
    std::fputs("    stringstyle=\\color{red!60!black},\n", out);
    std::fputs("    frame=single,\n", out);
    std::fputs("    columns=flexible,\n", out);
    std::fputs("    numbers=left,\n", out);
    std::fputs("    numberstyle=\\footnotesize\\ttfamily\\color{gray},\n", out);
    std::fputs("    basicstyle=\\small\\ttfamily,\n", out);
    std::fputs("    rulecolor=\\color{blue},\n", out);
    std::fputs("}\n", out);

        // 当前 TeX Live 的 listings 没有这些语言，手动补上，否则
        // \begin{lstlisting}[language=Rust] 会报 "Couldn't load requested language"
    std::fputs("\\lstdefinelanguage{Rust}{\n", out);
    std::fputs("    morekeywords={as,async,await,break,const,continue,crate,dyn,else,enum,", out);
    std::fputs("extern,false,fn,for,if,impl,in,let,loop,match,mod,move,mut,pub,ref,return,", out);
    std::fputs("self,Self,static,struct,super,trait,true,type,unsafe,use,where,while,yield},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{JavaScript}{\n", out);
    std::fputs("  morekeywords={abstract,arguments,await,boolean,break,byte,case,catch,char,", out);
    std::fputs("class,const,continue,debugger,default,delete,do,double,else,enum,eval,export,", out);
    std::fputs("extends,false,final,finally,float,for,function,goto,if,implements,import,in,", out);
    std::fputs("instanceof,int,interface,let,long,native,new,null,package,private,protected,", out);
    std::fputs("public,return,short,static,super,switch,synchronized,this,throw,throws,", out);
    std::fputs("transient,true,try,typeof,var,void,volatile,while,with,yield},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("  morestring=[b]`,\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{TypeScript}{\n", out);
    std::fputs("  morekeywords={abstract,any,as,asserts,async,await,boolean,break,case,catch,", out);
    std::fputs("class,const,continue,debugger,declare,default,delete,do,else,enum,export,", out);
    std::fputs("extends,false,finally,for,from,function,get,if,implements,import,in,infer,", out);
    std::fputs("instanceof,interface,is,keyof,let,module,namespace,never,new,null,number,", out);
    std::fputs("object,of,package,private,protected,public,readonly,return,set,static,string,", out);
    std::fputs("super,switch,symbol,this,throw,true,try,type,typeof,undefined,unknown,var,", out);
    std::fputs("void,while,with,yield},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("  morestring=[b]`,\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{CSharp}{\n", out);
    std::fputs("  morekeywords={abstract,as,base,bool,break,byte,case,catch,char,checked,", out);
    std::fputs("class,const,continue,decimal,default,delegate,do,double,else,enum,event,", out);
    std::fputs("explicit,extern,false,finally,fixed,float,for,foreach,goto,if,implicit,in,", out);
    std::fputs("int,interface,internal,is,lock,long,namespace,new,null,object,operator,out,", out);
    std::fputs("override,params,private,protected,public,readonly,ref,return,sbyte,sealed,", out);
    std::fputs("short,sizeof,stackalloc,static,string,struct,switch,this,throw,true,try,", out);
    std::fputs("typeof,uint,ulong,unchecked,unsafe,ushort,using,virtual,void,volatile,while},\n", out);
    std::fputs("  morecomment=[l]{//},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{CSS}{\n", out);
    std::fputs("  morekeywords={align-items,align-self,animation,background,background-color,", out);
    std::fputs("border,border-radius,bottom,box-shadow,color,content,cursor,display,flex,", out);
    std::fputs("float,font,font-family,font-size,font-weight,grid,gap,height,justify-content,", out);
    std::fputs("left,line-height,list-style,margin,max-height,max-width,min-height,min-width,", out);
    std::fputs("opacity,overflow,padding,position,right,text-align,text-decoration,top,", out);
    std::fputs("transform,transition,visibility,width,z-index},\n", out);
    std::fputs("  morecomment=[s]{/*}{*/},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\lstdefinelanguage{Lua}{\n", out);
    std::fputs("  morekeywords={and,break,do,else,elseif,end,false,for,function,goto,if,in,", out);
    std::fputs("local,nil,not,or,repeat,return,then,true,until,while},\n", out);
    std::fputs("  morecomment=[l]{--},\n", out);
    std::fputs("  morecomment=[s]{--[[}{]]},\n", out);
    std::fputs("  morestring=[b]\",\n", out);
    std::fputs("  morestring=[b]',\n", out);
    std::fputs("}\n", out);
    std::fputs("\\colorlet{Red}{red}\\colorlet{Green}{green}\\colorlet{Blue}{blue}\n", out);
    std::fputs("\\colorlet{Orange}{orange}\\colorlet{Pink}{pink}\\colorlet{Purple}{purple}\n", out);
    std::fputs("\\colorlet{Cyan}{cyan}\\colorlet{Brown}{brown}\\colorlet{Teal}{teal}\n", out);
    std::fputs("\\colorlet{Violet}{violet}\\colorlet{White}{white}\\colorlet{Black}{black}\n", out);
    std::fputs("\\colorlet{Grey}{gray}\\colorlet{grey}{gray}\\colorlet{Gray}{gray}\n", out);
    std::fputs("\\colorlet{default}{black}\n", out);
    std::fputs("\\colorlet{normal}{black}\n", out);
    std::fputs("\\colorlet{transparent}{white}\n", out);
    std::fputs("\\definecolor{Aquamarine}{RGB}{127,255,212}\n", out);
    std::fputs("\\definecolor{gold}{RGB}{255,215,0}\n", out);
    std::fputs("\\providecommand{\\degree}{^{\\circ}}\n", out);
    std::fputs("\\providecommand{\\exist}{\\exists}\n", out);
    std::fputs("\\providecommand{\\infin}{\\infty}\n", out);
    std::fputs("\\providecommand{\\sube}{\\subseteq}\n", out);
    std::fputs("\\providecommand{\\supe}{\\supseteq}\n", out);
    std::fputs("\\providecommand{\\lang}{\\langle}\n", out);
    std::fputs("\\providecommand{\\rang}{\\rangle}\n", out);
    std::fputs("\\providecommand{\\rarr}{\\rightarrow}\n", out);
    std::fputs("\\providecommand{\\larr}{\\leftarrow}\n", out);
    std::fputs("\\providecommand{\\uarr}{\\uparrow}\n", out);
    std::fputs("\\providecommand{\\darr}{\\downarrow}\n", out);
    std::fputs("\\providecommand{\\lrarr}{\\leftrightarrow}\n", out);
    std::fputs("\\providecommand{\\xlongequal}[2][=]{\\overset{#2}{#1}}\n", out);
    std::fputs("\\providecommand{\\argmax}{\\operatorname*{arg\\,max}}\n", out);
    std::fputs("\\providecommand{\\argmin}{\\operatorname*{arg\\,min}}\n", out);
    std::fputs("\\providecommand{\\ctg}{\\cot}\n", out);
    std::fputs("\\providecommand{\\lt}{<}\n", out);
    std::fputs("\\providecommand{\\gt}{>}\n", out);
    std::fputs("\\providecommand{\\Alpha}{\\mathrm{A}}\n", out);
    std::fputs("\\providecommand{\\Beta}{\\mathrm{B}}\n", out);
    std::fputs("\\providecommand{\\Epsilon}{\\mathrm{E}}\n", out);
    std::fputs("\\providecommand{\\Zeta}{\\mathrm{Z}}\n", out);
    std::fputs("\\providecommand{\\Eta}{\\mathrm{H}}\n", out);
    std::fputs("\\providecommand{\\Iota}{\\mathrm{I}}\n", out);
    std::fputs("\\providecommand{\\Kappa}{\\mathrm{K}}\n", out);
    std::fputs("\\providecommand{\\Mu}{\\mathrm{M}}\n", out);
    std::fputs("\\providecommand{\\Nu}{\\mathrm{N}}\n", out);
    std::fputs("\\providecommand{\\Omicron}{\\mathrm{O}}\n", out);
    std::fputs("\\providecommand{\\Rho}{\\mathrm{P}}\n", out);
    std::fputs("\\providecommand{\\Tau}{\\mathrm{T}}\n", out);
    std::fputs("\\providecommand{\\Upsilon}{\\mathrm{Y}}\n", out);
    std::fputs("\\providecommand{\\Chi}{\\mathrm{X}}\n", out);
    std::fputs("\\providecommand{\\R}{\\mathbb{R}}\n", out);
    std::fputs("\\providecommand{\\N}{\\mathbb{N}}\n", out);
    std::fputs("\\providecommand{\\Z}{\\mathbb{Z}}\n", out);
    std::fputs("\\providecommand{\\Q}{\\mathbb{Q}}\n", out);
    std::fputs("\\providecommand{\\C}{\\mathbb{C}}\n", out);
    std::fputs("\\providecommand{\\red}[1]{\\textcolor{red}{#1}}\n", out);
    std::fputs("\\providecommand{\\blue}[1]{\\textcolor{blue}{#1}}\n", out);
    std::fputs("\\providecommand{\\green}[1]{\\textcolor{green}{#1}}\n", out);
    std::fputs("\\providecommand{\\pink}[1]{\\textcolor{pink}{#1}}\n", out);
    std::fputs("\\providecommand{\\orange}[1]{\\textcolor{orange}{#1}}\n", out);
    std::fputs("\\providecommand{\\purple}[1]{\\textcolor{purple}{#1}}\n", out);
    std::fputs("\\providecommand{\\brown}[1]{\\textcolor{brown}{#1}}\n", out);
    std::fputs("\\providecommand{\\gray}[1]{\\textcolor{gray}{#1}}\n", out);
    std::fputs("\\providecommand{\\cyan}[1]{\\textcolor{cyan}{#1}}\n", out);
    std::fputs("\\providecommand{\\teal}[1]{\\textcolor{teal}{#1}}\n", out);
    std::fputs("\\providecommand{\\magenta}[1]{\\textcolor{magenta}{#1}}\n", out);
    std::fputs("\\providecommand{\\yellow}[1]{\\textcolor{yellow}{#1}}\n", out);
    std::fputs("\\providecommand{\\violet}[1]{\\textcolor{violet}{#1}}\n", out);
    std::fputs("\\lstdefinelanguage{JavaScript}{\n", out);
    std::fputs("keywords={break, case, catch, class, const, continue, debugger, default, delete, do, else, export, extends, finally, for, function, if, import, in, instanceof, let, new, return, super, switch, this, throw, try, typeof, var, void, while, with, yield, await, async, of, from, as},", out);
    std::fputs("keywordstyle=\\color{blue}\\bfseries,\n", out);
    std::fputs("ndkeywords={boolean, number, string, null, undefined, true, false},\n", out);
    std::fputs("ndkeywordstyle=\\color{red}\\bfseries,\n", out);
    std::fputs("identifierstyle=\\color{black},\n", out);
    std::fputs("sensitive=false,\n", out);
    std::fputs("comment=[l]{//},\n", out);
    std::fputs("morecomment=[s]{/*}{*/},", out);
    std::fputs("commentstyle=\\color{green}\\ttfamily,\n", out);
    std::fputs("stringstyle=\\color{purple}\\ttfamily,\n", out);
    std::fputs("morestring=[b]',\n", out);
    std::fputs("morestring=[b]\"\n", out);
    std::fputs("}\n", out);
    std::fputs("\\title{luogu export}\n\\author{luogu-export}\n\\date{\\today}\n", out);

    std::fputs("\\setmonofont{Consolas}\n", out);
    std::fputs("\\setCJKmonofont{SimHei}\n", out);
    std::fputs("\\newfontfamily{\\tagsfonts}{Noto Sans}[Script = CJK, CJKFont = WenQuanYi Micro Hei]\n", out);

    std::fputs("\\begin{document}\n\n", out);
    std::fputs("\\maketitle\n", out);
    std::fputs("\\tableofcontents\n", out);
    std::fputs("\\newpage\n", out);

    std::fputs("\n\n", out);
    int cnt = 0, total = static_cast<int>(problems.size());
    for (const auto &p : problems)
    {
        std::fputs(problem_to_latex(p, opt).c_str(), out);
        std::fputs("\n", out);
        cnt++;
        std::printf("\rExporting: %3d %% (%d/%d). ", cnt * 100 / total, cnt, total);
        fflush(stdout);
    }
    std::printf("\rExporting: %3d %% (%d/%d), done.\n", cnt * 100 / total, cnt, total);
    fflush(stdout);

    std::fputs("\\end{document}\n", out);
    if (std::ferror(out))
    {
        std::fclose(out);
        error = "写入输出文件 '" + output_path.string() + "' 失败";
        return false;
    }
    std::fclose(out);
    return true;
}
