#include "frontends/bit_bar/bar_view_model.h"
#include "frontends/bit_bar/tray_menu_view.h"

#include <glib.h>
#include <string.h>

#include "frontends/bit_bar/bar_view_model.c"
#include "frontends/bit_bar/tray_menu_view.c"

static void
test_bar_vm_text_preserves_valid_utf8(void) {
  g_autofree char *copy = NULL;

  copy = bs_bar_vm_dup_valid_text("托盘中文");
  g_assert_nonnull(copy);
  g_assert_cmpstr(copy, ==, "托盘中文");
}

static void
test_bar_vm_text_sanitizes_invalid_utf8(void) {
  static const char raw[] = {'A', (char) 0xff, 'B', '\0'};
  g_autofree char *copy = NULL;

  copy = bs_bar_vm_dup_valid_text(raw);
  g_assert_nonnull(copy);
  g_assert_true(g_utf8_validate(copy, -1, NULL));
  g_assert_cmpstr(copy, ==, "A\xEF\xBF\xBD" "B");
}

static void
test_bar_vm_object_path_drops_invalid_value(void) {
  g_autofree char *valid = NULL;
  g_autofree char *invalid = NULL;

  valid = bs_bar_vm_dup_valid_object_path_or_null("/MenuBar");
  invalid = bs_bar_vm_dup_valid_object_path_or_null("/Menu Bar");
  g_assert_cmpstr(valid, ==, "/MenuBar");
  g_assert_null(invalid);
}

static void
test_tray_menu_view_safe_label_strips_and_sanitizes(void) {
  BsTrayMenuNode node = {0};
  char raw_label[] = {'_',
                      (char) 0xE5, (char) 0x90, (char) 0xAF,
                      (char) 0xFF,
                      (char) 0xE7, (char) 0x94, (char) 0xA8,
                      '\0'};
  g_autofree char *label = NULL;

  node.label = raw_label;
  label = bs_bar_tray_menu_view_dup_safe_label_text(&node);
  g_assert_nonnull(label);
  g_assert_true(g_utf8_validate(label, -1, NULL));
  g_assert_cmpstr(label, ==, "启" "\xEF\xBF\xBD" "用");
}

int
main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);

  g_test_add_func("/tray_text_utf8/vm_preserves_valid_utf8",
                  test_bar_vm_text_preserves_valid_utf8);
  g_test_add_func("/tray_text_utf8/vm_sanitizes_invalid_utf8",
                  test_bar_vm_text_sanitizes_invalid_utf8);
  g_test_add_func("/tray_text_utf8/vm_drops_invalid_object_path",
                  test_bar_vm_object_path_drops_invalid_value);
  g_test_add_func("/tray_text_utf8/view_strips_and_sanitizes_label",
                  test_tray_menu_view_safe_label_strips_and_sanitizes);
  return g_test_run();
}
