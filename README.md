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

After exporting, compile to PDF:

```bash
latexmk -xelatex problems.tex
```

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
