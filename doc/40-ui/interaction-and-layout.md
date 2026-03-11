# 交互与布局原则

## 总体原则

- 统一主题、动画与交互语义。
- 键盘优先路径完整，鼠标路径保持最短操作链。
- 多显示器行为可配置，适配 scale/DPI 差异。

## v1 优先级

先保证可用性与稳定性，再逐步增强预览、拖拽、模糊和高级动效。

## dock 交互约束

- magnification 应基于整排连续指针场，而不是“当前 hovered item + 固定邻居表”
- dock hover 命中应以整排容器为单位计算，而不是依赖单个 child 的 enter/leave
- 图标之间的 spacing 不应形成 dead zone；推荐使用扩展命中带或最近中心兜底
- hover 位移与 magnification scale 应分层表达，避免 focused/hovered/active 互相覆盖 transform
- 推荐将横向位移放在 item 外层容器，将纵向位移放在内容层，将 scale 放在图标按钮本体
- hover 求解输入应使用稳定参考坐标系，避免动态 margin 或外溢补偿反向污染指针坐标
- 图标放大后的局部让位应通过连续约束求解完成，不应依赖离散 bucket 或固定邻居模板
- 连续动画应通过 tick 驱动的 `current/target` 插值实现，而不是在 motion 事件里直接跳到最终视觉状态
- dock 数据刷新后应重建基础几何与顺序缓存，再由动画状态继续逼近新目标
