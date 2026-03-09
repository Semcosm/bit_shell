# 事件与状态流

## 设计结论

niri IPC/event-stream 作为唯一上游状态源，`bit_shelld` 做集中归并并向前端发布本地 IPC 事件。

## 状态流

1. `NiriBackend` 连接 `$NIRI_SOCKET`。
2. 获取初始完整状态快照。
3. 订阅并持续消费 event-stream 增量。
4. 应用事件到 `StateStore`。
5. 生成 topic 级别的 view model 变化。
6. 通过本地 IPC 推送给 `bit_bar`/`bit_dock`/`bit_launchpad`。

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

推送事件示例：

```json
{ "topic": "dock", "kind": "items_changed", "version": 18, "items": [] }
{ "topic": "workspaces", "kind": "snapshot", "version": 31, "outputs": [] }
```

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

- `ipc_server` 已从纯 stub 推进到最小可用的 Unix socket skeleton
- `snapshot` 请求已能返回真实的 `generation` / `topic_versions` 以及按 topic 切分的占位 `state`
- `subscribe` 请求已可更新服务器侧订阅集合，并在 `StateStore` topic 变更时向订阅客户端主动发送 `event`
