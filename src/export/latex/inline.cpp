// src/export/latex/inline.cpp
#include "luogu-export/export/latex/inline.h"
#include "luogu-export/crawler/crawler.h"
#include "luogu-export/export/latex/image.h"
#include "luogu-export/export/latex/math.h"
#include "luogu-export/export/latex/text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace latex::detail
{
// 占位符：\x01R<n>\x02（注意用字符串拼接构造，避免 \x01R 被当作十六进制转义）
std::string placeholder(size_t index)
{
    return std::string(1, '\x01') + "R" + std::to_string(index) + std::string(1, '\x02');
}

// 恢复 \x01R<n>\x02 占位符
std::string restore_placeholders(const std::string &s, const std::vector<std::string> &raws)
{
    std::string out;
    size_t i = 0;
    while (i < s.size())
    {
        if (s[i] == '\x01' && i + 1 < s.size() && s[i + 1] == 'R')
        {
            size_t j = i + 2;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
                ++j;
            if (j < s.size() && s[j] == '\x02')
            {
                const int idx = std::stoi(s.substr(i + 2, j - i - 2));
                if (idx >= 0 && idx < static_cast<int>(raws.size()))
                    out += raws[static_cast<size_t>(idx)];
                i = j + 1;
                continue;
            }
        }
        out += s[i];
        ++i;
    }
    return out;
}

// 合并相邻的数学占位符：洛谷题面里 \$$ 等畸形写法会把一个公式拆成多段
// （段间可能夹着多余的 $ 和空白），这里把它们拼成一个，避免输出残缺公式
void merge_adjacent_math(std::string &s,
                         std::vector<std::string> &raws,
                         const std::vector<bool> &is_math)
{
    auto parse_placeholder = [&s](size_t p, size_t &idx, size_t &end) -> bool {
        if (p + 2 >= s.size() || s[p] != '\x01' || s[p + 1] != 'R')
            return false;
        size_t j = p + 2;
        while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])))
            ++j;
        if (j >= s.size() || s[j] != '\x02')
            return false;
        idx = static_cast<size_t>(std::stoul(s.substr(p + 2, j - p - 2)));
        end = j + 1;
        return true;
    };

    std::string out;
    size_t i = 0;
    while (i < s.size())
    {
        size_t idx = 0, end = 0;
        if (!parse_placeholder(i, idx, end) || idx >= is_math.size() || !is_math[idx])
        {
            out += s[i];
            ++i;
            continue;
        }

        // 块级公式 $$...$$ 不参与合并
        if (raws[idx].rfind("$$", 0) == 0)
        {
            out += s.substr(i, end - i);
            i = end;
            continue;
        }
        // 收集相邻的数学片段并合并：仅当间隙里含多余的 $ 才合并
        // （纯空白间隔的两个公式是独立的；如 $$...$$ 后跟 $...$ 不应合并）
        std::string merged = raws[idx];
        size_t run_end = end;
        while (run_end < s.size())
        {
            size_t gap_start = run_end;
            size_t gap_end = gap_start;
            bool gap_has_dollar = false;
            while (gap_end < s.size() &&
                   (s[gap_end] == '$' || std::isspace(static_cast<unsigned char>(s[gap_end]))))
            {
                if (s[gap_end] == '$')
                    gap_has_dollar = true;
                ++gap_end;
            }
            // 严格相邻（空间隙）也合并；只有非空且无 $ 的间隙（如 "$$...$$ 后跟 $...$"）才断开
            if (!gap_has_dollar && gap_end > gap_start)
                break;
            size_t nidx = 0, nend = 0;
            if (gap_end >= s.size() || !parse_placeholder(gap_end, nidx, nend) ||
                nidx >= is_math.size() || !is_math[nidx])
                break;
            if (raws[nidx].rfind("$$", 0) == 0)
                break;
            if (!merged.empty() && merged.back() == '$')
                merged.pop_back();
            for (size_t k = gap_start; k < gap_end; ++k)
                if (s[k] != '$')
                    merged += s[k]; // 保留空白，去掉多余的 $
            if (!raws[nidx].empty() && raws[nidx].front() == '$')
                merged += raws[nidx].substr(1);
            else
                merged += raws[nidx];
            run_end = nend;
        }
        raws.push_back(std::move(merged));
        out += "\x01R" + std::to_string(raws.size() - 1) + "\x02";
        i = run_end;
    }
    s = std::move(out);
}

// 图片自然宽度（pt）→ \luoguimg 的参数；无法得到像素尺寸时用 0 表示
// “交给 graphicx 自己决定自然尺寸”。
std::string format_natural_width(double width_pt)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", width_pt > 0 ? width_pt : 0.0);
    return buf;
}

// ulem 的 \sout 只会在空格/连字符处断行，中文删除线会被当成一个长单词而
// 溢出页面（Overfull \hbox）。中文可以在任意字符间换行，这里在每个 CJK
// 字符后插入 \allowbreak，让删除线文本能够像普通文本一样折行。
// \allowbreak{} 中的空组用于终止控制词（CJK 字符在 xeCJK 下是字母类字符，
// 直接紧跟控制词会被并入命令名）。
std::string allow_breaks_after_cjk(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + s.size() / 2);
    size_t i = 0;
    while (i < s.size())
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if (c >= 0xF0)
            len = 4;
        else if (c >= 0xE0)
            len = 3;
        else if (c >= 0xC0)
            len = 2;
        if (i + len > s.size())
            len = 1;
        out += s.substr(i, len);
        i += len;
        // 三字节及以上的 UTF-8 字符：CJK 汉字、全角标点、假名等
        if (c >= 0xE0)
            out += "\\allowbreak{}";
    }
    return out;
}

std::string inline_to_latex_impl(const std::string &text, std::vector<std::string> &raws,
                                 std::vector<bool> &is_math)
{
    auto protect = [&](std::string latex) {
        raws.push_back(std::move(latex));
        is_math.push_back(false);
        return placeholder(raws.size() - 1);
    };

    std::string s = text;

    // 1. 把 \$（转义美元符）保护起来，避免数学提取时把它的 $ 当成公式分隔符
    {
        const std::string dollar_sentinel = "\x01D\x02";
        std::string t;
        size_t p = 0, last = 0;
        while ((p = s.find("\\$", p)) != std::string::npos)
        {
            // \\$ 是“行分隔符 + 公式结束符”，不是转义美元符
            if (p > 0 && s[p - 1] == '\\')
            {
                p += 2;
                continue;
            }
            t += s.substr(last, p - last) + dollar_sentinel;
            p += 2;
            last = p;
        }
        t += s.substr(last);
        s = std::move(t);
    }
    // 2. 数学公式（原样保留）
    {
        static const std::regex re("(\\$\\$[^$]+\\$\\$|\\$[^$]+\\$)");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            raws.push_back(sanitize_math(m[1].str()));
            is_math.push_back(true);
            return placeholder(raws.size() - 1);
        });
    }
    // 2.5 合并相邻的数学片段（源数据畸形时一个公式可能被拆成多段）
    merge_adjacent_math(s, raws, is_math);
    // 3. 行内代码（放在数学之后：数学里的反引号是字面量，不应被当成代码分隔符）
    {
        // 支持 1~N 个反引号包裹的代码段（CommonMark 规则），
        // 避免 ``code`` 这类写法留下多余反引号
        static const std::regex re("`+([^`]+?)`+");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\texttt{" + escape_latex(m[1].str()) + "}");
        });
    }
    // 4. 图片包在链接里：[![](img)](url) → \href{url}{图片}；
    //     必须先于普通链接处理，否则内层 ] 会破坏链接解析
    {
        static const std::regex re(
            R"(\[!\[[^\]]*\]\s*\(\s*([^\s)]+)(?:\s+["'][^"']*["'])?\s*\)\s*\]\s*\(\s*([^\s)]+)(?:\s+["'][^"']*["'])?\s*\))");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            const std::string img_url = m[1].str();
            const std::string link_url = m[2].str();
            if (is_data_uri(img_url) || is_data_uri(link_url))
                return std::string(); // data URI 直接丢弃
            if (!looks_like_url(img_url))
                return m[0].str(); // 内层不是可下载的图片链接，保留原文
            if (is_video_url(link_url) || is_video_url(img_url))
                return protect(video_link_latex(link_url));
            // 按缓存文件真实内容判断能否加载，扩展名与内容不符的图片
            // 先经 prepare_cached_image 归一化（修正密度/补扩展名）
            const std::filesystem::path usable =
                prepare_cached_image(crawler::image_cache_path(img_url));
            if (usable.empty())
                return std::string(); // xelatex 无法加载的格式，直接跳过
            const std::string path = escape_path(usable.string());
            const std::string img = "\\luoguimg{" + path + "}{" +
                                    format_natural_width(image_natural_width_pt(usable)) + "}";
            // 链接目标可能是站内相对地址（如 /problem/P1）
            const std::string link_abs = resolve_url(link_url);
            if (link_abs.empty())
                return protect(img); // 链接目标不可用：只显示图片
            return protect("\\href{" + escape_latex(bilibili_https_url(link_abs)) + "}{" + img + "}");
        });
    }
    // 4.5 图片 → 缓存文件；视频 → 仅链接
    {
        static const std::regex re("!\\[[^\\]]*\\]\\s*\\(\\s*([^\\s)]+)(?:\\s+[\"'][^\"']*[\"'])?\\s*\\)");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            const std::string url = m[1].str();
            if (is_data_uri(url))
                return std::string(); // data URI 直接丢弃
            if (!looks_like_url(url))
                return m[0].str(); // 不是真正的图片链接，保留原文（转义阶段处理）
            if (is_video_url(url))
                return protect(video_link_latex(url));
            // 按缓存文件真实内容判断能否加载：GIF/WebP/SVG/BMP/ICO 等
            // xelatex 无法加载的格式直接跳过；扩展名与内容不符的图片
            // 先经 prepare_cached_image 归一化（修正密度/补扩展名）
            const std::filesystem::path usable =
                prepare_cached_image(crawler::image_cache_path(url));
            if (usable.empty())
                return std::string();
            const std::string path = escape_path(usable.string());
            return protect("\\luoguimg{" + path + "}{" +
                           format_natural_width(image_natural_width_pt(usable)) + "}");
        });
    }
    // 5. 链接
    {
        static const std::regex re("\\[([^\\]]*)\\]\\s*\\(\\s*([^\\s)]+)(?:\\s+[\"'][^\"']*[\"'])?\\s*\\)");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            const std::string text = inline_to_latex_impl(m[1].str(), raws, is_math);
            const std::string dest = m[2].str();
            if (is_data_uri(dest))
                return protect(text); // data URI 链接：只保留链接文字
            // 站内相对地址（/problem/P1）补全为绝对地址；bilibili: 协议同样转换
            const std::string abs = resolve_url(dest);
            if (abs.empty())
            {
                // 不是可解析的链接目标：
                // - 空链接文字（如洛谷防 AI 的 [](一段说明文字)）整段丢弃，
                //   与网页上“不可见链接”的效果一致；
                // - 其余保留原文，避免破坏题面里展示 markdown 语法的示例。
                if (m[1].str().empty())
                    return std::string();
                return m[0].str();
            }
            return protect("\\href{" + escape_latex(bilibili_https_url(abs)) + "}{" + text + "}");
        });
    }
    // 6. 自动链接 <https://...>
    {
        static const std::regex re("<(https?://[^>]+)>");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\url{" + m[1].str() + "}");
        });
    }
    // 7. 粗体
    {
        static const std::regex re("\\*\\*([^*]+)\\*\\*");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\textbf{" + inline_to_latex_impl(m[1].str(), raws, is_math) + "}");
        });
    }
    // 8. 斜体（下划线形式的斜体不转换，避免误伤标识符中的 _）
    {
        static const std::regex re("\\*([^*]+)\\*");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            return protect("\\textit{" + inline_to_latex_impl(m[1].str(), raws, is_math) + "}");
        });
    }
    // 9. 删除线
    {
        static const std::regex re("~~([^~]+)~~");
        s = regex_transform(s, re, [&](const std::smatch &m) {
            // \sout 不会在中文间断行，长删除线文本会溢出页面；
            // allow_breaks_after_cjk 插入断行点让删除线可以折行
            return protect("\\sout{" +
                           allow_breaks_after_cjk(
                               inline_to_latex_impl(m[1].str(), raws, is_math)) +
                           "}");
        });
    }
    // 10. 转义剩余特殊字符
    s = escape_latex(s);
    // 11. 恢复占位符。链接/粗体等递归生成的片段内部可能还嵌着占位符，
    //     需要迭代还原直到不再出现 \x01
    std::string result = restore_placeholders(s, raws);
    while (result.find('\x01') != std::string::npos)
        result = restore_placeholders(result, raws);
    // 还原被保护的 \$（转义美元符）
    {
        const std::string dollar_sentinel = "\x01D\x02";
        size_t p = 0;
        while ((p = result.find(dollar_sentinel, p)) != std::string::npos)
        {
            result.replace(p, dollar_sentinel.size(), "\\$");
            p += 2;
        }
    }
    return result;
}

std::string inline_to_latex(const std::string &text)
{
    // 占位符池只建一次；粗体/链接等递归调用共用同一池，
    // 否则嵌套占位符在递归中无法还原会死循环
    std::vector<std::string> raws;
    std::vector<bool> is_math;
    return inline_to_latex_impl(text, raws, is_math);
}
} // namespace latex::detail
