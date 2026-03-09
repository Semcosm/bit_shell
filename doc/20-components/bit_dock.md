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
