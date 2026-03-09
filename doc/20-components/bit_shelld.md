# bit_shelld

## 角色

后台核心守护进程，唯一状态持有者。

## 责任

- 连接 niri socket
- 获取初始状态 + 订阅 event-stream
- 维护统一内存状态树
- 提供本地 IPC
- 管理应用注册表与托盘注册表
- 持久化配置与用户状态
- 将前端动作转译为 niri action/内部命令

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
