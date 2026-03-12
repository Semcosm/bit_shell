#ifndef BIT_SHELL_CORE_FRONTENDS_BIT_DOCK_DOCK_LAYOUT_H
#define BIT_SHELL_CORE_FRONTENDS_BIT_DOCK_DOCK_LAYOUT_H

#include <stdbool.h>

#include <glib.h>

#include "model/config.h"

typedef struct {
  int item_size_px;
  int slot_width_px;
  int items_spacing_px;
  int edge_reserve_px;
  int bottom_margin_px;
  int root_pad_top_px;
  int root_pad_bottom_px;
  int root_pad_side_px;
  int root_border_radius_px;
  int slot_side_margin_px;
  int content_pad_top_px;
  int content_pad_bottom_px;
  int content_gap_px;
  int item_border_radius_px;
  int indicator_size_px;
  int focused_indicator_size_px;
  int hover_range_cap_units;
  double max_visual_scale;
  double max_hover_lift_px;
  double focused_lift_px;
  double hover_range_base_factor;
  double hover_range_span_factor;
  double hover_range_min_factor;
  double tau_scale_s;
  double tau_lift_s;
  double tau_offset_s;
} BsDockMetrics;

void bs_dock_metrics_init_defaults(BsDockMetrics *metrics);
void bs_dock_metrics_derive(BsDockMetrics *metrics, const BsDockConfig *config);
int bs_dock_metrics_indicator_size(const BsDockMetrics *metrics, bool focused);
double bs_dock_metrics_base_step(const BsDockMetrics *metrics);
double bs_dock_metrics_hover_range(const BsDockMetrics *metrics, guint item_count);
char *bs_dock_metrics_build_css(const BsDockMetrics *metrics);

#endif
