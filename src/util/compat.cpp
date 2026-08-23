// src/util/compat.cpp
#include "luogu-export/util/compat.h"

#include <cstring>
#include <cwchar>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace luogu
{
namespace compat
{

FILE *fopen(const std::filesystem::path &path, const char *mode)
{
#ifdef _WIN32
    std::wstring wmode;
    for (const char *p = mode; *p; ++p)
        wmode += static_cast<wchar_t>(static_cast<unsigned char>(*p));
    return _wfopen(path.c_str(), wmode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

gzFile gzopen(const std::filesystem::path &path, const char *mode)
{
#ifdef _WIN32
    std::wstring wmode;
    for (const char *p = mode; *p; ++p)
        wmode += static_cast<wchar_t>(static_cast<unsigned char>(*p));
    return gzopen_w(path.c_str(), wmode.c_str());
#else
    return ::gzopen(path.c_str(), mode);
#endif
}

std::string getenv_utf8(const char *name)
{
#ifdef _WIN32
    std::wstring wname;
    for (const char *p = name; *p; ++p)
        wname += static_cast<wchar_t>(static_cast<unsigned char>(*p));

    const DWORD need = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (need == 0)
        return "";
    std::wstring buffer(need, L'\0');
    const DWORD got = GetEnvironmentVariableW(wname.c_str(), buffer.data(), need);
    if (got == 0 || got > need)
        return "";
    buffer.resize(got);

    const int len = WideCharToMultiByte(CP_UTF8, 0, buffer.data(),
                                        static_cast<int>(buffer.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return "";
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer.data(),
                        static_cast<int>(buffer.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
#else
    const char *value = std::getenv(name);
    return value ? value : "";
#endif
}

long long read_line(FILE *in, std::string &out)
{
    out.clear();
    char buffer[65536];
    while (std::fgets(buffer, sizeof(buffer), in))
    {
        const size_t n = std::strlen(buffer);
        out.append(buffer, n);
        if (n > 0 && buffer[n - 1] == '\n')
        {
            out.pop_back();
            return static_cast<long long>(out.size());
        }
    }
    // 文件结束：最后一行为内容但无换行符时，返回该行
    return out.empty() ? -1 : static_cast<long long>(out.size());
}

void init_console()
{
#ifdef _WIN32
    // 启用虚拟终端处理：让传统 Windows 控制台正确渲染 ANSI 转义序列。
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!handle || handle == INVALID_HANDLE_VALUE)
        return;
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode))
        return; // 输出被重定向（非控制台）时保持原样
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(handle, mode);
#else
    (void)0;
#endif
}

} // namespace compat
} // namespace luogu
