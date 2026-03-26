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
static GtkWidget *bs_clock_widget_build_calendar_grid(GDateTime *now);
static GtkWidget *bs_clock_widget_build_calendar_cell(const char *text,
                                                      const char *css_class,
                                                      gboolean dimmed);

BsClockWidget *
bs_clock_widget_new(void) {
  BsClockWidget *clock = g_new0(BsClockWidget, 1);

  clock->root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  clock->label = gtk_label_new("--:--");
  clock->visible_enabled = TRUE;
  gtk_widget_add_css_class(clock->root, "bit-bar-clock");
  gtk_widget_add_css_class(clock->label, "bit-bar-clock-label");
  gtk_widget_set_halign(clock->root, GTK_ALIGN_FILL);
  gtk_widget_set_hexpand(clock->root, TRUE);
  gtk_widget_set_halign(clock->label, GTK_ALIGN_CENTER);
  gtk_widget_set_hexpand(clock->label, TRUE);
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

GtkWidget *
bs_clock_widget_build_popover_content(BsClockWidget *clock) {
  static const char *weekday_headers[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  g_autoptr(GDateTime) now = NULL;
  g_autofree char *headline = NULL;
  g_autofree char *date_text = NULL;
  GtkWidget *root = NULL;
  GtkWidget *summary_box = NULL;
  GtkWidget *headline_label = NULL;
  GtkWidget *date_label = NULL;
  GtkWidget *weekdays = NULL;
  GtkWidget *calendar = NULL;

  g_return_val_if_fail(clock != NULL, NULL);

  now = g_date_time_new_now_local();
  headline = g_date_time_format(now, "%A");
  date_text = g_date_time_format(now, "%Y-%m-%d");

  root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  summary_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  headline_label = gtk_label_new(headline != NULL ? headline : "");
  date_label = gtk_label_new(date_text != NULL ? date_text : "");
  weekdays = gtk_grid_new();
  calendar = bs_clock_widget_build_calendar_grid(now);

  gtk_widget_add_css_class(root, "bit-bar-clock-popover");
  gtk_widget_add_css_class(summary_box, "bit-bar-clock-popover-summary");
  gtk_widget_add_css_class(headline_label, "bit-bar-clock-popover-headline");
  gtk_widget_add_css_class(date_label, "bit-bar-clock-popover-date");
  gtk_widget_add_css_class(weekdays, "bit-bar-clock-popover-weekdays");
  gtk_widget_add_css_class(calendar, "bit-bar-clock-popover-calendar");
  gtk_widget_set_margin_top(root, 12);
  gtk_widget_set_margin_bottom(root, 12);
  gtk_widget_set_margin_start(root, 12);
  gtk_widget_set_margin_end(root, 12);
  gtk_label_set_xalign(GTK_LABEL(headline_label), 0.0f);
  gtk_label_set_xalign(GTK_LABEL(date_label), 0.0f);

  for (gint i = 0; i < 7; i++) {
    GtkWidget *label = bs_clock_widget_build_calendar_cell(weekday_headers[i],
                                                           "bit-bar-clock-popover-weekday",
                                                           TRUE);

    gtk_grid_attach(GTK_GRID(weekdays), label, i, 0, 1, 1);
  }

  gtk_box_append(GTK_BOX(summary_box), headline_label);
  gtk_box_append(GTK_BOX(summary_box), date_label);
  gtk_box_append(GTK_BOX(root), summary_box);
  gtk_box_append(GTK_BOX(root), weekdays);
  gtk_box_append(GTK_BOX(root), calendar);
  return root;
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

static GtkWidget *
bs_clock_widget_build_calendar_grid(GDateTime *now) {
  GtkWidget *grid = NULL;
  g_autoptr(GDateTime) month_start = NULL;
  gint year = 0;
  gint month = 0;
  gint today = 0;
  gint first_weekday = 0;
  guint days_in_month = 0;

  g_return_val_if_fail(now != NULL, NULL);

  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 6);

  year = g_date_time_get_year(now);
  month = g_date_time_get_month(now);
  today = g_date_time_get_day_of_month(now);
  month_start = g_date_time_new_local(year, month, 1, 0, 0, 0);
  if (month_start == NULL) {
    return grid;
  }

  first_weekday = g_date_time_get_day_of_week(month_start);
  days_in_month = g_date_get_days_in_month((GDateMonth) month, (GDateYear) year);
  for (guint day = 1; day <= days_in_month; day++) {
    const gint offset = (first_weekday - 1) + (gint) day - 1;
    const gint column = offset % 7;
    const gint row = offset / 7;
    g_autofree char *day_text = g_strdup_printf("%u", day);
    GtkWidget *cell = bs_clock_widget_build_calendar_cell(day_text,
                                                          day == (guint) today
                                                            ? "is-today"
                                                            : "bit-bar-clock-popover-day",
                                                          FALSE);

    gtk_grid_attach(GTK_GRID(grid), cell, column, row, 1, 1);
  }

  return grid;
}

static GtkWidget *
bs_clock_widget_build_calendar_cell(const char *text, const char *css_class, gboolean dimmed) {
  GtkWidget *label = gtk_label_new(text != NULL ? text : "");

  gtk_widget_add_css_class(label, "bit-bar-clock-popover-cell");
  if (css_class != NULL && *css_class != '\0') {
    gtk_widget_add_css_class(label, css_class);
  }
  if (dimmed) {
    gtk_widget_set_opacity(label, 0.7);
  }
  gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(label, 24, 20);
  return label;
}
