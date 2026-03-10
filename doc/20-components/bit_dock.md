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
- 左键连击：循环同 app 多窗口
- 右键：应用动作菜单
- 滚轮：切换同 app 窗口
- 拖拽：重排 pinned

## 激活规则

1. 未运行：启动。
2. 已运行未聚焦：聚焦最近使用窗口。
3. 已聚焦单窗口：保持不变（最小化语义后续再定）。
4. 已聚焦多窗口：循环或弹菜单。

## 主键约定

- dock item 的应用主键在 v1 中应稳定使用 `desktop_id`
- 仅当窗口缺少 `desktop_id` 时，core 才回退到 `app_id` 做归并匹配

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
- `last_focus_ts`
- `window_ids`

排序规则当前为：

1. pinned item 按 `pinned_index`
2. running only item 按 `focused` / `last_focus_ts`

当前 backend 已支持通过 `pin_app` / `unpin_app` 命令修改 pinned 状态，并立即同步到 `state.json`。
