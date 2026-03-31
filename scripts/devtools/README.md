# devtools

用于放置开发过程中的测试、诊断和分析脚本。

- 适合收纳临时验证脚本、日志分析脚本、状态检查脚本
- 新增脚本时优先保持可重复执行，并注明输入、输出和依赖
- 脚本默认应支持稳定日志路径、明确退出码与最小环境假设

## 分类

### Acceptance

用于固化可重复的本地验收流程，优先覆盖 runtime 关键链路。

- `acceptance_tray_runtime.sh`：tray / tray_menu 的自动化验收入口，包含现有测试集与可选的 Wayland layer smoke
- `acceptance_dock_lifecycle.sh`：dock widget lifecycle 的自动化验收入口，包含现有测试集与可选的 Wayland live smoke

### Probes

用于短期诊断、采样和行为观察。

- `bit_dock_mem_probe.sh`
- `bit_dock_abcd_mem_probe.sh`

## 使用约束

- 脚本应尽量幂等，可重复运行，不依赖手工编辑脚本内容
- 文件头应注明输入、输出、依赖和可选环境变量
- 成功与失败必须使用可靠的退出码
- 若生成日志，默认路径应稳定，并允许通过环境变量或参数覆写
- 依赖 GUI / Wayland / niri 的步骤应显式标注为可选 smoke，而不是默认强依赖
