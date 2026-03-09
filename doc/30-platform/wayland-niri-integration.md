# Wayland + niri 集成

## 平台结论

`bit_shell` v1 围绕 Wayland + niri 设计，不提供 X11 或非 niri 通用兼容层。

## niri 适配重点

- 工作区是动态、按输出组织的模型。
- 输出断开时，工作区可能迁移到其他输出。
- 命名工作区可长期存在。

状态模型必须显式建模 `workspace -> output` 归属关系，而非全局线性工作区列表。

## layer rules 与 namespace

建议固定 namespace（以 `00-overview/constants.md` 为唯一登记源）：

- `bit-bar`
- `bit-dock`
- `bit-launchpad`

便于用户在 niri layer rules 中按 namespace 做阴影、透明度、几何和截图行为调整。
