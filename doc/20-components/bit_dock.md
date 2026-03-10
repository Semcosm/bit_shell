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
