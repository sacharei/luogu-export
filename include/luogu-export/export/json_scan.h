// include/luogu-export/export/json_scan.h
#ifndef LUOGU_EXPORT_EXPORT_JSON_SCAN_H
#define LUOGU_EXPORT_EXPORT_JSON_SCAN_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// 在原始 NDJSON 文本上做的快速预筛：不做完整 JSON 解析，只在能严格证明
// “不可能命中筛选条件”时跳过该行，任何不确定情况都保守返回“可能命中”。
// 从 common.cpp 中独立出来，供 select_problems 使用。
namespace luogu::detail
{
    // 检查整行里是否存在包含 filter_tags 的 "tags" 数组。
    // 每个标签在数组里以“名字字符串”（去 BOM 后比较）或“数字 ID”任一种形式
    // 出现都算命中；返回 false 表示确定不命中，true 表示可能命中或无法可靠判断。
    // 实现上抽取一个共享的扫描函数：返回“已确认出现的名字位掩码”，
    // 遇到无法可靠判断的输入时通过 may_any 置位表示“可能命中”（保守放行）。
    struct TagScanResult
    {
        bool may_any = false; // 输入无法可靠判断（未闭合数组/含未知转义等）→ 保守视为可能命中
        uint64_t found = 0;   // 已确认出现的名字位掩码
    };

    TagScanResult raw_tags_scan(std::string_view line,
                                const std::vector<std::string> &names,
                                const std::vector<long> &ids);

    // 综合预筛：难度、类型、标签任一条件在原始文本上就确定不满足时返回 false。
    bool raw_may_match(std::string_view line,
                       const std::vector<int> &difficulties,
                       const std::vector<std::string> &filter_tags,
                       const std::vector<long> &filter_tag_ids,
                       const std::vector<std::string> &types);
}

#endif // LUOGU_EXPORT_EXPORT_JSON_SCAN_H
