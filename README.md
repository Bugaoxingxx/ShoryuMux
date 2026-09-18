# ShoryuMux

把一个 **HORI Fighting Stick mini for PC**（XInput 摇杆，VID `0x0F0D` / PID `0x01C7`）
变成 macOS 上的「按钮 → 按键 / 命令」触发器。典型用途：当 **cmux** 里的 AI agent 弹出审批
prompt 时，按一下摇杆上的键就表示同意；其它键可自定义成快捷键或 shell 命令。

## 为什么需要它

这根摇杆只支持 **XInput**（枚举为厂商自定义类 `0xFF`），VID/PID 不在苹果的白名单里，
所以 macOS **不会**把它识别成 HID 手柄——系统层面「看不到」它，任何游戏/工具也用不了。

本项目绕过系统识别，**直接用 IOKit 读原始 USB 报文**并按 XInput（Xbox 360 线协议）解码。
实测在 Apple M1 / macOS 26 上：**无需驱动、无需付费开发者账号、无需关 SIP、无需任何 entitlement**
即可打开并读取设备。发按键用系统的 `CGEvent`（需要「辅助功能」授权）。

## 组成

```
shoryumux/
├── core/shoryumuxd.c              # 守护程序 / CLI：读 USB → 解码 → 发键或跑命令
├── core/config.conf.default      # 默认配置模板
├── app/                          # SwiftUI 菜单栏 App（可视化配置）
├── launchd/…plist.template       # 开机自启的 LaunchAgent 模板
├── scripts/
│   ├── install-launchagent.sh    # 构建 + 安装守护 + 注册开机自启
│   ├── uninstall-launchagent.sh  # 卸载（--purge 连二进制/日志一起删）
│   └── build-app.sh              # 构建 ShoryuMux.app
└── build/                        # 产物：shoryumuxd, ShoryuMux.app
```

运行时文件：
- 配置：`~/.config/shoryumux/config.conf`
- pid：`~/.config/shoryumux/shoryumux.pid`
- 事件日志：`~/.config/shoryumux/events.log`
- 守护二进制：`~/Library/Application Support/ShoryuMux/shoryumuxd`
- launchd 日志：`~/Library/Logs/ShoryuMux/shoryumuxd.*.log`

## 快速开始

```bash
cd ~/ShoryuMux
make                     # 构建守护程序 + 菜单栏 App
./scripts/install-launchagent.sh   # 安装守护并设为开机自启
open build/ShoryuMux.app  # 打开菜单栏 App 做配置
```

### 授予「辅助功能」权限（必须，否则按键发不出去）

发按键的进程是**守护程序 `shoryumuxd`**，所以要授权给它，不是授权给 App：

1. 菜单栏 App 里点 **Reveal daemon**（在访达中定位 `shoryumuxd`），
2. 打开 **系统设置 → 隐私与安全性 → 辅助功能**，把刚定位到的 `shoryumuxd` 文件拖进列表并打勾
   （App 里的 **Open Accessibility settings** 可直接打开该面板），
3. 回到 App 点 **Reload**（重启守护使权限生效）。

> 每次重新编译 `shoryumuxd` 后，二进制内容变化，可能需要重新授权一次。

### 使用

- 让 **cmux（或目标终端）保持在前台**——合成的按键发给当前最前面的窗口。
- 按摇杆上的键触发对应动作；App 的 **Recent presses** 会实时显示。

## 配置

一份配置被守护程序和 App 共享：`~/.config/shoryumux/config.conf`。
可以在菜单栏 App 里可视化编辑（改完点 **Save & Reload**），也可以直接编辑文件后
`kill -HUP $(cat ~/.config/shoryumux/shoryumux.pid)` 热重载。

格式：

```
BUTTON = keys: <token> [token ...]     # 合成按键到最前面的 App
BUTTON = shell: <command>              # 执行 shell 命令
```

- 可用按钮：`UP DOWN LEFT RIGHT START BACK L3 R3 LB RB GUIDE A B X Y LT RT`
- 键 token：`a-z` `0-9` `- = [ ] \ ; ' , . / \``，以及
  `enter/return` `tab` `space` `esc/escape` `delete`(退格) `fdel`(前删)
  `up down left right home end pgup pgdn f1..f12`
- 组合键用 `+`：`cmd+c` `shift+up` `ctrl+alt+t` `cmd+shift+4`
  （修饰键：`cmd/command` `ctrl/control` `opt/alt/option` `shift`）

示例：

```
A     = keys: enter          # 同意（确认高亮项）
B     = keys: esc            # 拒绝
UP    = keys: up
DOWN  = keys: down
Y     = keys: y enter        # 先打 y 再回车
X     = keys: cmd+c
START = shell: say approved
```

> **把「同意」调成你的 agent 实际需要的键。** 大多数 TUI 用回车确认高亮项；
> 若是 Claude Code 那种「按数字选择」，就写 `A = keys: 1`；若需要字母，写 `A = keys: y enter`。

## 手动跑（不用 LaunchAgent）

```bash
./build/shoryumuxd                       # 用 ~/.config/shoryumux/config.conf
./build/shoryumuxd /path/to/config.conf  # 指定配置
# Ctrl-C 退出；kill -HUP <pid> 热重载
```

## 开机自启

```bash
./scripts/install-launchagent.sh     # 安装并加载 LaunchAgent
launchctl kickstart -k gui/$(id -u)/com.shoryumux.daemon   # 手动重启守护
launchctl bootout gui/$(id -u)/com.shoryumux.daemon        # 临时停止
./scripts/uninstall-launchagent.sh   # 卸载（加 --purge 删二进制和日志）
```

> LaunchAgent 在登录时启动（`RunAtLoad`，`KeepAlive=false`）。如果登录时摇杆没插，
> 守护会退出且不会自动重拉——插上后在 App 里点 **Start**，或重新 `kickstart` 即可。

## 诊断小工具（core 之外，之前验证用）

`usbtest/probe.c`（能否打开设备 + 列端点）、`usbtest/readstick.c`（打印原始报文/解码按键）
仍保留在 `~/Documents/Qoder/2026-09-18/usbtest/`，可随时重新编译排查。

## 已知限制

- 按键发给**最前面的窗口**，不区分目标 App（v1）。想做「按 App 切换映射」需要额外逻辑。
- 守护独占 USB 接口：同一时刻只能有一个进程读摇杆（App 不读 USB，靠事件日志显示）。
- 重新编译守护后可能要重新授予辅助功能权限。
