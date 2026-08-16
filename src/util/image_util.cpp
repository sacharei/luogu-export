// src/image_util.cpp
#include <regex>
#include <unordered_set>
#include "luogu-export/util/image_util.h"

namespace
{

// ![alt](url) 或 ![](url)，可带 "title"
const std::regex kMarkdownImage(R"(!\[[^\]]*\]\(\s*([^\s)]+)[^)]*\))", std::regex::icase);

// <img ... src="url" ...>
const std::regex kHtmlImage(R"(<img[^>]*\bsrc\s*=\s*["']([^"']+)["'])", std::regex::icase);

} // namespace

std::vector<std::string> image_util::extract_urls(const std::string &markdown)
{
    std::vector<std::string> urls;
    std::unordered_set<std::string> seen;
    auto add = [&](const std::string &url) {
        if (url.empty() || seen.count(url))
            return;
        seen.insert(url);
        urls.push_back(url);
    };

    for (std::sregex_iterator it(markdown.begin(), markdown.end(), kMarkdownImage), end;
         it != end; ++it)
        add((*it)[1].str());

    for (std::sregex_iterator it(markdown.begin(), markdown.end(), kHtmlImage), end;
         it != end; ++it)
        add((*it)[1].str());

    return urls;
}
