<div align="center">

# luogu-export

A command-line tool to export problems from Luogu

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-≥3.23-brightgreen.svg)](https://cmake.org/)

**English** | [简体中文](README.zh_cn.md)

</div>

---

## Introduction

**luogu-export** is a command-line tool to export problems from Luogu.  
It supports exporting to **Markdown** or **LaTeX** format, and provides filtering by certain criteria.

---

## Features

- Update problem and tag caches, support offline filtering and exporting
- Export to Markdown (default output `problems.md`)
- Export to LaTeX (default output `problems.tex`)
- Filter by tag (`--tag`), difficulty (`--difficulty`), type (`--type`) and language (`--lang`)

---

## Dependencies

- CMake (≥ 3.23)
- A C++17-capable compiler
- libcurl
- libxml2
- nlohmann_json
- zlib
- fontconfig

These dependencies are automatically discovered and linked in [CMakeLists.txt](CMakeLists.txt).

---

## Build

```bash
git clone https://github.com/sacharei/luogu-export.git
cd luogu-export
mkdir build && cd build
cmake ..
make -j
```

After building, the executable `luogu-export` is located in the `build/` directory.

---

## Usage

Show help and all available options:

```bash
./luogu-export -h
```

### Common Examples

- **Update problem list and tag cache** (generates/updates `tags.json`):

```bash
./luogu-export -U
```

- **Export all problems to Markdown** (default output `problems.md`):

```bash
./luogu-export -M
```

- **Export problems within a difficulty range** (e.g. difficulty 1 to 4):

```bash
./luogu-export -M --difficulty 1-4
```

- **Filter by tags** (repeat `--tag` for multiple tags; quote the tag name if it contains spaces):

```bash
# Multiple tags (following arguments can be appended directly)
./luogu-export -M --tag 贪心 模拟 --difficulty 1-4

# Tag name with spaces
./luogu-export -M --tag 'NOIP 普及组'
```

- **Specify an output filename**:

```bash
./luogu-export -M --output my_problems.md
```

- **Export to LaTeX** (default `problems.tex`; LaTeX mode hides difficulty/tag display settings):

```bash
./luogu-export -L --output problems.tex
```

- **List all tags and their IDs** (useful for filtering by ID or name):

```bash
./luogu-export --tags
```

- **Inspect or change the LaTeX font configuration**:

```bash
./luogu-export -O --list
./luogu-export -O --set section Arial section_cjk SimHei
```

After exporting, compile to PDF:

```bash
latexmk -xelatex problems.tex
```

---

## Configuration (fonts, paths and export defaults)

Settings for the LaTeX export live in one INI file:

```
$XDG_CONFIG_HOME/luogu-export/settings.conf   # usually ~/.config/luogu-export/settings.conf
```

It has three sections — `[fonts]`, `[paths]` and `[export]` — and is written automatically on
the first `-L` run, pre-filled with every key and its default value, so the initial output
matches the built-in look.

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

### Font values

A font value can be:

- a system font family name — `Consolas`, `Noto Sans`;
- a font file name — `FandolSong-Regular.otf`;
- the name of another font key, to reuse it — `sans_cjk: main_cjk`;
- `default`, meaning "do not set a font here" (keep the font this place would have had).

References are followed transitively. Settings are validated: a cycle in the references
(e.g. `main: main_cjk` plus `main_cjk: main`), a reference to a non-font key, and a font that
is not installed on this machine are all reported. `-O --set` refuses invalid values and
leaves the file untouched; exporting only warns and falls back to `default` for the affected
keys, so a typo never makes the document uncompilable.

Every key has two independent variants: the plain key sets the Latin font and the same key
with a `_cjk` suffix sets the Chinese font.

Some first-run defaults are themselves references, so changing one key also updates the
related ones:

- `subsection` and `subsubsection` follow `section` (their `_cjk` variants follow `section_cjk`);
- `code_keyword`, `code_comment`, `code_string`, `code_number` and `code_inline` follow
  `code_block` (their `_cjk` variants follow `code_block_cjk`).

### Bold text

`bold` / `bold_cjk` set the font used by `\textbf`. The normal `\bfseries` weight is applied
on top, so a family that ships a bold face is shown in real bold. Defaults: Consolas / SimHei.

### Cache paths

`problem_cache` is the directory of the problem cache (`latest.ndjson`, `tags.json`), used by
`-U`, `-M` and `-L`; `image_cache` is where problem images are saved. On the first run both
defaults are resolved and written as absolute paths (`$XDG_CACHE_HOME/luogu-export` and its
`images` subdirectory, falling back to `~/.cache/luogu-export`). A leading `~` in a value is
expanded to `$HOME`; relative paths are resolved against the current directory. The two paths
are independent.

### Keys

| Section | Group | Keys |
|---|---|---|
| `fonts` | global | `main`, `sans`, `mono`, `body` (each with `_cjk`) |
| `fonts` | structure | `title`, `toc`, `section`, `subsection`, `subsubsection`, `paragraph`, `header`, `footer` |
| `fonts` | body | `quote`, `list`, `table`, `table_header`, `table_caption`, `callout_title`, `callout_body`, `image_caption`, `footnote`, `link`, `url` |
| `fonts` | bold | `bold` (and `bold_cjk`) |
| `fonts` | tags | `tag` |
| `fonts` | code | `code_block`, `code_keyword`, `code_comment`, `code_string`, `code_number`, `code_inline` |
| `paths` | cache | `image_cache`, `problem_cache` |
| `export` | output | `default_title` |

`main` / `sans` / `mono` map to `\setmainfont` / `\setsansfont` / `\setmonofont`; the
remaining font keys are applied only at the corresponding place in the document.

`default_title` is the default output file name without extension: `problems` produces
`problems.md` for `-M` and `problems.tex` for `-L`. `--output` still overrides it.

### Editing the configuration from the command line

```bash
# print every key, its current value and a description, grouped by section
./luogu-export -O --list

# print the current value of one key
./luogu-export -O --show section

# set one or more keys (pairs of key and value)
./luogu-export -O --set section Arial section_cjk SimHei
./luogu-export -O --set tag default
./luogu-export -O --set default_title my_problems
./luogu-export -O --set image_cache ~/luogu-images
```

`--set` fails without changing anything if the number of arguments is odd, a key is unknown,
a reference would form a cycle, or a font is not installed; `--show` fails on an unknown key.

---

## Project Structure (partial)

```
luogu-export/
├── CMakeLists.txt
├── LICENSE
├── include/
│   └── luogu-export/
└── src/
    └── main.cpp
```

Command-line option parsing and core logic are implemented in:
- [`src/main.cpp`](src/main.cpp)
- Public headers in [`include/luogu-export/`](include/luogu-export)

---

## Contributing

Issues and pull requests are welcome.  
Please include a clear description of the change and add tests when applicable.

---

## Acknowledgements

This project was developed with assistance from [DeepSeek](https://www.deepseek.com/).

---

## License

This project is open-sourced under the **MIT License**. See [LICENSE](LICENSE) for details.
