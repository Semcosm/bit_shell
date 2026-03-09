# 应用模型：Desktop Entry / Desktop Menu

## 设计约束

应用发现与启动基于 freedesktop 标准，不自定义私有应用元数据格式。

## 主键策略

- 首选：`desktop_id`
- 次选：`app_id`
- 再次选：归一化 `exec/name`

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
