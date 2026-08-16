// include/luogu-export/crawler/crawler.h
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace crawler
{
    /// 爬取相关的错误码
    enum derror
    {
        SUCCESS = 0,
        CANT_CREAT_FILE,     // 无法创建或打开目标文件
        INIT_ERROR,          // libcurl 初始化失败
        DOWNLOAD_FAIL,       // 网络传输失败
        HTTP_ERROR,          // 服务器返回非 200 状态码
        EMPTY_RESPONSE,      // 响应内容为空
        INVALID_ARGUMENT,    // 参数无效（如空的题目/文章编号）
        ENV_ERROR,           // 缺少必要的环境变量
        DECOMPRESS_ERROR,    // gzip 解压失败
    };

    /// @param url 目标网址
    /// @param error 可选输出参数，成功时为 SUCCESS，失败时为对应错误码
    /// @return 成功返回网页内容，失败返回空字符串
    std::string get_html(const std::string& url, derror* error = nullptr);

    /// 下载进度回调：参数为 (url, 已下载字节数, 总字节数)；total 为 0 表示未知
    using download_progress_callback = std::function<void(const std::string &url,
                                                         long long downloaded,
                                                         long long total)>;

    /// @param url      下载地址
    /// @param fpath    保存路径
    /// @param progress 可选进度回调；不传时使用默认的百分比进度显示
    /// @return SUCCESS 或对应错误码
    derror downloadFile(const std::string &url, const std::string &fpath,
                        const download_progress_callback &progress = nullptr);

    /// @param p 题目编号
    /// @param error 可选输出参数，成功时为 SUCCESS，失败时为对应错误码
    /// @return 成功返回 html 的题面，失败返回空字符串
    std::string get_html_prob(std::string p, derror* error = nullptr);

    /// @param id 文章编号
    /// @param error 可选输出参数，成功时为 SUCCESS，失败时为对应错误码
    /// @return 成功返回 html 的文章内容，失败返回空字符串
    std::string get_html_article(std::string id, derror* error = nullptr);

    /// @return the base cache directory used by this program
    std::filesystem::path get_cache_dir();

    /// 更新标签缓存（来源：官方标签接口 /_lfe/tags/zh-CN）
    /// 保存为 <cache_dir>/tags.json：JSON 对象，键为标签数字 ID，
    /// 值为 {"name": 中文名称, "type": 官方分类}，按数字 ID 升序排列，
    /// 便于直接按键查找。
    /// @return SUCCESS 或对应错误码
    derror update_tags();

    /// 下载一批图片链接到缓存目录（<cache_dir>/images/）。
    /// 文件名取完整链接（特殊字符替换为 _），避免不同图床同名互相覆盖；
    /// 已存在的文件直接跳过。
    /// @param urls 图片链接列表
    /// @return SUCCESS 或对应错误码（部分失败时返回第一个错误码）
    derror download_images(const std::vector<std::string> &urls);

    /// 返回图片 URL 在缓存中对应的文件路径（<cache_dir>/images/<文件名>）。
    /// 文件名由完整链接生成，与 download_images 的落盘位置一致。
    std::filesystem::path image_cache_path(const std::string &url);

    /// @return SUCCESS 或对应错误码
    derror update();
}
