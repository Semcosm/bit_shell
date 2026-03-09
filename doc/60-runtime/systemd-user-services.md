# systemd user services 部署

## 部署目标

所有组件以 user service 运行在 niri session 内。

## 服务清单

- `bit-shelld.service`
- `bit-bar.service`
- `bit-dock.service`
- `bit-launchpad.service`

## 推荐依赖

- `bit-shelld.service` 为主服务
- `bit-bar.service`：`After=bit-shelld.service`
- `bit-dock.service`：`After=bit-shelld.service`
- `bit-launchpad.service`：v1 建议常驻，`After=bit-shelld.service`

## 运行策略

- `Restart=on-failure`
- 会话环境变量注入（Wayland/niri）
- niri session 生命周期内统一托管
