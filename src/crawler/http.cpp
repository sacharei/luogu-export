// src/crawler/http.cpp
// HTTP 抓取与文件下载（get_html / downloadFile 及 libcurl 回调）。
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

static size_t write_callback_html(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    auto *response = static_cast<std::string *>(userp);
    response->append(static_cast<char *>(contents), total);
    return total;
}

size_t write_callback_file(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    if (total == 0)
        return 0;

    auto *out = static_cast<FILE *>(userp);
    if (std::fwrite(contents, 1, total, out) != total)
        return 0;
    return total;
}

// 默认进度回调：显示百分比（与旧行为一致）
void default_progress(const std::string &url, long long downloaded, long long total)
{
    (void)url;
    if (total > 0)
    {
        int cur = static_cast<int>(downloaded * 100 / total);
        printf("\033[u\033[K%3d %%", cur);   // 恢复位置 → 清到行尾 → 输出进度
        fflush(stdout);
    }
}

// libcurl 进度回调：转发到用户提供的回调
struct ProgressContext
{
    const crawler::download_progress_callback *callback;
    std::string url;
};

int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal; (void)ulnow;
    auto *ctx = static_cast<ProgressContext *>(clientp);
    if (dltotal > 0 && ctx->callback)
        (*ctx->callback)(ctx->url,
                         static_cast<long long>(dlnow),
                         static_cast<long long>(dltotal));
    return 0;
}

} // namespace

using namespace detail;

std::string get_html(const std::string &url, derror *error)
{
    if (error) *error = SUCCESS;

    std::string res;
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        print_error("Failed to initialize libcurl while fetching " + url);
        if (error) *error = INIT_ERROR;
        return "";
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_html);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);

    CURLcode curl_res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (curl_res != CURLE_OK)
    {
        print_error("Failed to fetch " + url + ": " + curl_easy_strerror(curl_res));
        if (error) *error = DOWNLOAD_FAIL;
        return "";
    }
    if (http_code != 200)
    {
        print_error("Failed to fetch " + url + ": HTTP status code " + std::to_string(http_code));
        if (error) *error = HTTP_ERROR;
        return "";
    }
    if (res.empty())
    {
        print_error("Failed to fetch " + url + ": empty response");
        if (error) *error = EMPTY_RESPONSE;
        return "";
    }
    return res;
}

derror downloadFile(const std::string &url, const std::string &fpath,
                                      const crawler::download_progress_callback &progress)
{
    std::filesystem::path path(fpath);
    std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            print_error("Failed to create directory '" + parent.string() + "': " + ec.message());
            return CANT_CREAT_FILE;
        }
    }

    FILE *out_file = std::fopen(fpath.c_str(), "wb");
    if (!out_file)
    {
        print_error("Failed to open file '" + fpath + "' for writing");
        return CANT_CREAT_FILE;
    }

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        print_error("Failed to initialize libcurl while downloading " + url);
        std::fclose(out_file);
        return INIT_ERROR;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_file);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out_file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    fflush(stdout);
    printf("\033[s");  // 保存光标位置

    // 未提供回调时使用默认的百分比进度显示
    crawler::download_progress_callback effective;
    if (progress)
        effective = progress;
    else
        effective = default_progress;

    ProgressContext ctx;
    ctx.callback = &effective;
    ctx.url = url;

    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);
    std::fclose(out_file);

    if (res != CURLE_OK)
    {
        std::error_code ec;
        std::filesystem::remove(fpath, ec);
        print_error("Failed to download " + url + ": " + curl_easy_strerror(res));
        return DOWNLOAD_FAIL;
    }
    if (http_code != 200)
    {
        std::error_code ec;
        std::filesystem::remove(fpath, ec);
        print_error("Failed to download " + url + ": HTTP status code " + std::to_string(http_code));
        return HTTP_ERROR;
    }

    return SUCCESS;
}

std::string get_html_prob(std::string p, derror *error)
{
    if (error) *error = SUCCESS;
    if (p.empty())
    {
        print_error("Invalid problem id: expected a non-empty string");
        if (error) *error = INVALID_ARGUMENT;
        return "";
    }

    std::string url = "https://www.luogu.com.cn/problem/" + p;
    derror fetch_error = SUCCESS;
    std::string html = crawler::get_html(url, &fetch_error);
    if (fetch_error != SUCCESS)
    {
        print_error("Failed to fetch problem page for '" + p + "'");
        if (error) *error = fetch_error;
        return "";
    }
    return html;
}

std::string get_html_article(std::string id, derror *error)
{
    if (error) *error = SUCCESS;
    if (id.empty())
    {
        print_error("Invalid article id: expected a non-empty string");
        if (error) *error = INVALID_ARGUMENT;
        return "";
    }

    std::string url = "https://www.luogu.com.cn/article/" + id;
    derror fetch_error = SUCCESS;
    std::string html = crawler::get_html(url, &fetch_error);
    if (fetch_error != SUCCESS)
    {
        print_error("Failed to fetch article page for '" + id + "'");
        if (error) *error = fetch_error;
        return "";
    }
    return html;
}
} // namespace crawler
