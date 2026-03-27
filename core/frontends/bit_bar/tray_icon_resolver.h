#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_ICON_RESOLVER_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_TRAY_ICON_RESOLVER_H

#include <gtk/gtk.h>

#include "frontends/bit_bar/bar_view_model.h"

typedef enum {
  BS_BAR_TRAY_ICON_THEME = 0,
  BS_BAR_TRAY_ICON_TEXTURE,
  BS_BAR_TRAY_ICON_FALLBACK_LABEL,
} BsBarTrayResolvedIconKind;

typedef struct {
  BsBarTrayResolvedIconKind kind;
  char *icon_name;
  GdkTexture *texture;
  char *fallback_label;
} BsBarTrayResolvedIcon;

void bs_bar_tray_resolved_icon_clear(BsBarTrayResolvedIcon *icon);

void bs_bar_tray_icon_resolve(const BsBarTrayItemView *item,
                              int slot_size,
                              BsBarTrayResolvedIcon *out_icon);

#endif
