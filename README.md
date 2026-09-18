# ShoryuMux

把一个 **HORI Fighting Stick mini for PC**（XInput 摇杆，VID `0x0F0D` / PID `0x01C7`）
变成「按钮 → 按键 / 命令」触发器。典型用途：当 **cmux** 里的 AI agent 弹出审批
prompt 时，按一下摇杆上的键就表示同意；其它键可自定义成快捷键或 shell 命令。

三端共用同一份 [`core/config.conf.default`](core/config.conf.default) 映射。读杆和发键按平台分开：

| 平台 | 读杆 | 发键 |
|------|------|------|
| macOS | IOKit 原始 USB（系统不认这根 XInput 杆） | `CGEvent`（需辅助功能） |
| Windows | `XInputGetState` | `SendInput` |
| Linux | libusb 读同一套 Xbox 360 报文 | `uinput`（需 udev） |

## 为什么 macOS 特别

这根摇杆只支持 **XInput**（枚举为厂商自定义类 `0xFF`），VID/PID 不在苹果的白名单里，
所以 macOS **不会**把它识别成 HID 手柄。本项目在 Mac 上**直接用 IOKit 读原始 USB 报文**。
实测在 Apple M1 / macOS 26 上：无需驱动、无需付费开发者账号、无需关 SIP、无需 entitlement。

Windows 上它本来就是 XInput 手柄；Linux 上 xpad 也可能抢走接口——守护会用 libusb 认领，udev 规则里会尝试把 xpad unbind 掉。

## 组成

```
shoryumux/
├── core/                 # 跨平台守护（config / 边沿 / keys|shell + 平台 input/output）
├── core/config.conf.default
├── linux/99-shoryumux.rules
├── CMakeLists.txt        # Windows（也可用在 Unix）
├── app/                  # macOS SwiftUI 菜单栏（其它平台用 CLI）
├── launchd/              # macOS LaunchAgent
└── scripts/              # macOS 安装 / 构建 App
```

运行时文件（三端相同布局）：

- 配置：`~/.config/shoryumux/config.conf`（Windows 为 `%USERPROFILE%/.config/shoryumux/config.conf`）
- pid：`~/.config/shoryumux/shoryumux.pid`
- 状态：`~/.config/shoryumux/status`（`waiting` / `ready`）
- 事件日志：`~/.config/shoryumux/events.log`
- macOS 守护安装路径：`~/Library/Application Support/ShoryuMux/shoryumuxd`
- macOS launchd 日志：`~/Library/Logs/ShoryuMux/shoryumuxd.*.log`

## 构建

### macOS

```bash
cd ~/ShoryuMux
make                     # 守护 + 菜单栏 App
./scripts/install-launchagent.sh
open build/ShoryuMux.app
```

发按键的进程是 **`shoryumuxd`**，不是 App：

1. 菜单栏点 **Reveal daemon**，把 `shoryumuxd` 拖进 **系统设置 → 隐私与安全性 → 辅助功能**，
2. **Stop** 再 **Start**（**Reload** 只热重载配置，不刷新辅助功能）。

重编二进制后可能要重新授权。Quit App 会停掉守护；若开了开机自启，下次登录 LaunchAgent 仍会拉起守护。

### Linux

依赖：`libusb-1.0` 开发包（Debian/Ubuntu: `libusb-1.0-0-dev`）。

```bash
make core                # -> build/shoryumuxd
sudo cp linux/99-shoryumux.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -aG input,plugdev "$USER"   # 然后重新登录
make install
./build/shoryumuxd
```

uinput 在多数发行版需要 `input` 组或上述 udev 规则，否则守护会在 `output_init` 失败退出。
若 xpad 仍占着设备，拔插一次摇杆，或看 stderr 里的 claim 错误。

开机自启（第二期；可自行加 systemd user unit）：不要做成系统 service，发键必须在图形会话里。

### Windows

```bat
cmake -B build
cmake --build build --config Release
build\Release\shoryumuxd.exe
```

（Ninja/单配置生成器则是 `build\shoryumuxd.exe`。）用 XInput 读杆、`SendInput` 打到前台窗口，无需额外驱动。
GUIDE 键依赖 `XInputGetStateEx`（xinput1_4 未公开 ordinal）；没有的话 GUIDE 不会触发。

开机自启第二期：放到用户「启动」文件夹或登录任务，**不要**做成 Windows Service。

## 配置

守护（和 macOS App）共用 `~/.config/shoryumux/config.conf`。
macOS 菜单栏里改完点 **Save & Reload**（SIGHUP，不断杆）。也可以直接改文件：

```bash
kill -HUP $(cat ~/.config/shoryumux/shoryumux.pid)   # Unix
```

Windows 无 SIGHUP：保存配置后守护会按文件 mtime 自动重载（Unix 同样支持）。

格式：

```
BUTTON = keys: <token> [token ...]     # 合成按键到最前面的窗口
BUTTON = shell: <command>              # 执行 shell 命令（后台，不卡住读杆）
```

- 可用按钮：`UP DOWN LEFT RIGHT START BACK L3 R3 LB RB GUIDE A B X Y LT RT`
- 键 token：`a-z` `0-9` `- = [ ] \ ; ' , . / \``，以及
  `enter/return` `tab` `space` `esc/escape` `delete`(退格) `fdel`(前删)
  `up down left right home end pgup pgdn f1..f12`
- 组合键用 `+`：`cmd+c` `shift+up` `ctrl+alt+t` `cmd+shift+4`
  （修饰键：`cmd/command/super/win/meta` `ctrl/control` `opt/alt/option` `shift`）
  `cmd` 在 Windows 是 Win 键，在 Linux 是 Super。

示例：

```
A     = keys: enter
B     = keys: esc
UP    = keys: up
DOWN  = keys: down
Y     = keys: y enter
X     = keys: cmd+c
START = shell: say approved
```

> **把「同意」调成你的 agent 实际需要的键。** 大多数 TUI 用回车确认；数字选择就写 `A = keys: 1`。

## 手动跑

```bash
./build/shoryumuxd                       # ~/.config/shoryumux/config.conf
./build/shoryumuxd /path/to/config.conf
# Ctrl-C 退出；Unix: kill -HUP <pid> 热重载
```

没插摇杆时守护保持运行并等待重连。

## macOS 开机自启

```bash
./scripts/install-launchagent.sh
launchctl kickstart -k gui/$(id -u)/com.shoryumux.daemon
launchctl bootout gui/$(id -u)/com.shoryumux.daemon
./scripts/uninstall-launchagent.sh   # --purge 连二进制和日志一起删
```

LaunchAgent：`RunAtLoad`，`KeepAlive=false`。守护自己等杆，不靠 launchd 重启。

## 已知限制

- 按键发给**最前面的窗口**，不按 App 切换映射。
- 同一时刻只能有一个进程读杆；第二份 `shoryumuxd` 会因 pid 锁退出。
- macOS：重编后可能要重新授予辅助功能，然后 Stop + Start。
- Linux：不保证每个 Wayland 合成器的「全局热键」语义，只做 uinput 注入。
- Windows：标准 XInput 可能看不到 GUIDE。
- `shell:` 跑在当前用户下，只写你信任的命令。
- Linux/Windows 开机自启未打包进第一期；菜单栏 UI 目前仅 macOS。
