# systemd user services 部署

## 部署目标

所有组件以 user service 运行在 niri session 内。

## 服务清单

- `bit-shelld.service`
- `bit-bar.service`
- `bit-dock.service`
- `bit-launchpad.service`

## 服务关系图

```text
graphical-session.target
        |
        +-- bit-shelld.service
              |
              +-- bit-bar.service
              +-- bit-dock.service
              +-- bit-launchpad.service
```

## 推荐依赖

### 核心服务

- `bit-shelld.service` 为主服务
- 建议：`PartOf=graphical-session.target`
- 建议：`WantedBy=graphical-session.target`

### 前端服务

- `bit-bar.service`：`Wants=bit-shelld.service`，`After=bit-shelld.service`，`PartOf=graphical-session.target`
- `bit-dock.service`：`Wants=bit-shelld.service`，`After=bit-shelld.service`，`PartOf=graphical-session.target`
- `bit-launchpad.service`：v1 建议常驻，`Wants=bit-shelld.service`，`After=bit-shelld.service`，`PartOf=graphical-session.target`

## BindsTo 策略

默认**不建议**在三个前端服务上使用 `BindsTo=bit-shelld.service`。

原因：

- 允许 `bit_bar`、`bit_dock`、`bit_launchpad` 单独崩溃和单独重启
- 避免核心服务短暂重启时把全部前端一并强制拉倒
- 更符合“核心状态常驻、前端可恢复”的架构目标

只有在后续确认需要把前端与核心服务做成强生命周期绑定时，再考虑引入 `BindsTo=`。

## 单独重启策略

以下前端应允许单独重启：

- `bit-bar.service`
- `bit-dock.service`
- `bit-launchpad.service`

重启前端时，不应要求 `bit-shelld` 一起重启；前端应在重连本地 IPC 后自行拉取 snapshot 并恢复显示。

## bit_launchpad 常驻策略

v1 建议 `bit-launchpad.service` 常驻运行，而不是 socket/on-demand：

- 实现简单，便于先稳定 overlay surface、输入焦点和动画路径
- 避免首次唤起时额外的冷启动延迟
- 方便后续与 shell 级主题、搜索索引和 recent state 做持续同步

后续若需要降低常驻资源占用，可以在专门的设计文档中再评估 socket activation 或按需拉起模式。

## 运行策略

- `Restart=on-failure`
- 会话环境变量注入（Wayland/niri）
- niri session 生命周期内统一托管
