// include/luogu-export/export/latex/preamble.h
#ifndef LUOGU_EXPORT_LATEX_PREAMBLE_H
#define LUOGU_EXPORT_LATEX_PREAMBLE_H

#include <cstdio>

// 导出文档的 LaTeX 导言区（\documentclass 到 \begin{document} 之前）。
namespace latex::detail
{
    // 把导出文档所需的导言区写入 out
    void write_preamble(FILE *out);
}

#endif // LUOGU_EXPORT_LATEX_PREAMBLE_H
