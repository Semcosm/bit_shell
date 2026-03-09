# 托盘模型：StatusNotifierItem

## 组件关系

- `StatusNotifierItem`: 应用侧图标对象
- `StatusNotifierWatcher`: 跟踪注册/注销
- `Host`: 壳层展示端

## 架构落点

- `bit_shelld` 承担 host/watcher 生命周期管理。
- `bit_bar` 仅渲染 `TrayItemViewModel` 并回传交互。

## 交互建议

- 左键映射 `Activate(x, y)`
- 右键映射 `ContextMenu(x, y)`
- 菜单定位坐标由 `bit_bar` 上报给 shell
