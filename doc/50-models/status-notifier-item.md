# 托盘模型：StatusNotifierItem

## 组件关系

- `StatusNotifierItem`: 应用侧图标对象
- `StatusNotifierWatcher`: 跟踪注册/注销
- `Host`: 壳层展示端

## 架构落点

- `bit_shelld` 承担 host/watcher 生命周期管理。
- `bit_bar` 仅渲染 `TrayItemViewModel` 并回传交互。

## 当前 3B-1 范围

- `bit_shelld` 当前已导出最小 `StatusNotifierWatcher`，接受 item 注册并在 session bus 上镜像 item 生命周期。
- `tray` topic / snapshot / IPC event 当前会输出真实 item 数组，当前已镜像 `id/title/status/icon_name/attention_icon_name/menu_object_path/item_is_menu/has_activate/has_context_menu/presentation_seq`。
- `tray_menu` topic / snapshot / IPC event 当前会按 `item_id` 输出 shell-owned dbusmenu 树；前端不直接连 D-Bus 拉菜单。
- `tray_activate` 与 `tray_context_menu` 当前会分别透传到 item 的 `Activate(x, y)` 与 `ContextMenu(x, y)`；`tray_menu_activate` 则用于执行 shell 已同步的菜单项。
- item 对应的 bus name 消失时，shell 会自动把该 item 从 tray state 中移除。
- item 注销、bus owner 消失或 tray service 重建时，shell 也会同步清理对应的 menu tree。
- tray 展示顺序当前由 shell 侧 `presentation_seq` 拥有：已存在 item 的属性刷新不改变位置，新注册 item 追加到尾部，注销后重注册视为新项。
- `IconPixmap` 与 `AttentionIconPixmap` 当前已沿 `tray` topic / snapshot 透传到前端；前端会优先用 theme icon，其次用 pixmap，再退回文本。多 host 管理仍后置到后续阶段；`shell.tray_watcher_name` 已支持通过 app 层重建 tray service 热切换。
- 前端图标选择顺序当前是：可用 theme icon 优先，其次匹配 slot 的 pixmap，最后才退回文本 fallback。
- `bit_bar` 当前已引入 tray menu bridge：它拥有 popup 生命周期、anchor 与单实例打开策略；菜单树本身仍由 shell 持有。
- 当前 popup placement 与菜单交互仍完全收在前端 bridge/view 子系统：shell 不参与键盘导航、滚动策略或 monitor-aware clamp。

## 交互建议

- 左键映射 `Activate(x, y)`
- 右键优先打开 shell-owned tray menu；当 shell 尚未持有该 item 的菜单树时，再回退到 `ContextMenu(x, y)`
- 对带 `menu_object_path` 的 item，前端应优先等待或主动刷新 shell-owned menu tree，而不是直接把它降级成 legacy `ContextMenu(x, y)` 项
- 菜单定位坐标由 `bit_bar` 上报给 shell
- 同一时刻只允许一个 tray popup 打开；点击空白区、按 `Esc`、monitor 变化或前端重连时应关闭
- shell-owned 菜单打开后应支持基础键盘导航、submenu 进退、disabled 跳过和普通项激活后自动关闭
