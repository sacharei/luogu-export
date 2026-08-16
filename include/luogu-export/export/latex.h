// include/luogu-export/export/latex.h
#ifndef LUOGU_EXPORT_LATEX_H
#define LUOGU_EXPORT_LATEX_H

#include <filesystem>
#include <string>
#include "luogu-export/contents/article.h"
#include "luogu-export/contents/problem.h"
#include "luogu-export/export/common.h"

namespace latex
{
    // -L 导出的显示选项
    struct Options
    {
        std::string lang = "zh-CN"; // 题面语言：zh-CN / en（英文缺失时回退中文）
        std::string show = "00";    // 第 1 位=难度，第 2 位=标签；1 显示 0 隐藏。
                                    // 默认不显示难度；标签位为 0 时仅隐藏“算法”类
                                    // （type 2），其他类型（来源/年份/地区/特殊）始终显示
    };

    // 把一段 markdown / HTML 文本转换为 LaTeX。
    // 标题映射为 \section 及更低层级；图片链接映射为缓存中的文件
    // （crawler::image_cache_path），视频（Bilibili 等）只输出链接；
    // 数学公式原样保留。
    // 依赖的宏包：graphicx、hyperref、ulem（删除线）、amsmath/amssymb（公式/任务框）。
    std::string markdown_to_latex(const std::string &markdown);

    // 把一题转换为以 \section 开头的 LaTeX 内容（结构同 markdown 导出：
    // 难度/标签/作者/时空限制 + 背景/描述/输入输出格式/样例/提示）
    std::string problem_to_latex(const problem::Problem &p, const Options &opt = {});

    // 把一篇文章转换为以 \section 开头的 LaTeX 内容
    std::string article_to_latex(const article::Article &a);

    // 读取缓存并按条件筛选题目（与 -M 共用筛选逻辑），
    // 导出为一份完整的、可直接用 xelatex 编译的 LaTeX 文档。
    // @param filter      筛选条件
    // @param output_path 输出 .tex 文件路径
    // @param error       失败时返回的错误信息
    // @return 成功返回 true
    bool export_latex(const luogu::ExportFilter &filter,
                      const std::filesystem::path &output_path,
                      std::string &error);
}

#endif // LUOGU_EXPORT_LATEX_H
