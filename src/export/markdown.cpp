// src/export/markdown.cpp
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "luogu-export/contents/problem.h"
#include "luogu-export/export/common.h"
#include "luogu-export/export/markdown.h"
#include "luogu-export/util/problem_info.h"

using nlohmann::json;

namespace
{

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

// 多语言字段取值：键存在但为 null 时按缺省处理
std::string safe_string(const json &j, const char *key)
{
    if (!j.contains(key) || !j[key].is_string())
        return "";
    return j[key].get<std::string>();
}

} // namespace

bool markdown::export_markdown(const luogu::ExportFilter &filter,
                               const std::filesystem::path &output_path,
                               std::string &error)
{
    error.clear();

    // 筛选（-M / -L 共用），结果已按题号排序
    std::vector<problem::Problem> problems;
    std::vector<std::string> resolved_tags;
    if (!luogu::select_problems(filter, problems, &resolved_tags, error))
        return false;

    // 显示开关：第 1 位 = 难度，第 2 位 = 标签
    const bool show_difficulty = (filter.show.size() >= 2 && filter.show[0] == '1');
    const bool show_tags = (filter.show.size() >= 2 && filter.show[1] == '1');
    const bool use_en = (filter.lang == "en");

    // 写出 markdown 文件
    FILE *out = std::fopen(output_path.c_str(), "w");
    if (!out)
    {
        error = "无法打开输出文件 '" + output_path.string() + "'";
        return false;
    }

    std::fprintf(out, "# 洛谷题目导出（共 %zu 道题）\n\n", problems.size());

    const std::string conds = luogu::describe_filter(filter, resolved_tags);
    std::fputs("筛选条件：", out);
    std::fputs(conds.empty() ? "无（导出全部题目）" : conds.c_str(), out);
    std::fputs("\n\n", out);

    for (const auto &p : problems)
    {
        // 题面语言：英文优先取 translations，缺失时回退中文
        std::string title = p.name;
        std::string background = p.background;
        std::string description = p.description;
        std::string formatI = p.formatI;
        std::string formatO = p.formatO;
        std::string hint = p.hint;
        if (use_en)
        {
            std::string en = safe_string(p.translations, "title");
            if (!en.empty()) title = en;
            en = safe_string(p.translations, "background");
            if (!en.empty()) background = en;
            en = safe_string(p.translations, "description");
            if (!en.empty()) description = en;
            en = safe_string(p.translations, "inputFormat");
            if (!en.empty()) formatI = en;
            en = safe_string(p.translations, "outputFormat");
            if (!en.empty()) formatO = en;
            en = safe_string(p.translations, "hint");
            if (!en.empty()) hint = en;
        }

        std::fputs("---\n\n", out);
        std::fprintf(out, "# %s %s\n\n", p.pid.c_str(), title.c_str());

        if (show_difficulty)
            std::fprintf(out, "难度：%s\n\n", luogu::difficulty_label(p.difficulty));

        // 标签：--show 末位为 0 时仅隐藏“算法”类（type 2）标签，其余类型始终显示
        std::vector<std::string> shown_tags = luogu::filter_display_tags(p.tags, show_tags);
        if (!shown_tags.empty())
            std::fprintf(out, "标签：%s\n\n", join_strings(shown_tags, "、").c_str());

        // 时空限制：多组限制输出最小-最大范围
        const pss limits = luogu::format_limits(p.time, p.memory);
        const std::string limits_text = "时间限制: " + limits.first + "\\ \n内存限制: " + limits.second + " \n\n";

        if (!limits_text.empty())
            std::fprintf(out, "%s\n\n", limits_text.c_str());

        if (!background.empty())
        {
            std::fputs("## 题目背景\n\n", out);
            std::fputs(background.c_str(), out);
            std::fputs("\n\n", out);
        }
        if (!description.empty())
        {
            std::fputs("## 题目描述\n\n", out);
            std::fputs(description.c_str(), out);
            std::fputs("\n\n", out);
        }
        if (!formatI.empty())
        {
            std::fputs("## 输入格式\n\n", out);
            std::fputs(formatI.c_str(), out);
            std::fputs("\n\n", out);
        }
        if (!formatO.empty())
        {
            std::fputs("## 输出格式\n\n", out);
            std::fputs(formatO.c_str(), out);
            std::fputs("\n\n", out);
        }

        int sample_no = 1;
        for (const auto &s : p.samples)
        {
            std::fprintf(out, "## 输入输出样例 #%d\n\n", sample_no);
            std::fprintf(out, "### 输入 #%d\n\n```\n", sample_no);
            std::fputs(s.first.c_str(), out);
            std::fputs("```\n\n", out);
            std::fprintf(out, "### 输出 #%d\n\n```\n", sample_no);
            std::fputs(s.second.c_str(), out);
            std::fputs("```\n\n", out);
            ++sample_no;
        }

        if (!hint.empty())
        {
            std::fputs("## 说明/提示\n\n", out);
            std::fputs(hint.c_str(), out);
            std::fputs("\n\n", out);
        }
    }

    if (std::ferror(out))
    {
        std::fclose(out);
        error = "写入输出文件 '" + output_path.string() + "' 失败";
        return false;
    }
    std::fclose(out);
    return true;
}
