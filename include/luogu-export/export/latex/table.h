// include/luogu-export/export/latex/table.h
#ifndef LUOGU_EXPORT_LATEX_TABLE_H
#define LUOGU_EXPORT_LATEX_TABLE_H

#include <string>
#include <vector>

// 块级结构（段落、表格）的识别与输出。
namespace latex::detail
{
    // 段落片段：一行 markdown（或一行中被图片切开的一小段）
    struct ParaPart
    {
        std::string text; // 文本内容；若 image 为 true，则是完整图片语法
        bool hard = false; // 片段末尾是否有硬换行（两个空格）
        bool image = false; // 是否为独立图片片段（图片单独成行）
    };

    // 表格单元格（含合并信息）。Luogu 扩展语法：
    //   ^ —— 与上方单元格合并（该格被上方纵向跨段覆盖）
    //   < —— 与左侧单元格合并（该格被左侧横向跨段覆盖）
    // 合并标记必须是单元格内唯一的纯文本内容。
    // 模型：每个单元格要么是“视觉内容格”（covered=false，含 rowspan/colspan），
    // 要么是“覆盖格”（covered=true，owner_row/owner_col 指向其所属的视觉内容格）。
    // 纵向跨段结束行由拥有者的 rowspan 动态计算，避免合并扩展后旧信息残留。
    struct TableCell
    {
        std::string content;  // 单元格原始内容（覆盖格为空）
        int rowspan = 1;
        int colspan = 1;
        bool covered = false; // 被上方/左侧合并覆盖，不输出
        int owner_row = -1;   // 所属视觉内容格的行（覆盖格使用）
        int owner_col = -1;   // 所属视觉内容格的列（覆盖格使用）
    };

    // 是否以某种“块级”语法开头（用于结束普通段落）
    bool is_block_start(const std::string &t);

    // 是否为表格分隔行（|:---|:---:| 或 :-:|:-: 等）
    bool is_table_separator_row(const std::string &line);

    // 一行是否可能是表格行（包含 |）
    bool is_table_row(const std::string &line);

    // 把多个行内片段拼成一个段落（硬换行用 \\，丢弃转换后为空的片段）
    std::string join_inline_parts(const std::vector<ParaPart> &parts);

    // 在单个 part 中按图片语法切分：独立图片切成 image 片段
    std::vector<ParaPart> split_part_images(const std::string &body, bool hard);

    // 输出一个段落（parts 列表）：独立图片拆成“自成一页段落”的块
    void emit_paragraph_parts(const std::vector<ParaPart> &parts, std::string &out);

    // 一行文本是否有未闭合的括号/方括号（链接可能跨行）
    bool has_unclosed_paren_or_bracket(const std::string &s);

    // 一行文本是否有未闭合的数学分隔符（$ 的数量为奇数）
    bool has_unclosed_math(const std::string &s);

    // 近似估算 markdown 片段的渲染宽度（pt），用于决定表格是否换行及列宽
    double estimate_cell_width(const std::string &s);

    // 表格：rows[0] 表头，rows[1] 对齐行，其余为内容行
    void emit_table(const std::vector<std::string> &rows, std::string &out);
}

#endif // LUOGU_EXPORT_LATEX_TABLE_H
