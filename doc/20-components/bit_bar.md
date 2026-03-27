# bit_bar

## 角色

顶部菜单栏前端。

## 布局建议

- 左：bit_shell 菜单 / 输出名 / 工作区摘要
- 中：当前 app 名 / 当前窗口标题；布局上以视觉居中为约束，不随左右内容宽度漂移
- 右：托盘 / 音量 / 网络 / 电池 / 时钟

## v1 行为

- 点击 workspace 标签切换工作区
- 左侧工作区条在数量增多时自动进入 compact overflow policy，focused workspace 保持最高可识别性
- 前端显式消费 `shell` readiness；连接中与首帧未齐阶段使用稳定占位，而不是把过渡态误显示为空状态
- 标题区可弹出窗口列表
- 托盘 item 左键激活、右键菜单
- tray 优先按图标渲染：theme icon 优先，其次使用应用提供的 pixmap，文本只作最终回退；无 tray item 时右侧不显示错误文案
- tray 顺序以 shell 提供的稳定展示序为准：属性刷新不重排，新项追加到尾部，重注册项视为新项
- tray 菜单当前经前端 bridge 统一管理生命周期：一次只允许一个 popup 打开；点同一 item 再次触发会关闭，切到另一 item 会先关闭旧 popup 再打开新 popup
- tray 菜单内容当前走 shell-owned `tray_menu` topic：`bit_bar` 只渲染 shell 下发的菜单树，并把点击回传成 `tray_menu_activate`
- 若某个 item 尚未同步到 shell-owned 菜单树，`bit_bar` 仍回退到既有 `tray_context_menu -> ContextMenu(x, y)` 透传链路
- tray popup 的锚定由 `bit_bar` 前端拥有：基于 tray button 几何和 monitor 可用区域做 placement / clamp；点击空白区、按 `Esc`、monitor 变化、前端重连或右侧重建时统一关闭
- tray 菜单行当前按结构化 row 渲染：check/radio 状态与 submenu affordance 分栏显示，长菜单进入滚动容器，键盘支持 `Up/Down/Home/End/Enter/Space/Left/Right/Esc`
- 时钟作为独立 trailing module 渲染，点击后显示本地轻量 popover；popover 内容完全前端本地生成，不依赖额外 IPC
- 右侧 cluster 以稳定几何优先：clock 预留独立宽度，tray 与 clock 之间使用固定 gap，tray 数量变化不应拖动 clock
- 右侧系统模块可插拔

## 非目标

v1 不将“通用 Linux 全局应用菜单桥接”作为核心能力；当前只同步 tray item 自己的 dbusmenu 树。
