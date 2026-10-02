// include/luogu-export/export/latex/math.h
#ifndef LUOGU_EXPORT_LATEX_MATH_H
#define LUOGU_EXPORT_LATEX_MATH_H

#include <string>

// 数学公式的清洗与对齐环境归一化（洛谷题面里的 KaTeX 写法 → 标准 LaTeX）。
namespace latex::detail
{
    // 对齐环境行归一化：洛谷题面里 \begin{array}{c} 等常有多余/缺少的 &，
    // 导致 "Extra alignment tab"；把每行统一到目标列数。
    std::string normalize_alignment(std::string s);

    // 数学公式里是否有“顶层”（不在任何 \begin 环境、也不在花括号内）的 & 或 \\ / \cr
    bool has_top_level_align(const std::string &s);

    // 把一段数学公式（不含 $ 定界符）清洗为 xelatex 可编译的形式
    std::string sanitize_math(std::string s);
}

#endif // LUOGU_EXPORT_LATEX_MATH_H
