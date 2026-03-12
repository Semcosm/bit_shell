# bit_shell

`bit_shell` 是一个围绕 niri 构建的桌面 shell 项目。它不替代 compositor，而是在 niri 之上提供 shell 级能力，包括状态归并、本地 IPC、程序坞前端，以及后续的顶部栏和启动台。

当前仓库以 C17 + GLib/GIO + GTK4 实现，使用 Meson 构建。

## 当前架构

整体结构分成一个后台守护进程和多个前端：

- `bit_shelld`：唯一状态源，负责 niri IPC/event-stream、状态树、应用聚合、配置/状态持久化、本地 IPC 与命令路由
- `bit_dock`：底部程序坞前端，负责 pinned/running 应用展示、启动/激活、多窗口切换与交互动画
- `bit_bar`、`bit_launchpad`：设计已进入文档，但当前仓库主要已落地 `bit_shelld` 与 `bit_dock`

核心原则：

- `bit_shelld` 是唯一状态持有者，前端不直连 niri
- 前端只负责渲染与交互，不持有权威业务状态
- Dock 几何与动画参数在前端运行时派生，不写入磁盘

更完整的总体架构见 [system-architecture.md](doc/10-architecture/system-architecture.md)。

## 仓库结构

```text
bit_shell/
├─ core/
│  ├─ shelld/        # daemon 入口、路由、本地 IPC
│  ├─ model/         # 配置、IPC payload、snapshot
│  ├─ services/      # dock / launcher / tray / settings 等服务
│  ├─ state/         # StateStore 与 topic 版本推进
│  ├─ niri/          # niri 后端接入
│  └─ frontends/
│     └─ bit_dock/   # GTK4 dock 前端
├─ doc/              # 设计、架构、组件、运行时文档
├─ scripts/
│  ├─ startup/       # 启动 bit_shell 相关脚本
│  └─ devtools/      # 开发过程中的测试、诊断、分析脚本
└─ build/            # Meson 构建产物
```

文档目录导航见 [doc/README.md](doc/README.md)。

## 已实现内容

当前代码已经具备：

- `bit_shelld` 本地 IPC server
- niri event-stream 消费、状态应用与自动重连
- `snapshot` / `subscribe` / topic event 推送
- Dock item 聚合：`pinned + running windows -> dock items`
- `launch_app`、`activate_app`、`focus_next_app_window`、`focus_prev_app_window`
- `pin_app` / `unpin_app` 与 `state.json` 落盘
- `config.toml` stub 自动生成、TOML 标量解析、Dock 配置 normalize
- `bit_dock` 的 magnification、局部横向让位、启动 bounce 与运行态指示点

当前 `bit_dock` 的动画链路采用：

- 单个全局 GTK tick
- `target/current` 双态
- 动态 CSS 合成
- 启动 bounce 作为独立瞬态分量，不进入 hover 几何求解器

具体见 [bit_dock.md](doc/20-components/bit_dock.md)。

## 构建与运行

首次配置：

```bash
meson setup build
```

编译：

```bash
meson compile -C build
```

运行守护进程：

```bash
./build/core/bit_shelld
```

运行 Dock 前端：

```bash
./build/core/bit_dock
```

仓库里也提供了辅助脚本：

- [start_bit_shelld.sh](scripts/startup/start_bit_shelld.sh)
- [start_bit_dock.sh](scripts/startup/start_bit_dock.sh)

测试命令：

```bash
meson test -C build
```

当前仓库还没有完整测试集；提交前至少应保证 `meson compile -C build` 通过。

## 配置与状态文件

默认路径：

- 配置：`~/.config/bit_shell/config.toml`
- 状态：`~/.local/state/bit_shell/state.json`

若文件不存在，`bit_shelld` 会自动创建父目录并写入 stub 文件。

Dock 当前公开的持久化配置包括：

- `icon_size_px`
- `magnification_enabled`
- `magnification_scale`
- `hover_range_cap_units`
- `spacing_px`
- `bottom_margin_px`
- `show_running_indicator`
- `animate_opening_apps`
- `display_mode`
- `center_on_primary_output`

配置模型与持久化边界见 [config-and-state.md](doc/60-runtime/config-and-state.md)。

## IPC 与状态流

本地 IPC 当前使用 Unix domain socket + 单行 JSON，支持：

- `snapshot`
- `subscribe`
- `launch_app`
- `activate_app`
- `focus_next_app_window`
- `focus_prev_app_window`
- `pin_app`
- `unpin_app`
- `switch_workspace`

协议与事件模型见：

- [event-and-state-flow.md](doc/10-architecture/event-and-state-flow.md)
- [ipc-contract.md](doc/10-architecture/ipc-contract.md)

## 文档阅读顺序

如果第一次看这个仓库，建议按这个顺序：

1. [project-scope.md](doc/00-overview/project-scope.md)
2. [system-architecture.md](doc/10-architecture/system-architecture.md)
3. [event-and-state-flow.md](doc/10-architecture/event-and-state-flow.md)
4. [ipc-contract.md](doc/10-architecture/ipc-contract.md)
5. [bit_dock.md](doc/20-components/bit_dock.md)
6. [config-and-state.md](doc/60-runtime/config-and-state.md)

## 当前状态

这个仓库还处在持续迭代阶段。当前更接近“核心 runtime + Dock 前端已成型，剩余组件和更完整系统集成继续补齐”的状态，而不是完整桌面发行版。
