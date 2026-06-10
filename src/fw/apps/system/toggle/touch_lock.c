/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "touch_lock.h"

#include "applib/app.h"
#include "applib/ui/action_toggle.h"
#include "process_management/app_manager.h"
#include "pbl/services/i18n/i18n.h"
#include "shell/prefs.h"

// The toggle's "on" state is touch being locked, i.e. globally disabled. Navigation of the
// confirmation dialog is by button, so the watch can still be unlocked while touch is off.
static bool prv_get_state(void *context) {
  return !touch_is_globally_enabled();
}

static void prv_set_state(bool locked, void *context) {
  touch_set_globally_enabled(!locked);
}

static const ActionToggleImpl s_touch_lock_action_toggle_impl = {
  .window_name = "Touch Lock Toggle",
  .prompt_icon = RESOURCE_ID_GENERIC_CONFIRMATION_LARGE,
  .result_icon = RESOURCE_ID_GENERIC_CONFIRMATION_LARGE,
  .prompt_enable_message = i18n_noop("Lock Touch?"),
  .prompt_disable_message = i18n_noop("Unlock Touch?"),
  .result_enable_message = i18n_noop("Touch\nLocked"),
  .result_disable_message = i18n_noop("Touch\nUnlocked"),
  .callbacks = {
    .get_state = prv_get_state,
    .set_state = prv_set_state,
  },
};

static void prv_main(void) {
  action_toggle_push(&(ActionToggleConfig) {
    .impl = &s_touch_lock_action_toggle_impl,
    .set_exit_reason = true,
  });
  app_event_loop();
}

const PebbleProcessMd *touch_lock_toggle_get_app_info(void) {
  static const PebbleProcessMdSystem s_app_info = {
    .common = {
      .main_func = &prv_main,
      .uuid = TOUCH_LOCK_TOGGLE_UUID,
      .visibility = ProcessVisibilityQuickLaunch,
    },
    .name = i18n_noop("Touch Lock"),
  };
  return &s_app_info.common;
}
