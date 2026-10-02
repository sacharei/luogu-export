// include/luogu-export/cli/options.h
#ifndef LUOGU_EXPORT_CLI_OPTIONS_H
#define LUOGU_EXPORT_CLI_OPTIONS_H

#include <string>
#include <vector>
#include "luogu-export/export/common.h"

// 命令行参数的定义与解析
namespace cli
{
    // 程序运行参数。以后新增参数时在这里添加字段。
    struct Options
    {
        bool update = false;    // -U, --update
        bool markdown = false;  // -M, --markdown
        bool latex = false;     // -L, --latex
        bool list_tags = false; // --tags
        bool help = false;      // -h, --help
        bool show_explicit = false; // 是否显式给了 --show
        std::string output;     // --output（空则按模式取默认 problems.md / problems.tex）
        luogu::ExportFilter filter; // -M / -L 共用的筛选条件
    };

    // 解析命令行参数。
    // @return true  解析成功，options 已填好，可以继续执行
    //         false 程序应立即结束，退出码放在 exit_code 中
    //               （-h / --tags 等“信息命令”也走这里，exit_code 为 0）
    bool parse(int argc, char *argv[], Options &options, int &exit_code);

    // 带颜色的错误/成功信息
    void printError(const std::string &message);
    void printSuccess(const std::string &message);
}

#endif // LUOGU_EXPORT_CLI_OPTIONS_H
