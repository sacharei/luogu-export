// include/luogu-export/contents/problem.h
#ifndef LUOGU_EXPORT_CONTENTS_PROBLEM_H
#define LUOGU_EXPORT_CONTENTS_PROBLEM_H

#include <vector>
#include <utility>
#include <unordered_map>
#include <nlohmann/json.hpp>

typedef std::pair<std::string, std::string> pss;

namespace problem
{
    struct Problem
    {
    public:
        std::string pid, type;
        int difficulty; std::vector<std::string> tags; // 标签名（中文），与缓存中的名称一致
        std::string name, background, description, formatI, formatO, hint;
        std::vector<pss> samples;
        std::vector<int> time, memory;
        nlohmann::json translations; // 多语言题面；当前仅保留 en（可能为空对象）
        
        Problem();

        // 从缓存 NDJSON 中的一行（JSON 对象）构造题目。
        // tag_id_to_name 可选：当 JSON 里的 tags 是数字 ID 时用于翻译成名称。
        Problem(const nlohmann::json &data,
                const std::unordered_map<int, std::string> *tag_id_to_name = nullptr);
        
        // 扫描题面 markdown（背景/描述/输入输出格式/提示）中的图片链接，
        // 返回去重后的链接列表
        std::vector<std::string> image_urls() const;
    };
}

#endif // LUOGU_EXPORT_CONTENTS_PROBLEM_H
