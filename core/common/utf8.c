#include "common/utf8.h"

char *
bs_utf8_dup_valid_or_null(const char *value) {
  if (value == NULL) {
    return NULL;
  }

  return g_utf8_make_valid(value, -1);
}

char *
bs_utf8_dup_valid_len_or_null(const char *value, gssize len) {
  if (value == NULL) {
    return NULL;
  }

  return g_utf8_make_valid(value, len);
}
