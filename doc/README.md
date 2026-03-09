# bit_shell 文档目录

## 当前文档状态

本目录已按 `bit_shell` 总体设计草案拆分，核心设计结论已落到对应分层文档。

## 目录

```text
doc/
├─ README.md
├─ 00-overview/
│  ├─ project-scope.md
│  ├─ glossary.md
│  ├─ constants.md
│  └─ roadmap.md
├─ 10-architecture/
│  ├─ system-architecture.md
│  └─ event-and-state-flow.md
├─ 20-components/
│  ├─ bit_shelld.md
│  ├─ bit_bar.md
│  ├─ bit_dock.md
│  └─ bit_launchpad.md
├─ 30-platform/
│  ├─ wayland-niri-integration.md
│  └─ niri-ipc-event-stream.md
├─ 40-ui/
│  ├─ layer-shell-surfaces.md
│  └─ interaction-and-layout.md
├─ 50-models/
│  ├─ desktop-entry-and-menu.md
│  └─ status-notifier-item.md
├─ 60-runtime/
│  ├─ systemd-user-services.md
│  └─ niri-session-lifecycle.md
└─ 90-adr/
   └─ ADR-0001-doc-structure.md
```

## 建议阅读顺序

1. `00-overview/project-scope.md`
2. `00-overview/constants.md`
3. `10-architecture/system-architecture.md`
4. `10-architecture/event-and-state-flow.md`
5. `40-ui/layer-shell-surfaces.md`
6. `20-components/*.md`
7. `50-models/*.md`
8. `60-runtime/*.md`
9. `00-overview/roadmap.md`

- 参考链接：`00-overview/references.md`
