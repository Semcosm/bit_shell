# 本地 IPC 协议（v1）

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
- `focus_next_app_window`
- `focus_prev_app_window`
- `focus_window`
- `switch_workspace`
- `toggle_launchpad`
- `reload_settings`
- `pin_app`
- `unpin_app`
- `tray_activate`
- `tray_context_menu`

参数约定：

- `launch_app.desktop_id` 必须传 `.desktop` 文件 ID
- `activate_app.app_key` / `focus_next_app_window.app_key` / `focus_prev_app_window.app_key` 应传稳定 `app_key`
- `pin_app.app_key` / `unpin_app.app_key` 在 v1 中也应传稳定 `app_key`
- `app_key` 在可解析时稳定等于 `desktop_id`
- 仅当窗口缺少 `desktop_id` 映射时，core 才会回退使用 `app_id` 做窗口匹配

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

成功响应示例（字段形态与当前 core 实现一致）：

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
    "shell": {
      "niri_connected": true,
      "degraded_reason": null,
      "focused_output_name": "HDMI-A-1",
      "focused_workspace_id": "2",
      "focused_window_id": "118",
      "focused_window_title": "Terminal"
    },
    "windows": {
      "windows": [
        {
          "id": "118",
          "title": "Terminal",
          "app_id": "foot",
          "desktop_id": "foot.desktop",
          "workspace_id": "2",
          "output_name": "HDMI-A-1",
          "focused": true,
          "floating": false,
          "fullscreen": false,
          "focus_ts": 1730000000000000000
        }
      ]
    },
    "workspaces": {
      "outputs": [
        {
          "name": "HDMI-A-1",
          "width": 1920,
          "height": 1080,
          "scale": 1.0,
          "focused": true
        }
      ],
      "workspaces": [
        {
          "id": "2",
          "name": "2",
          "output_name": "HDMI-A-1",
          "focused": true,
          "empty": false,
          "local_index": 1
        }
      ]
    },
    "dock": { "items": [] },
    "tray": { "items": [] },
    "settings": {
      "config_loaded": true,
      "pinned_apps": [],
      "dock": {
        "icon_size_px": 56,
        "magnification_enabled": true,
        "magnification_scale": 1.8,
        "hover_range_cap_units": 4,
        "spacing_px": 0,
        "bottom_margin_px": 14,
        "show_running_indicator": true,
        "animate_opening_apps": true,
        "display_mode": "immersive",
        "center_on_primary_output": true
      }
    }
  }
}
```

## 配置重载请求

```json
{ "op": "reload_settings" }
```

成功响应示例：

```json
{
  "ok": true,
  "kind": "reloaded",
  "command": "reload_settings",
  "changed": ["dock.*", "shell.auto_reconnect_niri"],
  "hot_applied": ["dock.*"],
  "restart_required": ["shell.auto_reconnect_niri"],
  "config_loaded": true
}
```

说明：

- 第一版 `reload_settings` 只对 `config.toml` 生效，不会导入 `state.json`
- 当前仅 `dock.*` 走热更新并通过 `settings` topic 推送给前端
- `shell.auto_reconnect_niri`、`shell.tray_watcher_name`、`shell.primary_output` 当前会报告为需要重启后端
- `bar.*`、`launchpad.*` 当前尚无运行时消费者，因此只会出现在 `restart_required`

## 订阅请求

```json
{ "op": "subscribe", "topics": ["shell", "dock", "tray"] }
```

响应示例（当前实现返回全量 topic_versions）：

```json
{
  "ok": true,
  "kind": "subscribed",
  "topics": ["shell", "dock", "tray"],
  "topic_versions": {
    "shell": 7,
    "windows": 42,
    "workspaces": 13,
    "dock": 19,
    "tray": 4,
    "settings": 3
  }
}
```

## 事件推送格式

订阅建立后，服务端会在对应 topic 变化时主动推送：

```json
{
  "kind": "event",
  "topic": "dock",
  "version": 19,
  "generation": 42,
  "payload": {
    "items": [
      {
        "app_key": "org.mozilla.firefox.desktop",
        "desktop_id": "org.mozilla.firefox.desktop",
        "name": "Firefox",
        "icon_name": "firefox",
        "pinned": true,
        "running": true,
        "focused": false,
        "pinned_index": 0,
        "window_ids": ["118", "132"]
      }
    ]
  }
}
```

说明：

- `payload` 的结构与 `snapshot.state.<topic>` 一致。
- 一个逻辑更新可同时触发多个 topic；这些事件可能共享同一 `generation`，但各自 `version` 独立递增。

### 版本语义

- `generation`：全局状态版本，只要本次更新有任一 topic 变化就递增
- `version`：topic 局部版本，仅该 topic 在本次更新被标记变化时递增

## 断线重连流程

1. 前端重连 socket
2. 发送 `snapshot`
3. 发送 `subscribe`
4. 用新的 `generation` / `topic_versions` 覆盖本地缓存

## 当前 core 落地状态

- `snapshot` 已输出真实 topic 结构：`shell/windows/workspaces/dock` 为完整结构，`settings` 已包含 `pinned_apps` 与 `dock` 配置对象，`tray` 仍为最小结构
- `subscribe` 已在 IPC server 内维持客户端订阅集合，并在 `StateStore` topic 变化时向对应客户端推送 `event`
- `StateStore` 支持批量更新事务（begin/finish），可在一次提交中原子推进多 topic 版本
- `switch_workspace`、`focus_window`、`activate_app`、`focus_next_app_window`、`focus_prev_app_window`、`launch_app`、`pin_app`、`unpin_app` 已接入真实执行链路
- `reload_settings` 已接入真实执行链路，并返回“已热应用 / 需重启”的结构化结果
- `pin_app` / `unpin_app` 当前会更新内存状态并立即 flush 到 `state.json`
- 其余命令当前仍以 `ack + params + todo` 形式回包
