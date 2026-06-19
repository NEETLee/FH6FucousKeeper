# 实机回归测试 (farm 流水线)

发版前用一条命令验证 farm 流水线是否有效,避免新版本带明显 bug。
**只做实机测试**(离线视觉回归对本项目无意义,一切以真机为准)。

## 原理

`run.ps1` 通过 debug 命令通道驱动正在运行的 debug 版 FocusKeeper,逐条执行
`cases.json` 里的用例,然后用三种证据判定通过/失败:

1. `build/FocusKeeper.log` 里的里程碑日志(expect 必须出现 / forbid 绝不能出现)
2. `build/debug/status.txt` 结构化状态(`status_assert` 精确断言,如 `bought==5`)
3. 操作员肉眼复核(`manual`,视觉断不了的项,如「涂装22B还在吗?」)

每条用例的 expect/forbid/manual 都绑定了历史真实 bug(起跑重试风暴、误删涂装车、
CR 误读),改坏会当场抓住。

## 前提

- 必须用 **debug 版**:`mingw32-make farm`(命令通道只在 `FK_DEBUG` 下存在)。
- 启动 `build\FocusKeeper.exe`(管理员,点 UAC)。
- 在「自动刷图」tab 选好 **22B + 带分享码的赛事配置**(涉及赛事/循环用例时)。

## 运行

```powershell
# 跑全部用例
powershell -ExecutionPolicy Bypass -File tests\regression\run.ps1

# 只跑某些用例
powershell -ExecutionPolicy Bypass -File tests\regression\run.ps1 -Only TC-READ,TC-RACE
```

每条用例会先打印「前置状态」要求,等你把游戏摆好后按回车;跑完打印 PASS/FAIL 汇总,
并写一份 `tests/regression/report_<时间戳>.md`(已被 gitignore)。退出码:有 FAIL 返回 1。

## 命令通道速查

往 `build\debug\cmd.txt` 写一行(程序每 500ms 轮询,执行后回写 `cmd.ack`):

| 命令 | 作用 |
|------|------|
| `read` | 读 CR/SP |
| `race` | 只跑赛事流程 |
| `buy N` / `spin N` / `remove N` | 单步,N 可省略(用 GUI 数量) |
| `loop` | 完整循环 |
| `stop` | 停止当前流水线 |
| `quit` | 优雅退出(释放 exe 供重建) |
| `status` | 把流水线状态写到 `build\debug\status.txt` |

`status.txt` 字段:`running / bought / spins / removed / races / balance / sp / computed / stage`。

## 加新用例

编辑 `cases.json`,在 `cases` 数组加一项:

```json
{
  "name": "TC-XXX",
  "desc": "一句话说明",
  "setup": "给操作员看的前置状态",
  "cmd": "buy 5",
  "timeout_s": 180,
  "expect": ["必须出现的正则"],
  "forbid": ["绝不能出现的正则(固化某个历史 bug)"],
  "status_assert": ["bought==5"],
  "manual": "肉眼复核问句,留空则跳过"
}
```

`status_assert` 支持 `== != >= <= > <`,左边是 status.txt 字段名,右边是整数。

## 改了 C 代码之后

1. 写 `quit` 到 `build\debug\cmd.txt`(或丢 `build\debug\stop.flag`)让程序退出。
2. `mingw32-make farm` 重建。
3. 重启 `build\FocusKeeper.exe`(点一次 UAC),再跑回归。
