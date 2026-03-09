# 本地 IPC 协议（v1 草案）

## 目标

`bit_shelld` 与 `bit_bar` / `bit_dock` / `bit_launchpad` 之间采用 **Unix domain socket + JSON**。

v1 的协议目标不是追求泛化，而是保证以下几点：

- 前端可以稳定拉取完整快照
- 前端可以按 topic 订阅增量事件
- 命令集与 `core/model/ipc.h` 中的枚举保持一一对应
- 前端在断线重连后可以快速恢复

## 传输约定

- 传输层：Unix domain socket
- 编码：UTF-8 JSON
- 消息边界：一条请求对应一条 JSON 对象；实现层可采用单行 JSON 或长度前缀，v1 文档以“单对象”描述语义
- socket 路径：由 `shell.paths.ipc_socket_path` 决定

## 角色

- 服务端：`bit_shelld`
- 客户端：`bit_bar` / `bit_dock` / `bit_launchpad`

## topic 枚举

topic 与 `BsTopic` 一致：

- `shell`
- `windows`
- `workspaces`
- `dock`
- `tray`
- `settings`

### topic 语义

- `shell`：全局状态、焦点 app、launchpad 显示状态、错误与生命周期事件
- `windows`：窗口增删改、焦点变化、窗口标题与 app 归并结果
- `workspaces`：输出与工作区变化
- `dock`：dock item 聚合结果
- `tray`：SNI item 列表与状态变化
- `settings`：配置与用户状态变化

## command 枚举

命令与 `BsCommand` 一致：

- `subscribe`
- `snapshot`
- `launch_app`
- `activate_app`
- `focus_window`
- `switch_workspace`
- `toggle_launchpad`
- `pin_app`
- `unpin_app`
- `tray_activate`
- `tray_context_menu`

## 基本信封格式

### 请求

```json
{ "op": "snapshot" }
```

### 成功响应

```json
{ "ok": true, "kind": "ack", "command": "snapshot" }
```

### 失败响应

```json
{ "ok": false, "kind": "error", "command": "snapshot", "code": "invalid_argument", "message": "missing field: topics" }
```

## 快照请求

```json
{ "op": "snapshot" }
```

成功响应示例：

```json
{
  "ok": true,
  "kind": "snapshot",
  "generation": 42,
  "topic_versions": {
    "shell": 7,
    "windows": 42,
    "workspaces": 13,
    "dock": 19,
    "tray": 4,
    "settings": 3
  },
  "state": {
    "shell": {},
    "windows": [],
    "workspaces": [],
    "dock": [],
    "tray": [],
    "settings": {}
  }
}
```

## 订阅请求

```json
{ "op": "subscribe", "topics": ["shell", "dock", "tray"] }
```

约束：

- `topics` 不能为空
- topic 必须可解析为 `BsTopic`
- 重复 topic 应去重

响应示例：

```json
{
  "ok": true,
  "kind": "subscribed",
  "topics": ["shell", "dock", "tray"]
}
```

## 事件推送格式

订阅建立后，服务端可主动推送：

```json
{
  "kind": "event",
  "topic": "dock",
  "version": 19,
  "generation": 42,
  "payload": {
    "items": []
  }
}
```

### 版本语义

- `generation`：全局状态版本，每次任一 topic 变化时递增
- `version`：topic 局部版本，仅该 topic 变化时递增

前端可用它判断是否漏事件、是否需要重新拉快照。

## 断线重连流程

1. 前端重连 socket
2. 发送 `snapshot`
3. 发送 `subscribe`
4. 用新的 `generation` / `topic_versions` 覆盖本地缓存

v1 不保证离线事件回放；重连后的唯一恢复路径是重新拉完整快照。

## 命令参数约定

### launch_app

```json
{ "op": "launch_app", "desktop_id": "org.mozilla.firefox.desktop" }
```

### activate_app

```json
{ "op": "activate_app", "app_key": "org.mozilla.firefox.desktop" }
```

### focus_window

```json
{ "op": "focus_window", "window_id": "42" }
```

### switch_workspace

```json
{ "op": "switch_workspace", "workspace_id": "7" }
```

### toggle_launchpad

```json
{ "op": "toggle_launchpad" }
```

### pin_app / unpin_app

```json
{ "op": "pin_app", "app_key": "org.telegram.desktop" }
{ "op": "unpin_app", "app_key": "org.telegram.desktop" }
```

### tray_activate / tray_context_menu

```json
{ "op": "tray_activate", "item_id": "org.kde.StatusNotifierItem-1", "x": 1200, "y": 28 }
{ "op": "tray_context_menu", "item_id": "org.kde.StatusNotifierItem-1", "x": 1200, "y": 28 }
```

## 错误码建议

- `invalid_argument`
- `unknown_command`
- `unknown_topic`
- `not_found`
- `backend_unavailable`
- `not_ready`
- `internal_error`

## 与 core 的对应关系

- `BsTopic`：topic 枚举
- `BsCommand`：命令枚举
- `BsTopicSet`：订阅集合
- `BsSnapshot.topic_generations[]`：topic 局部版本
