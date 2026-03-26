#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_BAR_CLOCK_WIDGET_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_BAR_CLOCK_WIDGET_H

#include <gtk/gtk.h>

typedef struct _BsClockWidget BsClockWidget;

BsClockWidget *bs_clock_widget_new(void);
void bs_clock_widget_free(BsClockWidget *clock);

GtkWidget *bs_clock_widget_root(BsClockWidget *clock);
GtkWidget *bs_clock_widget_build_popover_content(BsClockWidget *clock);

void bs_clock_widget_set_visible_enabled(BsClockWidget *clock, gboolean enabled);
void bs_clock_widget_start(BsClockWidget *clock);
void bs_clock_widget_stop(BsClockWidget *clock);
void bs_clock_widget_refresh_now(BsClockWidget *clock);

#endif
