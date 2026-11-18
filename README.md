# UEdit

[![build](https://github.com/ming2502/Uedit/actions/workflows/build.yml/badge.svg)](https://github.com/ming2502/Uedit/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/ming2502/Uedit)](https://github.com/ming2502/Uedit/releases)
[![license](https://img.shields.io/github/license/ming2502/Uedit)](LICENSE)

UEdit 是一个原生 Windows 大文件查看、搜索与抽取工具，使用 C++20 + Win32 +
Direct2D/DirectWrite 编写，界面自绘，支持 Windows 11 Mica。

目标很直接：**秒开 1GB+ 的单文件，全文搜索要快，内存占用要小**，并且能把命中内容
导出或剪切到新文件。

## 功能

- **大文件**：基于只读内存映射与稀疏行索引，1GB 文件立即可交互，支持 5GB+。
- **搜索**：字面量 / 忽略大小写 / 整词 / PCRE2 正则；支持 UTF-8、ANSI、UTF-16 LE/BE；
  多线程分块搜索，命中密度显示在滚动条上。
- **结果弹窗**：完整行上下文、命中高亮、`F3` 依次跳转；右侧操作列支持
  保存为新文件并打开、导出选项、转到所选、复制命中、重新搜索。
- **抽取 / 剪切**：按命中行、命中片段或带上下文抽取；剪切通过临时文件原子替换
  源文件，可选 `.bak` 备份。
- **多标签页**：每个标签独立保存滚动位置、光标、撤销栈与搜索结果；
  `Ctrl+Tab` / `Ctrl+W`，中键关闭，拖拽打开多个文件。
- **跳行**：`Ctrl+G` 输入行号直接定位。
- **编辑**：≤32MiB 的文本文件可编辑，支持输入、删除、选区替换、粘贴、撤销/重做与
  `Ctrl+S` 原子保存；UTF-8 / UTF-16 / ANSI 与 BOM 保持不变。
- **界面**：深色 / 浅色 / 跟随系统三种主题；Windows 11 Mica；150% 等缩放正常。
- 设置面板、最近文件、会话恢复、外部修改检测与 `F5` 重载。

## 性能（本机实测）

| 场景 | 结果 |
|------|------|
| 1GB 索引 | 约 0.42s |
| 1GB 字面量搜索 | 约 63ms |
| 1GB 正则搜索 | 约 63ms |
| 5GB 索引 / 搜索 | 2.13s / 327ms |
| 5GB 私有内存 | 11.8MB（工作集含 mmap 可回收） |
| 1GB 帧时间（Composition） | 1.7–4.7ms |

测试机：Windows 11，E: 机械盘（冷读受限），C: NVMe。

## 构建

要求：

- Windows 10 1809+ / Windows 11，x64
- Visual Studio 2022（含 C++ 桌面开发工作负载）或 VS 2022 Build Tools
- CMake 3.21+
- Windows SDK 10

```powershell
cmake --preset vs2022-x64 --fresh
cmake --build --preset release
```

产物：`build/Release/uedit.exe`（静态 CRT，无 VC++ 运行时依赖）。

单元测试：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

推送到 `main` 或提交 PR 时，GitHub Actions 会自动配置、构建、运行测试并打包；
推送 `v*` 标签会创建 GitHub Release 并附上便携版 zip 与 MSIX。

## 打包

```powershell
.\packaging\package_portable.ps1   # dist/UEdit-<version>-win64.zip
.\packaging\build_msix.ps1         # dist/UEdit_<version>_x64.msix（未签名）
.\packaging\verify_packages.ps1    # 校验两份 exe 与构建输出 SHA256 一致
```

正式分发前请用真实证书签名：

```powershell
.\packaging\sign_release.ps1 -PfxPath C:\path\codesign.pfx -PfxPassword "***"
```

## 命令行

```
uedit.exe [文件] [选项]

--search=TEXT      启动时搜索
--goto=LINE        启动时跳转到行
--zoom=N           界面缩放百分比
--theme=dark|light|system
--regex            正则搜索
--palette          打开命令面板
--settings         打开设置
--extract-to=PATH  导出命中
--cut              剪切命中
```

快捷键：`Ctrl+O` 打开、`Ctrl+F` 搜索、`F3` 下一个命中、`F4` 结果弹窗、
`Ctrl+G` 跳行、`Ctrl+W` 关闭标签、`F5` 重载、`F9` 设置、`Ctrl+=` / `Ctrl+-` / `Ctrl+0` 缩放。

配置、日志与崩溃转储位于 `%LOCALAPPDATA%\UEdit`。

## 已知限制

- 编辑仅支持 ≤32MiB 的文本文件；更大文件、二进制与只读文件保持只读浏览。
- 正则捕获组抽取尚未实现；按行/片段抽取可用。
- 搜索结果与导出上限 100 万条命中（弹窗默认列出前 5000 条）。
- 标签页不跨重启恢复；不支持拖拽重排或标签溢出列表。
- Mica 仅 Windows 11；Windows 10 下使用纯色界面。
- 超长行显示上限 8192 字节；尚无像素级平滑滚动。

## 许可

本项目使用 [MIT License](LICENSE)。

第三方组件：PCRE2 10.44（BSD 许可），许可文本见
[third_party/pcre2-10.44/LICENCE](third_party/pcre2-10.44/LICENCE)。
