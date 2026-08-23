// include/luogu-export/util/getopt_compat.h
// MSVC 没有 POSIX <getopt.h>，本头文件提供本程序所需的 getopt / getopt_long
// 最小实现，语义与 GNU getopt 保持一致（针对本程序用到的行为）：
//   - optstring 以 ':' 开头时不打印错误：未知选项返回 '?'，缺少参数返回 ':'
//   - 支持短选项簇（如 -UML）、长选项 --name、--name=value、--name value
//   - 支持长选项的无歧义前缀缩写（与 GNU getopt 一致）
//   - 出错时 argv[optind - 1] 指向出错的 token
//   - 必选参数会吞掉下一个 token，即使它以 '-' 开头（与 GNU 一致）
//   - 实现 GNU 式参数重排（permutation）：选项与裸参数可以混排，
//     裸参数保持原有相对顺序被挪到 argv 末尾，getopt 返回 -1 后
//     argv[optind..argc) 即全部裸参数
//   - "--" 之后的内容全部视为裸参数
// 本头文件仅在 Windows + MSVC 构建时使用；其余平台用系统 <getopt.h>。
// LUOGU_FORCE_COMPAT_GETOPT 用于在非 Windows 平台强制测试本实现。
#ifndef LUOGU_EXPORT_UTIL_GETOPT_COMPAT_H
#define LUOGU_EXPORT_UTIL_GETOPT_COMPAT_H

#include <cstring>

#define no_argument 0
#define required_argument 1
#define optional_argument 2

struct option
{
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

// 与 glibc/POSIX 相同的 C 链接：非 Windows 平台强制测试本实现时，
// 系统头文件（如 unistd.h）也会声明这些符号，C 链接可以保证兼容。
extern "C"
{

inline char *optarg = nullptr;
inline int optind = 1;
inline int opterr = 1;
inline int optopt = 0;

// GNU 式重排：把 argv[from..to) 这段裸参数整体挪到 argv 末尾的
// 裸参数区（[nonopt_tail, argc)），保持相对顺序；nonopt_tail 随之更新。
// 实现为逐个冒泡，命令行参数规模下代价可忽略。
// 与 glibc 相同：只交换指针本身，不修改字符串内容，因此先去掉
// char *const 的指针常量限定。
inline void getopt_permute(char *const argv[], int from, int to, int &nonopt_tail)
{
    char **av = const_cast<char **>(argv);
    const int len = to - from;
    for (int i = 0; i < len; ++i)
    {
        char *tmp = av[from];
        for (int j = from; j < nonopt_tail - 1; ++j)
            av[j] = av[j + 1];
        av[nonopt_tail - 1] = tmp;
        --nonopt_tail;
    }
}

inline int getopt_long(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longindex) noexcept
{
    // 当前短选项 token 内的解析位置（等价于 GNU getopt 的 nextchar）
    static int cluster_pos = 1;
    // 末尾裸参数区的起始下标（首次调用时初始化为 argc）
    static int nonopt_tail = -1;
    // 已遇到 "--"，其后全部视为裸参数
    static bool after_dashdash = false;

    if (longindex)
        *longindex = -1;
    optarg = nullptr;

    if (nonopt_tail < 0)
        nonopt_tail = argc;

    if (optind >= argc)
        return -1;

    const bool colon_mode = (optstring[0] == ':');

    // "--" 之后的内容都是裸参数：直接结束，交给调用方处理
    if (after_dashdash)
    {
        cluster_pos = 1;
        return -1;
    }

    // 裸参数（或单独的 "-"）：GNU 式重排——把这段裸参数挪到 argv 末尾，
    // 继续解析后面的选项；后面没有选项时结束解析
    if (argv[optind][0] != '-' || argv[optind][1] == '\0')
    {
        int j = optind + 1;
        while (j < argc && !(argv[j][0] == '-' && argv[j][1] != '\0'))
            ++j;
        if (j >= argc)
        {
            cluster_pos = 1;
            return -1; // 后面没有选项：剩余内容都是裸参数
        }
        getopt_permute(argv, optind, j, nonopt_tail);
    }

    const char *token = argv[optind];

    // ---- 长选项 ----
    if (token[1] == '-')
    {
        cluster_pos = 1;
        if (token[2] == '\0')
        {
            after_dashdash = true;
            ++optind; // "--"：其后的内容都是裸参数
            return -1;
        }

        const char *name = token + 2;
        const char *eq = std::strchr(name, '=');
        const size_t name_len = eq ? static_cast<size_t>(eq - name)
                                   : std::strlen(name);

        const struct option *match = nullptr;
        int match_index = -1;
        int match_count = 0;
        if (longopts)
        {
            for (int i = 0; longopts[i].name; ++i)
            {
                if (std::strncmp(longopts[i].name, name, name_len) == 0)
                {
                    ++match_count;
                    match = &longopts[i];
                    match_index = i;
                }
            }
        }

        if (match_count == 0)
        {
            optopt = 0;
            ++optind;
            return '?'; // 未知长选项
        }
        if (match_count > 1)
        {
            // 前缀缩写有歧义：仅当存在完全相等项时才接受
            match = nullptr;
            match_index = -1;
            for (int i = 0; longopts[i].name; ++i)
            {
                if (std::strcmp(longopts[i].name, name) == 0)
                {
                    match = &longopts[i];
                    match_index = i;
                    break;
                }
            }
            if (!match)
            {
                optopt = 0;
                ++optind;
                return '?';
            }
        }

        switch (match->has_arg)
        {
        case no_argument:
            if (eq)
            {
                optopt = 0;
                ++optind;
                return '?'; // 该选项不接受参数
            }
            ++optind;
            break;
        case required_argument:
            if (eq)
            {
                optarg = const_cast<char *>(eq + 1);
                ++optind;
            }
            else if (optind + 1 < argc)
            {
                // 与 GNU getopt 一致：下一个 token 无论是否以 '-' 开头
                // 都作为该选项的参数
                ++optind;
                optarg = argv[optind];
                ++optind;
            }
            else
            {
                ++optind;
                return colon_mode ? ':' : '?';
            }
            break;
        case optional_argument:
            optarg = eq ? const_cast<char *>(eq + 1) : nullptr;
            ++optind;
            break;
        }

        if (longindex)
            *longindex = match_index;
        if (match->flag)
        {
            *match->flag = match->val;
            return 0;
        }
        return match->val;
    }

    // ---- 短选项（支持簇，如 -UML） ----
    for (;;)
    {
        const char *c = token + cluster_pos;
        if (*c == '\0')
        {
            // 当前 token 解析完毕，前进到下一个 token
            ++optind;
            cluster_pos = 1;
            return getopt_long(argc, argv, optstring, longopts, longindex);
        }
        const char *found =
            std::strchr(optstring, static_cast<unsigned char>(*c));
        if (!found)
        {
            optopt = static_cast<unsigned char>(*c);
            ++optind;
            cluster_pos = 1;
            return '?'; // 未知短选项
        }
        if (found[1] == ':')
        {
            // 需要参数：优先取簇内剩余部分
            if (c[1] != '\0')
            {
                optarg = const_cast<char *>(c + 1);
                ++optind;
                cluster_pos = 1;
            }
            else if (optind + 1 < argc)
            {
                optarg = argv[optind + 1];
                optind += 2;
                cluster_pos = 1;
            }
            else
            {
                optopt = static_cast<unsigned char>(*c);
                ++optind;
                cluster_pos = 1;
                return colon_mode ? ':' : '?';
            }
            return static_cast<unsigned char>(*found);
        }
        ++cluster_pos;
        return static_cast<unsigned char>(*found);
    }
}

inline int getopt(int argc, char *const argv[], const char *optstring) noexcept
{
    return getopt_long(argc, argv, optstring, nullptr, nullptr);
}

} // extern "C"

#endif // LUOGU_EXPORT_UTIL_GETOPT_COMPAT_H
