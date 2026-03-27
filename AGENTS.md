# Repository Guidelines

## Project Structure & Module Organization
`bit_shell` is a C17 project built with Meson. Core runtime code lives in `core/`, split by responsibility:
- `core/shelld/`: daemon entrypoint and app lifecycle (`main.c`, router, IPC server)
- `core/model/`: shared data contracts (config, IPC payloads, snapshots)
- `core/services/`: service-level logic (dock, launcher, tray, workspace, settings)
- `core/state/`: state store and topic change tracking
- `core/niri/`: niri integration backend
- `scripts/`: repository-local helper scripts for development, diagnostics, and maintenance
  - `scripts/startup/`: startup scripts and wrappers for running `bit_shell`
  - `scripts/devtools/`: development-time testing, diagnostics, and analysis scripts

Design and architecture docs are in `doc/` (overview, components, runtime, ADRs). Build artifacts are generated under `build/` and should not be manually edited.

## Build, Test, and Development Commands
- `meson setup build`  
  Configure the local build directory (run once, or after clean checkout).
- `meson compile -C build`  
  Compile `bit_shelld` and dependencies.
- `meson test -C build`  
  Run Meson test targets (currently minimal/none; still use for future test additions).
- `./build/core/bit_shelld`  
  Run the daemon binary locally.

If you change build graph or source lists, update `meson.build` and `core/meson.build` together.

## Coding Style & Naming Conventions
Use C17 and keep code compatible with GLib/GIO usage already in the repo.
- Indentation: 2 spaces, no tabs.
- Naming: `snake_case` for files/functions, `Bs*` for typedefs/struct types, `bs_*` for public functions.
- Keep header/source pairing consistent (e.g., `foo.h` + `foo.c` in same module).
- Prefer small, focused functions and explicit error paths via `GError **`.

## Testing Guidelines
There is no full test suite yet. For contributions:
- Build cleanly with `meson compile -C build`.
- Run `meson test -C build` if test targets exist.
- Add targeted tests when introducing non-trivial logic; place new tests in a dedicated Meson test target and document how to run them.
- Include manual verification notes in PRs when automated coverage is unavailable.

## Commit & Pull Request Guidelines
Follow the existing commit pattern: `<scope>: <imperative summary>` (examples: `core: add ...`, `docs: add ...`, `build: add ...`).
- Keep commits logically scoped and reviewable.
- PRs should include: purpose, key changes, validation steps, and impacted docs.
- Link related issues/ADRs when relevant, and add screenshots/log snippets for behavior changes.

## Agent-Specific Instructions
- 默认使用中文与仓库协作者沟通与回复，除非对方明确要求其他语言。
- 开发时保持谨慎；如果需要额外查资料，先向协作者说明要查什么、为什么查；对不确定且可能影响实现方向的事项，先提问确认，再继续开发。
- 分析运行日志时，默认优先查看 `/home/cestlavie/logs` 下的最新日志文件。

## PR 结果记录规范
- 先写“目标”，再写“结果”，不要直接堆提交记录
- “结果概述”只写用户可感知或架构上有意义的变化
- “实现要点”强调模块职责变化，而不是逐行描述代码
- “验证结果”必须包含构建、测试、以及具体测试目标
- “提交记录”按提交顺序列出，保持 message 原文
- “影响与结论”用 1~2 句话说明这次 PR 为后续工作铺平了什么

## PR-{N} 结果记录

### 状态
- 已完成并推送

### 本次目标
- {一句话说明这次 PR 解决的问题或推进的目标}

### 结果概述
- {结果 1：说明核心能力/链路上的新增支持}
- {结果 2：说明数据流、接口或快照层的变化}
- {结果 3：说明前端/渲染/调用侧的改动}
- {结果 4：说明职责收敛、模块拆分或结构优化}
- {结果 5：说明测试补齐情况}

### 实现要点
- {模块/文件 A}：{做了什么，为什么这样做}
- {模块/文件 B}：{做了什么，为什么这样做}
- {模块/文件 C}：{做了什么，为什么这样做}

### 验证结果
- `meson compile -C build`：通过
- `meson test -C build`：通过
- 测试项：
  - `{test target 1}`
  - `{test target 2}`

### 提交记录
- `{commit sha 1}` `{commit message 1}`
- `{commit sha 2}` `{commit message 2}`
- `{commit sha 3}` `{commit message 3}`

### 推送信息
- 远端：`{remote}`
- 分支：`{branch}`
- 推送范围：`{start}..{end}`

### 影响与结论
- {一句话总结这次 PR 带来的直接收益}
- {一句话说明当前状态是否可作为后续 PR 的基线}
