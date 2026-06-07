# FH6 FocusKeeper

**Forza Horizon 6 防暂停 + 自动赛事工具** — 防止游戏在失去焦点时暂停，并支持后台自动刷技术点。

[🌐 English](docs/README_EN.md)

## 功能

- **防暂停**：通过 DLL Hook 拦截窗口焦点消息，游戏切到后台也不会暂停
- **自动赛事**：可配置的自动比赛循环，后台刷技术点/经验（PostMessage 注入，不影响正常使用电脑）
- **配置文件驱动**：INI 格式定义比赛流程，支持自定义步骤/按键/时序
- **静音控制**：独立的静音按钮，可随时静音/恢复游戏音频
- **通用方案**：Steam 和 Microsoft Store 版本均适用
- **多语言**：支持简体中文 / 繁体中文 / English，默认跟随系统语言
- **高 DPI 支持**：自动适配 2K/4K 等高分辨率显示器
- **系统托盘**：支持最小化到托盘，后台静默运行
- **全局热键**：Ctrl+F12 快速开关防暂停
- **阻止休眠**：防暂停期间自动阻止系统进入休眠状态
- **版本检测**：自动检查 GitHub 新版本并提示

## 前置条件

> ⚠️ **请将游戏切换到窗口模式运行**（快捷键 Alt+Enter）。本工具仅在窗口模式下有效。

## 测试环境

- Windows 10 / Windows 11
- Forza Horizon 6 Steam 版
- Forza Horizon 6 Microsoft Store 版

## 原理

### 防暂停

通过 `SetWindowsHookEx(WH_CALLWNDPROC)` 安装全局消息钩子，将 `hook.dll` 注入到游戏进程，然后对游戏窗口进行子类化（Subclass）。子类化后的窗口过程会拦截以下消息：

| 消息 | 作用 |
|------|------|
| `WM_ACTIVATEAPP` | 应用程序激活/失活通知 |
| `WM_KILLFOCUS` | 键盘焦点丢失通知 |
| `WM_NCACTIVATE` | 非客户区激活状态变化 |
| `WM_ACTIVATE` | 窗口激活状态变化 |

当游戏失去焦点时，这些消息被静默丢弃，游戏"认为"自己始终是前台窗口。

### 自动赛事

通过 `PostMessage` 向游戏窗口发送 `WM_KEYDOWN`/`WM_KEYUP` 消息模拟按键。完全后台运行，不抢占前台焦点，不影响正常使用电脑。

比赛流程由 INI 配置文件驱动，支持以下模式：
- **wait**：等待指定时间
- **hold**：按住某键指定时间（周期性重发 WM_KEYDOWN）
- **tap**：定时点按某键（按下→松开→间隔→重复）
- **sequence**：多键序列操作

## 编译

### 环境要求

- [w64devkit](https://github.com/skeeto/w64devkit)（便携 MinGW-w64 工具链，无需安装）

### 构建步骤

```bash
# 打开 w64devkit 终端，切到项目目录
cd /path/to/FH6FocusKeeper

# 编译
make

# 输出文件
# build/FocusKeeper.exe       - 主程序
# build/hook.dll              - 钩子 DLL（需与 exe 放在同一目录）
# build/profiles/170516901.ini - 默认赛事配置
```

### 其他命令

```bash
make clean    # 清理构建产物
make rebuild  # 重新构建
make debug    # 调试构建（含符号信息）
```

## 使用方法

### 基本使用

1. 将 `FocusKeeper.exe`、`hook.dll` 和 `profiles/` 文件夹放在同一目录
2. **以管理员权限运行** `FocusKeeper.exe`（需要管理员权限才能注入到游戏进程）
3. **将游戏切换到窗口模式**（Alt+Enter）
4. 启动游戏后，点击「查找游戏窗口」
5. 点击「开启防暂停」或使用快捷键 Ctrl+F12

### 自动赛事

1. 切换到「自动赛事」标签页
2. 选择赛事配置文件（或编辑 `profiles/` 目录下的 INI 文件）
3. 点击「开始自动赛事」（会自动启用防暂停）
4. 程序会按配置循环执行比赛流程

### 配置文件格式

```ini
; 注释行（分号开头）会显示在程序的配置说明区域
[Profile]
Name=170516901
StepCount=4

[Step0]
Name=选择重赛
Mode=tap
Duration=1000
Key=88
HoldMs=80
Interval=2000

[Step1]
Name=确认1
Mode=tap
Duration=10000
Key=13
HoldMs=80
Interval=6000
```

常用键码：W=87 A=65 S=83 D=68 X=88 E=69 Enter=13 Esc=27 Space=32

## 项目结构

```
FH6FocusKeeper/
├── src/
│   ├── hook/
│   │   ├── hook.h            # Hook DLL 接口
│   │   ├── hook.c            # 子类化窗口过程 + 消息拦截
│   │   └── hook.def          # 共享段定义
│   └── loader/
│       ├── main.c            # 入口 + 模块协调 (Mediator)
│       ├── gui.c/h           # Win32 标签页 GUI（DPI 感知）
│       ├── tray.c/h          # 系统托盘图标
│       ├── hook_manager.c/h  # DLL 加载管理 (Facade)
│       ├── window_finder.c/h # 窗口查找 (Strategy)
│       ├── audio_control.c/h # WASAPI 进程静音
│       ├── i18n.c/h          # 国际化字符串表（简/繁/英）
│       ├── logger.c/h        # 日志系统 (Observer)
│       ├── settings.c/h      # INI 配置管理
│       ├── auto_race.c/h     # 自动赛事引擎
│       ├── race_profile.c/h  # 赛事配置加载（编码自适应）
│       ├── race_controller.c/h # 赛事控制器
│       ├── step_executor.c/h # 步骤执行策略 (Strategy)
│       ├── input_hook_backend.c/h # 按键注入 (PostMessage)
│       └── version_check.c/h # GitHub 版本检测
├── data/
│   └── profiles/
│       └── 170516901.ini     # 默认赛事配置模板
├── res/
│   ├── resource.h            # 控件 ID 定义
│   ├── app.rc                # 资源脚本
│   ├── app.manifest          # 应用清单（PerMonitorV2 DPI）
│   └── app.ico               # 应用图标
├── Makefile
└── README.md
```

## 设计模式

- **Strategy**：WindowFinder 按不同策略查找游戏窗口；StepExecutor 按不同模式执行步骤
- **Observer**：Logger 多回调通知，HookManager/AutoRace 状态回调
- **Facade**：HookManager 封装 DLL 加载和 Hook 安装细节；InputBackend 封装按键注入
- **Mediator**：main.c 协调各模块通信
- **Repository**：RaceProfile 管理配置文件的加载/枚举
- **Chain of Responsibility**：SubclassProc 消息过滤链

## 作者

NEETLee & Claude Opus 4.6

## 许可证

MIT License
