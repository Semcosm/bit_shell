#include <gtk/gtk.h>

#include "frontends/bit_bar/tray_icon_resolver.h"

static BsTrayPixmap *
make_pixmap(gint width, gint height, guint8 seed) {
  BsTrayPixmap *pixmap = g_new0(BsTrayPixmap, 1);
  gsize len = (gsize) width * (gsize) height * 4U;
  guint8 *bytes = g_malloc0(len);

  for (gsize i = 0; i < len; i += 4) {
    bytes[i] = 0xff;
    bytes[i + 1] = seed;
    bytes[i + 2] = seed;
    bytes[i + 3] = seed;
  }

  pixmap->width = width;
  pixmap->height = height;
  pixmap->argb32 = g_bytes_new_take(bytes, len);
  return pixmap;
}

static void
free_pixmap(gpointer data) {
  BsTrayPixmap *pixmap = data;

  if (pixmap == NULL) {
    return;
  }

  g_clear_pointer(&pixmap->argb32, g_bytes_unref);
  g_free(pixmap);
}

static void
test_tray_icon_resolver_fallback_label(void) {
  BsBarTrayItemView item = {0};
  BsBarTrayResolvedIcon resolved = {0};

  item.fallback_label = "F";
  item.visual_state = BS_BAR_TRAY_VISUAL_PASSIVE;

  bs_bar_tray_icon_resolve(&item, 20, &resolved);
  g_assert_cmpint(resolved.kind, ==, BS_BAR_TRAY_ICON_FALLBACK_LABEL);
  g_assert_nonnull(resolved.fallback_label);
  g_assert_cmpstr(resolved.fallback_label, ==, "F");
  bs_bar_tray_resolved_icon_clear(&resolved);
}

static void
test_tray_icon_resolver_prefers_attention_pixmap(void) {
  BsBarTrayItemView item = {0};
  BsBarTrayResolvedIcon resolved = {0};

  item.icon_pixmaps = g_ptr_array_new_with_free_func(free_pixmap);
  item.attention_icon_pixmaps = g_ptr_array_new_with_free_func(free_pixmap);
  item.visual_state = BS_BAR_TRAY_VISUAL_ATTENTION;
  item.fallback_label = "F";

  g_ptr_array_add(item.icon_pixmaps, make_pixmap(16, 16, 0x11));
  g_ptr_array_add(item.attention_icon_pixmaps, make_pixmap(24, 24, 0x22));

  bs_bar_tray_icon_resolve(&item, 20, &resolved);
  g_assert_cmpint(resolved.kind, ==, BS_BAR_TRAY_ICON_TEXTURE);
  g_assert_nonnull(resolved.texture);
  g_assert_cmpint(gdk_texture_get_width(resolved.texture), ==, 24);
  g_assert_cmpint(gdk_texture_get_height(resolved.texture), ==, 24);

  bs_bar_tray_resolved_icon_clear(&resolved);
  g_ptr_array_unref(item.icon_pixmaps);
  g_ptr_array_unref(item.attention_icon_pixmaps);
}

static void
test_tray_icon_resolver_selects_closest_pixmap_size(void) {
  BsBarTrayItemView item = {0};
  BsBarTrayResolvedIcon resolved = {0};

  item.icon_pixmaps = g_ptr_array_new_with_free_func(free_pixmap);
  item.visual_state = BS_BAR_TRAY_VISUAL_PASSIVE;
  item.fallback_label = "F";

  g_ptr_array_add(item.icon_pixmaps, make_pixmap(16, 16, 0x11));
  g_ptr_array_add(item.icon_pixmaps, make_pixmap(32, 32, 0x22));

  bs_bar_tray_icon_resolve(&item, 20, &resolved);
  g_assert_cmpint(resolved.kind, ==, BS_BAR_TRAY_ICON_TEXTURE);
  g_assert_nonnull(resolved.texture);
  g_assert_cmpint(gdk_texture_get_width(resolved.texture), ==, 16);
  g_assert_cmpint(gdk_texture_get_height(resolved.texture), ==, 16);

  bs_bar_tray_resolved_icon_clear(&resolved);
  g_ptr_array_unref(item.icon_pixmaps);
}

int
main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/tray_icon_resolver/fallback_label", test_tray_icon_resolver_fallback_label);
  g_test_add_func("/tray_icon_resolver/attention_pixmap", test_tray_icon_resolver_prefers_attention_pixmap);
  g_test_add_func("/tray_icon_resolver/closest_size", test_tray_icon_resolver_selects_closest_pixmap_size);
  return g_test_run();
}
