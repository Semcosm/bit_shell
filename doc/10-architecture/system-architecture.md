# 系统架构（总体设计草案）

## 架构定位

`bit_shell` 围绕 niri 构建，不替代 compositor，而是通过独立 shell 组件补齐完整桌面体验。

## 核心设计原则

1. 单一状态源：`bit_shelld` 作为唯一状态持有者，前端不直连 niri socket。
2. 前端要薄：`bit_bar`、`bit_dock`、`bit_launchpad` 只负责渲染与交互。
3. 围绕 niri 工作区模型：按输出组织、动态工作区、命名工作区常驻。
4. layer-shell 是产品语义：layer/anchor/exclusive-zone/keyboard 行为前置定义。

## 总体架构图

```text
+------------------------------------------------------+
|                       niri                           |
|------------------------------------------------------|
| outputs / workspaces / windows / focus / IPC socket  |
+-------------------------------+----------------------+
                                |
                                | JSON request / event-stream
                                v
+------------------------------------------------------+
|                     bit_shelld                       |
|------------------------------------------------------|
| NiriBackend                                          |
| StateStore                                           |
| AppRegistry                                          |
| WorkspaceService                                     |
| DockService                                          |
| LauncherService                                      |
| TrayService                                          |
| SettingsService                                      |
| CommandRouter                                        |
| Local IPC Server                                     |
+-------------+-------------------------+--------------+
              |                         |
              | local IPC               | local IPC
              v                         v
       +-------------+           +--------------+
       |  bit_dock   |           |   bit_bar    |
       +-------------+           +--------------+
                 \
                  \ local IPC
                   v
             +---------------+
             | bit_launchpad |
             +---------------+
```

## 进程模型

### `bit_shelld`

后台核心守护进程，负责 niri IPC/event-stream、状态树、应用/托盘注册、配置持久化、本地 IPC 与命令路由。

### `bit_bar`

顶部菜单栏前端，负责工作区摘要、聚焦窗口信息、托盘区、时钟与系统状态入口。

### `bit_dock`

底部程序坞前端，负责 pinned/running 应用展示、激活/启动、重排、菜单和模式切换。

### `bit_launchpad`

全屏启动台前端，负责应用搜索、分类浏览、最近与收藏。

## 统一内存模型

建议模型对象：`BsShellState`、`BsWindow`、`BsWorkspace`、`BsOutput`、`BsAppState`、`BsDockItem`。

关键约束：

- `workspace_id` 由 niri 决定。
- `local_index` 只在单输出内有效。
- dock 以 app 聚合而不是窗口平铺。
- `window -> app` 映射以 `desktop_id` 为主，`app_id` 仅作回退匹配。

## 核心模块职责

- `StateStore`：集中状态存储、快照版本、订阅、view model 输出。
- `AppRegistry`：Desktop Entry 扫描与索引、动作与分类解析。
- `WorkspaceService`：工作区序列/标签/切换策略。
- `DockService`：pinned + running 聚合、焦点与多窗口策略。
- `LauncherService`：索引、分类、搜索、最近使用。
- `TrayService`：SNI host/watcher 管理、激活与菜单调用。
- `SettingsService`：配置与用户状态持久化。

## 技术选型（建议）

- 语言：C17
- 主循环：GLib main loop
- UI：GTK4
- layer-shell：gtk4-layer-shell
- D-Bus：GIO/GDBus
- 构建：Meson + Ninja
- JSON：JSON-GLib 或 cJSON/jansson

## 建议实现顺序

1. `bit_shelld`
2. `NiriBackend`
3. `StateStore`
4. `AppRegistry`
5. `bit_bar` 最小版
6. `bit_dock` 最小版
7. `bit_launchpad` 最小版
8. `TrayService`
9. 主题与动画
