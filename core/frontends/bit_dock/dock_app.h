#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_DOCK_DOCK_APP_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_DOCK_DOCK_APP_H

#include <gtk/gtk.h>

#include "model/config.h"

typedef struct _BsDockApp BsDockApp;

BsDockApp *bs_dock_app_new(void);
void bs_dock_app_free(BsDockApp *app);
void bs_dock_app_set_config(BsDockApp *app, const BsDockConfig *config);
void bs_dock_app_get_config(BsDockApp *app, BsDockConfig *out_config);

int bs_dock_app_run(BsDockApp *app, int argc, char **argv);

#endif
