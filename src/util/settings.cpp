// src/util/settings.cpp
// 程序的配置文件：默认值表、INI 解析、字体引用解析、字体存在性检查、
// 写入与 --list 输出。
#include "luogu-export/util/settings.h"

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

#include "luogu-export/util/string_util.h"

namespace settings
{
namespace
{
// 一个配置项。section 决定它写进哪个 [节]，group 是 --list 里的细分标题，
// comment 同时用作配置文件注释与 --list 的说明，因此统一写成英文。
struct OptionDef
{
    const char *key;
    const char *section;
    const char *group;
    const char *value;   // 默认值
    const char *comment;
};

// 全部配置项，顺序即写入顺序，也是 --list 的显示顺序。
// 默认值刻意对齐「没有这份配置时」的导出结果：
//   - 正文/标题/表格等用 default，沿用 ctex 的默认字体；
//   - 代码、标签沿用原先硬编码的 Consolas / SimHei / Noto Sans。
const OptionDef kOptions[] = {
    // ---- [fonts] 全局字体 ----
    {"main", "fonts", "global", "default",
     "Body Latin text; default = the ctex default (Latin Modern Roman)"},
    {"main_cjk", "fonts", "global", "default",
     "Body Chinese text; default = the ctex default fontset (FandolSong)"},
    {"sans", "fonts", "global", "default",
     "Sans-serif Latin text (\\textsf); default = the ctex default (Latin Modern Sans)"},
    {"sans_cjk", "fonts", "global", "default",
     "Sans-serif Chinese text; default = the ctex default (FandolHei)"},
    {"mono", "fonts", "global", "Consolas",
     "Monospaced Latin text (\\ttfamily); default = Consolas"},
    {"mono_cjk", "fonts", "global", "SimHei",
     "Monospaced Chinese text (\\ttfamily); default = SimHei"},

    // ---- [fonts] 文档结构 ----
    {"title", "fonts", "structure", "default",
     "\\maketitle title/author/date; default = the body font (main)"},
    {"title_cjk", "fonts", "structure", "default",
     "As above, Chinese part; default = main_cjk"},
    {"toc", "fonts", "structure", "default",
     "Table of contents; default = the body font (main)"},
    {"toc_cjk", "fonts", "structure", "default",
     "As above, Chinese part; default = main_cjk"},
    {"section", "fonts", "structure", "Consolas",
     "\\section heading (e.g. P1081); default = Consolas"},
    {"section_cjk", "fonts", "structure", "SimHei",
     "As above, Chinese part; default = SimHei"},
    {"subsection", "fonts", "structure", "section",
     "\\subsection heading (e.g. the statement sections); default = section"},
    {"subsection_cjk", "fonts", "structure", "section_cjk",
     "As above, Chinese part; default = section_cjk"},
    {"subsubsection", "fonts", "structure", "section",
     "\\subsubsection heading (e.g. Sample input #1); default = section"},
    {"subsubsection_cjk", "fonts", "structure", "section_cjk",
     "As above, Chinese part; default = section_cjk"},
    {"paragraph", "fonts", "structure", "default",
     "\\paragraph heading (e.g. the constraints block); default = the body font"},
    {"paragraph_cjk", "fonts", "structure", "default",
     "As above, Chinese part; default = main_cjk"},
    {"header", "fonts", "structure", "default",
     "Page header (running section name and page number); default = the body font"},
    {"header_cjk", "fonts", "structure", "default",
     "As above, Chinese part; default = main_cjk"},
    {"footer", "fonts", "structure", "default",
     "Page footer (currently empty); default = the body font"},
    {"footer_cjk", "fonts", "structure", "default",
     "As above, Chinese part; default = main_cjk"},

    // ---- [fonts] 正文类 ----
    {"body", "fonts", "body", "default",
     "Body paragraphs; applied globally, wins over main when both are set"},
    {"body_cjk", "fonts", "body", "default",
     "As above, Chinese part; overrides main_cjk when both are set"},
    {"quote", "fonts", "body", "default",
     "Block quote (> ...) and :::align blocks; default = the body font"},
    {"quote_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"list", "fonts", "body", "default",
     "itemize/enumerate items and their labels; default = the body font"},
    {"list_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"table", "fonts", "body", "default",
     "Table cell text; default = the body font"},
    {"table_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"table_header", "fonts", "body", "default",
     "Header row of ::cute-table (tuack/three); default = the table font"},
    {"table_header_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = table_cjk"},
    {"table_caption", "fonts", "body", "default",
     "Table caption (not emitted currently); default = the body font"},
    {"table_caption_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"callout_title", "fonts", "body", "default",
     "Title of :::info / :::success / :::warning / :::error callouts"},
    {"callout_title_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = callout_body_cjk"},
    {"callout_body", "fonts", "body", "default",
     "Body text inside callouts; default = the body font"},
    {"callout_body_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"image_caption", "fonts", "body", "default",
     "Image caption (not emitted currently); default = the body font"},
    {"image_caption_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"footnote", "fonts", "body", "default",
     "Footnotes (not emitted currently); default = the body font"},
    {"footnote_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"link", "fonts", "body", "default",
     "Text of \\href links; default = the body font"},
    {"link_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},
    {"url", "fonts", "body", "default",
     "\\url autolinks; default = \\urlstyle{same}, i.e. the surrounding font"},
    {"url_cjk", "fonts", "body", "default",
     "As above, Chinese part; default = main_cjk"},

    // ---- [fonts] 粗体 ----
    {"bold", "fonts", "bold", "Consolas",
     "Bold text (\\textbf); default = Consolas"},
    {"bold_cjk", "fonts", "bold", "SimHei",
     "As above, Chinese part; default = SimHei"},

    // ---- [fonts] 洛谷标签 ----
    {"tag", "fonts", "tags", "Noto Sans",
     "Luogu tag chips (e.g. NOIP 提高组); default = Noto Sans"},
    {"tag_cjk", "fonts", "tags", "WenQuanYi Micro Hei",
     "As above, Chinese part; default = WenQuanYi Micro Hei"},

    // ---- [fonts] 代码 ----
    {"code_block", "fonts", "code", "Consolas",
     "Code block (lstlisting) body; default = Consolas"},
    {"code_block_cjk", "fonts", "code", "SimHei",
     "As above, Chinese part; default = SimHei"},
    {"code_keyword", "fonts", "code", "code_block",
     "Code block keywords (blue bold by default); default = code_block"},
    {"code_keyword_cjk", "fonts", "code", "code_block_cjk",
     "As above, Chinese part; default = code_block_cjk"},
    {"code_comment", "fonts", "code", "code_block",
     "Code block comments (green by default); default = code_block"},
    {"code_comment_cjk", "fonts", "code", "code_block_cjk",
     "As above, Chinese part; default = code_block_cjk"},
    {"code_string", "fonts", "code", "code_block",
     "Code block strings (dark red by default); default = code_block"},
    {"code_string_cjk", "fonts", "code", "code_block_cjk",
     "As above, Chinese part; default = code_block_cjk"},
    {"code_number", "fonts", "code", "code_block",
     "Code block line numbers (gray by default); default = code_block"},
    {"code_number_cjk", "fonts", "code", "code_block_cjk",
     "As above, Chinese part; default = code_block_cjk"},
    {"code_inline", "fonts", "code", "code_block",
     "Inline code `...` and \\verb; default = code_block"},
    {"code_inline_cjk", "fonts", "code", "code_block_cjk",
     "As above, Chinese part; default = code_block_cjk"},

    // ---- [paths] 缓存路径（默认值在 option_default 中按环境解析）----
    {"image_cache", "paths", "cache", "",
     "Directory where problem images are saved; default = $XDG_CACHE_HOME/luogu-export/images"},
    {"problem_cache", "paths", "cache", "",
     "Directory of the problem cache (latest.ndjson, tags.json); default = $XDG_CACHE_HOME/luogu-export"},

    // ---- [export] 导出默认值 ----
    {"default_title", "export", "output", "problems",
     "Default output file name without extension (problems -> problems.md / problems.tex)"},
};

std::string trim(const std::string &s)
{
    size_t b = 0;
    size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n'))
        ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n'))
        --e;
    return s.substr(b, e - b);
}

// 去掉值外面成对的引号（'...' 或 "..."），方便写含空格的字体名
std::string unquote(const std::string &s)
{
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

// 写进配置文件时的取值写法：含空格的字体名（如 Noto Sans）加双引号，
// 读回来时由 unquote 去掉，语义不变。
std::string quote_if_needed(const std::string &value)
{
    if (value.find(' ') == std::string::npos)
        return value;
    return "\"" + value + "\"";
}

// 解析形如 "key: value" / "key = value" / "key value" 的一行。
// 不是配置行（空行、注释、节标题、没有分隔符）时返回 false。
bool parse_line(const std::string &raw, std::string &key, std::string &value)
{
    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[')
        return false;

    size_t sep = std::string::npos;
    size_t value_start = std::string::npos;
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '=' || line[i] == ':')
        {
            sep = i;
            value_start = i + 1;
            break;
        }
        if (line[i] == ' ' || line[i] == '\t')
        {
            sep = i;
            value_start = line.find_first_not_of(" \t", i);
            break;
        }
    }
    if (sep == std::string::npos || value_start == std::string::npos)
        return false;

    key = trim(line.substr(0, sep));
    if (key.empty())
        return false;
    value = unquote(trim(line.substr(value_start)));
    return true;
}

// 解析节标题 "[fonts]"
bool parse_section(const std::string &raw, std::string &name)
{
    const std::string line = trim(raw);
    if (line.size() < 2 || line.front() != '[')
        return false;
    const size_t close = line.find(']');
    if (close == std::string::npos)
        return false;
    name = util::to_lower_ascii(trim(line.substr(1, close - 1)));
    return true;
}

const OptionDef *find_option(const std::string &key)
{
    for (const OptionDef &entry : kOptions)
        if (key == entry.key)
            return &entry;
    return nullptr;
}

std::vector<std::string> read_lines(const std::filesystem::path &path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in)
        return lines;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

// 程序默认的缓存根目录：$XDG_CACHE_HOME/luogu-export，
// 回退到 $HOME/.cache/luogu-export（与旧版 get_cache_dir 的行为一致）。
std::filesystem::path default_cache_base()
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

// 配置项的默认值。字体等是表里的常量；缓存路径依赖运行环境，运行时解析。
std::string option_default(const OptionDef &entry)
{
    if (std::strcmp(entry.key, "problem_cache") == 0)
        return default_cache_base().string();
    if (std::strcmp(entry.key, "image_cache") == 0)
        return (default_cache_base() / "images").string();
    return entry.value;
}

// 展开路径开头的 ~（配置里允许写 ~/.cache/...）
std::filesystem::path expand_path(const std::string &value)
{
    const std::string s = trim(value);
    const char *home = std::getenv("HOME");
    if (s == "~" && home && *home)
        return std::filesystem::path(home);
    if (s.rfind("~/", 0) == 0 && home && *home)
        return std::filesystem::path(std::string(home) + s.substr(1));
    return std::filesystem::path(s);
}

// 配置缺失某个 key 或值写成空时补上默认值
void fill_defaults(Values &values)
{
    for (const OptionDef &entry : kOptions)
    {
        const std::string def = option_default(entry);
        auto it = values.find(entry.key);
        if (it == values.end())
            values.emplace(entry.key, def);
        else if (it->second.empty())
            it->second = def;
    }
}

// default_title 之类的导出选项校验：空值或含路径分隔符都不行
std::string validate_export_value(const std::string &key, const std::string &value)
{
    const std::string v = trim(value);
    if (v.empty())
        return key + " must not be empty";
    if (v.find('/') != std::string::npos || v.find('\\') != std::string::npos)
        return key + " must be a file name without path separators";
    if (v == "." || v == "..")
        return key + " is not a valid file name";
    return std::string();
}

// ---- 字体存在性检查 ----

bool looks_like_file_name(const std::string &name)
{
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return true;
    const std::string lower = util::to_lower_ascii(name);
    static const char *kExts[] = {".otf", ".ttf", ".ttc", ".otc",
                                  ".pfb", ".pfa", ".afm", ".dfont", ".fon"};
    for (const char *ext : kExts)
    {
        const size_t n = std::strlen(ext);
        if (lower.size() > n && lower.compare(lower.size() - n, n, ext) == 0)
            return true;
    }
    return false;
}

// fontconfig 的字体库快照：族名与文件名各收一份，全部小写便于比较
struct FontDb
{
    bool available = false;
    std::set<std::string> families;
    std::set<std::string> basenames;
};

FontDb build_font_db()
{
    FontDb db;
    if (!FcInit())
        return db;

    FcObjectSet *os = FcObjectSetBuild(FC_FAMILY, FC_FILE, nullptr);
    FcPattern *pattern = FcPatternCreate();
    FcFontSet *fonts = (os && pattern) ? FcFontList(nullptr, pattern, os) : nullptr;
    if (fonts)
    {
        for (int i = 0; i < fonts->nfont; ++i)
        {
            FcPattern *font = fonts->fonts[i];
            for (int idx = 0;; ++idx)
            {
                FcChar8 *family = nullptr;
                if (FcPatternGetString(font, FC_FAMILY, idx, &family) != FcResultMatch ||
                    !family)
                    break;
                db.families.insert(util::to_lower_ascii(
                    std::string(reinterpret_cast<const char *>(family))));
            }
            FcChar8 *file = nullptr;
            if (FcPatternGetString(font, FC_FILE, 0, &file) == FcResultMatch && file)
            {
                std::string path(reinterpret_cast<const char *>(file));
                const size_t slash = path.find_last_of("/\\");
                if (slash != std::string::npos)
                    path = path.substr(slash + 1);
                db.basenames.insert(util::to_lower_ascii(path));
            }
        }
        FcFontSetDestroy(fonts);
        db.available = true;
    }
    if (pattern)
        FcPatternDestroy(pattern);
    if (os)
        FcObjectSetDestroy(os);
    return db;
}

const FontDb &font_db()
{
    static const FontDb db = build_font_db();
    return db;
}

std::string shell_quote(const std::string &s)
{
    std::string out = "'";
    for (char c : s)
    {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

// 字体文件名由 XeTeX 通过 kpathsea 查找（TeX Live 自带的字体不在 fontconfig 里，
// 如 FandolSong-Regular.otf），所以文件名还要问一次 kpsewhich。
bool kpsewhich_finds(const std::string &name)
{
    static std::map<std::string, bool> cache;
    auto it = cache.find(name);
    if (it != cache.end())
        return it->second;

    bool found = false;
    const std::string cmd = "kpsewhich " + shell_quote(name) + " 2>/dev/null";
    if (FILE *pipe = popen(cmd.c_str(), "r"))
    {
        char buf[1024];
        if (std::fgets(buf, sizeof(buf), pipe) && buf[0] != '\0' && buf[0] != '\n')
            found = true;
        pclose(pipe);
    }
    cache.emplace(name, found);
    return found;
}
} // namespace

bool is_default(const std::string &value)
{
    return value.empty() || value == kDefault;
}

std::filesystem::path config_path()
{
    if (const char *xdg_config_home = std::getenv("XDG_CONFIG_HOME"))
    {
        if (xdg_config_home && *xdg_config_home)
            return std::filesystem::path(xdg_config_home) / "luogu-export" / "settings.conf";
    }

    if (const char *home_env = std::getenv("HOME"))
    {
        if (home_env && *home_env)
            return std::filesystem::path(home_env) / ".config" / "luogu-export" / "settings.conf";
    }

    std::error_code ec;
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path(ec);
    if (ec)
        return std::filesystem::path(".config") / "luogu-export" / "settings.conf";
    return temp_dir / "luogu-export" / "settings.conf";
}

const std::vector<std::string> &all_keys()
{
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> v;
        v.reserve(sizeof(kOptions) / sizeof(kOptions[0]));
        for (const OptionDef &entry : kOptions)
            v.emplace_back(entry.key);
        return v;
    }();
    return keys;
}

bool has_key(const std::string &key)
{
    return find_option(key) != nullptr;
}

std::string key_section(const std::string &key)
{
    const OptionDef *entry = find_option(key);
    return entry ? entry->section : std::string();
}

std::string key_group(const std::string &key)
{
    const OptionDef *entry = find_option(key);
    return entry ? entry->group : std::string();
}

std::string key_comment(const std::string &key)
{
    const OptionDef *entry = find_option(key);
    return entry ? entry->comment : std::string();
}

bool load(Values &out)
{
    out.clear();
    std::ifstream in(config_path());
    if (!in)
    {
        fill_defaults(out);
        return false;
    }

    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::string section;
        if (parse_section(line, section))
            continue; // 节标题只影响书写，key 按定义表归类
        std::string key;
        std::string value;
        if (!parse_line(line, key, value))
            continue;
        out.emplace(std::move(key), std::move(value));
    }
    fill_defaults(out);
    return true;
}

std::vector<std::string> documented_keys(const std::filesystem::path &path)
{
    std::vector<std::string> keys;
    for (const std::string &raw : read_lines(path))
    {
        std::string line = trim(raw);
        while (!line.empty() && (line[0] == '#' || line[0] == ';'))
            line = trim(line.substr(1));
        std::string key;
        std::string value;
        if (parse_line(line, key, value) && has_key(key))
            keys.push_back(key);
    }
    return keys;
}

bool append_missing_defaults(const std::filesystem::path &path)
{
    std::set<std::string> written;
    for (const std::string &key : documented_keys(path))
        written.insert(key);

    const bool exists = std::filesystem::exists(path);
    if (exists)
    {
        bool complete = true;
        for (const OptionDef &entry : kOptions)
        {
            if (!written.count(entry.key))
            {
                complete = false;
                break;
            }
        }
        if (complete)
            return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        return false;

    std::ofstream out(path, exists ? std::ios::app : std::ios::out);
    if (!out)
        return false;

    if (!exists)
    {
        out << "# luogu-export settings\n"
            << "#\n"
            << "# INI style: [section] headers, one \"key: value\" pair per line\n"
            << "# (# starts a comment). \"=\" also works as the separator.\n"
            << "#\n"
            << "# [fonts]  fonts used in the exported LaTeX document.\n"
            << "#   A value is a font family name (Consolas, Noto Sans) or a font file\n"
            << "#   name (FandolSong-Regular.otf). It may also be the name of another\n"
            << "#   font key, to reuse it (e.g. \"sans_cjk: main_cjk\").\n"
            << "#   \"default\" keeps the built-in font of that place.\n"
            << "#\n"
            << "# [paths] cache directories. Relative paths are resolved against the\n"
            << "#   current directory; a leading ~ means $HOME. The defaults written\n"
            << "#   below are absolute paths resolved on the first run.\n"
            << "#\n"
            << "# [export] export defaults, e.g. the default output file name.\n"
            << "#\n"
            << "# Edit this file directly, or use:\n"
            << "#   luogu-export -O --list\n"
            << "#   luogu-export -O --show <key>\n"
            << "#   luogu-export -O --set <key> <value> [<key> <value> ...]\n";
    }
    else
    {
        out << "\n# ---- keys added by this version ----\n";
    }

    // 按节写出缺失的 key
    std::vector<std::string> sections;
    for (const OptionDef &entry : kOptions)
    {
        if (written.count(entry.key))
            continue;
        if (std::find(sections.begin(), sections.end(), entry.section) == sections.end())
            sections.push_back(entry.section);
    }
    for (const std::string &section : sections)
    {
        out << "\n[" << section << "]\n";
        for (const OptionDef &entry : kOptions)
        {
            if (std::string(entry.section) != section || written.count(entry.key))
                continue;
            out << "# " << entry.comment << "\n";
            const std::string value = option_default(entry);
            out << entry.key << ": " << quote_if_needed(value) << "\n";
            if (value != std::string(kDefault))
                out << "\n"; // 设置了字体的条目之间空一行，便于阅读
        }
    }
    return out.good();
}

const Values &shared()
{
    static const Values values = [] {
        Values loaded;
        load(loaded);
        return loaded;
    }();
    return values;
}

std::string raw_value(const Values &values, const std::string &key)
{
    auto it = values.find(key);
    if (it != values.end())
        return it->second;
    const OptionDef *entry = find_option(key);
    return entry ? option_default(*entry) : std::string(kDefault);
}

std::filesystem::path problem_cache_dir()
{
    const std::string value = raw_value(shared(), "problem_cache");
    return is_default(value) ? default_cache_base() : expand_path(value);
}

std::filesystem::path image_cache_dir()
{
    const std::string value = raw_value(shared(), "image_cache");
    return is_default(value) ? default_cache_base() / "images" : expand_path(value);
}

Resolution resolve_font(const Values &values, const std::string &key)
{
    Resolution result;
    std::vector<std::string> chain{key};
    std::string current = raw_value(values, key);

    while (true)
    {
        if (is_default(current))
            return result; // value 保持 default，reference 已在下面填好
        if (!has_key(current))
        {
            result.value = current; // 普通字体名
            return result;
        }

        // current 是一个已知的配置项 → 引用
        if (result.reference.empty())
            result.reference = current;

        const OptionDef *target = find_option(current);
        if (!target || std::string(target->section) != "fonts")
        {
            result.error = "references '" + current + "', which is not a font key";
            result.involved = {current};
            result.value = kDefault;
            return result;
        }

        // 环检测：current 已经在链上
        auto seen = std::find(chain.begin(), chain.end(), current);
        if (seen != chain.end())
        {
            std::string path;
            for (auto it = seen; it != chain.end(); ++it)
                path += *it + " -> ";
            path += current;
            result.error = "cycle in font references: " + path;
            result.involved.assign(seen, chain.end());
            result.value = kDefault;
            return result;
        }

        chain.push_back(current);
        current = raw_value(values, current);
    }
}

Values resolve_fonts(const Values &values)
{
    Values resolved;
    for (const std::string &key : all_keys())
    {
        if (key_section(key) != "fonts")
            continue;
        resolved[key] = resolve_font(values, key).value;
    }
    return resolved;
}

bool font_exists(const std::string &name)
{
    if (is_default(name))
        return true;

    const FontDb &db = font_db();
    if (!db.available)
        return true; // 无法校验时不要误报

    if (looks_like_file_name(name))
    {
        std::error_code ec;
        if (std::filesystem::exists(name, ec))
            return true;
        std::string base = name;
        const size_t slash = base.find_last_of("/\\");
        if (slash != std::string::npos)
            base = base.substr(slash + 1);
        if (db.basenames.count(util::to_lower_ascii(base)))
            return true;
        return kpsewhich_finds(name);
    }

    return db.families.count(util::to_lower_ascii(name)) > 0;
}

FontSetup prepare_fonts(const Values &values)
{
    FontSetup setup;
    std::map<std::string, std::vector<std::string>> missing; // 字体 -> 使用它的 key
    std::set<std::string> reported;                          // 问题去重

    for (const std::string &key : all_keys())
    {
        if (key_section(key) != "fonts")
            continue;

        const Resolution resolved = resolve_font(values, key);
        if (!resolved.error.empty())
        {
            std::vector<std::string> involved = resolved.involved;
            std::sort(involved.begin(), involved.end());
            if (reported.insert("ref:" + util::join_strings(involved, ",")).second)
                setup.problems.push_back({key, resolved.error});
            setup.resolved[key] = kDefault;
            continue;
        }

        if (is_default(resolved.value) || font_exists(resolved.value))
        {
            setup.resolved[key] = is_default(resolved.value) ? std::string(kDefault)
                                                            : resolved.value;
            continue;
        }

        missing[resolved.value].push_back(key);
        setup.resolved[key] = kDefault;
    }

    for (const auto &entry : missing)
    {
        setup.problems.push_back(
            {entry.second.front(),
             "font '" + entry.first + "' not found on this system (used by " +
                 util::join_strings(entry.second, ", ") + "); using the default font"});
    }
    return setup;
}

std::vector<Problem> validate_fonts(const Values &values)
{
    return prepare_fonts(values).problems;
}

bool set(const std::vector<std::pair<std::string, std::string>> &entries,
         std::string &error)
{
    for (const auto &entry : entries)
    {
        if (!has_key(entry.first))
        {
            error = "unknown key: '" + entry.first + "' (use -O --list to see all keys)";
            return false;
        }
    }

    Values current;
    load(current);
    Values candidate = current;
    for (const auto &entry : entries)
        candidate[entry.first] = entry.second;

    // 只校验本次修改的 key：已有的无关问题不应阻止修改
    std::vector<Problem> problems;
    std::set<std::string> reported; // 同一个环由多个 key 触发时只报一次
    for (const auto &entry : entries)
    {
        if (key_section(entry.first) == "fonts")
        {
            const Resolution resolved = resolve_font(candidate, entry.first);
            if (!resolved.error.empty())
            {
                std::vector<std::string> involved = resolved.involved;
                std::sort(involved.begin(), involved.end());
                if (reported.insert(util::join_strings(involved, ",")).second)
                    problems.push_back({entry.first, resolved.error});
            }
            else if (!is_default(resolved.value) && !font_exists(resolved.value))
                problems.push_back({entry.first, "font '" + resolved.value +
                                                     "' not found on this system (key '" +
                                                     entry.first + "')"});
        }
        else if (key_section(entry.first) == "paths")
        {
            if (trim(entry.second).empty())
                problems.push_back({entry.first, entry.first + " must not be empty"});
        }
        else
        {
            const std::string issue = validate_export_value(entry.first, entry.second);
            if (!issue.empty())
                problems.push_back({entry.first, issue});
        }
    }
    if (!problems.empty())
    {
        error = "cannot apply settings:";
        for (const Problem &problem : problems)
            error += "\n  " + problem.message;
        return false;
    }

    const std::filesystem::path path = config_path();
    if (!std::filesystem::exists(path))
    {
        if (!append_missing_defaults(path))
        {
            error = "cannot create config file '" + path.string() + "'";
            return false;
        }
    }

    std::vector<std::string> lines = read_lines(path);

    // 生效行优先；只有被注释掉的写法时退而记录该注释行，--set 会就地启用它
    std::unordered_map<std::string, size_t> positions;
    std::unordered_map<std::string, size_t> commented;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string line = trim(lines[i]);
        bool is_comment = false;
        while (!line.empty() && (line[0] == '#' || line[0] == ';'))
        {
            line = trim(line.substr(1));
            is_comment = true;
        }
        std::string key;
        std::string value;
        if (!parse_line(line, key, value) || !has_key(key))
            continue;
        if (is_comment)
            commented.emplace(key, i);
        else
            positions.emplace(key, i);
    }

    std::vector<std::pair<std::string, std::string>> to_append;
    for (const auto &entry : entries)
    {
        const std::string line = entry.first + ": " + quote_if_needed(entry.second);
        size_t index = std::string::npos;
        auto live = positions.find(entry.first);
        if (live != positions.end())
            index = live->second;
        else
        {
            auto off = commented.find(entry.first);
            if (off != commented.end())
                index = off->second;
        }

        if (index != std::string::npos)
            lines[index] = line;
        else
            to_append.push_back(entry);
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "cannot create directory '" + path.parent_path().string() + "': " + ec.message();
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        error = "cannot write config file '" + path.string() + "'";
        return false;
    }
    for (const std::string &line : lines)
        out << line << "\n";

    if (!to_append.empty())
    {
        out << "\n# ---- set via -O --set ----\n";
        std::vector<std::string> sections;
        for (const auto &entry : to_append)
        {
            const std::string section = key_section(entry.first);
            if (std::find(sections.begin(), sections.end(), section) == sections.end())
                sections.push_back(section);
        }
        for (const std::string &section : sections)
        {
            out << "\n[" << section << "]\n";
            for (const auto &entry : to_append)
            {
                if (key_section(entry.first) != section)
                    continue;
                out << "# " << key_comment(entry.first) << "\n";
                out << entry.first << ": " << quote_if_needed(entry.second) << "\n";
            }
        }
    }

    if (!out.good())
    {
        error = "failed to write config file '" + path.string() + "'";
        return false;
    }
    return true;
}

void print_list(const Values &values)
{
    std::printf("Settings file: %s\n", config_path().string().c_str());

    // 每个 key 的显示值：引用会附上解析结果，字体不在本机时标注 not found
    auto display_value = [&](const std::string &key) -> std::string {
        const std::string raw = raw_value(values, key);
        if (key_section(key) != "fonts")
            return raw;

        const Resolution resolved = resolve_font(values, key);
        if (!resolved.error.empty())
            return raw + " (invalid reference)";
        if (is_default(raw))
            return raw;
        if (resolved.reference.empty())
            return font_exists(raw) ? raw : raw + " (not found)";
        if (is_default(resolved.value))
            return raw + " (-> default)";
        if (!font_exists(resolved.value))
            return raw + " (-> " + resolved.value + ", not found)";
        return raw + " (-> " + resolved.value + ")";
    };

    // 按节、组分类输出
    std::vector<std::string> sections;
    for (const std::string &key : all_keys())
    {
        const std::string section = key_section(key);
        if (std::find(sections.begin(), sections.end(), section) == sections.end())
            sections.push_back(section);
    }

    for (const std::string &section : sections)
    {
        std::vector<std::string> groups;
        for (const std::string &key : all_keys())
        {
            if (key_section(key) != section)
                continue;
            const std::string group = key_group(key);
            if (std::find(groups.begin(), groups.end(), group) == groups.end())
                groups.push_back(group);
        }

        for (const std::string &group : groups)
        {
            std::vector<std::string> keys;
            for (const std::string &key : all_keys())
                if (key_section(key) == section && key_group(key) == group)
                    keys.push_back(key);

            size_t name_width = std::strlen("NAME");
            size_t value_width = std::strlen("VALUE");
            for (const std::string &key : keys)
            {
                name_width = std::max(name_width, key.size());
                value_width = std::max(value_width, display_value(key).size());
            }

            const char *title = section == "fonts" ? "Fonts"
                                : section == "paths" ? "Paths"
                                                     : "Export";
            std::printf("\n%s (%s)\n", title, group.c_str());
            std::printf("%-*s  %-*s  %s\n", static_cast<int>(name_width), "NAME",
                        static_cast<int>(value_width), "VALUE", "DESCRIPTION");
            for (const std::string &key : keys)
                std::printf("%-*s  %-*s  %s\n", static_cast<int>(name_width), key.c_str(),
                            static_cast<int>(value_width), display_value(key).c_str(),
                            key_comment(key).c_str());
        }
    }
}

std::string default_output_name(const std::string &extension,
                                std::vector<Problem> *problems)
{
    Values values;
    load(values);
    const std::string stem = raw_value(values, "default_title");
    const std::string issue = validate_export_value("default_title", stem);
    if (!issue.empty())
    {
        if (problems)
            problems->push_back({"default_title", issue + "; using 'problems'"});
        return "problems" + extension;
    }
    return trim(stem) + extension;
}

void print_warnings(const std::vector<Problem> &problems)
{
    for (const Problem &problem : problems)
        std::fprintf(stderr, "warning: %s\n", problem.message.c_str());
}
} // namespace settings
