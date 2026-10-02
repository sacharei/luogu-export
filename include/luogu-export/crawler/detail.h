// include/luogu-export/crawler/detail.h
#ifndef LUOGU_EXPORT_CRAWLER_DETAIL_H
#define LUOGU_EXPORT_CRAWLER_DETAIL_H

#include <string>

// crawler 各实现文件共享的内部工具（不属于公开 API）。
namespace crawler::detail
{
    // 带颜色的错误/成功信息（与旧行为一致：先 flush stdout 再输出）
    void print_error(const std::string &message);
    void print_success(const std::string &message);
}

#endif // LUOGU_EXPORT_CRAWLER_DETAIL_H
