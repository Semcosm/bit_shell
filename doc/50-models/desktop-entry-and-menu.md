# 应用模型：Desktop Entry / Desktop Menu

## 设计约束

应用发现与启动基于 freedesktop 标准，不自定义私有应用元数据格式。

## 主键策略

- 规范主键：`desktop_id`
- 临时回退匹配：`app_id`
- 最终兜底：归一化 `exec/name`

v1 中：

- 启动应用必须依赖 `desktop_id`
- pinned/favorites/recent apps 应持久化 `desktop_id`
- `app_id` 只用于窗口归类和映射补全，不作为长期主键

## 当前 core 落地状态

- `AppRegistry` 当前通过 `GAppInfo.get_all()` 建立应用索引，而不是手写 desktop 文件扫描器
- 当前会保留 `desktop_id`、名称、图标，并建立 `desktop_id` / basename / `startup_wm_class` 的别名映射
- 当前尚未把 `categories` / `keywords` / menu tree 暴露给前端 IPC；这部分留给 launchpad 阶段

## 应用对象（建议）

```c
typedef struct {
    char *desktop_id;
    char *app_id;
    char *name;
    char *generic_name;
    char *icon_name;
    char *exec;
    char *comment;
    char **categories;
    char **keywords;
    bool no_display;
    bool terminal;
    GPtrArray *actions;
} BsApp;
```

## launchpad 分类策略

1. 优先按 Desktop Menu 结果。
2. 退化按 Categories 聚类。
3. 再退化显示全部应用。
