// src/crawler/cache.cpp
// 缓存目录/路径、图片文件命名与图片下载。
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

using namespace detail;

std::filesystem::path get_cache_dir()
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

derror download_images(const std::vector<std::string> &urls)
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

std::filesystem::path image_cache_path(const std::string &url)
{
    return crawler::get_cache_dir() / "images" / image_cache_filename(url);
}
} // namespace crawler
