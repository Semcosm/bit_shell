# niri session 生命周期

## 启动

- niri session 就绪后启动 `bit-shelld`
- `bit-shelld` 会尝试连接 niri；若失败进入 degraded 模式但进程继续运行
- event-stream 连通后，`shell.niri_connected=true`；`outputs_ready/workspaces_ready/windows_ready` 分别表示首帧是否已齐
- bootstrap 超时后只会对缺失 topic 做一次 fallback 请求；若 fallback 成功，连接保持可用
- 前端服务在 shell 后启动并订阅状态

## 运行期

- 前端可独立崩溃与重启
- 前端重连后先拉快照再接增量事件
- niri event-stream 断开后，`bit-shelld` 会更新 `shell.niri_connected=false` 与 `degraded_reason`
- 若启用自动重连，`bit-shelld` 会周期性重连 niri，成功后恢复 `shell.niri_connected=true`

## 退出

- 会话结束时组件有序停止
- `bit-shelld` 停止 IPC server、tray、niri backend，并执行设置状态落盘
