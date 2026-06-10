/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "swipe.h"
#include "menu.h"
#include "window.h"

#include "applib/ui/ui.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/i18n/i18n.h"
#include "shell/prefs.h"
#include "system/passert.h"

typedef enum {
  SwipeSettingRowVertical = 0,
  SwipeSettingRowHorizontal,
  SwipeSettingRowCount,
} SwipeSettingRow;

typedef struct {
  SettingsCallbacks callbacks;
} SettingsSwipeData;

static const char *prv_mode_label(SwipeAxisMode mode) {
  switch (mode) {
    case SwipeAxisMode_Off:
      return i18n_noop("Off");
    case SwipeAxisMode_Normal:
      return i18n_noop("Normal");
    case SwipeAxisMode_Inverted:
      return i18n_noop("Inverted");
    case SwipeAxisModeCount:
      break;
  }
  return "";
}

static void prv_deinit_cb(SettingsCallbacks *context) {
  SettingsSwipeData *data = (SettingsSwipeData *)context;
  i18n_free_all(data);
  app_free(data);
}

static void prv_draw_row_cb(SettingsCallbacks *context, GContext *ctx, const Layer *cell_layer,
                            uint16_t row, bool selected) {
  SettingsSwipeData *data = (SettingsSwipeData *)context;
  const char *title = NULL;
  SwipeAxisMode mode = SwipeAxisMode_Off;
  switch (row) {
    case SwipeSettingRowVertical:
      title = i18n_noop("Vertical");
      mode = shell_prefs_get_swipe_vertical_axis_mode();
      break;
    case SwipeSettingRowHorizontal:
      title = i18n_noop("Horizontal");
      mode = shell_prefs_get_swipe_horizontal_axis_mode();
      break;
    default:
      WTF;
  }
  menu_cell_basic_draw(ctx, cell_layer, i18n_get(title, data),
                       i18n_get(prv_mode_label(mode), data), NULL);
}

static void prv_select_click_cb(SettingsCallbacks *context, uint16_t row) {
  switch (row) {
    case SwipeSettingRowVertical: {
      const SwipeAxisMode next =
          (shell_prefs_get_swipe_vertical_axis_mode() + 1) % SwipeAxisModeCount;
      shell_prefs_set_swipe_vertical_axis_mode(next);
      break;
    }
    case SwipeSettingRowHorizontal: {
      const SwipeAxisMode next =
          (shell_prefs_get_swipe_horizontal_axis_mode() + 1) % SwipeAxisModeCount;
      shell_prefs_set_swipe_horizontal_axis_mode(next);
      break;
    }
    default:
      WTF;
  }
  settings_menu_reload_data(SettingsMenuItemSwipe);
  settings_menu_mark_dirty(SettingsMenuItemSwipe);
}

static uint16_t prv_num_rows_cb(SettingsCallbacks *context) {
  return SwipeSettingRowCount;
}

static Window *prv_init(void) {
  SettingsSwipeData *data = app_malloc_check(sizeof(*data));
  *data = (SettingsSwipeData){};

  data->callbacks = (SettingsCallbacks) {
    .deinit = prv_deinit_cb,
    .draw_row = prv_draw_row_cb,
    .select_click = prv_select_click_cb,
    .num_rows = prv_num_rows_cb,
  };

  return settings_window_create(SettingsMenuItemSwipe, &data->callbacks);
}

const SettingsModuleMetadata *settings_swipe_get_info(void) {
  static const SettingsModuleMetadata s_module_info = {
    .name = i18n_noop("Swipe"),
    .init = prv_init,
  };

  return &s_module_info;
}
