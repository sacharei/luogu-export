// include/luogu-export/util/settings.h
#ifndef LUOGU_EXPORT_SETTINGS_H
#define LUOGU_EXPORT_SETTINGS_H

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// 程序的配置文件（$XDG_CONFIG_HOME/luogu-export/settings.conf）。
//
// INI 风格：用 [section] 分节，节内每行一条 "key: value"（"=" 也接受）。
// 现有三节：
//   [fonts]  导出的 LaTeX 文档里各处使用的字体
//   [paths]  题目缓存与图片缓存目录
//   [export] 导出相关的默认值（如默认输出文件名）
//
// 字体取值可以是：
//   - 字体族名（Consolas、Noto Sans）或字体文件名（FandolSong-Regular.otf）；
//   - 另一个字体 key 的名字（如 sans_cjk: main_cjk）表示复用它的取值；
//   - "default"，表示不设置字体，沿用该位置原本的字体。
// 键名里带 "_cjk" 的负责中文，不带的那份负责西文，两者相互独立。
namespace settings
{
    // 解析后的配置：key -> 原始取值
    using Values = std::unordered_map<std::string, std::string>;

    // 「不设置」的取值
    inline const char *kDefault = "default";

    // 该值是否表示「不设置」（default，或历史版本写的空值）
    bool is_default(const std::string &value);

    // 配置文件路径：$XDG_CONFIG_HOME/luogu-export/settings.conf，
    // 回退到 $HOME/.config/luogu-export/settings.conf。
    std::filesystem::path config_path();

    // 按定义顺序返回全部 key
    const std::vector<std::string> &all_keys();
    bool has_key(const std::string &key);

    // key 所属的节（"fonts" / "export"）与节内分组（如 "structure"）
    std::string key_section(const std::string &key);
    std::string key_group(const std::string &key);

    // key 的英文说明（配置文件注释与 -O --list 共用）
    std::string key_comment(const std::string &key);

    // 读取配置。文件不存在或无法读取时返回 false，out 已填入全部默认值。
    bool load(Values &out);

    // 文件中出现过的 key（含被注释掉的），用于判断是否需要补全
    std::vector<std::string> documented_keys(const std::filesystem::path &path);

    // 补全缺失的 key（首次运行时整份写出）。已存在的 key（含注释掉的写法）
    // 不覆盖，因此用户的设置与注释都会保留。
    // @return 文件被新建或追加过内容时为 true
    bool append_missing_defaults(const std::filesystem::path &path);

    // 进程内共享的配置（只从文件读取一次）。
    // 缓存路径等会被频繁查询的地方用它，避免反复读文件。
    const Values &shared();

    // 原始取值（不解析引用）；key 不存在时返回该 key 的默认值
    std::string raw_value(const Values &values, const std::string &key);

    // 题目缓存目录（latest.ndjson / tags.json），受 problem_cache 控制
    std::filesystem::path problem_cache_dir();

    // 图片缓存目录，受 image_cache 控制
    std::filesystem::path image_cache_dir();

    // 一个字体 key 的解析结果
    struct Resolution
    {
        std::string value = kDefault; // 解析后的字体名，或 default
        std::string reference;        // 非空表示原值是引用（值为被引用的 key）
        std::string error;            // 非空表示解析失败（环 / 引用了非字体 key）
        std::vector<std::string> involved; // 出错时涉及的 key（用于去重）
    };
    Resolution resolve_font(const Values &values, const std::string &key);

    // 解析全部字体 key：key -> 最终字体名（有问题的回退为 default）
    Values resolve_fonts(const Values &values);

    // 字体是否存在于本机（libfontconfig + kpathsea）
    bool font_exists(const std::string &name);

    // 一条配置问题
    struct Problem
    {
        std::string key;
        std::string message;
    };

    // 校验全部字体设置：引用环、引用非字体 key、字体不在本机
    std::vector<Problem> validate_fonts(const Values &values);

    // 解析并校验，得到可直接给导言区用的「key -> 最终字体名」，
    // 有问题的 key 回退为 default，问题列表用于打印警告。
    struct FontSetup
    {
        Values resolved;
        std::vector<Problem> problems;
    };
    FontSetup prepare_fonts(const Values &values);

    // 把 -O --set 的键值对写入配置文件。写入前做完整校验（key 是否存在、
    // 引用是否成环、字体是否存在），有任何一个问题都不修改文件。
    // @param error 失败原因（英文，可能多行）
    bool set(const std::vector<std::pair<std::string, std::string>> &entries,
             std::string &error);

    // 打印 -O --list 的表格（按节与分组分类）
    void print_list(const Values &values);

    // 默认导出文件名（含扩展名，extension 形如 ".md"）。
    // default_title 无效时回退为 problems，并把原因放进 problems。
    std::string default_output_name(const std::string &extension,
                                    std::vector<Problem> *problems);

    // 把问题以 "warning: ..." 的形式打印到 stderr
    void print_warnings(const std::vector<Problem> &problems);
}

#endif // LUOGU_EXPORT_SETTINGS_H
