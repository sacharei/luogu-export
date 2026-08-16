// src/main.cpp
#include <getopt.h>
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/export/common.h"
#include "luogu-export/export/latex.h"
#include "luogu-export/export/markdown.h"
#include "luogu-export/util/tag_cache.h"

namespace
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

// 只作为长选项使用的选项码（getopt_long 返回值）
enum
{
    OPT_TAG = 1000,
    OPT_DIFFICULTY,
    OPT_OUTPUT,
    OPT_TYPE,
    OPT_LANG,
    OPT_SHOW,
    OPT_TAGS,
};

const char *kUsage =
    "Usage: luogu-export [options]\n"
    "\n"
    "Options:\n"
    "  -U, --update    Update the problem list and tag caches\n"
    "  -M, --markdown  Export problems to a markdown file (all problems if no filter given)\n"
    "  -L, --latex     Export problems to a LaTeX document file (all problems if no filter given)\n"
    "      --tags              List all tags with their numeric IDs, grouped by category\n"
    "                          (an info command like -h; can be combined: -h --tags)\n"
    "      --tag <name|ID>...  Filter by tag; multiple values may be separated by spaces\n"
    "                          or by repeating --tag (a problem must contain all given tags)\n"
    "                          A quoted value that exactly matches a known tag name\n"
    "                          (e.g. \"NOIP 普及组\") is treated as one tag; otherwise\n"
    "                          spaces separate multiple tags\n"
    "      --difficulty <spec> Filter by difficulty: numbers 0-8, ranges like 1-4,\n"
    "                          separated by spaces or by repeating --difficulty (any match is enough)\n"
    "      --type <B|P>        Filter by problem type (repeatable; empty means all types)\n"
    "      --lang <zh-CN|en>   Problem statement language (default: zh-CN)\n"
    "      --show <NN>         Show flags for -M only: first bit = difficulty, second bit = tags;\n"
    "                          1 shows, 0 hides (default: 11). Hiding tags only hides\n"
    "                          algorithm-type tags, other types are always shown\n"
    "      --output <file>     Output file (default: problems.md / problems.tex)\n"
    "  -h, --help      Show this help message\n";

void printUsage()
{
    std::printf("%s", kUsage);
}

inline void printError(const std::string &message)
{
    std::fprintf(stderr, "\033[1;31merror: \033[0m%s\n", message.c_str());
}

inline void printSuccess(const std::string &message)
{
    std::printf("\033[1;32m%s\033[0m\n", message.c_str());
}

// 按空白把字符串拆成多个 token（用于 --tag 模拟 贪心 这类写法）
inline std::vector<std::string> split_whitespace(const std::string &s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        const size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i > start)
            out.push_back(s.substr(start, i - start));
    }
    return out;
}

// 解析难度规格："N"（单个数字）或 "A-B"（闭区间），展开后追加到 difficulties
inline bool parse_difficulty_spec(const std::string &spec, std::vector<int> &difficulties)
{
    auto parse_num = [](const std::string &s, long &out) -> bool {
        if (s.empty())
            return false;
        char *end = nullptr;
        out = std::strtol(s.c_str(), &end, 10);
        if (end == s.c_str() || *end != '\0' || out < 0 || out > 8)
            return false;
        return true;
    };

    const size_t dash = spec.find('-');
    if (dash == std::string::npos)
    {
        long v = 0;
        if (!parse_num(spec, v))
            return false;
        difficulties.push_back(static_cast<int>(v));
        return true;
    }

    // 区间 A-B（不允许再出现第二个 '-'，如 "1-2-3"）
    const std::string a = spec.substr(0, dash);
    const std::string b = spec.substr(dash + 1);
    if (b.find('-') != std::string::npos)
        return false;

    long lo = 0, hi = 0;
    if (!parse_num(a, lo) || !parse_num(b, hi))
        return false;
    if (lo > hi)
        return false;
    for (long v = lo; v <= hi; ++v)
        difficulties.push_back(static_cast<int>(v));
    return true;
}

// --tags：按官方分类（type）打印标签 ID 对照表
inline bool print_tag_list()
{
    if (!tagcache::shared_cache_loaded())
    {
        printError("找不到标签缓存 tags.json，请先运行 -U 更新缓存");
        return false;
    }
    const tagcache::Cache &cache = tagcache::shared_cache();

    static const char *kTypeLabels[] = {
        "未知",         // 0
        "地区/赛区",    // 1
        "算法与技巧",   // 2
        "竞赛来源",     // 3
        "年份",         // 4
        "特殊题目属性", // 5
        "旧版标签",     // 6
    };
    const int kKnownTypes = 7;

    std::vector<std::vector<int>> groups(kKnownTypes + 1);
    for (const auto &kv : cache.id_to_name)
    {
        int type = 0;
        auto it = cache.name_to_type.find(kv.second);
        if (it != cache.name_to_type.end())
            type = it->second;
        if (type < 0 || type >= kKnownTypes)
            type = kKnownTypes;
        groups[type].push_back(kv.first);
    }

    std::printf("洛谷标签 ID 对照表（共 %zu 个）\n\n", cache.id_to_name.size());
    for (int g = 0; g <= kKnownTypes; ++g)
    {
        if (groups[g].empty())
            continue;
        std::sort(groups[g].begin(), groups[g].end());
        const char *label = (g == kKnownTypes) ? "其他" : kTypeLabels[g];
        std::printf("【%s】(type %d) %zu 个\n", label, g, groups[g].size());
        for (int id : groups[g])
            std::printf("  %-6d %s\n", id, cache.id_to_name.at(id).c_str());
        std::printf("\n");
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // 长选项表：以后新增参数时在这里加一项，并在下方 switch 中处理。
    static const struct option kLongOptions[] = {
        {"update",     no_argument,       nullptr, 'U'},
        {"markdown",   no_argument,       nullptr, 'M'},
        {"latex",      no_argument,       nullptr, 'L'},
        {"tag",        required_argument, nullptr, OPT_TAG},
        {"difficulty", required_argument, nullptr, OPT_DIFFICULTY},
        {"output",     required_argument, nullptr, OPT_OUTPUT},
        {"type",       required_argument, nullptr, OPT_TYPE},
        {"lang",       required_argument, nullptr, OPT_LANG},
        {"show",       required_argument, nullptr, OPT_SHOW},
        {"tags",       no_argument,       nullptr, OPT_TAGS},
        {"help",       no_argument,       nullptr, 'h'},
        {nullptr,      0,                 nullptr, 0},
    };

    Options options;
    int opt;
    while ((opt = getopt_long(argc, argv, "UMLh", kLongOptions, nullptr)) != -1)
    {
        switch (opt)
        {
        case 'U':
            options.update = true;
            break;
        case 'M':
            options.markdown = true;
            break;
        case 'L':
            options.latex = true;
            break;
        case OPT_TAG:
            // 先整体保留，具体按一个标签还是按空格拆分，交给 select_problems
            // 结合 tags.json 判断：整体是已知标签名（如 "NOIP 普及组"）就按一个，
            // 否则按空格拆成多个（如 --tag "模拟 贪心"）
            options.filter.tags.push_back(optarg);
            break;
        case OPT_DIFFICULTY:
            // 支持空格分隔的多个难度（如 --difficulty 1 3-5）
            for (const auto &spec : split_whitespace(optarg))
            {
                if (!parse_difficulty_spec(spec, options.filter.difficulties))
                {
                    printError("invalid difficulty spec: '" + spec + "' (expected 0-8 or a range like 1-4)");
                    return 1;
                }
            }
            break;
        case OPT_OUTPUT:
            options.output = optarg;
            break;
        case OPT_TYPE:
        {
            std::string t = optarg;
            for (auto &c : t)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (t != "B" && t != "P")
            {
                printError("invalid type: '" + std::string(optarg) + "' (expected B or P)");
                return 1;
            }
            options.filter.types.push_back(t);
            break;
        }
        case OPT_LANG:
        {
            std::string lang = optarg;
            if (lang != "zh-CN" && lang != "zh" && lang != "en")
            {
                printError("invalid lang: '" + std::string(optarg) + "' (expected zh-CN or en)");
                return 1;
            }
            options.filter.lang = lang;
            break;
        }
        case OPT_SHOW:
        {
            const std::string s = optarg;
            options.show_explicit = true;
            if (s.size() != 2 ||
                (s[0] != '0' && s[0] != '1') ||
                (s[1] != '0' && s[1] != '1'))
            {
                printError("invalid show: '" + std::string(optarg) + "' (expected two bits, e.g. 11 / 01 / 10 / 00)");
                return 1;
            }
            options.filter.show = s;
            break;
        }
        case OPT_TAGS:
            options.list_tags = true;
            break;
        case 'h':
            options.help = true;
            break;
        default: // getopt_long 已输出具体错误信息
            printUsage();
            return 1;
        }
    }

    if (options.help)
    {
        printUsage();
        return options.list_tags ? (print_tag_list() ? 0 : 1) : 0;
    }

    if (options.list_tags)
        return print_tag_list() ? 0 : 1;

    if (options.latex && options.show_explicit)
    {
        printError("option --show is only supported with -M; -L always hides difficulty and tags");
        return 1;
    }

    if (optind < argc)
    {
        if (!options.markdown && !options.latex)
        {
            printError("unexpected argument: " + std::string(argv[optind]));
            printUsage();
            return 1;
        }

        // -M/-L 模式下，剩余裸参数按难度解析；解析不了则当作 --tag 的后续值，
        // 从而同时支持 "--difficulty 1 2 3" 与 "--tag 模拟 贪心" 两种空格分隔写法
        for (int i = optind; i < argc; ++i)
        {
            std::vector<int> tmp = options.filter.difficulties;
            if (parse_difficulty_spec(argv[i], tmp))
                options.filter.difficulties = std::move(tmp);
            else
                options.filter.tags.push_back(argv[i]);
        }
    }

    if (!options.update && !options.markdown && !options.latex)
    {
        printError("no operation specified (use -h for help)");
        return 1;
    }

    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
    {
        printError("failed to initialize libcurl");
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
            printSuccess("Exported matching problems to '" + out_path + "'");
        else
        {
            printError(error);
            result = 1;
        }
    }
    else if (options.latex)
    {
        const std::string out_path = options.output.empty() ? "problems.tex" : options.output;
        std::string error;
        if (!latex::export_latex(options.filter, out_path, error))
        {
            printError(error);
            result = 1;
        }
    }

    curl_global_cleanup();
    return result;
}
