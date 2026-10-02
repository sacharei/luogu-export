// src/export/latex/table.cpp
#include "luogu-export/export/latex/table.h"
#include "luogu-export/export/latex/inline.h"
#include "luogu-export/export/latex/text.h"

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
std::string join_inline_parts(const std::vector<ParaPart> &parts)
{
    std::vector<ParaPart> out;
    for (const auto &rp : parts)
    {
        const std::string c = inline_to_latex(rp.text);
        if (!c.empty())
            out.emplace_back(ParaPart{c, rp.hard, rp.image});
    }
    std::string para;
    for (size_t k = 0; k < out.size(); ++k)
    {
        if (k)
        {
            // 硬换行前补 {}：前一段可能是缺失图片（编译期为空），
            // 没有 {} 的话 \\ 前无内容会报 "There's no line here to end"
            para += out[k - 1].hard ? " {}\\\\ " : " ";
            if (out[k].text[0] == '[')
                para += "{}";
        }
        para += out[k].text;
    }
    return para;
}

// 在单个 part 中按图片语法切分：独立图片切成 image 片段，
// 其余保留为文本片段（链接图片 [![](img)](url) 不拆，交给链接处理）。
std::vector<ParaPart> split_part_images(const std::string &body, bool hard)
{
    std::vector<ParaPart> out;
    // 交替：先是链接图片（不拆），再是普通图片
    static const std::regex kAnyImg(
        R"(\[!\[[^\]]*\]\s*\([^)]*\)\]\s*\([^)]*\)|!\[[^\]]*\]\s*\([^)]*\))");
    size_t pos = 0;
    bool saw_any = false;
    for (std::sregex_iterator it(body.begin(), body.end(), kAnyImg), end; it != end; ++it)
    {
        const std::smatch &m = *it;
        const size_t mp = static_cast<size_t>(m.position());
        // 图片前的文本
        if (mp > pos)
        {
            std::string pre = body.substr(pos, mp - pos);
            if (!trim(pre).empty())
                out.push_back(ParaPart{pre, false, false});
            else if (!out.empty())
                out.back().text += pre; // 纯空白并入前一个片段
        }
        const std::string tok = m.str();
        const bool is_linked = !tok.empty() && tok[0] == '[';
        if (is_linked)
        {
            // 链接图片留在文本里（步骤 4 会转成 \href）
            if (!out.empty() && !out.back().image)
                out.back().text += tok;
            else
                out.push_back(ParaPart{tok, false, false});
        }
        else
        {
            out.push_back(ParaPart{tok, false, true});
        }
        pos = mp + m.length();
        saw_any = true;
    }
    if (pos < body.size())
    {
        const std::string tail = body.substr(pos);
        if (!trim(tail).empty())
            out.push_back(ParaPart{tail, hard, false});
        else if (!out.empty())
            out.back().hard = hard; // 尾部空白：硬换行归最后一段
    }
    else if (!saw_any)
    {
        out.push_back(ParaPart{body, hard, false});
    }
    else if (!out.empty())
    {
        out.back().hard = hard; // 图片在行尾时，硬换行归最后片段（图片段，不使用）
    }
    // 合并相邻的文本片段
    std::vector<ParaPart> merged;
    for (auto &p : out)
    {
        if (p.image || merged.empty() || merged.back().image)
        {
            merged.push_back(std::move(p));
        }
        else
        {
            merged.back().text += p.text;
            merged.back().hard = merged.back().hard || p.hard;
        }
    }
    return merged;
}

// 输出一个段落（parts 列表）：独立图片拆成“自成一页段落”的块
// （{\par\noindent...\par}，贴左对齐、不再随行内文字偏移），
// 其余片段按行内拼接成普通段落。
void emit_paragraph_parts(const std::vector<ParaPart> &parts, std::string &out)
{
    std::vector<std::vector<ParaPart>> segments;
    std::vector<ParaPart> cur;
    for (const auto &p : parts)
    {
        const std::vector<ParaPart> frags = split_part_images(p.text, p.hard);
        for (const auto &f : frags)
        {
            if (f.image)
            {
                if (!cur.empty())
                {
                    segments.push_back(std::move(cur));
                    cur.clear();
                }
                segments.push_back({f});
            }
            else
            {
                cur.push_back(f);
            }
        }
    }
    if (!cur.empty())
        segments.push_back(std::move(cur));

    for (const auto &seg : segments)
    {
        // 整段只有一张图片（独立图片，或整段只有一张“链接图片” [![](img)](url)）
        // 时按独立图片排版：图片可能铺满行宽，段落首行缩进（ctex 下
        // \parindent ≈ 2em）会把它挤出页面。
        static const std::regex kOnlyLinkedImage(
            R"(^\s*\[!\[[^\]]*\]\s*\([^)]*\)\]\s*\([^)]*\)\s*$)");
        const bool standalone = (seg.size() == 1) &&
            (seg[0].image || std::regex_match(seg[0].text, kOnlyLinkedImage));
        if (standalone)
        {
            // 独立图片：自成一页段落，左对齐贴边，避免行内大图偏移/溢出
            const std::string img = inline_to_latex(seg[0].text);
            if (!img.empty())
                out += "{\\par\\noindent" + img + "\\par}\n\n";
        }
        else
        {
            const std::string para = join_inline_parts(seg);
            if (!para.empty())
                out += para + "\n\n";
        }
    }
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

// 一行文本是否有未闭合的数学分隔符：$ 的数量为奇数。
// 转义的 \$（以及其它 \x 转义序列）不计入；用于把跨行的 $...$ 数学块
// （如 $\begin{aligned}...\end{aligned}$ 逐行书写）合并成完整片段，
// 否则每行单独转换时 $ 无法配对，整段会变成转义文本。
bool has_unclosed_math(const std::string &s)
{
    int dollars = 0;
    size_t i = 0;
    while (i < s.size())
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            i += 2; // 跳过转义序列
            continue;
        }
        if (s[i] == '$')
            ++dollars;
        ++i;
    }
    return (dollars % 2) == 1;
}

// 近似估算 markdown 片段的渲染宽度（pt）。
// 只用于判断“不换行时整表是否会超出页面”并给出按比例的列宽。
// 各类字符取实测平均宽（10pt 西文小写/数字 ≈5.3pt、大写 ≈7.3pt、全角
// CJK ≈10.5pt），取整后各常数都略大于实测值，使估算整体成为渲染宽度的
// 上界——这样“估算总宽超过行宽才启用换行”就能保证不换行的表不会溢出页面。
double estimate_cell_width(const std::string &s)
{
    double w = 0;
    size_t i = 0;
    while (i < s.size())
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        // markdown 链接 [text](url) 只渲染 text，URL 不会显示，
        // 不能把 URL 字符串计入列宽（否则含链接的单元格会被严重高估，
        // 按比例压缩其它列导致表头文字溢出）
        if (c == '!' && i + 1 < s.size() && s[i + 1] == '[')
        {
            // 图片 ![alt](url)：按 alt 文本 + 固定图片宽度估算（图片常被
            // 缩放到行宽以内，固定值取一个保守偏大的值）
            const size_t close = s.find(']', i + 2);
            if (close != std::string::npos && close + 1 < s.size() &&
                s[close + 1] == '(')
            {
                const size_t paren = s.find(')', close + 2);
                if (paren != std::string::npos)
                {
                    w += 150.0;
                    i = paren + 1;
                    continue;
                }
            }
            ++i;
            continue;
        }
        if (c == '[')
        {
            const size_t close = s.find(']', i + 1);
            if (close != std::string::npos && close + 1 < s.size() &&
                s[close + 1] == '(')
            {
                const size_t paren = s.find(')', close + 2);
                if (paren != std::string::npos)
                {
                    // 只统计链接文本部分（正常按字符计宽），URL 部分跳过
                    w += estimate_cell_width(s.substr(i + 1, close - i - 1));
                    i = paren + 1;
                    continue;
                }
            }
            ++i; // 普通方括号不计宽
            continue;
        }
        if (c == '\\')
        {
            // LaTeX 命令/转义：控制词按名字长度计宽（名字不会渲染出来，
            // 这样计只会偏大，安全）
            size_t j = i + 1;
            if (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j])))
            {
                while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j])))
                    ++j;
                w += 5.5 * static_cast<double>(j - i);
                i = j;
                continue;
            }
            w += 5.5;
            i += 2;
            continue;
        }
        if (c >= 0x80)
        {
            w += 11.0; // 全角 CJK 实测 ≈10.5pt
            i += (c < 0xE0) ? 2 : 3;
            continue;
        }
        if (c == ' ' || c == '\t')
        {
            w += 3.5;
            ++i;
            continue;
        }
        if (c == '{' || c == '}' || c == '$' || c == '*' || c == '_' ||
            c == '`' || c == ']' || c == '#' || c == '>' || c == '^')
        {
            ++i; // markdown/数学语法符号不计宽（上标/下标由内容字符计宽）
            continue;
        }
        w += (c >= 'A' && c <= 'Z') ? 7.8 : 5.5;
        ++i;
    }
    return w;
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

    // 表头 + 内容行（对齐行不参与输出）
    std::vector<std::vector<std::string>> cell_rows;
    cell_rows.push_back(split_cells(rows[0]));
    for (size_t i = 2; i < rows.size(); ++i)
        cell_rows.push_back(split_cells(rows[i]));
    if (cell_rows.empty())
        return;

    size_t col_count = 0;
    for (const auto &cells : cell_rows)
        col_count = std::max(col_count, cells.size());
    if (col_count == 0)
        return;
    for (auto &cells : cell_rows)
        cells.resize(col_count);

    // 列对齐（来自对齐行）
    std::vector<char> aligns(col_count, 'l');
    {
        const std::vector<std::string> align_cells = split_cells(rows[1]);
        for (size_t c = 0; c < col_count && c < align_cells.size(); ++c)
        {
            const std::string t = trim(align_cells[c]);
            if (t.size() >= 3 && t.front() == ':' && t.back() == ':')
                aligns[c] = 'c';
            else if (!t.empty() && t.front() == ':')
                aligns[c] = 'l';
            else if (!t.empty() && t.back() == ':')
                aligns[c] = 'r';
            else
                aligns[c] = 'l';
        }
    }

    const size_t nrows = cell_rows.size();
    std::vector<std::vector<TableCell>> grid(nrows);
    for (size_t r = 0; r < nrows; ++r)
    {
        grid[r].reserve(col_count);
        for (size_t c = 0; c < col_count; ++c)
            grid[r].push_back(TableCell{cell_rows[r][c], 1, 1, false,
                                        static_cast<int>(r), static_cast<int>(c)});
    }

    // 处理合并标记：^ 向上、< 向左（可链式合并）。
    // 合并标记必须是单元格内唯一的纯文本内容。
    auto is_merge_marker = [](const std::string &s) {
        const std::string t = trim(s);
        return t == "^" || t == "<" || t.empty();
    };
    // 解析 (r,c) 所属的视觉内容格（沿 owner 链回溯）
    auto resolve = [&](size_t r, size_t c) -> std::pair<size_t, size_t> {
        while (grid[r][c].covered)
        {
            r = static_cast<size_t>(grid[r][c].owner_row);
            c = static_cast<size_t>(grid[r][c].owner_col);
        }
        return {r, c};
    };
    for (size_t r = 1; r < nrows; ++r)
    {
        for (size_t c = 0; c < col_count; ++c)
        {
            if (grid[r][c].covered)
                continue; // 已被覆盖的位置不再处理
            const std::string t = trim(grid[r][c].content);
            if (t == "^")
            {
                // 与上方单元格合并：找到上方所属的视觉内容格并扩展其纵向跨度
                const auto [tr, tc] = resolve(r - 1, c);
                TableCell &owner = grid[tr][tc];
                // 若该跨段尚未覆盖本行才扩展（如 < 已把跨度扩到本列而 ^ 只需确认覆盖）
                if (tr + owner.rowspan - 1 < r)
                    ++owner.rowspan;
                // 拥有者横向跨段内的本行单元格（一致表格中为空/合并标记）一并覆盖
                for (size_t c2 = tc;
                     c2 < tc + static_cast<size_t>(owner.colspan) && c2 < col_count; ++c2)
                {
                    if (is_merge_marker(grid[r][c2].content))
                    {
                        grid[r][c2].covered = true;
                        grid[r][c2].owner_row = static_cast<int>(tr);
                        grid[r][c2].owner_col = static_cast<int>(tc);
                        grid[r][c2].content.clear();
                    }
                }
            }
            else if (t == "<")
            {
                if (c == 0)
                    continue; // 第一列无法向左合并
                // 与左侧单元格合并：找到左侧所属的视觉内容格并扩展其横向跨度
                const auto [tr, tc] = resolve(r, c - 1);
                TableCell &owner = grid[tr][tc];
                // 若该跨段尚未覆盖本列才扩展（如 ^ 已把跨度扩到本行而 < 只需确认覆盖）
                if (tc + owner.colspan - 1 < c)
                    ++owner.colspan;
                // 拥有者纵向跨段内的本列单元格（一致表格中为空/合并标记）一并覆盖
                for (size_t r2 = tr;
                     r2 < tr + static_cast<size_t>(owner.rowspan) && r2 < nrows; ++r2)
                {
                    if (is_merge_marker(grid[r2][c].content))
                    {
                        grid[r2][c].covered = true;
                        grid[r2][c].owner_row = static_cast<int>(tr);
                        grid[r2][c].owner_col = static_cast<int>(tc);
                        grid[r2][c].content.clear();
                    }
                }
            }
        }
    }

    // (r,c) 所在纵向跨段的结束行（跨段覆盖到第几行）
    auto span_end = [&](size_t r, size_t c) -> int {
        const TableCell &cell = grid[r][c];
        if (cell.covered)
            return grid[cell.owner_row][cell.owner_col].rowspan +
                   cell.owner_row - 1;
        return static_cast<int>(r) + cell.rowspan - 1;
    };

    // ---- 列宽估算：判断整表是否需要换行（改用 p{width} 列自动折行）----
    // 页面参数与 preamble 的 \geometry{margin=2cm} 对应：letter 612pt - 2*56.7pt
    const double kLineWidth = 498.6;
    const double kTabColsep = 6.0;   // \tabcolsep
    const double kRuleWidth = 0.4;   // \arrayrulewidth

    std::vector<double> col_nat(col_count, 0.0);
    for (size_t r = 0; r < nrows; ++r)
    {
        for (size_t c = 0; c < col_count; ++c)
        {
            const TableCell &cell = grid[r][c];
            if (cell.covered || trim(cell.content).empty())
                continue;
            const double cell_w = estimate_cell_width(cell.content);
            const int cs = std::max(1, cell.colspan);
            const double per = cell_w / static_cast<double>(cs);
            for (int k = 0; k < cs && c + static_cast<size_t>(k) < col_count; ++k)
                col_nat[c + static_cast<size_t>(k)] =
                    std::max(col_nat[c + static_cast<size_t>(k)], per);
        }
    }
    double total_est = 0;
    for (double v : col_nat)
        total_est += v;
    const double padding = col_count * 2 * kTabColsep + (col_count + 1) * kRuleWidth;
    // 估算已是渲染宽度的上界（各类字符常数都取实测值的上限），
    // 因此“估算总宽接近行宽才启用换行”：0.95 的余量只兜底估算误差，
    // 能让内容并不长的小表保持 LaTeX 自动宽度（l/c/r），
    // 只有内容确实过长的表才改用 p{} 列自动折行
    const bool need_wrap = (total_est + padding) > 0.95 * kLineWidth;

    std::vector<double> pwidth(col_count, 0.0);
    auto fmt_pt = [](double w) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1fpt", w);
        return std::string(buf);
    };
    // 合并区域的总渲染宽度：各列 p{} 内容宽之和 + 每列的 tabcolsep 内边距
    // （跨 cs 列时有 cs 份 2*\tabcolsep 和 cs-1 条内部竖线）
    auto span_total_width = [&](size_t c, int cs) {
        double w = 0;
        for (int k = 0; k < cs && c + static_cast<size_t>(k) < col_count; ++k)
            w += pwidth[c + static_cast<size_t>(k)];
        w += static_cast<double>(cs) * 2 * kTabColsep;
        if (cs > 1)
            w += static_cast<double>(cs - 1) * kRuleWidth;
        return w;
    };
    // \multicolumn 的 p{} 宽度：合并区域总宽减去该格自身的 tabcolsep
    auto multicol_width = [&](size_t c, int cs) {
        return span_total_width(c, cs) - 2 * kTabColsep;
    };
    // p{} 列的对齐前缀（需要 array 宏包）
    auto pcol_prefix = [&](char a) -> std::string {
        if (a == 'c')
            return ">{\\centering\\arraybackslash}p{";
        if (a == 'r')
            return ">{\\raggedleft\\arraybackslash}p{";
        return ">{\\raggedright\\arraybackslash}p{";
    };

    std::string spec = "|";
    if (need_wrap)
    {
        // 按估算比例分配列宽，总和铺满行宽（保证任意长内容都能折行）
        const double available = kLineWidth - padding;
        double sum = 0;
        for (double v : col_nat)
            sum += v;
        double scale = (sum > 0) ? (available / sum) : 1.0;
        for (size_t c = 0; c < col_count; ++c)
            pwidth[c] = std::max(col_nat[c] * scale, 16.0);
        double psum = 0;
        for (double v : pwidth)
            psum += v;
        if (psum + padding > kLineWidth) // 最小列宽导致超出时再整体缩放
        {
            const double s2 = (kLineWidth - padding) / psum;
            for (auto &v : pwidth)
                v *= s2;
        }
        for (size_t c = 0; c < col_count; ++c)
        {
            spec += pcol_prefix(aligns[c]) + fmt_pt(pwidth[c]) + "}";
            spec += '|';
        }
    }
    else
    {
        for (size_t c = 0; c < col_count; ++c)
        {
            spec += aligns[c];
            spec += '|';
        }
    }

    auto emit_cell = [&](const TableCell &cell, size_t c) {
        const std::string content = inline_to_latex(cell.content);
        const std::string ncols = std::to_string(cell.colspan);
        const std::string nrows_s = std::to_string(cell.rowspan);
        // \multicolumn 会吞掉其右侧边界的竖线（\multispan 跳过被跨列的模板，
        // 紧跟其后的单元格也不再画左竖线），必须在 spec 末尾补一个 | 把
        // 合并区域的右边界竖线画出来。注意不能在 spec 开头加 |：XeLaTeX 的
        // halign 会把开头的 | 拆成独立的一列，导致合并格内部出现多余的竖线
        // 并破坏整表列结构；跨到第 0 列时左侧边框因此缺失（该情况很罕见，
        // 原实现同样没有左侧边框）。
        const std::string mc_trail = "|";
        if (need_wrap)
        {
            // p{} 列：单元格内容自动折行。合并格的 \multicolumn 宽度取整个合并区域
            // 的总宽（含跨列的内边距），否则 \multicolumn 比它覆盖的列窄、内容偏左；
            // \multirow 盒子宽度取该区域的内容宽（不含自身 tabcolsep），
            // 否则盒子比单元格内容区宽 2*\tabcolsep 会触发 Overfull \hbox
            const std::string mcw = fmt_pt(multicol_width(c, cell.colspan));
            const std::string mc_spec = pcol_prefix(aligns[c]) + mcw + "}" +
                                        mc_trail;
            if (cell.rowspan > 1 && cell.colspan > 1)
            {
                // p{} 列中 \multirow 内容默认左对齐，用 \centering 居中
                out += "\\multicolumn{" + ncols + "}{" + mc_spec + "}{\\multirow{" +
                       nrows_s + "}{" + mcw + "}{\\centering " + content + "}}";
            }
            else if (cell.rowspan > 1)
            {
                out += "\\multirow{" + nrows_s + "}{" + mcw + "}{\\centering " + content + "}";
            }
            else if (cell.colspan > 1)
            {
                out += "\\multicolumn{" + ncols + "}{" + mc_spec + "}{" + content + "}";
            }
            else
            {
                out += content;
            }
            return;
        }
        if (cell.rowspan > 1 && cell.colspan > 1)
        {
            out += "\\multicolumn{" + ncols + "}{" + aligns[c] +
                   mc_trail + "}{\\multirow{" + nrows_s + "}{*}{" + content + "}}";
        }
        else if (cell.rowspan > 1)
        {
            out += "\\multirow{" + nrows_s + "}{*}{" + content + "}";
        }
        else if (cell.colspan > 1)
        {
            out += "\\multicolumn{" + ncols + "}{" + aligns[c] +
                   mc_trail + "}{" + content + "}";
        }
        else
        {
            out += content;
        }
    };

    // 输出一行：被覆盖的列输出空单元格，multicolumn 吞掉其覆盖的列。
    // 被 \multirow 纵向覆盖的延续行里，普通空单元格仍会画出列模板的竖线
    // （合并区域内出现多余的内部竖线），改用 \multicolumn{1}{c} 输出。
    // 左边界竖线由左侧单元格一侧负责绘制（普通单元格模板的尾竖线、
    // 合并格的尾 |），只有第 0 列需要自己补表格左边框；
    // 右边界竖线必须自己画——紧跟在 \multicolumn 后面的单元格
    // 不再画自己的左竖线。
    auto emit_row_latex = [&](size_t r) {
        size_t c = 0;
        while (c < col_count)
        {
            if (c > 0)
                out += " & ";
            const TableCell &cell = grid[r][c];
            if (cell.covered)
            {
                const auto owner = resolve(r, c);
                // 左边界由左侧单元格一侧负责绘制（普通单元格模板的尾竖线、
                // 合并格的尾 |），因此这里不画左竖线——在开头加 | 会被
                // XeLaTeX 拆成独立列、破坏结构（见 emit_cell 注释）。
                const bool right_rule =
                    (c + 1 >= col_count) || (resolve(r, c + 1) != owner);
                out += "\\multicolumn{1}{c";
                if (right_rule)
                    out += "|";
                out += "}{}";
                ++c;
                continue;
            }
            emit_cell(cell, c);
            c += static_cast<size_t>(cell.colspan);
        }
        out += " \\\\\n";
    };

    // 行间横线：列 c 的纵向跨段在本行结束时画线，否则（跨段延续）不画
    auto emit_boundary = [&](size_t r) {
        std::vector<bool> border(col_count, false);
        for (size_t c = 0; c < col_count; ++c)
            border[c] = (span_end(r, c) <= static_cast<int>(r));
        std::vector<std::pair<size_t, size_t>> runs;
        size_t c = 0;
        while (c < col_count)
        {
            if (!border[c])
            {
                ++c;
                continue;
            }
            size_t e = c;
            while (e + 1 < col_count && border[e + 1])
                ++e;
            runs.emplace_back(c, e);
            c = e + 1;
        }
        if (runs.empty())
            return;
        if (runs.size() == 1 && runs[0].first == 0 &&
            runs[0].second == col_count - 1)
        {
            out += "\\hline\n";
        }
        else
        {
            for (const auto &run : runs)
                out += "\\cline{" + std::to_string(run.first + 1) + "-" +
                       std::to_string(run.second + 1) + "}\n";
        }
    };

    // \noindent：表格铺满行宽时，段落首行缩进（\parindent）会把表格推出页面
    out += "\\noindent\\begin{tabular}{" + spec + "}\n";
    out += "\\hline\n"; // 顶部全宽横线
    for (size_t r = 0; r < nrows; ++r)
    {
        emit_row_latex(r);
        if (r + 1 < nrows)
            emit_boundary(r);
    }
    out += "\\hline\n"; // 底部全宽横线
    out += "\\end{tabular}\n\n";
}
} // namespace latex::detail
