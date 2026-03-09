# 术语表

- `shell`：桌面壳层，负责顶层 UI 编排与交互。
- `layer-shell`：Wayland 协议扩展，用于桌面级表面（bar/panel/overlay）。
- `niri IPC`：niri 提供的进程间通信接口。
- `event-stream`：来自窗口管理器或系统事件的连续流。
- `Desktop Entry`：`.desktop` 应用元数据规范。
- `Desktop Menu`：应用分类与菜单组织模型。
- `StatusNotifierItem`：现代托盘图标与菜单协议。
- `systemd --user`：用户级服务管理器。
- `namespace`：layer-shell surface 的稳定命名，用于 niri layer rules、调试与跨文档引用。
- 公共命名常量清单：见 `00-overview/constants.md`。
