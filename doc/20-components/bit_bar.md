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
- 托盘 item 左键激活、右键菜单；若 shell 下发 `menu_object_path`，优先走 shell-owned `tray_menu` 主题，本地旧式 `tray_context_menu -> ContextMenu(x, y)` 只保留给没有菜单对象路径的项目
- tray 优先按图标渲染：theme icon 优先，其次使用应用提供的 pixmap，文本只作最终回退；无 tray item 时右侧不显示错误文案
- tray 顺序以 shell 提供的稳定展示序为准：属性刷新不重排，新项追加到尾部，重注册项视为新项
- tray 本地菜单当前由对应 tray item widget 直接承载，遵循 GTK popover 相对触发 widget 弹出的语义；controller 只负责 one-open-menu、pending refresh / reopen 与关闭状态收口
- tray 菜单内容当前走 shell-owned `tray_menu` topic：`bit_bar` 只渲染 shell 下发的菜单树，并把点击回传成 `tray_menu_activate`
- 若某个 item 带有 `menu_object_path` 但 shell-owned 菜单树暂未就绪，`bit_bar` 会优先请求一次 `tray_menu_refresh`；只有根本没有 `menu_object_path` 的旧式项目才回退到 `tray_context_menu -> ContextMenu(x, y)` 透传链路
- tray 菜单按内容自然展开，并受当前 monitor 可用区域约束：最大高度不超过可用高度，最大宽度不超过可用宽度；超过上限后进入垂直滚动，长文本允许换行
- tray 菜单行按结构化 row 渲染：check / radio 状态与 submenu affordance 分栏显示，键盘支持 `Up/Down/Home/End/Enter/Space/Left/Right/Esc`
- tray / tray_menu 文本在 shell 的 D-Bus ingress 处先净化为有效 UTF-8，`bit_bar` VM 与视图层再做轻量兜底；有效中文保持原样，坏字节以替代字符呈现而不污染状态链
- 时钟作为独立 trailing module 渲染，点击后显示本地轻量 popover；popover 内容完全前端本地生成，不依赖额外 IPC
- 右侧 cluster 以稳定几何优先：clock 预留独立宽度，tray 与 clock 之间使用固定 gap，tray 数量变化不应拖动 clock
- 右侧系统模块可插拔

## 非目标

v1 不将“通用 Linux 全局应用菜单桥接”作为核心能力；当前只同步 tray item 自己的 dbusmenu 树。
