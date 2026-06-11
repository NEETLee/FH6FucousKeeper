# 更新日志

本项目所有重要变更都记录在此文件中。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.2.1] - 2026-06-09

### 修复
- 修复「清除日志」按钮点击无效的问题（原先在主窗口上查找日志控件，而它实际是日志页面板的子控件，导致找不到、清不掉；现改为直接通过保存的句柄清空）。

### 变更
- 更换为高清多尺寸应用图标（16~256 像素，多分辨率）。
- 新增发布流程规则文档。

## [1.2.0] - 2026-06-07

### 新增
- 新增繁体中文界面。

### 变更
- UI 改进与细节打磨。
- 移除旧的消息回放（msg_replay）模块。

### 修复
- 发布包补入 `profiles/` 目录。
- 修正发布工作流的 YAML 编码问题。

## [1.1.0] - 2026-06-06

### 新增
- 自动跑图（auto-race）系统。
- 启动时检查新版本。
- 运行期间防止系统休眠。

## [1.0.0] - 2026-06-05

### 新增
- 防暂停核心：DLL 注入 + 窗口子类化，拦截失焦相关消息，使游戏失去焦点时不暂停（Steam 版与 Microsoft Store 版通用）。
- Win32 多标签图形界面（状态 / 窗口列表 / 日志 / 设置）。
- 游戏静音开关。
- 中英文国际化与系统语言自动检测。
- 高 DPI 支持（2K / 4K 缩放）。
- 作者签名、GitHub 仓库链接、窗口模式使用提示。
- 基于 GitHub Actions 的自动构建与发布流水线。

[1.2.1]: https://github.com/NEETLee/FH6FucousKeeper/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/NEETLee/FH6FucousKeeper/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/NEETLee/FH6FucousKeeper/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/NEETLee/FH6FucousKeeper/releases/tag/v1.0.0
