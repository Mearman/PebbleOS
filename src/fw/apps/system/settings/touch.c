/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "touch.h"
#include "menu.h"
#include "window.h"

#include "applib/ui/ui.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/i18n/i18n.h"
#include "shell/prefs.h"
#include "system/passert.h"
#include "util/size.h"

#include <stdio.h>

// Logical rows. The visible set is built per draw from the current state: the scroll-feel rows
// only show in continuous mode, and the three knob rows only when advanced mode is on.
typedef enum {
  RowEnabled,
  RowScrolling,
  RowScrollFeel,
  RowAdvanced,
  RowStep,
  RowFlick,
  RowFlickCap,
  RowVertical,
  RowHorizontal,
  RowAlwaysOn,
  RowLogicalCount,
} SwipeSettingRow;

typedef struct {
  SettingsCallbacks callbacks;
  char value_buf[8];  // scratch for numeric row values during a draw
} SettingsTouchData;

// Scroll-feel presets bundle the three knobs. Standard matches the built-in defaults.
typedef struct {
  uint8_t step;
  uint8_t gain;
  uint8_t cap;
  const char *name;
} ScrollPreset;

static const ScrollPreset s_presets[] = {
  { .step = 36, .gain = 3, .cap = 6,  .name = i18n_noop("Gentle") },
  { .step = 28, .gain = 5, .cap = 12, .name = i18n_noop("Standard") },
  { .step = 18, .gain = 8, .cap = 20, .name = i18n_noop("Fast") },
};

// Selectable values for the advanced knobs. Each preset's values appear here so advanced tuning
// can land on (and cycle away from) a preset cleanly.
static const uint8_t s_step_options[] = { 44, 36, 28, 22, 18 };
static const uint8_t s_gain_options[] = { 0, 3, 5, 8, 12 };
static const uint8_t s_cap_options[] = { 6, 12, 20, 32 };

static uint16_t prv_build_rows(SwipeSettingRow *rows) {
  uint16_t n = 0;
  rows[n++] = RowEnabled;
  rows[n++] = RowScrolling;
  if (shell_prefs_get_swipe_continuous_scroll()) {
    rows[n++] = RowScrollFeel;
    rows[n++] = RowAdvanced;
    if (shell_prefs_get_swipe_scroll_advanced()) {
      rows[n++] = RowStep;
      rows[n++] = RowFlick;
      rows[n++] = RowFlickCap;
    }
  }
  rows[n++] = RowVertical;
  rows[n++] = RowHorizontal;
  rows[n++] = RowAlwaysOn;
  return n;
}

static SwipeSettingRow prv_row_at(uint16_t index) {
  SwipeSettingRow rows[RowLogicalCount];
  const uint16_t n = prv_build_rows(rows);
  return (index < n) ? rows[index] : RowLogicalCount;
}

static int prv_preset_index(void) {
  const uint8_t step = shell_prefs_get_swipe_scroll_step();
  const uint8_t gain = shell_prefs_get_swipe_flick_gain();
  const uint8_t cap = shell_prefs_get_swipe_flick_cap();
  for (size_t i = 0; i < ARRAY_LENGTH(s_presets); i++) {
    if (s_presets[i].step == step && s_presets[i].gain == gain && s_presets[i].cap == cap) {
      return (int)i;
    }
  }
  return -1;
}

static void prv_apply_next_preset(void) {
  const int current = prv_preset_index();
  const size_t next = (current < 0) ? 0 : (((size_t)current + 1) % ARRAY_LENGTH(s_presets));
  shell_prefs_set_swipe_scroll_step(s_presets[next].step);
  shell_prefs_set_swipe_flick_gain(s_presets[next].gain);
  shell_prefs_set_swipe_flick_cap(s_presets[next].cap);
}

static uint8_t prv_cycle_option(uint8_t current, const uint8_t *options, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (options[i] == current) {
      return options[(i + 1) % count];
    }
  }
  return options[0];
}

static const char *prv_mode_label(SwipeAxisMode mode) {
  switch (mode) {
    case SwipeAxisMode_Off:
      return i18n_noop("Off");
    case SwipeAxisMode_Normal:
      return i18n_noop("Standard");
    case SwipeAxisMode_Inverted:
      return i18n_noop("Natural");
    case SwipeAxisModeCount:
      break;
  }
  return "";
}

static void prv_deinit_cb(SettingsCallbacks *context) {
  SettingsTouchData *data = (SettingsTouchData *)context;
  i18n_free_all(data);
  app_free(data);
}

static void prv_draw_row_cb(SettingsCallbacks *context, GContext *ctx, const Layer *cell_layer,
                            uint16_t row, bool selected) {
  SettingsTouchData *data = (SettingsTouchData *)context;
  const char *title = NULL;
  const char *value = NULL;     // translated/static string value
  bool numeric = false;         // value is in data->value_buf instead
  uint8_t number = 0;

  switch (prv_row_at(row)) {
    case RowEnabled:
      title = i18n_noop("Swipe Gestures");
      value = shell_prefs_get_swipe_enabled() ? i18n_noop("On") : i18n_noop("Off");
      break;
    case RowScrolling:
      title = i18n_noop("Scrolling");
      value = shell_prefs_get_swipe_continuous_scroll() ? i18n_noop("Continuous")
                                                        : i18n_noop("Steps");
      break;
    case RowScrollFeel: {
      title = i18n_noop("Scroll Feel");
      const int preset = prv_preset_index();
      value = (preset >= 0) ? s_presets[preset].name : i18n_noop("Custom");
      break;
    }
    case RowAdvanced:
      title = i18n_noop("Advanced");
      value = shell_prefs_get_swipe_scroll_advanced() ? i18n_noop("On") : i18n_noop("Off");
      break;
    case RowStep:
      title = i18n_noop("Step Size");
      number = shell_prefs_get_swipe_scroll_step();
      numeric = true;
      break;
    case RowFlick:
      title = i18n_noop("Flick");
      number = shell_prefs_get_swipe_flick_gain();
      numeric = true;
      break;
    case RowFlickCap:
      title = i18n_noop("Flick Cap");
      number = shell_prefs_get_swipe_flick_cap();
      numeric = true;
      break;
    case RowVertical:
      title = i18n_noop("Vertical");
      value = prv_mode_label(shell_prefs_get_swipe_vertical_axis_mode());
      break;
    case RowHorizontal:
      title = i18n_noop("Horizontal");
      value = prv_mode_label(shell_prefs_get_swipe_horizontal_axis_mode());
      break;
    case RowAlwaysOn:
      title = i18n_noop("Always-On Touch");
      value = shell_prefs_get_swipe_touch_always_on() ? i18n_noop("On") : i18n_noop("Off");
      break;
    default:
      WTF;
  }

  const char *subtitle;
  if (numeric) {
    snprintf(data->value_buf, sizeof(data->value_buf), "%u", (unsigned)number);
    subtitle = data->value_buf;
  } else {
    subtitle = i18n_get(value, data);
  }
  menu_cell_basic_draw(ctx, cell_layer, i18n_get(title, data), subtitle, NULL);
}

static void prv_select_click_cb(SettingsCallbacks *context, uint16_t row) {
  switch (prv_row_at(row)) {
    case RowEnabled:
      shell_prefs_set_swipe_enabled(!shell_prefs_get_swipe_enabled());
      break;
    case RowScrolling:
      shell_prefs_set_swipe_continuous_scroll(!shell_prefs_get_swipe_continuous_scroll());
      break;
    case RowScrollFeel:
      prv_apply_next_preset();
      break;
    case RowAdvanced:
      shell_prefs_set_swipe_scroll_advanced(!shell_prefs_get_swipe_scroll_advanced());
      break;
    case RowStep:
      shell_prefs_set_swipe_scroll_step(
          prv_cycle_option(shell_prefs_get_swipe_scroll_step(), s_step_options,
                           ARRAY_LENGTH(s_step_options)));
      break;
    case RowFlick:
      shell_prefs_set_swipe_flick_gain(
          prv_cycle_option(shell_prefs_get_swipe_flick_gain(), s_gain_options,
                           ARRAY_LENGTH(s_gain_options)));
      break;
    case RowFlickCap:
      shell_prefs_set_swipe_flick_cap(
          prv_cycle_option(shell_prefs_get_swipe_flick_cap(), s_cap_options,
                           ARRAY_LENGTH(s_cap_options)));
      break;
    case RowVertical:
      shell_prefs_set_swipe_vertical_axis_mode(
          (shell_prefs_get_swipe_vertical_axis_mode() + 1) % SwipeAxisModeCount);
      break;
    case RowHorizontal:
      shell_prefs_set_swipe_horizontal_axis_mode(
          (shell_prefs_get_swipe_horizontal_axis_mode() + 1) % SwipeAxisModeCount);
      break;
    case RowAlwaysOn:
      shell_prefs_set_swipe_touch_always_on(!shell_prefs_get_swipe_touch_always_on());
      break;
    default:
      WTF;
  }
  settings_menu_reload_data(SettingsMenuItemTouch);
  settings_menu_mark_dirty(SettingsMenuItemTouch);
}

static uint16_t prv_num_rows_cb(SettingsCallbacks *context) {
  SwipeSettingRow rows[RowLogicalCount];
  return prv_build_rows(rows);
}

static Window *prv_init(void) {
  SettingsTouchData *data = app_malloc_check(sizeof(*data));
  *data = (SettingsTouchData){};

  data->callbacks = (SettingsCallbacks) {
    .deinit = prv_deinit_cb,
    .draw_row = prv_draw_row_cb,
    .select_click = prv_select_click_cb,
    .num_rows = prv_num_rows_cb,
  };

  return settings_window_create(SettingsMenuItemTouch, &data->callbacks);
}

const SettingsModuleMetadata *settings_touch_get_info(void) {
  static const SettingsModuleMetadata s_module_info = {
    .name = i18n_noop("Touch"),
    .init = prv_init,
  };

  return &s_module_info;
}
