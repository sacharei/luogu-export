// include/luogu-export/export/markdown.h
#ifndef LUOGU_EXPORT_MARKDOWN_H
#define LUOGU_EXPORT_MARKDOWN_H

#include <filesystem>
#include <string>
#include "luogu-export/export/common.h"

namespace markdown
{
    // 读取缓存的题目列表（latest.ndjson），按条件筛选后合并成一个 markdown 文件
    // @param filter      筛选条件（与 -L 共用）
    // @param output_path 输出文件路径
    // @param error       失败时返回的错误信息
    // @return 成功返回 true
    bool export_markdown(const luogu::ExportFilter &filter,
                         const std::filesystem::path &output_path, std::string &error);
}

#endif // LUOGU_EXPORT_MARKDOWN_H
