# **目标**

- 针对 **FH6** 或类似游戏
- **窗口失去焦点时不触发暂停**
- 支持 Microsoft Store 版 / Steam 版（Steam版成功率高）
- 尽可能免费、可打包为 exe

---

# **1️⃣ 核心原理**

DisplayFusion 的 Prevent Window Deactivation 功能原理：

1. 游戏接收 Windows 消息来判断焦点：
   - `WM_ACTIVATE`
   - `WM_KILLFOCUS`
   - `WM_ACTIVATEAPP`
   - `WM_NCACTIVATE`
2. 游戏内部逻辑：
   - 检测窗口失焦 → 自动暂停
3. 防止暂停思路：
   - 拦截这些消息或修改消息返回值，让游戏“误以为自己仍在前台”
   - 或模拟窗口一直处于激活状态

简化理解：

> **欺骗游戏，让它认为自己一直在前台。**

---

# **2️⃣ 开发流程设计**

### **步骤 1：定位游戏窗口**

- 枚举窗口：
  - `FindWindow` / `EnumWindows`
  - 通过标题、类名或进程名识别 FH6 窗口
- 确定句柄 `HWND`

> 技术要点：
> 
> - Microsoft Store 版是 UWP 窗口，可能窗口类名比较奇怪
> - Steam 版 Win32 窗口更好识别

---

### **步骤 2：拦截焦点消息**

- 安装 **Windows 消息钩子**
  - C++：`SetWindowsHookEx` (WH_CBT 或 WH_CALLWNDPROC)
  - Python：`pywin32` 可以尝试 `SetWindowsHookEx`，但 UWP 限制严重
- 拦截以下消息：
  - `WM_KILLFOCUS` → 阻止或重写
  - `WM_ACTIVATEAPP` → 让游戏认为自己仍在激活状态
  - `WM_NCACTIVATE` → 保持窗口外观为激活状态

> 技术要点：
> 
> - 需要管理员权限（尤其对 Store 版）
> - 针对 UWP 进程可能无法注入，需要 Steam Win32 版更稳定
> - Hook 后要快速处理，避免影响 FPS 或输入延迟

---

### **步骤 3：保持窗口激活状态**

- 模拟游戏一直在前台：
  - `SetForegroundWindow(hwnd)`
  - `AttachThreadInput` (如果焦点被抢)
- 对于 Store 版可能无法 Hook，可以尝试：
  - Python/AHK 不断发送 `WM_ACTIVATEAPP`
  - 但成功率低

---

### **步骤 4：用户交互 / 配置**

- 可选：
  - 热键切换“保持激活”开关
  - 自动检测 FH6 窗口启动
  - 支持 Steam / Microsoft Store 自动识别

> 技术要点：
> 
> - 程序可以放托盘，减少占用
> - 窗口失焦不暂停功能最好默认开启

---

### **步骤 5：打包 / 发布**

- **C++**：
  - 生成 exe，可直接运行
  - Release 模式，静态链接，避免依赖 Visual Studio
- **Python**：
  - 用 PyInstaller 打包 exe
  - 依赖 pywin32

---

# **3️⃣ 技术难点**

|难点|描述|可行性|
|--|--|--|
|UWP / Microsoft Store 版|UWP 沙盒限制消息 Hook，几乎阻止外部程序修改焦点|低|
|Steam Win32 版|Hook 更稳定，可拦截 WM_* 消息|高|
|消息 Hook 处理速度|如果慢可能影响 FPS / 输入|高|
|权限问题|需要管理员权限|中|
|多显示器 / Alt+Tab|保持主窗口激活不被 Alt+Tab 干扰|中|
|反作弊|在线多人模式可能检测 Hook|高风险|

---

# **4️⃣ 语言选择建议**

|语言|优势|劣势|
|--|--|--|
|C++|官方 Hook 支持最好，性能最高，容易打包 exe|需要编译环境（可用 MinGW 或 GitHub Release）|
|Python|开发快速，可 PyInstaller 打包|UWP / FH6 成功率低，Hook 复杂，延迟问题|
|AutoHotkey|低门槛，可模拟按键、窗口消息|对 FH6 成功率低，几乎只适合 Steam 版|
|C#|.NET Hook 比较方便|UWP 版限制严重|

> **结论**：
> 
> - 如果只针对 **Steam 版** → Python / AHK 也可能实现
> - 如果要兼容 **Microsoft Store 版** → C++ 或已有 Release exe 最靠谱

---

# **5️⃣ 可行优化思路**

1. **只针对 Steam 版开发**，避免 UWP 沙盒问题
2. **支持托盘启动 + 自动检测窗口**
3. **提供热键开关**，避免在线多人模式触发反作弊检测
4. **C++ 或 Python 都可以做原型**，最终发布 exe 方便使用
5. **避免 DLL 注入**，降低封号风险

---

💡 **总结**

技术关键点：

- **窗口识别**（Steam vs Store）
- **Hook / 消息拦截**（WM_KILLFOCUS, WM_ACTIVATEAPP, WM_NCACTIVATE）
- **保持窗口激活**（SetForegroundWindow / AttachThreadInput）
- **低延迟处理**（FPS 和输入）
- **管理员权限**（尤其 Store 版）
- **可选热键 / 托盘 UI**
