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

使用 Unix domain socket + JSON。

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

## 协议细节

本页只给出状态流总览；字段级 contract、版本语义和错误码见 `10-architecture/ipc-contract.md`。
