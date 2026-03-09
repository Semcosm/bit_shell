# niri IPC / event-stream 设计

## 定位

niri IPC/event-stream 是 `bit_shell` 的唯一状态上游。

## `NiriBackend` 职责

- 连接 `$NIRI_SOCKET`
- 请求初始状态
- 进入 event-stream
- 事件应用到 `StateStore`
- 用户动作翻译为 niri 命令

## 设计收益

- 统一状态，不易 desync
- 前端逻辑简化
- 重连恢复集中处理
- 易于新增前端模块
