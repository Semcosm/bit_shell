# Acceptance Flow

本文档把 tray / dock 当前最常见的运行态验收路径收成一套固定流程。目标不是替代单元测试，而是把“最近几轮修过、也最容易回退的链路”变成团队可重复执行的检查表。

## 验收矩阵

### Core Acceptance

所有相关改动默认先跑：

- `meson compile -C build`
- `meson test -C build`

### Tray Runtime Acceptance

优先入口：

- `scripts/devtools/acceptance_tray_runtime.sh`

它默认覆盖：

- `bit_bar_tray_menu_roundtrip`
- `tray_utf8_ingress`
- `bit_bar_tray_text_utf8`
- `bit_bar_tray_menu_ux`

可选 live smoke：

- `scripts/devtools/acceptance_tray_runtime.sh --live-wayland`

前置条件：

- 已编译 `bit_shelld` 与 `bit_bar`
- 当前会话可访问 Wayland
- 已设置 `WAYLAND_DISPLAY`
- 已设置 `NIRI_SOCKET`
- `niri` 命令在 `PATH`
- 运行前应关闭现有 `bit_bar` 实例，避免 `GApplication` 把新进程转发到旧实例

成功信号：

- 自动化 tray 测试全部通过
- live smoke 中能从 `niri msg --json layers` 观察到 `bit-shell-bar`
- `bit_bar` 退出后 layer surface 正常清理

仍建议补一轮人工交互：

- tray item 正常出现
- shell-owned 菜单可打开、关闭、切换项目
- 长标签菜单能换行并进入滚动
- 中文 / UTF-8 标签显示正常，坏字节以替代字符呈现

常见失败信号：

- `tray_menu_refresh` 链路不走
- `gtk_native_get_surface_transform` 相关 critical 再次出现
- menu sizing 回退，出现超出 monitor 的 popup

### Dock Runtime Acceptance

优先入口：

- `scripts/devtools/acceptance_dock_lifecycle.sh`

它默认覆盖：

- `bit_dock_widget_lifecycle`

可选 live smoke：

- `scripts/devtools/acceptance_dock_lifecycle.sh --live-wayland`

live smoke 会驱动一条受控序列：

- `icon -> label -> settings(layout) -> icon`

前置条件：

- 已编译 `bit_dock`
- 当前会话可访问 Wayland
- 已设置 `WAYLAND_DISPLAY`
- `python3` 可用
- 若希望验证 layer surface，还需要 `NIRI_SOCKET` 与 `niri`
- 运行前应关闭现有 `bit_dock` 实例，避免 `GApplication` 把新进程转发到旧实例

成功信号：

- `G_DEBUG=fatal-criticals` 下 `bit_dock` 不因 GTK critical 退出
- 运行日志不出现 `invalid unclassed pointer in cast to 'GtkImage'`
- 运行日志不出现 `GTK_IS_IMAGE (image) failed`
- 若启用 niri layer 检查，运行中能观察到 `bit-dock`，退出后 surface 正常清理

仍建议补一轮人工交互：

- 同一 item 从有图标切到文本 fallback，再切回有图标
- 中间反复 hover / magnify
- 触发一次真实 dock layout 更新

## 什么时候必须重跑

### 必跑 Tray Runtime Acceptance 的改动

- tray popup host / lifecycle
- tray menu sizing / geometry / monitor 约束
- tray VM 解析、tray_menu 视图渲染
- tray / tray_menu UTF-8 处理

### 必跑 Dock Runtime Acceptance 的改动

- dock item widget tree
- dock icon / label 切换
- dock layout / metrics
- dock 运行态 Wayland surface 管理

### 两条都建议重跑的改动

- frontend IPC 行为
- snapshot / topic 结构调整
- 与 niri geometry、monitor 或 layer shell 相关的公共逻辑
