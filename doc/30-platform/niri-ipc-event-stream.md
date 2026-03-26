# niri IPC / event-stream 设计

## 定位

niri IPC/event-stream 是 `bit_shell` 的唯一状态上游。

## `NiriBackend` 当前职责

- 连接 `$NIRI_SOCKET`
- 启动时拉取一次 `Outputs`
- 建立 `"EventStream"` 长连接并持续异步读行
- 解析 niri JSON 事件并应用到 `StateStore`
- 在连接故障时维护 `shell` topic 的降级状态
- 根据 `shell.auto_reconnect_niri` 进行自动重连

## 启动与降级行为

1. `bs_niri_backend_start()` 将 backend 标记为 running。
2. 尝试连接 event-stream（连接前会先刷新 `Outputs`）。
3. 若 event-stream 连接成功：立即设置 `shell.niri_connected = true`，并开始 bootstrap。
4. bootstrap 优先等待初始 `WorkspacesChanged` / `WindowsChanged`；若超时，只对缺失 topic 发一次补偿请求。
5. bootstrap 完成后开始异步读取事件；若补偿请求也失败，则关闭连接并进入 degraded 模式。
6. 若连接失败：进程仍继续运行（degraded 模式），设置 `shell.niri_connected = false` 和 `shell.degraded_reason`，若允许自动重连则调度重连定时器。

## 已接入的 niri 事件

- `WorkspacesChanged`：全量替换工作区列表
- `WorkspaceActivated`：更新工作区聚焦态
- `WorkspaceActiveWindowChanged`：刷新工作区空/非空派生信息
- `WindowsChanged`：全量替换窗口列表
- `WindowOpenedOrChanged`：窗口增量 upsert
- `WindowClosed`：删除窗口
- `WindowFocusChanged`：更新窗口聚焦态
- `WindowFocusTimestampChanged`：更新窗口焦点时间戳

## 状态映射（当前实现）

- `shell`：`niri_connected/outputs_ready/workspaces_ready/windows_ready/bootstrap_used_fallback/degraded_reason/focused_output_name/focused_workspace_id/focused_window_id/focused_window_title`
- `workspaces.outputs[]`：`name/width/height/scale/focused`
- `workspaces.workspaces[]`：`id/name/output_name/focused/empty/local_index`
- `windows.windows[]`：`id/title/app_id/desktop_id/workspace_id/output_name/focused/floating/fullscreen/focus_ts`

## 当前边界

- `switch_workspace` / `focus_window` 已接入真实 niri action 请求。
- 应用启动目前通过 Desktop Entry 启动；`toggle_launchpad` / tray 命令等动作仍未接入真实执行链路。

## 设计收益

- 统一状态，不易 desync
- 前端逻辑简化
- 重连恢复集中处理
- 易于新增前端模块
