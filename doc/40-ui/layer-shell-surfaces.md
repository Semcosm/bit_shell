# 3 个独立 layer-shell surface

## 分配

- `bit_bar`: `top`
- `bit_dock`: `top`
- `bit_launchpad`: `overlay`

## 参数建议

### bit_bar

- layer: `top`
- anchor: `top,left,right`
- keyboard: `none`
- exclusive_zone: `bar_height`

### bit_dock

- layer: `top`
- anchor: `bottom`
- keyboard: `none`
- exclusive_zone: 可配置

dock 模式：

- `immersive`: `0`
- `reserved`: dock 高度
- `autohide`: 显示时保留，隐藏时清零

### bit_launchpad

- layer: `overlay`
- anchor: `top,bottom,left,right`
- keyboard: `exclusive`
- exclusive_zone: `0` 或不依赖 zone

## 关键语义

- bar/dock 属于壳层 UI，不应作为 workspace 内容一起缩放。
- launchpad 需在全屏窗口上方，使用 `overlay`。
