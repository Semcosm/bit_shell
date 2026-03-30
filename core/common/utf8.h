#ifndef BIT_SHELL_CORE_COMMON_UTF8_H
#define BIT_SHELL_CORE_COMMON_UTF8_H

#include <glib.h>

char *bs_utf8_dup_valid_or_null(const char *value);
char *bs_utf8_dup_valid_len_or_null(const char *value, gssize len);

#endif
