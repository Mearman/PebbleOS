/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "swipe_navigation.h"

#ifdef CONFIG_TOUCH

#include "applib/event_service_client.h"
#include "applib/ui/recognizer/recognizer.h"
#include "applib/ui/recognizer/recognizer_private.h"
#include "applib/ui/recognizer/swipe.h"
#include "drivers/button_id.h"
#include "kernel/events.h"
#include "pbl/services/touch/touch_event.h"
#include "shell/prefs.h"

// Touch is subscribed for the whole life of the shell so swipes work everywhere the buttons do -
// apps, menus, and full-screen modals like notifications (a modal unfocuses the app beneath it, so
// focus-gating the subscription would drop touch exactly on those screens). The sensor stays
// powered while the shell runs as a result.
static Recognizer *s_swipe_recognizer;
static EventServiceInfo s_touch_event_info;

static ButtonId prv_button_for_direction(SwipeDirection direction) {
  // The settings are read live, so changes in Settings take effect immediately. The master switch
  // ignores all swipes when off; otherwise each axis mode drives the button the swipe points at
  // (Normal), the opposite button (Inverted, so the content moves the way a touchscreen drag
  // expects) or nothing (Off).
  if (!shell_prefs_get_swipe_enabled()) {
    return NUM_BUTTONS;
  }
  switch (direction) {
    case SwipeDirection_Up:
    case SwipeDirection_Down:
      switch (shell_prefs_get_swipe_vertical_axis_mode()) {
        case SwipeAxisMode_Normal:
          return (direction == SwipeDirection_Up) ? BUTTON_ID_UP : BUTTON_ID_DOWN;
        case SwipeAxisMode_Inverted:
          return (direction == SwipeDirection_Up) ? BUTTON_ID_DOWN : BUTTON_ID_UP;
        case SwipeAxisMode_Off:
        case SwipeAxisModeCount:
          break;
      }
      break;
    case SwipeDirection_Left:
    case SwipeDirection_Right:
      switch (shell_prefs_get_swipe_horizontal_axis_mode()) {
        case SwipeAxisMode_Normal:
          return (direction == SwipeDirection_Left) ? BUTTON_ID_BACK : BUTTON_ID_SELECT;
        case SwipeAxisMode_Inverted:
          return (direction == SwipeDirection_Left) ? BUTTON_ID_SELECT : BUTTON_ID_BACK;
        case SwipeAxisMode_Off:
        case SwipeAxisModeCount:
          break;
      }
      break;
    case SwipeDirection_None:
      break;
  }
  return NUM_BUTTONS;
}

static void prv_inject_button_click(ButtonId button) {
  // Synthesize the same down/up event pair the button driver posts; a quick pair reads as a single
  // click, driving whatever click handler the focused window has configured.
  event_put(&(PebbleEvent) { .type = PEBBLE_BUTTON_DOWN_EVENT, .button.button_id = button });
  event_put(&(PebbleEvent) { .type = PEBBLE_BUTTON_UP_EVENT, .button.button_id = button });
}

static void prv_swipe_event_cb(const Recognizer *recognizer, RecognizerEvent event_type) {
  if (event_type != RecognizerEvent_Completed) {
    return;
  }
  const ButtonId button = prv_button_for_direction(swipe_recognizer_get_direction(recognizer));
  if (button < NUM_BUTTONS) {
    prv_inject_button_click(button);
  }
}

static void prv_handle_touch(PebbleEvent *e, void *context) {
  if (!s_swipe_recognizer) {
    return;
  }
  const TouchEvent *touch_event = &e->touch.event;
  if (touch_event->type == TouchEvent_Touchdown) {
    // Start a fresh gesture from a known (Possible) state.
    recognizer_reset(s_swipe_recognizer);
  } else if (!recognizer_is_active(s_swipe_recognizer)) {
    // No gesture in progress (e.g. subscribed mid-touch); wait for the next touchdown.
    return;
  }
  recognizer_handle_touch_event(s_swipe_recognizer, touch_event);
}

void swipe_navigation_init(void) {
  s_swipe_recognizer = swipe_recognizer_create(prv_swipe_event_cb, NULL);
  s_touch_event_info = (EventServiceInfo) {
    .type = PEBBLE_TOUCH_EVENT,
    .handler = prv_handle_touch,
  };
  event_service_client_subscribe(&s_touch_event_info);
}

#else  // CONFIG_TOUCH

void swipe_navigation_init(void) {}

#endif  // CONFIG_TOUCH
