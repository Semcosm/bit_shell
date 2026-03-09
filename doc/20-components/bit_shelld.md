# bit_shelld

## 角色

后台核心守护进程，唯一状态持有者。

## 设计定位

`bit_shelld` 是整个 `bit_shell` 的系统核心，而不是一个只负责“拉状态”的后台进程。
它统一吸收来自 niri、应用模型、托盘模型、配置文件和前端命令的输入，再向三个薄前端输出一致的快照、主题化事件与动作执行结果。

## 模块边界

### bit_shelld 负责什么

- 连接 niri socket
- 获取初始状态 + 订阅 event-stream
- 维护统一内存状态树
- 提供本地 IPC
- 管理应用注册表与托盘注册表
- 持久化配置与用户状态
- 将前端动作转译为 niri action/内部命令
- 对外提供统一的 snapshot / topic event 视图

### bit_shelld 不负责什么

- 不直接渲染 bar / dock / launchpad UI
- 不承担具体 GTK widget 布局与动画实现
- 不让前端直接写入核心状态
- 不把 compositor 状态解析逻辑分散到前端

## 输入

- `niri IPC`：初始状态、event-stream、动作响应
- `Desktop Entry scan`：应用元数据、分类、动作、图标索引
- `SNI registration`：托盘项注册、注销、状态变更与菜单请求
- `config/state files`：静态配置、pinned apps、recent apps、用户状态
- `frontend commands`：来自 `bit_bar`、`bit_dock`、`bit_launchpad` 的本地 IPC 命令

## 输出

- `state snapshot`：给前端的完整一致性快照
- `topic events`：按主题分发的增量事件，如 `windows`、`workspaces`、`dock`、`tray`
- `launch/tray/menu actions`：应用启动、托盘激活、上下文菜单、shell 内部动作的执行结果
- `persisted state`：更新后的 pinned、recents、favorites 与运行时状态落盘

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
