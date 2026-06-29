# 更新日志

本项目所有重要变更都记录在此文件中。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

## [1.3.0] - 2026-06-29

### 新增
- 自动刷图（farm）流水线：赛事 → 读取 CR/SP → 自动买车 → 超级抽奖 → 删车 的完整循环，支持按经济自动计算每轮数量、按目标技术点自动计算赛事圈数。
- 视觉识别后端：WGC（Windows Graphics Capture）屏幕捕获 + OpenCV 模板匹配 + Windows OCR，分辨率自适应。
- EventLab 赛事自动进入：通过分享码经 UIA 自动填入并确定，进入指定赛事。
- OCR 选车：按赛事 profile 中的车名（CarName）+ 性能评分（CarPI）自动选中刷图用车，无需按分辨率的车辆模板图；删车后仍能在下一轮稳定选回目标车。
- 赛事 profile 新增 `CarName` / `CarPI` 字段。
- 调试构建（FK_DEBUG）：文件日志、关键决策快照、文件命令通道，以及实时回归测试框架。

### 修复
- 点赞/点踩评价弹窗改为 OCR 判定（需同时识别「点赞」「点踩」），不再被赛事 HUD 误触发、避免卡死或被点出赛事导致循环中断。
- 修正跑圈计数：按实际完成圈数累加。
- 流程结束后关闭 WGC 捕获，消除游戏窗口的黄色捕获边框。

### 变更
- 发布包接入 UPX 压缩，显著减小体积；CI 改为构建 `farm-release` 并打包完整运行目录。

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

[1.3.0]: https://github.com/NEETLee/FH6FucousKeeper/compare/v1.2.1...v1.3.0
[1.2.1]: https://github.com/NEETLee/FH6FucousKeeper/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/NEETLee/FH6FucousKeeper/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/NEETLee/FH6FucousKeeper/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/NEETLee/FH6FucousKeeper/releases/tag/v1.0.0
