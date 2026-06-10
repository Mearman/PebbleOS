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

// Touch is only subscribed to (which powers the sensor) while an app is focused, so the sensor and
// the gesture-wake path are unaffected when nothing is on screen.
static Recognizer *s_swipe_recognizer;
static EventServiceInfo s_focus_event_info;
static EventServiceInfo s_touch_event_info;
static bool s_touch_subscribed;

static ButtonId prv_button_for_direction(SwipeDirection direction) {
  // The per-axis mode is read live, so changing it in Settings takes effect immediately. Normal
  // drives the button the swipe points at; Inverted drives the opposite, so the content moves the
  // way a touchscreen drag expects; Off ignores that axis.
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

static void prv_set_touch_active(bool active) {
  if (active == s_touch_subscribed) {
    return;
  }
  if (active) {
    s_touch_event_info = (EventServiceInfo) {
      .type = PEBBLE_TOUCH_EVENT,
      .handler = prv_handle_touch,
    };
    event_service_client_subscribe(&s_touch_event_info);
  } else {
    event_service_client_unsubscribe(&s_touch_event_info);
    if (s_swipe_recognizer) {
      recognizer_reset(s_swipe_recognizer);
    }
  }
  s_touch_subscribed = active;
}

static void prv_handle_focus_change(PebbleEvent *e, void *context) {
  prv_set_touch_active(e->app_focus.in_focus);
}

void swipe_navigation_init(void) {
  s_swipe_recognizer = swipe_recognizer_create(prv_swipe_event_cb, NULL);
  s_focus_event_info = (EventServiceInfo) {
    .type = PEBBLE_APP_DID_CHANGE_FOCUS_EVENT,
    .handler = prv_handle_focus_change,
  };
  event_service_client_subscribe(&s_focus_event_info);
}

#else  // CONFIG_TOUCH

void swipe_navigation_init(void) {}

#endif  // CONFIG_TOUCH
