# 项目范围（bit_shell）

## 项目名

`bit_shell`

## 产品形态

完整桌面壳体验。

## 组成

- `bit_shelld`
- `bit_bar`
- `bit_dock`
- `bit_launchpad`

## 目标平台

- Wayland
- niri

## 状态源

- niri IPC
- event-stream

## UI 形态

3 个独立 layer-shell surface。

## 应用与托盘模型

- 应用模型：Desktop Entry / Desktop Menu
- 托盘模型：StatusNotifierItem

## 部署模型

在 niri session 中，以 systemd user services 运行。

## 首发边界（v1）

支持：

- Wayland
- niri
- systemd user session
- layer-shell compositor 路径

不支持：

- X11
- GNOME on Wayland 兼容模式
- 非 niri 的通用 backend
- 强一致全局菜单复制

## 产品目标

1. 让 niri 拥有统一壳层：bar、dock、launchpad 共享同一状态模型与交互语义。
2. 让 UI 行为服从 niri：工作区、输出、焦点、窗口状态以 niri 为准。
3. 把复杂度压到后台：业务状态集中在 `bit_shelld`。
4. 先做稳定，再做炫技：v1 优先可用性与架构稳定。

## 非目标

- 通用 Wayland compositor 兼容层
- X11 任务栏兼容逻辑
- 复杂全局菜单桥接
- 深度系统设置中心
- 文件管理器/桌面图标
- 通知守护进程
- 锁屏

## 项目宣言

> `bit_shell` 是一个面向 niri 的统一桌面壳层，由 `bit_shelld` 统一管理状态与系统集成，并通过 `bit_bar`、`bit_dock`、`bit_launchpad` 三个独立 layer-shell 前端提供完整的一体化桌面体验。
