// src/cli/options.cpp
// 命令行参数解析（见 include/luogu-export/cli/options.h）。
#include "luogu-export/cli/options.h"

#include <getopt.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include "luogu-export/export/common.h"
#include "luogu-export/util/settings.h"
#include "luogu-export/util/string_util.h"
#include "luogu-export/util/tag_cache.h"

namespace cli
{
namespace
{

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
    "      --show <NN>         Without -O: show flags for -M only; first bit = difficulty,\n"
    "                          second bit = tags; 1 shows, 0 hides (default: 11)\n"
    "                          Hiding tags only hides algorithm-type tags, other types\n"
    "                          are always shown\n"
    "      --output <file>     Output file (default: from default_title, e.g. problems.md)\n"
    "  -O, --config    Configuration mode (fonts and export defaults); use with one of:\n"
    "      --list              Print every key, its current value and a description\n"
    "      --show <key>        Print the current value of one key\n"
    "      --set <key> <value> [<key> <value> ...]\n"
    "                          Set keys (pairs of key and value). A font value may be a\n"
    "                          font name, another font key, or \"default\"\n"
    "  -h, --help      Show this help message\n";

void printUsage()
{
    std::printf("%s", kUsage);
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

void printError(const std::string &message)
{
    std::fprintf(stderr, "\033[1;31merror: \033[0m%s\n", message.c_str());
}

void printSuccess(const std::string &message)
{
    std::printf("\033[1;32m%s\033[0m\n", message.c_str());
}

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
    OPT_SET,
    OPT_LIST,
};

bool parse(int argc, char *argv[], Options &options, int &exit_code)
{
    exit_code = 0;

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
        {"set",        required_argument, nullptr, OPT_SET},
        {"list",       no_argument,       nullptr, OPT_LIST},
        {"help",       no_argument,       nullptr, 'h'},
        {nullptr,      0,                 nullptr, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "UMLOh", kLongOptions, nullptr)) != -1)
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
        case 'O':
            options.config = true;
            break;
        case OPT_TAG:
            // 先整体保留，具体按一个标签还是按空格拆分，交给 select_problems
            // 结合 tags.json 判断：整体是已知标签名（如 "NOIP 普及组"）就按一个，
            // 否则按空格拆成多个（如 --tag "模拟 贪心"）
            options.filter.tags.push_back(optarg);
            break;
        case OPT_DIFFICULTY:
            // 支持空格分隔的多个难度（如 --difficulty 1 3-5）
            for (const auto &spec : util::split_whitespace(optarg))
            {
                if (!parse_difficulty_spec(spec, options.filter.difficulties))
                {
                    printError("invalid difficulty spec: '" + spec + "' (expected 0-8 or a range like 1-4)");
                    { exit_code = 1; return false; }
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
                { exit_code = 1; return false; }
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
                { exit_code = 1; return false; }
            }
            options.filter.lang = lang;
            break;
        }
        case OPT_SHOW:
            // --show 有两种含义（-M 的显示选项 / -O 的查询 key），
            // 与选项顺序无关，统一等解析完再判断
            options.show_given = true;
            options.show_arg = optarg;
            break;
        case OPT_TAGS:
            options.list_tags = true;
            break;
        case OPT_SET:
            options.config_set = true;
            options.config_pairs.emplace_back(optarg);
            break;
        case OPT_LIST:
            options.config_list = true;
            break;
        case 'h':
            options.help = true;
            break;
        default: // getopt_long 已输出具体错误信息
            printUsage();
            { exit_code = 1; return false; }
        }
    }

    // --show 的含义取决于有没有 -O
    if (options.show_given)
    {
        if (options.config)
        {
            options.config_show = true;
            options.config_key = options.show_arg;
        }
        else
        {
            const std::string &s = options.show_arg;
            options.show_explicit = true;
            if (s.size() != 2 ||
                (s[0] != '0' && s[0] != '1') ||
                (s[1] != '0' && s[1] != '1'))
            {
                printError("invalid show: '" + s + "' (expected two bits, e.g. 11 / 01 / 10 / 00)");
                { exit_code = 1; return false; }
            }
            options.filter.show = s;
        }
    }

    if (options.help)
    {
        printUsage();
        { exit_code = options.list_tags ? (print_tag_list() ? 0 : 1) : 0; return false; }
    }

    if (options.list_tags)
        { exit_code = print_tag_list() ? 0 : 1; return false; }

    // -O：字体配置模式。--set 的第二个及以后的参数由 getopt 留在 optind 之后，
    // 这里一并收进来（它们不是待筛选的题目参数）。
    if (options.config && options.config_set && optind < argc)
    {
        for (int i = optind; i < argc; ++i)
            options.config_pairs.emplace_back(argv[i]);
        optind = argc;
    }

    if (options.config)
    {
        if (options.markdown || options.latex || options.update)
        {
            printError("option -O cannot be combined with -U, -M or -L");
            { exit_code = 1; return false; }
        }
        const int selected = (options.config_list ? 1 : 0) +
                             (options.config_show ? 1 : 0) +
                             (options.config_set ? 1 : 0);
        if (selected == 0)
        {
            printError("option -O needs one of --list, --show <key> or --set <key> <value>");
            { exit_code = 1; return false; }
        }
        if (selected > 1)
        {
            printError("options --list, --show and --set cannot be combined");
            { exit_code = 1; return false; }
        }
        if (options.config_show && !settings::has_key(options.config_key))
        {
            printError("unknown key: '" + options.config_key + "' (use -O --list to see all keys)");
            { exit_code = 1; return false; }
        }
        if (options.config_set)
        {
            if (options.config_pairs.size() < 2)
            {
                printError("--set expects at least one key/value pair");
                { exit_code = 1; return false; }
            }
            if (options.config_pairs.size() % 2 != 0)
            {
                printError("--set expects an even number of arguments (key value [key value ...])");
                { exit_code = 1; return false; }
            }
            for (size_t i = 0; i + 1 < options.config_pairs.size(); i += 2)
            {
                if (!settings::has_key(options.config_pairs[i]))
                {
                    printError("unknown key: '" + options.config_pairs[i] +
                               "' (use -O --list to see all keys)");
                    { exit_code = 1; return false; }
                }
            }
        }
        // 校验通过：交给 main 执行（与 -U 一样属于“要真正干活”的模式）
        return true;
    }

    if (options.latex && options.show_explicit)
    {
        printError("option --show is only supported with -M; -L always hides difficulty and tags");
        { exit_code = 1; return false; }
    }

    if (optind < argc)
    {
        if (!options.markdown && !options.latex)
        {
            printError("unexpected argument: " + std::string(argv[optind]));
            printUsage();
            { exit_code = 1; return false; }
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

    if (!options.update && !options.markdown && !options.latex && !options.config)
    {
        printError("no operation specified (use -h for help)");
        { exit_code = 1; return false; }
    }

    return true;
}
} // namespace cli
