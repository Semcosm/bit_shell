# bit_shelld

## 角色

后台核心守护进程，唯一状态持有者。

## 设计定位

`bit_shelld` 是整个 `bit_shell` 的系统核心，而不是一个只负责“拉状态”的后台进程。
它统一吸收来自 niri、应用模型、托盘模型、配置文件和前端命令的输入，再向三个薄前端输出一致的快照、主题化事件与动作执行结果。

## 模块边界

### bit_shelld 负责什么

- 连接 niri socket
- 管理 event-stream 生命周期（连接、断线、重连、降级状态）
- 维护统一内存状态树
- 提供本地 IPC
- 管理应用注册表与托盘注册表
- 持久化配置与用户状态
- 对外提供统一的 snapshot / topic event 视图

### bit_shelld 不负责什么

- 不直接渲染 bar / dock / launchpad UI
- 不承担具体 GTK widget 布局与动画实现
- 不让前端直接写入核心状态
- 不把 compositor 状态解析逻辑分散到前端

## 输入

- `niri IPC`：`Outputs` 拉取、event-stream、连接状态变化
- `Desktop Entry scan`：应用元数据、分类、动作、图标索引
- `SNI registration`：托盘项注册、注销、状态变更与菜单请求
- `config/state files`：静态配置、pinned apps、recent apps、用户状态
- `frontend commands`：来自 `bit_bar`、`bit_dock`、`bit_launchpad` 的本地 IPC 命令

## 输出

- `state snapshot`：给前端的完整一致性快照
- `topic events`：按主题分发的增量事件（`shell/windows/workspaces/dock/tray/settings`）
- `persisted state`：更新后的 pinned、recents、favorites 与运行时状态落盘

## 当前实现状态

- 已实现：`snapshot` / `subscribe` 的完整 IPC 闭环，含 topic 版本与事件推送。
- 已实现：`NiriBackend` 事件消费、状态映射、断线重连与 degraded 状态上报。
- 已实现：`shell/windows/workspaces` 三个 topic 的真实 payload 生成。
- 已实现：`AppRegistry` 基于 `GAppInfo/GDesktopAppInfo` 的可见应用索引，含 `desktop_id` 主键与 `app_id/startup_wm_class` 回退别名。
- 已实现：`DockService` 的 `pinned + running` 聚合，`dock` topic 已输出真实 item 列表与顺序信息。
- 已实现：`state.json` 中 `pinned_apps` 的读取与写回，并驱动 dock 聚合。
- 已实现：`focus_window` / `switch_workspace` / `activate_app` / `launch_app` / `pin_app` / `unpin_app` 的最小真实路由执行。
- 未实现：`toggle_launchpad` / tray 命令等业务动作的真实执行链路。

## 关键内部模块

- `NiriBackend`
- `StateStore`
- `AppRegistry`
- `WorkspaceService`
- `DockService`
- `LauncherService`
- `TrayService`
- `SettingsService`
- `CommandRouter`
