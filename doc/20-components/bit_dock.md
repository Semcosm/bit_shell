# bit_dock

## 角色

底部程序坞前端。

## item 类型

- pinned only
- running only
- pinned + running
- special item（launchpad/trash/settings）

## 默认交互

- 左键：启动/激活
- 左键单击已聚焦多窗口 app：切到下一个窗口
- 右键：应用动作菜单
- 滚轮：切换同 app 窗口
- 拖拽：重排 pinned
- hover：按整排连续指针场驱动 magnification、局部横向让位和竖向抬升
- 启动反馈：`launch_app` 发送成功后触发两段式 bounce；应用进入 `running/focused` 后允许提前结束，但会先平顺收尾到静止位

## 激活规则

1. 未运行：启动。
2. 已运行未聚焦：聚焦最近使用窗口。
3. 已聚焦单窗口：发送 `activate_app`
4. 已聚焦多窗口：发送 `focus_next_app_window`

## 主键约定

- dock item 的应用主键在 v1 中统一为 `app_key`
- `app_key` 在可解析时稳定等于 `desktop_id`
- 仅当窗口缺少 `desktop_id` 映射时，core 才回退到 `app_id` 做归并匹配

## 当前 core 输出

`dock` topic 当前已输出真实 item 列表，至少包含：

- `app_key`
- `desktop_id`
- `name`
- `icon_name`
- `pinned`
- `running`
- `focused`
- `pinned_index`
- `window_ids`

排序规则当前为：

1. pinned item 按 `pinned_index`
2. running item 按首次进入运行态的顺序稳定排列
3. 未运行的 pinned item 保持在 pinned 区域，不参与 running 顺序

## 当前职责边界

- `bit_dock` 只负责展示与交互，不自己决定目标 `window_id`
- `bit_dock` 当前会发送：
  - `launch_app`
  - `activate_app`
  - `focus_next_app_window`
  - `focus_prev_app_window`
  - `pin_app`
  - `unpin_app`
- `bit_shelld` 基于 `StateStore` 权威状态决定 app 激活与多窗口切换目标

当前 backend 已支持通过 `pin_app` / `unpin_app` 命令修改 pinned 状态，并立即同步到 `state.json`。

## 当前前端实现约束

当前 `bit_dock` GTK 前端已落地以下交互语义：

- motion 事件挂在整排 `items_box` 上，而不是每个 item 上
- hover 输入状态以连续 `pointer_x / pointer_y` 表达，不再以单个 hovered item 作为主状态
- magnification 采用有限支撑的连续权重函数；权重按鼠标到每个图标基础中心的横向距离连续计算
- 图标 scale、lift 和 offset 先算 `target`，再由 tick 回调按时间常数插值到 `current`
- 横向让位不再依赖固定邻居权重，而是按最小中心距约束做局部连续求解
- hover 求解使用稳定参考坐标系；输入指针和 `base_center_x` 都定义在 `layout_box` 坐标系下，避免动态 margin 反馈抖动
- dock 宽度会吸收左右视觉外溢；顶部采用透明 headroom 吸收放大与抬升，但不改变白色玻璃框自身高度
- dock 数据刷新后会重建基础几何与视觉顺序，并保持后续动画基于新的基础中心继续插值

## 当前视觉分层

当前实现将位移与缩放拆到三层：

- `dock-slot`：负责横向位移
- `dock-slot-content`：保留内容容器与指示点布局，不再承担纵向位移
- `dock-item`：负责图标按钮自身的纵向位移与 magnification scale
- `dock-indicator`：只表达 running/focused 指示；随 slot 横向滑动，但不跟随图标的纵向抬升/启动 bounce 一起上下位移

当前状态不再通过离散 bucket class 叠加，而是连续输出到动态样式：

- `dock-slot`：`translateX(current_offset_x)`
- `dock-slot-content`：保持 `translateY(0)`
- `dock-item`：`translateY(current_lift + launch_feedback_y) scale(current_scale)`

当前动画模型：

- `scale`、`lift`、`offset` 都保留 `current_*` 与 `target_*`
- 每帧通过 GTK tick 回调做时间常数插值，而不是通过固定 `mag-*` bucket 切换
- 目标布局使用一维约束投影求解，避免鼠标位于两个图标中缝时出现离散锚点切换抖动
- 启动 bounce 复用同一个全局 tick 与动态样式链路，不单独建立第二套 CSS/timer 动画系统
- 启动 bounce 是 item 级瞬态反馈分量，不进入 hover range、横向让位或 magnification 的几何求解器
