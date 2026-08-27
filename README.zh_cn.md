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

这些依赖项在 [CMakeLists.txt](CMakeLists.txt) 中自动发现并链接。

---

## 构建

```bash
git clone <此仓库地址>
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

导出后编译为 pdf：

```bash
latexmk -xelatex problems.tex
```

---

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