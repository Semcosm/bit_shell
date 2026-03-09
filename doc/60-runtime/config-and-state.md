# 配置与状态文件

## 设计目标

`bit_shell` 将“静态配置”和“用户运行时状态”分开存放，避免把用户偏好、最近使用记录与布局参数混在一个文件里。

## 文件位置

### 静态配置

建议路径：

- `~/.config/bit_shell/config.toml`

### 运行时状态

建议路径：

- `~/.local/state/bit_shell/state.json`

路径在 core 中由 `BsRuntimePaths` 表达，并归入 `BsShellConfig.paths`。

## 静态配置内容

### shell

- `shell.auto_reconnect_niri`
- `shell.tray_watcher_name`
- `shell.primary_output`

### bar

- `bar.height_px`
- `bar.show_workspace_strip`
- `bar.show_focused_title`
- `bar.show_tray`
- `bar.show_clock`

### dock

- `dock.icon_size_px`
- `dock.spacing_px`
- `dock.display_mode`
- `dock.enable_magnification`
- `dock.center_on_primary_output`

### launchpad

- `launchpad.resident`
- `launchpad.grid_icon_size_px`
- `launchpad.max_recent_apps`
- `launchpad.show_categories`

## 运行时状态内容

### pinned apps

```json
{
  "pinned_apps": [
    "org.mozilla.firefox.desktop",
    "org.wezfurlong.wezterm.desktop"
  ]
}
```

### recent apps

```json
{
  "recent_apps": [
    { "app_key": "org.mozilla.firefox.desktop", "score": 120 },
    { "app_key": "org.wezfurlong.wezterm.desktop", "score": 90 }
  ]
}
```

### launch counts / favorites

- `launch_counts`
- `favorites`
- `recent_workspaces`

## 默认值建议

### shell

- `auto_reconnect_niri = true`
- `tray_watcher_name = "org.kde.StatusNotifierWatcher"`

### bar

- `height_px = 32`
- `show_workspace_strip = true`
- `show_focused_title = true`
- `show_tray = true`
- `show_clock = true`

### dock

- `icon_size_px = 48`
- `spacing_px = 8`
- `display_mode = "immersive"`
- `enable_magnification = true`
- `center_on_primary_output = true`

### launchpad

- `resident = true`
- `grid_icon_size_px = 64`
- `max_recent_apps = 12`
- `show_categories = true`

## 落盘策略

- 配置文件读取失败：记录错误并回退到默认值
- 状态文件损坏：记录错误并生成空状态，不阻塞 shell 启动
- 用户操作导致的状态更新：异步或批量 flush
- shell 正常退出：执行一次最终 flush

## 与 core 的对应关系

- `BsRuntimePaths`：路径集合
- `BsShellConfig`：静态配置视图
- `BsBarConfig` / `BsDockConfig` / `BsLaunchpadConfig`：分组件配置
- `BsSettingsService`：读取、持有与落盘这些数据
