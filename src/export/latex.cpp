// src/export/latex.cpp
// 导出入口：读取缓存、筛选题目、写出完整的 LaTeX 文档。
// 具体的 markdown/题目/文章转换与各类工具分别位于 src/export/latex/ 下。
#include "luogu-export/contents/problem.h"
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/export/common.h"
#include "luogu-export/export/latex.h"
#include "luogu-export/export/latex/image.h"
#include "luogu-export/export/latex/preamble.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace latex
{
using namespace detail;

bool export_latex(const luogu::ExportFilter &filter,
                  const std::filesystem::path &output_path,
                  std::string &error)
{
    error.clear();

    // 筛选（-M / -L 共用），结果已按题号排序
    std::vector<problem::Problem> problems;
    std::vector<std::string> resolved_tags;
    if (!luogu::select_problems(filter, problems, &resolved_tags, error))
        return false;

    if(problems.empty())
    {
        error = "No problems matched the filter criteria.";
        return false;
    }
    
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
        error = "Cannot open output file '" + output_path.string() + "'";
        return false;
    }

    write_preamble(out);

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
        // total 为 0（筛选无结果）时避免除零
        if (total > 0)
        {
            std::printf("\rExporting: %3d %% (%d/%d). ", cnt * 100 / total, cnt, total);
            fflush(stdout);
        }
    }
    if (total > 0)
        std::printf("\rExporting: %3d %% (%d/%d), done.\n", cnt * 100 / total, cnt, total);
    fflush(stdout);
    if (total == 0)
        std::printf("Note: no problems matched the filter; an empty document was written.\n");

    std::fputs("\\end{document}\n", out);
    if (std::ferror(out))
    {
        std::fclose(out);
        error = "Failed to write to output file '" + output_path.string() + "'";
        return false;
    }
    std::fclose(out);
    return true;
}
} // namespace latex
