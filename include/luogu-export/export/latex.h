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

        // 目录条目是否带跳转到对应题目的超链接（--no-toc-links 置为 false；默认 true）
        bool toc_links = true;
        // 页眉页码是否为跳回目录页的超链接（--toc-backlinks 置为 true；默认 false）
        bool toc_backlinks = false;
        // bilibili 视频 URL 是否输出为超链接（--no-bilibili-link 置为 false；默认 true）
        bool bilibili_links = true;

        // 字体设置：空串表示使用原代码中的默认字体。
        // 值既可以是系统已安装的字体名称，也可以是字体文件地址（main 中已规范化）。
        std::string font_cover;    // 封面标题字体（--set-font-cover-page）
        std::string font_body_zh;  // 正文中文字体（--set-font-body-zh-CN）
        std::string font_body_en;  // 正文西文字体，不作用于公式（--set-font-body-en-US）
        std::string font_code;     // 代码块字体（--set-font-body-codes）
        std::string font_title_zh; // 标题中文字体（--set-font-title-zh-CN）
        std::string font_title_en; // 标题西文字体（--set-font-title-en-US）

        // 封面标题文字（--set-cover-title；空串表示默认 "luogu export"）
        std::string cover_title;
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
    // @param opt         显示选项（目录超链接/回链、字体、封面标题、bilibili 链接等）
    // @return 成功返回 true
    bool export_latex(const luogu::ExportFilter &filter,
                      const std::filesystem::path &output_path,
                      std::string &error,
                      const Options &opt = {});
}

#endif // LUOGU_EXPORT_LATEX_H
