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
- 消息边界：当前实现采用**单行 JSON**，即一条请求/响应/事件占一行；客户端建立长连接后可持续收发多条消息
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

响应示例：

```json
{
  "ok": true,
  "kind": "subscribed",
  "topics": ["shell", "dock", "tray"],
  "topic_versions": {
    "shell": 7,
    "dock": 19,
    "tray": 4
  }
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

## 断线重连流程

1. 前端重连 socket
2. 发送 `snapshot`
3. 发送 `subscribe`
4. 用新的 `generation` / `topic_versions` 覆盖本地缓存

## 当前 core 落地状态

- `BsCommandRequest` 已作为命令参数对象进入 core，替代仅靠 `op` 和字符串探测的轻量结构
- `snapshot` 已有最小 JSON 序列化壳子，可输出 `generation`、`topic_versions` 与按 topic 切分的 `state`
- `subscribe` 已在 IPC server 内维持客户端订阅集合，并在 `StateStore` topic 变化时向对应客户端推送事件
- 非 `snapshot` / `subscribe` 命令当前仍以 `ack + params` 形式回包，真实动作路由留待后续接入 service/backend
