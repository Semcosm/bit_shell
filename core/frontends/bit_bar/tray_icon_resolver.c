#include "frontends/bit_bar/tray_icon_resolver.h"

static gboolean bs_bar_tray_icon_resolver_theme_icon_available(const char *icon_name);
static const BsTrayPixmap *bs_bar_tray_icon_resolver_select_best_pixmap(const GPtrArray *pixmaps,
                                                                        int target_size);
static GdkTexture *bs_bar_tray_icon_resolver_texture_from_pixmap(const BsTrayPixmap *pixmap);

void
bs_bar_tray_resolved_icon_clear(BsBarTrayResolvedIcon *icon) {
  if (icon == NULL) {
    return;
  }

  g_clear_pointer(&icon->icon_name, g_free);
  g_clear_object(&icon->texture);
  g_clear_pointer(&icon->fallback_label, g_free);
  icon->kind = BS_BAR_TRAY_ICON_FALLBACK_LABEL;
}

static gboolean
bs_bar_tray_icon_resolver_theme_icon_available(const char *icon_name) {
  GdkDisplay *display = NULL;
  GtkIconTheme *icon_theme = NULL;

  if (icon_name == NULL || *icon_name == '\0') {
    return FALSE;
  }

  display = gdk_display_get_default();
  if (display == NULL) {
    return FALSE;
  }

  icon_theme = gtk_icon_theme_get_for_display(display);
  return icon_theme != NULL && gtk_icon_theme_has_icon(icon_theme, icon_name);
}

static const BsTrayPixmap *
bs_bar_tray_icon_resolver_select_best_pixmap(const GPtrArray *pixmaps, int target_size) {
  const BsTrayPixmap *best = NULL;
  guint best_score = G_MAXUINT;
  gint best_max_dim = 0;

  if (pixmaps == NULL || pixmaps->len == 0) {
    return NULL;
  }

  for (guint i = 0; i < pixmaps->len; i++) {
    const BsTrayPixmap *pixmap = g_ptr_array_index((GPtrArray *) pixmaps, i);
    gint max_dim = 0;
    guint score = 0;

    if (pixmap == NULL || pixmap->argb32 == NULL || pixmap->width <= 0 || pixmap->height <= 0) {
      continue;
    }

    max_dim = MAX(pixmap->width, pixmap->height);
    score = (guint) ABS(max_dim - target_size);
    if (best == NULL || score < best_score || (score == best_score && max_dim > best_max_dim)) {
      best = pixmap;
      best_score = score;
      best_max_dim = max_dim;
    }
  }

  return best;
}

static GdkTexture *
bs_bar_tray_icon_resolver_texture_from_pixmap(const BsTrayPixmap *pixmap) {
  g_autoptr(GBytes) bytes = NULL;

  g_return_val_if_fail(pixmap != NULL, NULL);
  g_return_val_if_fail(pixmap->argb32 != NULL, NULL);

  bytes = g_bytes_ref(pixmap->argb32);
  return gdk_memory_texture_new(pixmap->width,
                                pixmap->height,
                                GDK_MEMORY_A8R8G8B8,
                                bytes,
                                (gsize) pixmap->width * 4U);
}

void
bs_bar_tray_icon_resolve(const BsBarTrayItemView *item,
                         int slot_size,
                         BsBarTrayResolvedIcon *out_icon) {
  const gboolean needs_attention = item != NULL && item->visual_state == BS_BAR_TRAY_VISUAL_ATTENTION;
  const GPtrArray *preferred_pixmaps = NULL;
  const GPtrArray *fallback_pixmaps = NULL;
  const BsTrayPixmap *pixmap = NULL;

  g_return_if_fail(item != NULL);
  g_return_if_fail(out_icon != NULL);

  bs_bar_tray_resolved_icon_clear(out_icon);

  if (bs_bar_tray_icon_resolver_theme_icon_available(item->effective_icon_name)) {
    out_icon->kind = BS_BAR_TRAY_ICON_THEME;
    out_icon->icon_name = g_strdup(item->effective_icon_name);
    return;
  }

  preferred_pixmaps = needs_attention ? item->attention_icon_pixmaps : item->icon_pixmaps;
  fallback_pixmaps = needs_attention ? item->icon_pixmaps : item->attention_icon_pixmaps;
  pixmap = bs_bar_tray_icon_resolver_select_best_pixmap(preferred_pixmaps, slot_size);
  if (pixmap == NULL) {
    pixmap = bs_bar_tray_icon_resolver_select_best_pixmap(fallback_pixmaps, slot_size);
  }
  if (pixmap != NULL) {
    out_icon->texture = bs_bar_tray_icon_resolver_texture_from_pixmap(pixmap);
    if (out_icon->texture != NULL) {
      out_icon->kind = BS_BAR_TRAY_ICON_TEXTURE;
      return;
    }
  }

  out_icon->kind = BS_BAR_TRAY_ICON_FALLBACK_LABEL;
  out_icon->fallback_label = g_strdup(item->fallback_label != NULL ? item->fallback_label : "?");
}
