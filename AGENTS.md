# Repository Guidelines

## Project Structure & Module Organization
`bit_shell` is a C17 project built with Meson. Core runtime code lives in `core/`, split by responsibility:
- `core/shelld/`: daemon entrypoint and app lifecycle (`main.c`, router, IPC server)
- `core/model/`: shared data contracts (config, IPC payloads, snapshots)
- `core/services/`: service-level logic (dock, launcher, tray, workspace, settings)
- `core/state/`: state store and topic change tracking
- `core/niri/`: niri integration backend

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
