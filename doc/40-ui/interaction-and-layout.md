# 交互与布局原则

## 总体原则

- 统一主题、动画与交互语义。
- 键盘优先路径完整，鼠标路径保持最短操作链。
- 多显示器行为可配置，适配 scale/DPI 差异。

## v1 优先级

先保证可用性与稳定性，再逐步增强预览、拖拽、模糊和高级动效。

## dock 交互约束

- magnification 优先保证稳定性，不以整排连续函数为前提
- hover 位移与 magnification scale 应分层表达，避免 focused/hovered/active 互相覆盖 transform
- 推荐将纵向位移放在 item 外层容器，将 scale 放在图标按钮本体
- dock 数据刷新后应重新校验 hover 关联，避免指向已删除或已替换 item 的悬空状态
