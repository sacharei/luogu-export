// src/crawler/update.cpp
// 缓存更新：标签缓存与题目列表（-U）。
#include <string>
#include <functional>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <atomic>
#include <mutex>
#include <curl/curl.h>
#include <zlib.h>
#include <filesystem>
#include <cstring>
#include <limits>
#include <vector>
#include <algorithm>
#include <utility>
#include <nlohmann/json.hpp>
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/crawler/detail.h"

using nlohmann::json;

namespace crawler
{
namespace
{

static bool decompress_gzip_file(const std::string &input_path, const std::string &output_path)
{
    gzFile in = gzopen(input_path.c_str(), "rb");
    if (!in)
        return false;

    std::filesystem::path output(output_path);
    if (!output.parent_path().empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(output.parent_path(), ec);
        if (ec)
        {
            gzclose(in);
            return false;
        }
    }

    FILE *out = std::fopen(output_path.c_str(), "wb");
    if (!out)
    {
        gzclose(in);
        return false;
    }

    char buffer[8192];
    int read_bytes = 0;
    while ((read_bytes = gzread(in, buffer, sizeof(buffer))) > 0)
    {
        if (std::fwrite(buffer, 1, static_cast<size_t>(read_bytes), out) !=
            static_cast<size_t>(read_bytes))
        {
            std::fclose(out);
            gzclose(in);
            std::filesystem::remove(output_path);
            return false;
        }
    }

    if (read_bytes < 0)
    {
        std::fclose(out);
        gzclose(in);
        std::filesystem::remove(output_path);
        return false;
    }

    std::fclose(out);
    int status = gzclose(in);
    if (status != Z_OK)
    {
        std::filesystem::remove(output_path);
        return false;
    }
    return true;
}

} // namespace

using namespace detail;

derror update_tags()
{
    std::filesystem::path cache_dir = crawler::get_cache_dir();
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec)
    {
        print_error("Failed to create cache directory '" + cache_dir.string() + "': " + ec.message());
        return ENV_ERROR;
    }

    // 官方标签接口（题目列表页中通过 __luoguTagRequest 暴露）
    const std::string url = "https://www.luogu.com.cn/_lfe/tags/zh-CN";
    derror fetch_error = SUCCESS;
    printf("Downloading tags: ");
    std::string body = get_html(url, &fetch_error);

    if (fetch_error != SUCCESS)
    {
        print_error("Failed to update the tag cache (download failed)");
        return fetch_error;
    }

    try
    {
        json data = json::parse(body);
        if (!data.contains("tags") || !data["tags"].is_array())
        {
            print_error("Failed to update the tag cache (unexpected response format)");
            return EMPTY_RESPONSE;
        }

        // 收集 (标签 ID, 中文名, 分类)，按数字 ID 升序排列，便于人工查阅
        struct TagEntry
        {
            int id;
            std::string name;
            int type;
        };
        std::vector<TagEntry> entries;
        for (const auto &t : data["tags"])
        {
            if (!t.contains("id") || !t.contains("name") ||
                !t["id"].is_number_integer() || !t["name"].is_string())
                continue;

            // 官方数据中个别名称带 BOM 字符（如 \ufeff基础算法），入库前清理
            std::string name = t["name"].get<std::string>();
            const std::string bom = "\xEF\xBB\xBF";
            size_t pos;
            while ((pos = name.find(bom)) != std::string::npos)
                name.erase(pos, bom.size());

            int type = 0;
            if (t.contains("type") && t["type"].is_number_integer())
                type = t["type"].get<int>();

            entries.push_back({t["id"].get<int>(), std::move(name), type});
        }

        if (entries.empty())
        {
            print_error("Failed to update the tag cache (no valid tags found)");
            return EMPTY_RESPONSE;
        }
        std::sort(entries.begin(), entries.end(),
                  [](const TagEntry &a, const TagEntry &b) { return a.id < b.id; });

        // 每条记录：{"<数字ID>": {"name": "<中文名>", "type": <分类>}, ...}，
        // 程序里可直接按键查找；type 用于区分“算法”类标签
        json tag_map = json::object();
        for (const auto &e : entries)
        {
            json item = json::object();
            item["name"] = e.name;
            item["type"] = e.type;
            tag_map[std::to_string(e.id)] = std::move(item);
        }

        std::filesystem::path save_path = cache_dir / "tags.json";
        FILE *out = std::fopen(save_path.c_str(), "w");
        if (!out)
        {
            print_error("Failed to open '" + save_path.string() + "' for writing");
            return CANT_CREAT_FILE;
        }
        std::fputs(tag_map.dump(4).c_str(), out);
        std::fputc('\n', out);
        std::fclose(out);
    }
    catch (const std::exception &e)
    {
        print_error(std::string("Failed to parse tag data: ") + e.what());
        return EMPTY_RESPONSE;
    }
    printf("100 %%, done.\n");
    return SUCCESS;
}

derror update()
{
    std::filesystem::path cache_dir = crawler::get_cache_dir();
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec)
    {
        print_error("Failed to create cache directory '" + cache_dir.string() + "': " + ec.message());
        return ENV_ERROR;
    }

    std::function<void(const std::string &, long long, long long)> progress = [](const std::string &url, long long downloaded, long long total)
    {
        (void)url;
        if (total > 0)
        {
            int cur = static_cast<int>(downloaded * 100 / total);
            printf("\033[u\033[K%3d %%.", cur);   // 恢复位置 → 清到行尾 → 输出进度
            fflush(stdout);
        }
    };

    std::string url = "https://cdn.luogu.com.cn/problemset-open/latest.ndjson.gz";
    std::filesystem::path save_path = cache_dir / "latest.ndjson.gz";
    std::filesystem::path extract_path = cache_dir / "latest.ndjson";
    printf("Downloading problems: ");
    derror result = downloadFile(url, save_path.string(), progress);
    
    if (result != SUCCESS)
    {
        print_error("Failed to update the problem list cache (download failed)");
        return result;
    }
    
    if (!decompress_gzip_file(save_path.string(), extract_path.string()))
    {
        print_error("Failed to decompress the downloaded file '" + save_path.string() + "'");
        return DECOMPRESS_ERROR;
    }
    
    printf("\033[u\033[K100 %%, done.\n");

    // 题目列表更新成功后，顺带更新标签缓存（保存为 tags.json）
    return crawler::update_tags();
}
} // namespace crawler
