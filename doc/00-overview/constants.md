# 公共常量

本文件固化跨文档共享的命名常量，避免在不同文档中出现同一概念的多种写法。

## 项目与进程名

- 项目名：`bit_shell`
- 核心守护进程：`bit_shelld`
- 顶部菜单栏前端：`bit_bar`
- 底部程序坞前端：`bit_dock`
- 启动台前端：`bit_launchpad`

## layer-shell namespace

以下 namespace 作为 niri layer rules、日志、调试输出和 UI surface 识别的统一命名来源：

- `bit-bar`
- `bit-dock`
- `bit-launchpad`

### 使用约束

- 文档内提及 layer-shell surface 时，优先使用上述 namespace 原样拼写。
- 与 systemd service、可执行文件、配置键关联时，应保持一一对应的命名关系，而不是重新发明别名。
- 后续新增 layer-shell 组件时，应先在本文件登记，再扩散到其他文档。

## systemd user services

- `bit-shelld.service`
- `bit-bar.service`
- `bit-dock.service`
- `bit-launchpad.service`

## 配置键前缀

建议后续统一采用以下配置键前缀：

- `bar.*`
- `dock.*`
- `launchpad.*`
- `shell.*`

## 日志前缀

建议统一采用以下日志前缀：

- `[bit_shelld]`
- `[bit_bar]`
- `[bit_dock]`
- `[bit_launchpad]`
