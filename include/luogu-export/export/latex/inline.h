// include/luogu-export/export/latex/inline.h
#ifndef LUOGU_EXPORT_LATEX_INLINE_H
#define LUOGU_EXPORT_LATEX_INLINE_H

#include <cstddef>
#include <string>
#include <vector>

// 行内（inline）markdown → LaTeX：数学、代码、链接、强调、图片等。
// 通过占位符机制先把数学/代码等原样内容抽出来，再转义普通文本。
namespace latex::detail
{
    // 占位符：\x01R<n>\x02
    std::string placeholder(size_t index);

    // 恢复 \x01R<n>\x02 占位符
    std::string restore_placeholders(const std::string &s, const std::vector<std::string> &raws);

    // 合并相邻的数学占位符：洛谷题面里 \$$ 等畸形写法会把一个公式拆成多段
    void merge_adjacent_math(std::string &s, std::vector<std::string> &raws,
                             const std::vector<bool> &is_math);

    // inline_to_latex 的递归实现：raws 为占位符池，is_math 标记哪些占位符是数学
    std::string inline_to_latex_impl(const std::string &text, std::vector<std::string> &raws,
                                     std::vector<bool> &is_math);

    // 把一行 markdown 文本转换为行内 LaTeX
    std::string inline_to_latex(const std::string &text);
}

#endif // LUOGU_EXPORT_LATEX_INLINE_H
