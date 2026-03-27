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
- `tray_activate` 与 `tray_context_menu` 当前会分别透传到 item 的 `Activate(x, y)` 与 `ContextMenu(x, y)`。
- item 对应的 bus name 消失时，shell 会自动把该 item 从 tray state 中移除。
- tray 展示顺序当前由 shell 侧 `presentation_seq` 拥有：已存在 item 的属性刷新不改变位置，新注册 item 追加到尾部，注销后重注册视为新项。
- `IconPixmap` 与 `AttentionIconPixmap` 当前已沿 `tray` topic / snapshot 透传到前端；dbusmenu 树同步与多 host 管理仍后置到后续阶段；`shell.tray_watcher_name` 已支持通过 app 层重建 tray service 热切换。
- 前端图标选择顺序当前是：可用 theme icon 优先，其次匹配 slot 的 pixmap，最后才退回文本 fallback。

## 交互建议

- 左键映射 `Activate(x, y)`
- 右键映射 `ContextMenu(x, y)`
- 菜单定位坐标由 `bit_bar` 上报给 shell
