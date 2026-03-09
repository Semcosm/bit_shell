# ADR-0001: 文档目录结构分层

## 状态

Accepted

## 背景

`bit_shell` 同时涉及 Wayland 协议、WM 集成、UI 组件和运行部署，文档需要按职责分层。

## 决策

采用 `overview -> architecture -> components -> platform/ui/models -> runtime -> adr` 的目录结构。

## 影响

- 降低新成员理解成本
- 便于并行维护
- 关键决策可追溯
