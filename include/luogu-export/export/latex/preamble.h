// include/luogu-export/export/latex/preamble.h
#ifndef LUOGU_EXPORT_LATEX_PREAMBLE_H
#define LUOGU_EXPORT_LATEX_PREAMBLE_H

#include <cstdio>
#include "luogu-export/util/settings.h"

// 导出文档的 LaTeX 导言区（\documentclass 到 \begin{document} 之前）。
namespace latex::detail
{
    // 把导出文档所需的导言区写入 out。
    // @param fonts 已解析的字体设置（见 settings）；键缺失或值为空时跳过对应的
    //              字体设置，沿用 ctex / 当前的默认字体。
    void write_preamble(FILE *out, const settings::Values &fonts);
}

#endif // LUOGU_EXPORT_LATEX_PREAMBLE_H
