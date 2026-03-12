# startup

用于放置启动 `bit_shell` 相关的脚本。

- 适合收纳本地启动器、调试启动包装脚本、环境准备脚本
- `start_bit_shelld.sh`：从仓库根目录的 `build/core/bit_shelld` 启动守护进程
- `start_bit_dock.sh`：从仓库根目录的 `build/core/bit_dock` 启动 Dock 前端
- 若脚本会影响 systemd、会话环境或发布方式，需要同步更新运行时文档
