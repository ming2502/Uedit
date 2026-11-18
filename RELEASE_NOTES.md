# UEdit 1.1.1.0 — Release Notes

发布日期：2026-11-18
平台：Windows 10 1809+ / Windows 11，x64

UEdit 是一个原生 Windows 大文件查看、搜索与抽取工具。打开 1GB 文件立即可交互，
全文搜索（字面量 / 正则 / UTF-16）、按行跳转、把命中内容导出或剪切到新文件，
界面使用自绘 Direct2D 与 Windows 11 Mica。

## 产物

| 文件 | 说明 |
|------|------|
| `dist/UEdit-1.1.1-win64.zip` | 便携版，解压即用，静态 CRT，无 VC++ 运行时依赖 |
| `dist/UEdit_1.1.1.0_x64.msix` | MSIX 安装包（**未签名**，安装前需签名） |
| `dist/test-signing/` | 自签名测试产物（已签名 exe/MSIX）与公钥证书，仅供测试 |

## 1.1.1 修复（2026-11-18）

- **文件对话框居中**：打开与导出文件改用系统 `IFileDialog`；对话框在创建
  与 `WM_WINDOWPOSCHANGING` 阶段就按最终尺寸给出居中坐标，首次可见时即在
  主窗口中心，不再出现“先显示、后跳转”的重定位。
- **欢迎页缩放/调整窗口异常**：欢迎页背景由 70% 半透明改为不透明，
  修复放大缩小、还原/最大化后出现的背景矩形和欢迎文字重影。
- **搜索结果列表滚动**：垂直与水平滚动条均可拖动，也可以直接按住结果列表
  上下/左右拖动平移；`Shift+滚轮` 横向滚动；结果保留整行不再截断
  （超长行安全上限 16384 字符），行号保持固定。
- **搜索框粘贴**：搜索框支持 `Ctrl+V`、`Shift+Insert` 与 `WM_PASTE`；
  多行/制表符自动转为空格，长查询显示末尾 64 字符；跳转行只保留数字，
  命令面板输入框同样支持粘贴。
- **焦点与光标**：搜索/跳转输入框显示强调色描边，输入光标闪烁；
  输入区与编辑区显示 I 型光标；结果弹窗的列表、操作按钮、
  垂直/水平滚动条统一使用系统标准箭头光标。
- **F3 修正**：弹窗打开时按 `F3` 会关闭弹窗并跳到下一条命中；连续 `F3`
  依次前进，不再停在同一条。
- **设置入口**：移除标题栏“设置”按钮；主快捷键改为 `F9`
  （`Ctrl+,` 在部分中文输入法下会被系统拦截，代码中仍保留兼容处理）。
  首页提示已改为 `F9 设置`。
- **默认主题**：默认改为“跟随系统”；设置持久化 `system`，
  系统深浅色变化时即时更新。
- 深/浅主题、Logo 与弹窗按钮居中修复保持有效。

## 1.1 新增

- **多文件标签页**：窗口顶部标签条，支持点击切换、`×`/中键关闭、`+` 打开、
  `Ctrl+Tab` / `Ctrl+Shift+Tab` 循环、`Ctrl+W` 关闭；每个标签独立保存滚动位置、
  光标、撤销栈与搜索结果；同名文件重复打开会激活已有标签。
- **搜索结果弹窗**：搜索后自动弹出，左侧为行号与整行上下文，右侧操作列提供
  “保存并打开 / 导出选项 / 转到所选 / 复制命中 / 重新搜索 / 关闭”。
  “保存并打开”把命中写入 `<源文件>.hits.txt` 并在新标签页打开；
  `F4` 可重新打开最近一次结果，`F3` 跳到下一个命中。
- **主题模式**：深色 / 浅色 / 跟随系统三种模式。设置面板可切换，
  `Ctrl+Shift+L` 循环，CLI `--theme=system` 可直接启动；
  跟随系统时监听 `WM_SETTINGCHANGE`/`WM_THEMECHANGED`，系统深浅色变化即时生效。
- **配色重设计**：深色采用深青灰底 `#0F1419` + 高对比文本 `#E8EDF2`，
  浅色采用 `#F6F8FA` + `#0F172A`；主色 `#2563EB`，浅色强调文本 `#7CB0FF`。
  正文、次要文字、状态栏、按钮、行号与高亮均按 WCAG AA 校验
  （正文 15.7:1，次要文字 ≥4.5:1，主按钮白字 5.17:1）。
- **视觉修复**：标题栏左上角 U 形图标改为居中绘制（不再顶到方框边缘）；
  搜索结果弹窗右侧按钮文字改为在按钮内垂直居中。
- **渲染修复**：Composition 交换链目标改为每帧绑定当前后台缓冲，修复半透明
  标题/状态栏中的上一帧文字残影。
- 编辑类文件搜索命中行号修正；结果上下文改为从行首截取。

## 主要能力

- 秒开 1GB+ 文件；稀疏行索引；跳转任意行。
- 字面量 / 忽略大小写 / 整词 / PCRE2 正则搜索；UTF-8、ANSI、UTF-16 LE/BE。
- 搜索历史、搜索结果弹窗（命中高亮、点击/F3 跳转）、滚动条命中密度。
- 选区、`Ctrl+C`、水平滚动、`Ctrl+=`/`Ctrl+-`/`Ctrl+0` 缩放。
- 小文件编辑（≤32MiB，文本且非只读）：输入、Enter、Backspace/Delete、选区替换、
  粘贴/剪切、撤销/重做、`Ctrl+S` 原子保存；UTF-8/UTF-16/ANSI 与 BOM 保持。
- 抽取到新文件：匹配行 / 匹配片段 / 带上下文，可选去重。
- 剪切：原子替换源文件，可选 `.bak` 备份，只读与磁盘空间预检。
- 设置面板、最近文件、会话恢复、外部修改检测与 `F5` 重载。
- Windows 11 Mica（D3D11 + DirectComposition）；深色 / 浅色 / 跟随系统三种主题；
  `UEDIT_NO_COMPOSITION=1` 可强制回退。

## 性能（本机实测）

| 场景 | 结果 |
|------|------|
| 1GB 索引 | 约 0.42s |
| 1GB 字面量搜索 | 约 63ms |
| 1GB 正则搜索 | 约 63ms |
| 1GB 跳行 | 约 0.06ms |
| 1GB 导出 7406 行 | 0.129s |
| 5GB 索引 / 搜索 | 2.13s / 327ms |
| 5GB 私有内存 | 11.8MB（工作集含 mmap 约 5GB，可回收） |
| 1GB 帧时间（Composition） | 1.7–4.7ms |
| 20 分钟浸泡（滚动 + 最大化/还原） | 私有内存 92.4–93.7MB、句柄 600–610、无增长 |
| 3 分钟多标签浸泡（1400 次切换 + 滚动） | 私有内存 94.3–94.4MB、句柄 600–601、无增长 |
| 2 分钟 UI 修订后多标签浸泡（926 次切换） | 私有内存 95.2–95.3MB、句柄 600–601、无增长 |
| 31MB 文件单次编辑 / 保存 | 约 30–40ms / 381ms |

测试环境：Windows 11 26300，Ryzen 级别 CPU，E: 机械盘（冷读受限），
C: NVMe。1GB 冷缓存索引在机械盘上约 7s，属磁盘上限。

## 安装

**便携版**：解压 `UEdit-1.1.1-win64.zip`，运行 `uedit.exe`。
已使用静态 CRT，`dumpbin` 验证无 `vcruntime/msvcp/ucrtbase` 依赖，无需安装 VC++ 运行库。

**MSIX**：仓库中的 MSIX 未签名。安装前请用代码签名证书签名：

```powershell
.\packaging\sign_release.ps1 -PfxPath C:\path\codesign.pfx -PfxPassword "***"
```

脚本会签名 exe、重新生成便携包与 MSIX、签名 MSIX，并用 `signtool verify /pa` 验证。
安装：

```powershell
Add-AppxPackage .\dist\UEdit_1.1.1.0_x64.msix
```

## 签名状态

构建机上没有生产代码签名证书，因此 `dist/` 下的正式产物目前**未签名**。

已使用临时自签名证书（CN=UEdit，Code Signing EKU）验证签名流水线：

- 1.1.1.0 的 `dist/test-signing/uedit.exe` 与
  `dist/test-signing/UEdit_test_signed.msix` 已重新签名并尝试添加时间戳；
- 临时证书与 PFX 私钥已从证书存储/磁盘移除，仅保留公钥
  `dist/test-signing/UEdit-Test.cer`；
- `Get-AuthenticodeSignature` 显示 `UnknownError`（根证书不受信任），属预期。

测试安装需先信任测试证书（仅限测试机）：

```powershell
Import-Certificate -FilePath .\dist\test-signing\UEdit-Test.cer `
  -CertStoreLocation Cert:\CurrentUser\TrustedPeople
Add-AppxPackage .\dist\test-signing\UEdit_test_signed.msix
```

`test-signing/` 下的产物**不可用于分发**。生产发布请使用真实证书执行
`packaging/sign_release.ps1`。

测试签名 MSIX 的实际安装未完成：自签名证书不在受信任根中，`Add-AppxPackage`
返回 `0x800B0109`（`CERT_E_UNTRUSTEDROOT`）。将测试证书导入
`Cert:\CurrentUser\Root` 后可用 `packaging/test_msix_install.ps1` 完成
安装/启动/卸载；该操作需在测试机上由用户授权。测试结束后系统未留下证书或
已安装包。

## 已知限制

- 编辑仅支持 ≤32MiB 的文本文件；更大文件、二进制文件与只读文件保持只读浏览/剪切。
- 每次编辑会重建行索引：接近 32MiB 的文件单次编辑约 30–40ms；无多光标/列编辑/另存为。
- 正则捕获组抽取未实现；按行/片段抽取可用。
- 搜索结果与导出上限 100 万条命中（结果弹窗默认列出前 5000 条，可设置；
  保存/导出会按需在后台重跑全量搜索）。
- 标签页不跨重启恢复；不支持拖拽重排或标签溢出列表。
- 已完成 3 分钟多标签切换浸泡（1400 次切换，私有内存 94.3–94.4MB、句柄 600–601）；
  尚未重跑 20 分钟与 8 小时全程浸泡。
- Mica 仅 Windows 11；Windows 10 下使用纯色界面。
- Composition 后端提交内存约 93–97MB；`UEDIT_NO_COMPOSITION=1` 回退时约 77–80MB。
- 8 小时稳定性浸泡尚未执行；已完成 5GB 冒烟、20 分钟滚动浸泡与 3 分钟多标签浸泡。
- 超长行显示上限 8192 字节；尚无像素级平滑滚动。

## 验证

```powershell
ctest --test-dir build -C Release --output-on-failure
build\Release\uedit_bench.exe fixtures\sample_1gb.log
.\packaging\verify_packages.ps1
```

`verify_packages.ps1` 会解开便携包与 MSIX，校验两者的 `uedit.exe` 与构建输出
SHA256 一致，并检查 `VERSION.txt`（1.1.1.0）与 `AppxManifest` 版本。

UI 验证：深色与浅色主题均已截图检查；“跟随系统”在本机
`AppsUseLightTheme=1` 下解析为浅色并显示“跟随系统（浅色）”，
关闭后设置仍保存为 `system`；`F9` 可直接打开设置面板；
150% 缩放下欢迎页缩放、还原/最大化均无重影或背景矩形；
结果弹窗垂直/水平滚动条拖动、`Shift+滚轮` 横向滚动与连续 `F3` 跳转均已实测；
标题栏图标与弹窗按钮文字居中正常；约 15ms 间隔采样确认打开/导出对话框
首次可见即位于 `480,246`（1280×820 主窗口内 960×540 对话框），无可见跳转；
通过对话框打开 `extract_demo.txt` 与导出命中文件均成功。

长时间稳定性测试（建议夜间执行，机器保持唤醒）：

```powershell
.\tools\soak.ps1 -Minutes 480
```

脚本会周期滚动、最大化/还原窗口，每 15 秒记录私有内存、工作集、句柄、GDI/USER
对象；结束后输出汇总表。

第三方组件：PCRE2 10.44（BSD 许可），许可文本随便携包分发。
