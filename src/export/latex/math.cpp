// src/export/latex/math.cpp
#include "luogu-export/export/latex/math.h"
#include "luogu-export/export/latex/text.h"
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

namespace latex::detail
{
// 对齐环境行归一化：洛谷题面里 \begin{array}{c} 等常有多余/缺少的 &，
// 导致 "Extra alignment tab"；把每行统一到目标列数（array 按 spec，矩阵按最大行宽）
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

// 洛谷题面常在公式里用 \newcommand/\renewcommand 自定义命令，
// 标准 LaTeX 中若与已有命令同名会报 "already defined"；统一转成 \def（允许重复定义）
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
            const auto it = kNamedHex.find(util::to_lower_ascii(x));
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
} // namespace latex::detail
