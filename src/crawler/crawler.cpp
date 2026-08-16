// src/cralwer/cralwer.cpp
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

using nlohmann::json;

namespace
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

// FNV-1a 64 位哈希（十六进制），用于给超长 URL 生成定长后缀
std::string fnv1a_hex(const std::string &s)
{
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : s)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

// URL -> 缓存文件名：完整链接做文件名（除字母数字 . - _ 外全部替换为 _），
// 并保留 URL 路径里的扩展名；名称过长时截断并附完整链接的哈希。
// 由于文件名包含完整链接，不同图床、不同参数不会互相覆盖。
std::string image_cache_filename(const std::string &url)
{
    std::string name;
    name.reserve(url.size());
    for (char c : url)
    {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '-' || c == '_')
            name += c;
        else
            name += '_';
    }

    // 扩展名取自 URL 路径（忽略查询参数），如 .png / .gif
    std::string path = url;
    const size_t query = path.find_first_of("?#");
    if (query != std::string::npos)
        path.resize(query);
    std::string ext;
    const size_t dot = path.find_last_of('.');
    const size_t slash = path.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        ext = path.substr(dot);

    if (!ext.empty() &&
        (name.size() < ext.size() ||
         name.compare(name.size() - ext.size(), ext.size(), ext) != 0))
        name += ext;

    if (name.size() > 200)
    {
        name = name.substr(0, 100) + "_" + fnv1a_hex(url);
        if (name.size() < ext.size() ||
            name.compare(name.size() - ext.size(), ext.size(), ext) != 0)
            name += ext;
    }
    return name;
}

// 是否为洛谷图床（cdn.luogu.com.cn 等）的图片
bool is_luogu_image_host(const std::string &url)
{
    return url.find("luogu.com.cn") != std::string::npos;
}

// 随机延时 0.5~3 秒，避免下载洛谷图床图片时请求过快
void random_delay()
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.5, 3.0);
    std::this_thread::sleep_for(std::chrono::duration<double>(dist(rng)));
}

// 校验文件是否真的是图片（按文件头魔数判断 PNG/JPEG/GIF/WebP/BMP/SVG）
bool looks_like_image_file(const std::filesystem::path &path)
{
    FILE *in = std::fopen(path.c_str(), "rb");
    if (!in)
        return false;
    unsigned char head[12] = {0};
    const size_t n = std::fread(head, 1, sizeof(head), in);
    std::fclose(in);
    if (n >= 8 && std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0)
        return true;
    if (n >= 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF)
        return true;
    if (n >= 6 && std::memcmp(head, "GIF8", 4) == 0)
        return true;
    if (n >= 12 && std::memcmp(head, "RIFF", 4) == 0 &&
        std::memcmp(head + 8, "WEBP", 4) == 0)
        return true;
    if (n >= 2 && head[0] == 'B' && head[1] == 'M')
        return true;
    if (n >= 4 && std::memcmp(head, "<svg", 4) == 0)
        return true;
    if (n >= 5 && std::memcmp(head, "<?xml", 5) == 0)
        return true;
    return false;
}
} // namespace

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

std::filesystem::path crawler::get_cache_dir()
{
    if (const char *xdg_cache_home = std::getenv("XDG_CACHE_HOME"))
    {
        if (xdg_cache_home && *xdg_cache_home)
            return std::filesystem::path(xdg_cache_home) / "luogu-export";
    }

    if (const char *home_env = std::getenv("HOME"))
    {
        if (home_env && *home_env)
            return std::filesystem::path(home_env) / ".cache" / "luogu-export";
    }

    std::error_code ec;
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path(ec);
    if (ec)
        return std::filesystem::path(".cache") / "luogu-export";
    return temp_dir / "luogu-export";
}

std::string crawler::get_html(const std::string &url, derror *error)
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

crawler::derror crawler::downloadFile(const std::string &url, const std::string &fpath,
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

std::string crawler::get_html_prob(std::string p, derror *error)
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

std::string crawler::get_html_article(std::string id, derror *error)
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

crawler::derror crawler::update_tags()
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
    printf("\n");

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

    print_success("Tag cache updated successfully");
    return SUCCESS;
}

crawler::derror crawler::download_images(const std::vector<std::string> &urls)
{
    std::filesystem::path cache_dir = crawler::get_cache_dir();
    std::filesystem::path image_dir = cache_dir / "images";
    std::error_code ec;
    std::filesystem::create_directories(image_dir, ec);
    if (ec)
    {
        print_error("Failed to create image cache directory '" + image_dir.string() + "': " + ec.message());
        return ENV_ERROR;
    }

    int total = static_cast<int>(urls.size());

    // 直接在同一行显示完整进度，避免与其他保存/恢复光标的序列冲突。
    // 先打印前缀，监视线程每次使用 '\r' 回到行首并重写整行内容。
    printf("Downloading image(s): ");

    // 并行下载：多个 worker 通过原子索引领取 URL，洛谷图床下载串行化并保持随机间隔
    std::atomic<size_t> next_index{0};
    std::atomic<int> downloaded{0}, skipped{0};
    std::atomic<bool> monitor_stop{false};
    std::mutex luogu_mutex;
    std::mutex error_mutex;
    bool first_luogu_download = true;
    derror first_error = SUCCESS;

    // 启动监视线程，定期读取 downloaded 并更新输出（基于 downloaded/total）
    std::thread monitor([&] {
        while (!monitor_stop.load())
        {
            int d = downloaded.load();
            // 使用浮点计算并四舍五入，避免长时间为 0 的地板除
            int cur = total > 0 ? static_cast<int>(std::floor((static_cast<double>(d) * 100.0) / static_cast<double>(total) + 0.5)) : 100;
            // 回到行首并清除到行尾，重写完整前缀 + 进度
            printf("\rDownloading image(s): %3d %% (%d/%d).\033[K", cur, d, total);
            fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        // 结束前再做一次最终输出并换行
        int d = downloaded.load();
        int cur = total > 0 ? static_cast<int>(std::floor((static_cast<double>(d) * 100.0) / static_cast<double>(total) + 0.5)) : 100;
        printf("\rDownloading image(s): %3d %% (%d/%d), done.\033[K\n", cur, d, total);
        fflush(stdout);
    });

    const size_t n_workers = std::min<size_t>(urls.size(),
                                              std::max<size_t>(1, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    workers.reserve(n_workers);
    for (size_t w = 0; w < n_workers; ++w)
    {
        workers.emplace_back([&] {
            for (;;)
            {
                const size_t i = next_index.fetch_add(1, std::memory_order_relaxed);
                if (i >= urls.size())
                    break;
                const std::string &url = urls[i];
                if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0)
                    continue; // 只处理 http(s) 图片链接

                const std::filesystem::path save_path = image_dir / image_cache_filename(url);
                if (std::filesystem::exists(save_path))
                {
                    ++skipped;
                    continue;
                }

                auto download_one = [&] {
                    // 图片下载不显示进度条（可自定义回调）
                    const derror result = downloadFile(url, save_path.string(),
                                                       [](const std::string &, long long, long long) {});
                    if (result != SUCCESS)
                    {
                        std::lock_guard<std::mutex> lock(error_mutex);
                        if (first_error == SUCCESS)
                            first_error = result;
                        return;
                    }

                    // 校验下载内容确实是图片；无效内容（如错误页）删除并视为失败
                    if (!looks_like_image_file(save_path))
                    {
                        std::error_code ec;
                        std::filesystem::remove(save_path, ec);
                        std::lock_guard<std::mutex> lock(error_mutex);
                        if (first_error == SUCCESS)
                            first_error = DOWNLOAD_FAIL;
                        return;
                    }
                    ++downloaded;
                };

                if (is_luogu_image_host(url))
                {
                    // 洛谷图床图片串行下载，之间随机间隔 0.5~3 秒
                    std::lock_guard<std::mutex> lock(luogu_mutex);
                    if (!first_luogu_download)
                        random_delay();
                    first_luogu_download = false;
                    download_one();
                }
                else
                {
                    download_one();
                }
            }
        });
    }
    for (auto &worker : workers)
        worker.join();

    // 停止监视线程并等待其结束
    monitor_stop.store(true);
    if (monitor.joinable())
        monitor.join();

    if (downloaded == 0 && skipped == 0)
    {
        print_error("No images to download");
        return first_error != SUCCESS ? first_error : EMPTY_RESPONSE;
    }
    return first_error;
}

std::filesystem::path crawler::image_cache_path(const std::string &url)
{
    return crawler::get_cache_dir() / "images" / image_cache_filename(url);
}

crawler::derror crawler::update()
{
    std::filesystem::path cache_dir = crawler::get_cache_dir();
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec)
    {
        print_error("Failed to create cache directory '" + cache_dir.string() + "': " + ec.message());
        return ENV_ERROR;
    }

    std::string url = "https://cdn.luogu.com.cn/problemset-open/latest.ndjson.gz";
    std::filesystem::path save_path = cache_dir / "latest.ndjson.gz";
    std::filesystem::path extract_path = cache_dir / "latest.ndjson";
    printf("Downloading problems: ");
    derror result = downloadFile(url, save_path.string());
    printf("\n");

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

    print_success("Problem list cache updated successfully");

    // 题目列表更新成功后，顺带更新标签缓存（保存为 tags.json）
    return crawler::update_tags();
}
