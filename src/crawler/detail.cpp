// src/crawler/detail.cpp
// crawler 内部共享的小工具（见 detail.h）。
#include "luogu-export/crawler/detail.h"

#include <cstdio>
#include <string>

namespace crawler::detail
{
const char *kColorReset  = "\033[0m";
const char *kColorRed    = "\033[1;31m";
const char *kColorGreen  = "\033[1;32m";

void print_error(const std::string &message)
{
    fflush(stdout);
    std::fprintf(stderr, "%serror: %s%s\n", kColorRed, kColorReset, message.c_str());
}

void print_success(const std::string &message)
{
    fflush(stdout);
    std::printf("%s%s%s\n", kColorGreen, message.c_str(), kColorReset);
}
} // namespace crawler::detail
