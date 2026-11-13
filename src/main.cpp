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
#include "luogu-export/util/settings.h"

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

    if (options.config)
    {
        // -O：只操作配置文件，不导出任何东西
        if (options.config_list)
        {
            settings::Values values;
            settings::load(values);
            settings::print_list(values);
            result = 0;
        }
        else if (options.config_show)
        {
            settings::Values values;
            settings::load(values);
            std::printf("%s\n", settings::raw_value(values, options.config_key).c_str());
            result = 0;
        }
        else if (options.config_set)
        {
            std::vector<std::pair<std::string, std::string>> entries;
            for (size_t i = 0; i + 1 < options.config_pairs.size(); i += 2)
                entries.emplace_back(options.config_pairs[i], options.config_pairs[i + 1]);
            std::string error;
            if (settings::set(entries, error))
            {
                for (const auto &entry : entries)
                    std::printf("%s: %s\n", entry.first.c_str(), entry.second.c_str());
                std::printf("saved to %s\n", settings::config_path().string().c_str());
                result = 0;
            }
            else
            {
                cli::printError(error);
                result = 1;
            }
        }
        curl_global_cleanup();
        return result;
    }

    if (options.markdown)
    {
        std::vector<settings::Problem> problems;
        const std::string out_path = options.output.empty()
                                         ? settings::default_output_name(".md", &problems)
                                         : options.output;
        settings::print_warnings(problems);
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
        std::vector<settings::Problem> problems;
        const std::string out_path = options.output.empty()
                                         ? settings::default_output_name(".tex", &problems)
                                         : options.output;
        settings::print_warnings(problems);
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
