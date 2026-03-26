#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_BAR_APP_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_BAR_APP_H

#include <gtk/gtk.h>

#include "model/config.h"

typedef struct _BsBarApp BsBarApp;

BsBarApp *bs_bar_app_new(void);
void bs_bar_app_free(BsBarApp *app);
void bs_bar_app_set_config(BsBarApp *app, const BsBarConfig *config);
void bs_bar_app_get_config(BsBarApp *app, BsBarConfig *out_config);

int bs_bar_app_run(BsBarApp *app, int argc, char **argv);

#endif
