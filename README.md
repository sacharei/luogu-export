# luogu-export

一个用 C++17 编写的命令行工具，用于抓取 [洛谷](https://www.luogu.com.cn/) 的题目列表与标签，并按条件筛选题目，导出为 **Markdown** 或 **LaTeX** 文档（便于离线阅读、打印成题册）。

## 功能特性

- **更新缓存**：从洛谷 CDN 下载全量题目列表（`problemset-open/latest.ndjson.gz`，gzip 解压后为 `latest.ndjson`），并从官方标签接口下载标签对照表（`tags.json`）。
- **按条件筛选题目**：
  - 按**标签**筛选（多个标签取「且」，即题目必须同时包含所有标签）；
  - 按**难度**筛选（支持单个数字 `0-8` 或闭区间 `1-4`，多个取「或」）；
  - 按**题目类型**筛选（`B` 基础题 / `P` 普通题）；
  - 按**题面语言**筛选（`zh-CN` / `en`，英文缺失时自动回退中文）。
- **导出 Markdown**（`-M`）：每题一个章节，包含难度、标签、时空限制、题目背景、题目描述、输入/输出格式、样例、说明/提示。
- **导出 LaTeX**（`-L`）：生成可直接用 `xelatex` 编译的完整 `.tex` 文档（含文档类、宏包、目录、页眉、标签徽章样式等），并内置大量针对洛谷题面公式/格式「坑」的自动修复。
- **图片下载**：并行下载题面中的图片到本地缓存；导出 LaTeX 时图片引用会替换为缓存文件路径。
- **标签 ID 对照表**（`--tags`）：按官方分类打印标签名称与数字 ID。

## 依赖与构建

- 编译标准：C++17
- 构建工具：CMake（>= 3.23）
- 外部库：
  - [libcurl](https://curl.se/)（网络请求 / 下载）
  - [libxml2](https://gitlab.gnome.org/GNOME/libxml2)（HTML 解析）
  - [nlohmann/json](https://github.com/nlohmann/json)（JSON 解析）
  - [zlib](https://zlib.net/)（gzip 解压）

```bash
cmake CMakeLists.txt
make
```

构建产物为可执行文件 `luogu-export`。

## 使用方法

```
Usage: luogu-export [options]

Options:
  -U, --update    Update the problem list and tag caches
  -M, --markdown  Export problems to a markdown file (all problems if no filter given)
  -L, --latex     Export problems to a LaTeX document file (all problems if no filter given)
      --tags              List all tags with their numeric IDs, grouped by category
      --tag <name|ID>...  Filter by tag (a problem must contain all given tags)
      --difficulty <spec> Filter by difficulty: numbers 0-8, ranges like 1-4
      --type <B|P>        Filter by problem type (repeatable; empty means all types)
      --lang <zh-CN|en>   Problem statement language (default: zh-CN)
      --show <NN>         Show flags for -M only: first bit = difficulty, second bit = tags
      --output <file>     Output file (default: problems.md / problems.tex)
  -h, --help      Show this help message
```

### 示例

```bash
# 1. 首次使用先更新题目列表与标签缓存
luogu-export -U

# 2. 导出全部题目为 Markdown（默认输出 problems.md）
luogu-export -M

# 3. 按标签与难度筛选后导出
luogu-export -M --tag 模拟 贪心 --difficulty 3-5

# 4. 按类型和语言筛选，导出为 LaTeX（默认输出 problems.tex）
luogu-export -L --type P --lang zh-CN --output 题册.tex

# 5. 查看所有标签及其数字 ID
luogu-export --tags
```

### 参数说明

| 选项 | 含义 |
| --- | --- |
| `-U, --update` | 更新题目列表缓存（`latest.ndjson`）与标签缓存（`tags.json`） |
| `-M, --markdown` | 筛选并导出 Markdown（默认输出 `problems.md`） |
| `-L, --latex` | 筛选并导出 LaTeX（默认输出 `problems.tex`） |
| `--tags` | 按官方分类打印标签 ID 对照表（可与 `-h` 组合） |
| `--tag <name\|ID>...` | 按标签筛选；多个值可用空格分隔或重复 `--tag`，题目须包含全部标签；引号整体恰好等于已知标签名（如 `"NOIP 普及组"`）时按一个标签处理 |
| `--difficulty <spec>` | 按难度筛选；支持 `0-8`、区间 `1-4`，多个用空格分隔或重复传入（任一命中即可） |
| `--type <B\|P>` | 按题目类型筛选（可重复，空表示全部类型） |
| `--lang <zh-CN\|en>` | 题面语言（默认 `zh-CN`；`en` 缺失时回退中文） |
| `--show <NN>` | 仅 `-M` 有效：第 1 位=是否显示难度，第 2 位=是否显示标签（默认 `11`）；隐藏标签仅隐藏「算法」类标签，其他类型始终显示 |
| `--output <file>` | 输出文件路径（默认 `problems.md` / `problems.tex`） |
| `-h, --help` | 显示帮助 |

## 缓存机制

缓存目录按以下顺序确定：

1. 环境变量 `XDG_CACHE_HOME` 存在时 → `$XDG_CACHE_HOME/luogu-export`；
2. 否则使用 `$HOME/.cache/luogu-export`；
3. 否则使用系统临时目录下的 `luogu-export`。

缓存目录中的文件：

| 文件 | 说明 |
| --- | --- |
| `latest.ndjson` | 全量题目列表（每行一个题目的 JSON），由 `-U` 下载并解压得到 |
| `tags.json` | 标签对照表：`{"<数字ID>": {"name": "<名称>", "type": <分类>}}` |
| `images/` | 图片缓存目录，文件名由完整 URL 生成 |

图片文件名由完整链接生成（特殊字符替换为 `_`，过长时截断并附 FNV-1a 哈希），避免不同图床的同名图片互相覆盖；已存在的文件会跳过。下载时按 CPU 核心数并行，洛谷图床（`luogu.com.cn`）的图片会串行下载并保持 0.5~3 秒随机间隔，避免请求过快。

## 导出格式说明

- **Markdown**：文件头包含题目总数与筛选条件；每道题以 `---` 分隔，`# <题号> <标题>` 为章节，随后是难度、标签、时空限制，以及各题面小节与样例代码块。
- **LaTeX**：生成完整可编译文档（`\documentclass{book}`），带目录、页眉页脚、章节无序号（`secnumdepth=-1`），并内置多种自定义命令与颜色别名以兼容洛谷题面。图片仅在缓存中存在时通过 `\IfFileExists` 引用，缺失图片不会导致编译失败；GIF/WebP/SVG/BMP/ICO 等 xelatex 无法加载的格式会被跳过，视频（Bilibili 等）只输出链接。
- 导出后请使用 `latexmk --xelatex problems.tex`。

## 许可证

[MIT](LICENSE)，版权 © 2026 sacharei。

## 鸣谢

本项目部分代码由 [DeepSeek](https://www.deepseek.com/) 辅助生成。
