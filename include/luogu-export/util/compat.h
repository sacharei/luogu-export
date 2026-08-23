// include/luogu-export/util/compat.h
// 跨平台（Windows / macOS / Linux）兼容性工具：
// - Windows 的 CRT fopen/gzopen/getenv 按 ANSI 代码页解释窄字符，
//   这里统一提供按 UTF-8 处理路径的封装；
// - POSIX getline 在 MSVC 上不存在，这里提供等价实现；
// - Windows 传统控制台默认不解析 ANSI 转义序列，这里提供初始化封装。
#ifndef LUOGU_EXPORT_UTIL_COMPAT_H
#define LUOGU_EXPORT_UTIL_COMPAT_H

#include <cstdio>
#include <filesystem>
#include <string>
#include <zlib.h>

namespace luogu
{
namespace compat
{
    // 用 UTF-8 路径打开文件。
    // Windows 下把路径转成宽字符后调用 _wfopen（中文路径可用）；
    // 其他平台直接透传 std::fopen。
    FILE *fopen(const std::filesystem::path &path, const char *mode);

    // zlib 的 gzopen 同样存在窄字符路径问题；Windows 下用 gzopen_w。
    gzFile gzopen(const std::filesystem::path &path, const char *mode);

    // 把 UTF-8 字符串转换为 filesystem::path。
    // Windows 下直接按窄字符构造会按 ANSI 代码页解释字节，必须经 u8path
    // （本工程为 C++17，u8path 可用）。
    inline std::filesystem::path path_from_utf8(const std::string &utf8)
    {
#ifdef _WIN32
        return std::filesystem::u8path(utf8);
#else
        return std::filesystem::path(utf8);
#endif
    }

    // 读取环境变量（返回 UTF-8）。
    // Windows 下用 GetEnvironmentVariableW 再转 UTF-8（getenv 按 ANSI 解释）；
    // 其他平台直接 std::getenv。
    std::string getenv_utf8(const char *name);

    // 从 FILE* 读取一行（结果不含末尾换行符），替代 POSIX getline。
    // 返回读取到的字符数；文件结束且未读到任何内容时返回 -1。
    // 语义与 getline + 去掉末尾 '\n' 一致。
    long long read_line(FILE *in, std::string &out);

    // 初始化控制台输出。Windows 传统控制台默认不解析 ANSI 转义序列
    // （彩色、\033[K 清行、\033[s/\033[u 光标保存恢复等会乱码），
    // 这里启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING；输出被重定向（非控制台）
    // 或启用失败时保持原样。其他平台为空操作。
    void init_console();
} // namespace compat
} // namespace luogu

#endif // LUOGU_EXPORT_UTIL_COMPAT_H
