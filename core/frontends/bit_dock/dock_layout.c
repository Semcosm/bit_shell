#include "frontends/bit_dock/dock_layout.h"

#include <glib.h>
#include <math.h>
#include <string.h>

static int
bs_round_to_int(double value) {
  return (int) floor(value + 0.5);
}

void
bs_dock_metrics_init_defaults(BsDockMetrics *metrics) {
  g_return_if_fail(metrics != NULL);

  memset(metrics, 0, sizeof(*metrics));
  metrics->item_size_px = 56;
  metrics->slot_width_px = 60;
  metrics->items_spacing_px = 8;
  metrics->edge_reserve_px = 8;
  metrics->bottom_margin_px = 14;
  metrics->root_pad_top_px = 10;
  metrics->root_pad_bottom_px = 8;
  metrics->root_pad_side_px = 14;
  metrics->root_border_radius_px = 24;
  metrics->slot_side_margin_px = 1;
  metrics->content_pad_top_px = 4;
  metrics->content_pad_bottom_px = 6;
  metrics->content_gap_px = 6;
  metrics->item_border_radius_px = 16;
  metrics->indicator_size_px = 6;
  metrics->focused_indicator_size_px = 7;
  metrics->hover_range_cap_units = 4;
  metrics->max_visual_scale = 1.80;
  metrics->max_hover_lift_px = 12.0;
  metrics->focused_lift_px = 2.0;
  metrics->hover_range_base_factor = 1.6;
  metrics->hover_range_span_factor = 0.12;
  metrics->hover_range_min_factor = 1.8;
  metrics->tau_scale_s = 0.038;
  metrics->tau_lift_s = 0.034;
  metrics->tau_offset_s = 0.036;
}

void
bs_dock_metrics_derive(BsDockMetrics *metrics, const BsDockConfig *config) {
  BsDockConfig defaults;
  guint icon_size = 56;
  int spacing_px = 8;

  g_return_if_fail(metrics != NULL);

  bs_dock_metrics_init_defaults(metrics);
  bs_dock_config_init_defaults(&defaults);
  if (config == NULL) {
    config = &defaults;
  }

  icon_size = CLAMP(config->icon_size_px > 0 ? config->icon_size_px : defaults.icon_size_px, 32, 128);
  spacing_px = config->spacing_px > 0
                 ? (int) MIN(config->spacing_px, 64)
                 : MAX(0, bs_round_to_int((double) icon_size * 0.14));

  metrics->item_size_px = (int) icon_size;
  metrics->slot_width_px = MAX(metrics->item_size_px + 4,
                               bs_round_to_int((double) metrics->item_size_px * 1.07));
  metrics->items_spacing_px = spacing_px;
  metrics->edge_reserve_px = MAX(8, metrics->items_spacing_px);
  metrics->bottom_margin_px = (int) (config->bottom_margin_px > 0 ? MIN(config->bottom_margin_px, 128) : defaults.bottom_margin_px);
  metrics->root_pad_top_px = MAX(6, bs_round_to_int((double) metrics->item_size_px * 0.18));
  metrics->root_pad_bottom_px = MAX(6, bs_round_to_int((double) metrics->item_size_px * 0.14));
  metrics->root_pad_side_px = MAX(10, bs_round_to_int((double) metrics->item_size_px * 0.25));
  metrics->root_border_radius_px = MAX(18, bs_round_to_int((double) metrics->item_size_px * 0.43));
  metrics->slot_side_margin_px = MAX(1, bs_round_to_int((double) metrics->item_size_px * 0.02));
  metrics->content_pad_top_px = MAX(2, bs_round_to_int((double) metrics->item_size_px * 0.07));
  metrics->content_pad_bottom_px = MAX(4, bs_round_to_int((double) metrics->item_size_px * 0.11));
  metrics->content_gap_px = MAX(4, bs_round_to_int((double) metrics->item_size_px * 0.11));
  metrics->item_border_radius_px = MAX(12, bs_round_to_int((double) metrics->item_size_px * 0.29));
  metrics->indicator_size_px = MAX(4, bs_round_to_int((double) metrics->item_size_px * 0.11));
  metrics->focused_indicator_size_px = metrics->indicator_size_px + 1;
  metrics->hover_range_cap_units = (int) CLAMP(config->hover_range_cap_units > 0
                                                 ? config->hover_range_cap_units
                                                 : defaults.hover_range_cap_units,
                                               2,
                                               12);
  metrics->max_visual_scale = config->magnification_enabled
                                ? CLAMP(config->magnification_scale, 1.0, 3.0)
                                : 1.0;
  metrics->max_hover_lift_px = config->magnification_enabled
                                 ? (double) MAX(0, bs_round_to_int((double) metrics->item_size_px * 0.21))
                                 : 0.0;
  metrics->focused_lift_px = config->magnification_enabled
                               ? (double) MAX(1, bs_round_to_int((double) metrics->item_size_px * 0.036))
                               : 0.0;
}

int
bs_dock_metrics_indicator_size(const BsDockMetrics *metrics, bool focused) {
  g_return_val_if_fail(metrics != NULL, focused ? 7 : 6);
  return focused ? metrics->focused_indicator_size_px : metrics->indicator_size_px;
}

double
bs_dock_metrics_base_step(const BsDockMetrics *metrics) {
  g_return_val_if_fail(metrics != NULL, 68.0);
  return (double) metrics->slot_width_px + (double) metrics->items_spacing_px;
}

double
bs_dock_metrics_hover_range(const BsDockMetrics *metrics, guint item_count) {
  double step = 0.0;
  double dock_span = 0.0;
  double preferred = 0.0;
  double min_range = 0.0;
  double max_range = 0.0;

  g_return_val_if_fail(metrics != NULL, 0.0);

  step = bs_dock_metrics_base_step(metrics);
  dock_span = item_count > 1 ? (double) (item_count - 1) * step : 0.0;
  preferred = (metrics->hover_range_base_factor * step)
              + (metrics->hover_range_span_factor * dock_span);
  min_range = metrics->hover_range_min_factor * step;
  max_range = (double) metrics->hover_range_cap_units * step;
  return CLAMP(preferred, min_range, max_range);
}

char *
bs_dock_metrics_build_css(const BsDockMetrics *metrics) {
  GString *css = NULL;

  g_return_val_if_fail(metrics != NULL, g_strdup(""));

  css = g_string_new("");
  g_string_append_printf(css,
                         "window.bit-dock-window {"
                         "  background: transparent;"
                         "  box-shadow: none;"
                         "}"
                         ".dock-layout {"
                         "  background: transparent;"
                         "}"
                         ".dock-root {"
                         "  padding: %dpx %dpx %dpx %dpx;"
                         "  border-radius: %dpx;"
                         "  background-color: rgba(242, 245, 250, 0.12);"
                         "  background-image: linear-gradient(to bottom,"
                         "                    rgba(255, 255, 255, 0.22) 0%%,"
                         "                    rgba(248, 249, 252, 0.16) 38%%,"
                         "                    rgba(236, 240, 247, 0.11) 100%%);"
                         "  border: 1px solid rgba(255, 255, 255, 0.16);"
                         "  box-shadow:"
                         "    0 16px 36px rgba(0, 0, 0, 0.22),"
                         "    0 4px 10px rgba(0, 0, 0, 0.10),"
                         "    inset 0 1px 0 rgba(255, 255, 255, 0.34),"
                         "    inset 0 -1px 0 rgba(255, 255, 255, 0.08),"
                         "    inset 0 -10px 20px rgba(255, 255, 255, 0.03);"
                         "}"
                         ".dock-items {"
                         "  background: transparent;"
                         "}"
                         ".dock-slot {"
                         "  margin-left: %dpx;"
                         "  margin-right: %dpx;"
                         "  transform: translateX(0);"
                         "}"
                         ".dock-slot-content {"
                         "  padding-top: %dpx;"
                         "  padding-bottom: %dpx;"
                         "  transform: translateY(0);"
                         "}"
                         ".dock-item {"
                         "  min-width: %dpx;"
                         "  min-height: %dpx;"
                         "  padding: 0;"
                         "  border-radius: %dpx;"
                         "  border: 1px solid transparent;"
                         "  background: transparent;"
                         "  box-shadow: none;"
                         "  transform-origin: 50%% 100%%;"
                         "  transform: scale(1.0);"
                         "}"
                         ".dock-item-icon {"
                         "  color: rgba(24, 28, 34, 0.94);"
                         "}"
                         ".dock-item-label {"
                         "  color: rgba(24, 28, 34, 0.92);"
                         "  font-size: 11px;"
                         "  font-weight: 600;"
                         "}"
                         ".dock-indicator {"
                         "  min-width: %dpx;"
                         "  min-height: %dpx;"
                         "  border-radius: 999px;"
                         "  background: rgba(255, 255, 255, 0.72);"
                         "}"
                         ".dock-indicator.is-focused {"
                         "  background: rgba(255, 255, 255, 0.96);"
                         "}"
                         ".dock-indicator.is-hidden {"
                         "  opacity: 0.0;"
                         "}"
                         ".dock-status {"
                         "  margin-top: 2px;"
                         "  color: rgba(255, 255, 255, 0.68);"
                         "  font-size: 11px;"
                         "}",
                         metrics->root_pad_top_px,
                         metrics->root_pad_side_px,
                         metrics->root_pad_bottom_px,
                         metrics->root_pad_side_px,
                         metrics->root_border_radius_px,
                         metrics->slot_side_margin_px,
                         metrics->slot_side_margin_px,
                         metrics->content_pad_top_px,
                         metrics->content_pad_bottom_px,
                         metrics->item_size_px,
                         metrics->item_size_px,
                         metrics->item_border_radius_px,
                         metrics->indicator_size_px,
                         metrics->indicator_size_px);
  return g_string_free(css, false);
}
