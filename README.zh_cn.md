<div align="center">

# luogu-export

一个用于从洛谷导出题目的命令行工具

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-≥3.23-brightgreen.svg)](https://cmake.org/)

[English](README.md) | **简体中文**

</div>

---

## 简介

**luogu-export** 是一个命令行工具，用于从洛谷（Luogu）导出题目。  
它支持导出为 **Markdown** 或 **LaTeX** 格式，并支持按标一定标准行筛选过滤。

---

## 特性

- 新题目与标签缓存，支持离线筛选与导出
- 导出为 Markdown（默认输出 `problems.md`）
- 导出为 LaTeX（默认输出 `problems.tex`）
- 按标签（`--tag`）、难度（`--difficulty`）、题型（`--type`）和语言（`--lang`）筛选

## 依赖
- CMake（≥ 3.23）
- 支持 C++17 的编译器
- libcurl
- libxml2
- nlohmann_json
- zlib
- fontconfig

这些依赖项在 [CMakeLists.txt](CMakeLists.txt) 中自动发现并链接。

---

## 构建

```bash
git clone https://github.com/sacharei/luogu-export.git
cd luogu-export
mkdir build && cd build
cmake ..
make -j
```

构建完成后，可执行文件 `luogu-export` 位于 `build/` 目录下。

---

## 使用方法

查看帮助与所有可用选项：

```bash
./luogu-export -h
```

### 常见示例

- **更新题目列表与标签缓存**（生成 / 更新 `tags.json`）：

```bash
./luogu-export -U
```

- **导出所有题目为 Markdown**（默认输出 `problems.md`）：

```bash
./luogu-export -M
```

- **按难度范围导出**（例如难度 1 到 4）：

```bash
./luogu-export -M --difficulty 1-4
```

- **按标签筛选**（可重复 `--tag` 指定多个标签，标签名含空格时请加引号）：

```bash
# 多个标签（后续可直接加参数）
./luogu-export -M --tag 贪心 模拟 --difficulty 1-4

# 标签名包含空格
./luogu-export -M --tag 'NOIP 普及组'
```

- **指定输出文件名**：

```bash
./luogu-export -M --output my_problems.md
```

- **导出为 LaTeX**（默认 `problems.tex`，LaTeX 模式下会隐藏难度 / 标签显示设置）：

```bash
./luogu-export -L --output problems.tex
```

- **列出所有标签及其 ID**（便于按 ID 或名称筛选）：

```bash
./luogu-export --tags
```

- **查看或修改 LaTeX 字体配置**：

```bash
./luogu-export -O --list
./luogu-export -O --set section Arial section_cjk SimHei
```

导出后编译为 pdf：

```bash
latexmk -xelatex problems.tex
```

---

## 配置（字体、缓存路径与导出默认值）

LaTeX 导出的各项设置集中在一个 INI 文件里：

```
$XDG_CONFIG_HOME/luogu-export/settings.conf   # 一般是 ~/.config/luogu-export/settings.conf
```

文件分三节：`[fonts]`（导出文档里各处的字体）、`[paths]`（缓存路径）与 `[export]`
（导出默认值）。第一次运行 `-L` 时会自动创建，里面已写好全部 key 与默认值，因此首次导出
的效果与程序内置的字体完全一致。

```ini
[fonts]
section: Consolas
section_cjk: SimHei
bold: Consolas
bold_cjk: SimHei

[paths]
image_cache: /home/you/.cache/luogu-export/images
problem_cache: /home/you/.cache/luogu-export

[export]
default_title: problems
```

### 字体取值

字体取值可以是：

- 系统字体族名：`Consolas`、`Noto Sans`；
- 字体文件名：`FandolSong-Regular.otf`；
- 另一个字体 key 的名字，表示复用它：`sans_cjk: main_cjk`；
- `default`：这里不设置字体，沿用原本的字体。

引用会逐层解析。程序会校验配置：引用成环（如同时写 `main: main_cjk` 与
`main_cjk: main`）、引用到非字体 key、字体未安装在本机，都会被报出来。`-O --set` 遇到
这些问题会直接报错且不修改文件；导出时只给出警告并把有问题的 key 回退为 `default`，
所以写错字体名不会导致文档编译不出来。

每个 key 都有两个互相独立的版本：不带 `_cjk` 的负责西文，带 `_cjk` 的负责中文。

部分首次运行的默认值本身就是引用，改一个 key 会连带影响相关的几项：

- `subsection`、`subsubsection` 跟随 `section`（对应的 `_cjk` 跟随 `section_cjk`）；
- `code_keyword`、`code_comment`、`code_string`、`code_number`、`code_inline` 跟随
  `code_block`（对应的 `_cjk` 跟随 `code_block_cjk`）。

### 粗体

`bold` / `bold_cjk` 控制 `\textbf` 使用的字体，并在其上保留正常的 `\bfseries` 加粗
（字体自带粗体字形时就是真粗体）。默认值：Consolas / SimHei。

### 缓存路径

`problem_cache` 是题目缓存的目录（`latest.ndjson`、`tags.json`），`-U` / `-M` / `-L`
都会用它；`image_cache` 是题目图片的保存目录。第一次运行时这两个默认值会被解析成绝对
路径写进配置（`$XDG_CACHE_HOME/luogu-export` 及其下的 `images`，没有该环境变量时回退
到 `~/.cache/luogu-export`）。值开头的 `~` 会展开为 `$HOME`；相对路径按当前目录解析。
两个路径互相独立。

### key 一览

| 节 | 分组 | key |
|---|---|---|
| `fonts` | global | `main`、`sans`、`mono`、`body`（各自都有 `_cjk`） |
| `fonts` | structure | `title`、`toc`、`section`、`subsection`、`subsubsection`、`paragraph`、`header`、`footer` |
| `fonts` | body | `quote`、`list`、`table`、`table_header`、`table_caption`、`callout_title`、`callout_body`、`image_caption`、`footnote`、`link`、`url` |
| `fonts` | bold | `bold`（以及 `bold_cjk`） |
| `fonts` | tags | `tag` |
| `fonts` | code | `code_block`、`code_keyword`、`code_comment`、`code_string`、`code_number`、`code_inline` |
| `paths` | cache | `image_cache`、`problem_cache` |
| `export` | output | `default_title` |

`main` / `sans` / `mono` 分别对应 `\setmainfont` / `\setsansfont` / `\setmonofont`，
其余字体 key 只在文档中对应的位置生效。

`default_title` 是默认导出文件名的主干（不含扩展名）：`problems` 会让 `-M` 输出
`problems.md`、`-L` 输出 `problems.tex`；显式给了 `--output` 时仍以 `--output` 为准。

### 用命令行修改配置

```bash
# 按节分类列出所有 key 的当前取值与说明
./luogu-export -O --list

# 查看某个 key 的当前取值
./luogu-export -O --show section

# 设置一个或多个 key（成对的 key 与 value）
./luogu-export -O --set section Arial section_cjk SimHei
./luogu-export -O --set tag default
./luogu-export -O --set default_title my_problems
./luogu-export -O --set image_cache ~/luogu-images
```

`--set` 在参数个数为奇数、key 不存在、引用会成环、字体未安装时会报错且不修改文件；
`--show` 在 key 不存在时报错。

## 项目结构（部分）

```
luogu-export/
├── CMakeLists.txt
├── LICENSE
├── include/
│   └── luogu-export/
└── src/
    └── main.cpp
```

命令行选项与核心逻辑实现在：
- [`src/main.cpp`](src/main.cpp)
- 公共头文件位于 [`include/luogu-export/`](include/luogu-export)

---

## 贡献

欢迎提交 Issue 和 Pull Request。  
请尽量附带清晰的变更说明，并在适用时添加相关测试。

---

## 致谢

本项目部分代码由 [DeepSeek](https://www.deepseek.com/) 辅助生成。

---

## 许可证

本项目基于 **MIT 许可证** 开源。详见 [LICENSE](LICENSE) 文件。