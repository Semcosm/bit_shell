# 事件与状态流

## 设计结论

niri IPC/event-stream 作为唯一上游状态源，`bit_shelld` 做集中归并并向前端发布本地 IPC 事件。

## 状态流

1. `NiriBackend` 连接 `$NIRI_SOCKET`。
2. 启动时先拉取 `Outputs`，并建立 `EventStream` 长连接。
3. 持续消费 event-stream 增量事件。
4. 将事件应用到 `StateStore`。
5. `StateStore` 在一次更新事务内聚合 topic 变化并提交版本。
6. `IpcServer` 向订阅该 topic 的客户端推送 `event`。

## 为什么前端不直连 niri

- 避免 3 份状态副本与重复协议解析。
- 避免多处重连与错误恢复逻辑。
- 降低状态漂移与调试复杂度。

## 本地 IPC 方案（v1）

使用 Unix domain socket + JSON。当前实现采用**长连接 + 单行 JSON**（一行一个 JSON 对象），`subscribe` 后服务端会保留订阅关系并按 topic 主动推送事件。

订阅：

```json
{ "op": "subscribe", "topics": ["shell", "windows", "workspaces", "dock", "tray", "settings"] }
```

拉快照：

```json
{ "op": "snapshot" }
```

命令示例：

```json
{ "op": "launch_app", "desktop_id": "org.mozilla.firefox.desktop" }
{ "op": "activate_app", "app_key": "org.mozilla.firefox.desktop" }
{ "op": "switch_workspace", "workspace_id": "7" }
{ "op": "toggle_launchpad" }
```

说明：

- `launch_app.desktop_id` 必须传 `.desktop` 文件 ID
- `activate_app.app_key` 在 v1 中应传 `desktop_id`
- 只有在窗口尚未建立 `desktop_id` 映射时，core 才会回退到 `app_id` 做匹配

推送事件示例：

```json
{
  "kind": "event",
  "topic": "workspaces",
  "version": 31,
  "generation": 77,
  "payload": {
    "outputs": [],
    "workspaces": []
  }
}
```

## 状态更新与版本推进

- 更新通过 `bs_state_store_begin_update()` / `bs_state_store_finish_update()` 包裹。
- 事务内可标记多个 topic 变化；提交时：
- 每个变更 topic 的 `topic_version` 自增 1。
- 若至少有一个 topic 变化，`generation` 自增 1。
- 提交后按 topic 触发 observer，IPC 层据此发事件。

## backend 文件分层建议

```text
core/niri/
  niri_socket.c
  niri_json.c
  niri_events.c
  niri_apply.c
  niri_actions.c
```

## 当前 core 落地状态

- `snapshot` 请求返回真实 `generation` / `topic_versions`，并返回按 topic 切分的实际 payload（不是占位壳）。
- `subscribe` 已可更新服务器侧订阅集合，并在 `StateStore` topic 变更时向订阅客户端主动推送 `event`。
- `NiriBackend` 已实现 event-stream 消费、状态应用、断线检测和自动重连。
- `AppRegistry` / `SettingsService` / `DockService` 已形成 `visible apps + pinned_apps + running windows -> dock items` 的聚合链路。
- `switch_workspace`、`focus_window`、`activate_app`、`launch_app`、`pin_app`、`unpin_app` 已接入最小真实执行链路。
- `pin_app` / `unpin_app` 会立即落盘到 `state.json`；其余未实现命令仍为占位返回。
