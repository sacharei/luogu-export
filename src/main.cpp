// src/main.cpp
// 程序入口：解析命令行参数，然后按模式执行更新 / markdown 导出 / LaTeX 导出。
#include <curl/curl.h>
#include <cstdio>
#include <string>
#include "luogu-export/cli/options.h"
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/export/common.h"
#include "luogu-export/export/latex.h"
#include "luogu-export/export/markdown.h"

int main(int argc, char *argv[])
{
    cli::Options options;
    int exit_code = 0;
    if (!cli::parse(argc, argv, options, exit_code))
        return exit_code;

    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
    {
        cli::printError("failed to initialize libcurl");
        return 1;
    }

    int result = 0;
    if (options.update)
        result = crawler::update();

    if (options.markdown)
    {
        const std::string out_path = options.output.empty() ? "problems.md" : options.output;
        std::string error;
        if (markdown::export_markdown(options.filter, out_path, error))
            cli::printSuccess("Exported matching problems to '" + out_path + "'");
        else
        {
            cli::printError(error);
            result = 1;
        }
    }
    else if (options.latex)
    {
        const std::string out_path = options.output.empty() ? "problems.tex" : options.output;
        std::string error;
        if (!latex::export_latex(options.filter, out_path, error))
        {
            cli::printError(error);
            result = 1;
        }
    }

    curl_global_cleanup();
    return result;
}
