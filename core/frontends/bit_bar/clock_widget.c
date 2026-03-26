#include "frontends/bit_bar/clock_widget.h"

#include <glib.h>

struct _BsClockWidget {
  GtkWidget *root;
  GtkWidget *label;
  guint timeout_source_id;
  gboolean running;
  gboolean visible_enabled;
};

static void bs_clock_widget_schedule_next_tick(BsClockWidget *clock);
static guint bs_clock_widget_milliseconds_until_next_minute(void);
static gboolean bs_clock_widget_tick_cb(gpointer user_data);

BsClockWidget *
bs_clock_widget_new(void) {
  BsClockWidget *clock = g_new0(BsClockWidget, 1);

  clock->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  clock->label = gtk_label_new("--:--");
  clock->visible_enabled = TRUE;
  gtk_widget_add_css_class(clock->root, "bit-bar-clock");
  gtk_box_append(GTK_BOX(clock->root), clock->label);
  return clock;
}

void
bs_clock_widget_free(BsClockWidget *clock) {
  if (clock == NULL) {
    return;
  }

  bs_clock_widget_stop(clock);
  g_free(clock);
}

GtkWidget *
bs_clock_widget_root(BsClockWidget *clock) {
  g_return_val_if_fail(clock != NULL, NULL);
  return clock->root;
}

void
bs_clock_widget_set_visible_enabled(BsClockWidget *clock, gboolean enabled) {
  g_return_if_fail(clock != NULL);

  clock->visible_enabled = enabled;
  gtk_widget_set_visible(clock->root, enabled);
  if (enabled) {
    bs_clock_widget_start(clock);
  } else {
    bs_clock_widget_stop(clock);
  }
}

void
bs_clock_widget_start(BsClockWidget *clock) {
  g_return_if_fail(clock != NULL);

  clock->running = TRUE;
  if (!clock->visible_enabled) {
    return;
  }

  bs_clock_widget_refresh_now(clock);
  bs_clock_widget_schedule_next_tick(clock);
}

void
bs_clock_widget_stop(BsClockWidget *clock) {
  g_return_if_fail(clock != NULL);

  clock->running = FALSE;
  if (clock->timeout_source_id != 0) {
    g_source_remove(clock->timeout_source_id);
    clock->timeout_source_id = 0;
  }
}

void
bs_clock_widget_refresh_now(BsClockWidget *clock) {
  g_autoptr(GDateTime) now = NULL;
  g_autofree char *formatted = NULL;

  g_return_if_fail(clock != NULL);

  now = g_date_time_new_now_local();
  formatted = g_date_time_format(now, "%H:%M");
  gtk_label_set_text(GTK_LABEL(clock->label), formatted != NULL ? formatted : "--:--");
}

static void
bs_clock_widget_schedule_next_tick(BsClockWidget *clock) {
  guint delay_ms = 0;

  g_return_if_fail(clock != NULL);

  if (!clock->running || !clock->visible_enabled) {
    return;
  }
  if (clock->timeout_source_id != 0) {
    g_source_remove(clock->timeout_source_id);
    clock->timeout_source_id = 0;
  }

  delay_ms = bs_clock_widget_milliseconds_until_next_minute();
  clock->timeout_source_id = g_timeout_add(delay_ms, bs_clock_widget_tick_cb, clock);
}

static guint
bs_clock_widget_milliseconds_until_next_minute(void) {
  g_autoptr(GDateTime) now = NULL;
  gint second = 0;
  gint usec = 0;
  gint64 remaining_ms = 0;

  now = g_date_time_new_now_local();
  second = g_date_time_get_second(now);
  usec = g_date_time_get_microsecond(now);
  remaining_ms = ((60 - second) * 1000) - (usec / 1000);
  if (remaining_ms <= 0) {
    return 60000U;
  }
  return (guint) remaining_ms;
}

static gboolean
bs_clock_widget_tick_cb(gpointer user_data) {
  BsClockWidget *clock = user_data;

  g_return_val_if_fail(clock != NULL, G_SOURCE_REMOVE);

  clock->timeout_source_id = 0;
  if (!clock->running || !clock->visible_enabled) {
    return G_SOURCE_REMOVE;
  }

  bs_clock_widget_refresh_now(clock);
  bs_clock_widget_schedule_next_tick(clock);
  return G_SOURCE_REMOVE;
}
