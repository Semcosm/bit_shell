#include "common/utf8.h"

#include <glib.h>

#include "services/tray_menu_service.c"
#include "services/tray_service.c"

static GVariant *
test_new_trusted_string_variant(const guint8 *bytes, gsize len) {
  char *copy = NULL;

  g_return_val_if_fail(bytes != NULL, NULL);

  copy = g_malloc(len + 1);
  memcpy(copy, bytes, len);
  copy[len] = '\0';
  return g_variant_ref_sink(g_variant_new_from_data(G_VARIANT_TYPE_STRING,
                                                    copy,
                                                    len + 1,
                                                    TRUE,
                                                    g_free,
                                                    copy));
}

static GVariant *
test_new_layout_node(GVariant *label, GVariant *icon_name) {
  GVariantBuilder properties;
  GVariantBuilder children;

  g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
  if (label != NULL) {
    g_variant_builder_add(&properties, "{sv}", "label", label);
  }
  if (icon_name != NULL) {
    g_variant_builder_add(&properties, "{sv}", "icon-name", icon_name);
  }
  g_variant_builder_add(&properties, "{sv}", "type", g_variant_new_string("normal"));
  g_variant_builder_init(&children, G_VARIANT_TYPE("av"));

  return g_variant_ref_sink(g_variant_new("(i@a{sv}@av)",
                                          7,
                                          g_variant_builder_end(&properties),
                                          g_variant_builder_end(&children)));
}

static void
test_utf8_helper_preserves_valid_text(void) {
  g_autofree char *copy = NULL;

  copy = bs_utf8_dup_valid_or_null("托盘标题");
  g_assert_nonnull(copy);
  g_assert_cmpstr(copy, ==, "托盘标题");
}

static void
test_tray_service_text_sanitizes_invalid_utf8(void) {
  static const guint8 raw[] = {'A', 0xff, 'B'};
  g_autoptr(GVariant) value = NULL;
  g_autofree char *sanitized = NULL;

  value = test_new_trusted_string_variant(raw, sizeof(raw));
  sanitized = bs_tray_service_dup_variant_text(value);
  g_assert_nonnull(sanitized);
  g_assert_true(g_utf8_validate(sanitized, -1, NULL));
  g_assert_cmpstr(sanitized, ==, "A\xEF\xBF\xBD" "B");
}

static void
test_tray_service_object_path_is_validated_not_sanitized(void) {
  static const guint8 raw[] = {'/', 'M', 0xff, 'n', 'u'};
  g_autoptr(GVariant) value = NULL;
  g_autofree char *sanitized_text = NULL;
  g_autofree char *object_path = NULL;

  value = test_new_trusted_string_variant(raw, sizeof(raw));
  sanitized_text = bs_tray_service_dup_variant_text(value);
  object_path = bs_tray_service_dup_variant_object_path(value);
  g_assert_nonnull(sanitized_text);
  g_assert_true(g_utf8_validate(sanitized_text, -1, NULL));
  g_assert_null(object_path);
}

static void
test_tray_menu_service_layout_node_sanitizes_text(void) {
  static const guint8 label_raw[] = {'M', 'e', 'n', 'u', 0xff, 'L', 'a', 'b', 'e', 'l'};
  static const guint8 icon_raw[] = {'i', 'c', 'o', 'n', 0xff, 'n', 'a', 'm', 'e'};
  g_autoptr(GVariant) label = NULL;
  g_autoptr(GVariant) icon_name = NULL;
  g_autoptr(GVariant) value = NULL;
  BsTrayMenuNode *node = NULL;

  label = test_new_trusted_string_variant(label_raw, sizeof(label_raw));
  icon_name = test_new_trusted_string_variant(icon_raw, sizeof(icon_raw));
  value = test_new_layout_node(label, icon_name);
  node = bs_tray_menu_service_parse_layout_node(value);

  g_assert_nonnull(node);
  g_assert_nonnull(node->label);
  g_assert_nonnull(node->icon_name);
  g_assert_true(g_utf8_validate(node->label, -1, NULL));
  g_assert_true(g_utf8_validate(node->icon_name, -1, NULL));
  g_assert_cmpstr(node->label, ==, "Menu\xEF\xBF\xBD" "Label");
  g_assert_cmpstr(node->icon_name, ==, "icon\xEF\xBF\xBD" "name");

  bs_tray_menu_node_free(node);
}

int
main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/utf8/helper/preserves_valid_text", test_utf8_helper_preserves_valid_text);
  g_test_add_func("/tray_service/text_sanitizes_invalid_utf8",
                  test_tray_service_text_sanitizes_invalid_utf8);
  g_test_add_func("/tray_service/object_path_is_validated_not_sanitized",
                  test_tray_service_object_path_is_validated_not_sanitized);
  g_test_add_func("/tray_menu_service/layout_node_sanitizes_text",
                  test_tray_menu_service_layout_node_sanitizes_text);
  return g_test_run();
}
