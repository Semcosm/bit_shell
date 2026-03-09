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

- `pinned_apps`
- `recent_apps`
- `favorites`
- `recent_workspaces`

## 当前 core 落地状态

- `BsSettingsService.load()` 会确保 `config.toml` 与 `state.json` 的父目录存在
- 若文件缺失，core 会自动写入一份 **stub** 配置/状态文件
- 现阶段已实现“路径真实落盘”，但 TOML / JSON 的完整字段级解析仍为后续工作；存在旧文件时当前实现会读取并记录日志，但不会完整覆盖内存配置
- `flush()` 当前会把运行时状态以占位 JSON 结构写回 `state.json`，用于打通 I/O 路径与退出时最终落盘
