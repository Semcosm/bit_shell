# 应用与窗口匹配

## 目标

`bit_dock` 与 `bit_bar` 都不是直接显示“窗口列表”，而是需要把窗口归并到 app 级对象上。
因此必须定义稳定的 `window -> app` 匹配规则。

## 匹配优先级

建议按以下优先级进行：

1. `desktop_id`
2. `app_id`
3. 归一化后的 `Exec`
4. 回退到名称/已知规则表

## 主键约定

- 应用主键：`app_key`
- 首选值：`desktop_id`
- 次选值：`app_id`
- 回退值：稳定归一化字符串

dock item 以 `app_key` 聚合，而不是以 `window_id` 聚合。

## 特殊情况

### 终端启动的程序

不把“终端里的前台进程”视为独立 app。
`wezterm` / `foot` / `kitty` 这类窗口仍归并到其终端 app。

### 多窗口同 app

多个窗口只产生一个 dock item；聚合后保留：

- `window_ids`
- `focused`
- `last_focus_ts`

### 无法识别的窗口

无法稳定识别时，仍创建 fallback app：

- `app_key = unknown:<window_id>` 或稳定规则值
- 名称优先取窗口标题或 app_id
