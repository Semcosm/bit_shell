#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_DOCK_DOCK_APP_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_DOCK_DOCK_APP_H

#include <gtk/gtk.h>

typedef struct _BsDockApp BsDockApp;

BsDockApp *bs_dock_app_new(void);
void bs_dock_app_free(BsDockApp *app);

int bs_dock_app_run(BsDockApp *app, int argc, char **argv);

#endif
