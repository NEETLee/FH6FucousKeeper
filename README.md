# FH6 FocusKeeper

**Forza Horizon 6 防暂停工具** — 防止游戏在失去焦点时暂停。

[🌐 English](docs/README_EN.md)

## 功能

- **防暂停**：通过 DLL Hook 拦截窗口焦点消息，游戏切到后台也不会暂停
- **静音控制**：可在防暂停的同时静音游戏，适合挂机场景
- **通用方案**：Steam 和 Microsoft Store 版本均适用
- **中英双语**：支持中文/英文界面切换，默认跟随系统语言
- **系统托盘**：支持最小化到托盘，后台静默运行
- **全局热键**：Ctrl+F12 快速开关防暂停

## 截图

```
┌─ FH6 FocusKeeper ─────────────────────┐
│ [状态] [窗口列表] [日志] [设置]        │
│                                        │
│  ● 状态：防暂停已激活                  │
│  游戏窗口：Forza Horizon 6             │
│  窗口句柄：0x001A0B2C                  │
│  进程 PID：12345                       │
│                                        │
│  拦截统计：                            │
│    WM_KILLFOCUS    × 42                │
│    WM_ACTIVATEAPP  × 38                │
│                                        │
│  [查找游戏窗口] [开启防暂停] [防暂停+静音]│
└────────────────────────────────────────┘
```

## 原理

通过 `SetWindowsHookEx(WH_CALLWNDPROC)` 安装全局消息钩子，将 `hook.dll` 注入到游戏进程，然后对游戏窗口进行子类化（Subclass）。子类化后的窗口过程会拦截以下消息：

| 消息 | 作用 |
|------|------|
| `WM_ACTIVATEAPP` | 应用程序激活/失活通知 |
| `WM_KILLFOCUS` | 键盘焦点丢失通知 |
| `WM_NCACTIVATE` | 非客户区激活状态变化 |
| `WM_ACTIVATE` | 窗口激活状态变化 |

当游戏失去焦点时，这些消息被静默丢弃，游戏"认为"自己始终是前台窗口。

若 DLL Hook 不可用，自动回退到消息重放模式（定时发送激活消息）。

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
# build/FocusKeeper.exe  - 主程序
# build/hook.dll         - 钩子 DLL（需与 exe 放在同一目录）
```

### 其他命令

```bash
make clean    # 清理构建产物
make rebuild  # 重新构建
make debug    # 调试构建（含符号信息）
```

## 使用方法

1. 将 `FocusKeeper.exe` 和 `hook.dll` 放在同一目录
2. **以管理员权限运行** `FocusKeeper.exe`（需要管理员权限才能注入到游戏进程）
3. 启动游戏后，点击「查找游戏窗口」
4. 点击「开启防暂停」或使用快捷键 Ctrl+F12
5. 如需静音，点击「防暂停+静音」

## 项目结构

```
FH6FocusKeeper/
├── src/
│   ├── hook/
│   │   ├── hook.h          # Hook DLL 接口
│   │   ├── hook.c          # 子类化窗口过程 + 消息拦截
│   │   └── hook.def        # 共享段定义
│   └── loader/
│       ├── main.c          # 入口 + 模块协调 (Mediator)
│       ├── gui.c/h         # Win32 标签页 GUI
│       ├── tray.c/h        # 系统托盘图标
│       ├── hook_manager.c/h # DLL 加载管理 (Facade)
│       ├── msg_replay.c/h  # 消息重放 (Active Object)
│       ├── window_finder.c/h # 窗口查找 (Strategy)
│       ├── audio_control.c/h # WASAPI 进程静音
│       ├── i18n.c/h        # 国际化字符串表
│       ├── logger.c/h      # 日志系统 (Observer)
│       └── settings.c/h    # INI 配置管理
├── res/
│   ├── resource.h          # 控件 ID 定义
│   ├── app.rc              # 资源脚本
│   ├── app.manifest        # 应用清单
│   └── app.ico             # 应用图标
├── Makefile
└── README.md
```

## 设计模式

- **Strategy**：WindowFinder 按不同策略查找游戏窗口
- **Observer**：Logger 多回调通知，HookManager 状态回调
- **Facade**：HookManager 封装 DLL 加载和 Hook 安装细节
- **Mediator**：main.c 协调各模块通信
- **Active Object**：MsgReplay 独立线程定时发送消息
- **Chain of Responsibility**：SubclassProc 消息过滤链

## 许可证

MIT License
