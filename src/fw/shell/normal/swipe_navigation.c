/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "swipe_navigation.h"

#ifdef CONFIG_TOUCH

#include "applib/event_service_client.h"
#include "apps/system_app_ids.h"
#include "drivers/button_id.h"
#include "kernel/event_loop.h"
#include "kernel/events.h"
#include "kernel/ui/modals/modal_manager.h"
#include "pbl/services/light.h"
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/touch/touch_event.h"
#include "process_management/app_manager.h"
#include "shell/prefs.h"

#include <stdbool.h>
#include <stdint.h>

// Finger travel before the gesture commits to a vertical or horizontal axis.
#define AXIS_LOCK_PX 12
// Minimum travel for a discrete swipe (horizontal navigation, or a stepped vertical swipe).
#define MIN_SWIPE_PX 40
// Momentum glide: deliver the flick's extra steps over time, slowing down, so a flick coasts to a
// stop rather than jumping all at once. The interval between injected steps grows each tick.
#define MOMENTUM_START_INTERVAL_MS 45
#define MOMENTUM_MAX_INTERVAL_MS 240
// The continuous-scroll step, flick gain and flick cap are user-tunable preferences (see the
// Scroll Feel preset / advanced controls in Settings > Touch).

typedef enum {
  SwipeDirection_None,
  SwipeDirection_Up,
  SwipeDirection_Down,
  SwipeDirection_Left,
  SwipeDirection_Right,
} SwipeDirection;

typedef enum {
  SwipeAxis_None,
  SwipeAxis_Vertical,
  SwipeAxis_Horizontal,
} SwipeAxis;

typedef struct {
  bool active;
  SwipeAxis axis;
  int16_t start_x;
  int16_t start_y;
  int16_t last_y;
  int16_t scroll_anchor_y;  // last y at which a continuous scroll step was injected
  int16_t velocity;         // EMA of vertical movement per update, for the flick on release
} SwipeTouch;

static EventServiceInfo s_touch_event_info;
static EventServiceInfo s_backlight_event_info;
static bool s_touch_subscribed;
static SwipeTouch s_touch;

// Momentum glide state. Mutated only on KernelMain (the touch handler and the hopped timer tick);
// the timer callback itself only posts the hop, so the state needs no cross-task locking.
static TimerID s_momentum_timer = TIMER_INVALID_ID;
static int16_t s_momentum_steps_left;
static uint32_t s_momentum_interval_ms;
static SwipeDirection s_momentum_dir;

static int16_t prv_abs16(int16_t v) {
  return (v < 0) ? -v : v;
}

// Paged surfaces (the watchface and the built-in card/page apps) move a whole screen per click, so
// continuous scrolling and momentum would skip pages. On these a vertical swipe is a single
// discrete step; lists and menus keep continuous scrolling.
static bool prv_is_paged_surface(void) {
  // A focused modal (notification, its action menu, a dialog) receives the injected buttons, not
  // the app beneath it. Modals are list/scroll surfaces, so keep continuous scrolling there even
  // when the underlying app (e.g. the watchface) is paged.
  if (modal_manager_get_enabled() && !(modal_manager_get_properties() & ModalProperty_Unfocused)) {
    return false;
  }
  if (app_manager_is_watchface_running()) {
    return true;
  }
  switch (app_manager_get_current_app_id()) {
    case APP_ID_TIMELINE:
    case APP_ID_TIMELINE_PAST:
    case APP_ID_TIMELINE_FULL:
    case APP_ID_HEALTH_APP:
    case APP_ID_MUSIC:
    case APP_ID_WEATHER:
    case APP_ID_WORKOUT:
    case APP_ID_SPORTS:
      return true;
    default:
      return false;
  }
}

// Maps a swipe direction to the button it drives, honouring the live per-axis settings: the master
// switch (off ignores everything), and each axis mode - Normal drives the button the swipe points
// at, Inverted the opposite (so the content moves with the finger), Off ignores that axis.
static ButtonId prv_button_for_direction(SwipeDirection direction) {
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

static void prv_inject_for_direction(SwipeDirection direction) {
  const ButtonId button = prv_button_for_direction(direction);
  if (button >= NUM_BUTTONS) {
    return;
  }
  // Synthesize the same down/up pair the button driver posts; a quick pair reads as a single click,
  // driving whatever click handler the focused window (app, menu or modal) has configured.
  event_put(&(PebbleEvent) { .type = PEBBLE_BUTTON_DOWN_EVENT, .button.button_id = button });
  event_put(&(PebbleEvent) { .type = PEBBLE_BUTTON_UP_EVENT, .button.button_id = button });
}

static void prv_momentum_stop(void) {
  s_momentum_steps_left = 0;
  if (s_momentum_timer != TIMER_INVALID_ID) {
    new_timer_stop(s_momentum_timer);
  }
}

static void prv_momentum_timer_cb(void *data);

// Runs on KernelMain (hopped from the timer): inject one glide step, decelerate, re-arm or finish.
static void prv_momentum_tick(void *data) {
  if (s_momentum_steps_left <= 0) {
    return;
  }
  prv_inject_for_direction(s_momentum_dir);
  if (--s_momentum_steps_left <= 0) {
    return;
  }
  s_momentum_interval_ms += s_momentum_interval_ms / 4;  // slow down ~25% per step
  if (s_momentum_interval_ms > MOMENTUM_MAX_INTERVAL_MS) {
    s_momentum_interval_ms = MOMENTUM_MAX_INTERVAL_MS;
  }
  new_timer_start(s_momentum_timer, s_momentum_interval_ms, prv_momentum_timer_cb, NULL, 0);
}

// Runs on the NewTimers task: hop the actual work to KernelMain so glide state stays single-task.
static void prv_momentum_timer_cb(void *data) {
  launcher_task_add_callback(prv_momentum_tick, NULL);
}

static void prv_momentum_start(SwipeDirection dir, int16_t steps) {
  if (steps <= 0 || s_momentum_timer == TIMER_INVALID_ID) {
    return;
  }
  s_momentum_dir = dir;
  s_momentum_steps_left = steps;
  s_momentum_interval_ms = MOMENTUM_START_INTERVAL_MS;
  new_timer_start(s_momentum_timer, s_momentum_interval_ms, prv_momentum_timer_cb, NULL, 0);
}

static void prv_handle_touch(PebbleEvent *e, void *context) {
  const TouchEvent *te = &e->touch.event;
  switch (te->type) {
    case TouchEvent_Touchdown:
      prv_momentum_stop();  // a new touch stops any ongoing glide
      s_touch = (SwipeTouch) {
        .active = true,
        .axis = SwipeAxis_None,
        .start_x = te->x,
        .start_y = te->y,
        .last_y = te->y,
        .scroll_anchor_y = te->y,
      };
      break;

    case TouchEvent_PositionUpdate: {
      if (!s_touch.active) {
        return;
      }
      const int16_t dy_frame = te->y - s_touch.last_y;
      s_touch.velocity = (int16_t)((s_touch.velocity * 3 + dy_frame) / 4);
      s_touch.last_y = te->y;

      if (s_touch.axis == SwipeAxis_None) {
        const int16_t adx = prv_abs16(te->x - s_touch.start_x);
        const int16_t ady = prv_abs16(te->y - s_touch.start_y);
        if (adx >= AXIS_LOCK_PX || ady >= AXIS_LOCK_PX) {
          s_touch.axis = (adx > ady) ? SwipeAxis_Horizontal : SwipeAxis_Vertical;
        }
      }

      // Continuous mode: the list follows the finger, one step per row-height of vertical travel.
      // Skipped on paged surfaces, where a swipe is a single discrete step (handled on liftoff).
      if (s_touch.axis == SwipeAxis_Vertical && shell_prefs_get_swipe_continuous_scroll() &&
          !prv_is_paged_surface()) {
        int16_t step = shell_prefs_get_swipe_scroll_step();
        if (step < 1) {
          step = 1;
        }
        while (prv_abs16(te->y - s_touch.scroll_anchor_y) >= step) {
          const bool down = (te->y > s_touch.scroll_anchor_y);
          prv_inject_for_direction(down ? SwipeDirection_Down : SwipeDirection_Up);
          s_touch.scroll_anchor_y = (int16_t)(s_touch.scroll_anchor_y + (down ? step : -step));
        }
      }
      break;
    }

    case TouchEvent_Liftoff: {
      if (!s_touch.active) {
        return;
      }
      s_touch.active = false;
      const int16_t dx = te->x - s_touch.start_x;
      const int16_t dy = te->y - s_touch.start_y;

      if (s_touch.axis == SwipeAxis_Horizontal) {
        if (prv_abs16(dx) >= MIN_SWIPE_PX) {
          prv_inject_for_direction((dx > 0) ? SwipeDirection_Right : SwipeDirection_Left);
        }
      } else if (s_touch.axis == SwipeAxis_Vertical) {
        if (shell_prefs_get_swipe_continuous_scroll() && !prv_is_paged_surface()) {
          // The drag already followed the finger; a flick adds momentum that coasts to a stop.
          int16_t step = shell_prefs_get_swipe_scroll_step();
          if (step < 1) {
            step = 1;
          }
          const int16_t cap = shell_prefs_get_swipe_flick_cap();
          int16_t steps =
              (int16_t)(prv_abs16(s_touch.velocity) * shell_prefs_get_swipe_flick_gain() / step);
          if (steps > cap) {
            steps = cap;
          }
          prv_momentum_start((s_touch.velocity > 0) ? SwipeDirection_Down : SwipeDirection_Up,
                             steps);
        } else if (prv_abs16(dy) >= MIN_SWIPE_PX) {
          // Stepped mode, or a paged surface: one discrete step per swipe.
          prv_inject_for_direction((dy > 0) ? SwipeDirection_Down : SwipeDirection_Up);
        }
      }
      break;
    }

    default:
      break;
  }
}

static void prv_set_touch_subscribed(bool want) {
  if (want == s_touch_subscribed) {
    return;
  }
  s_touch_subscribed = want;
  if (want) {
    s_touch_event_info = (EventServiceInfo) {
      .type = PEBBLE_TOUCH_EVENT,
      .handler = prv_handle_touch,
    };
    event_service_client_subscribe(&s_touch_event_info);
  } else {
    event_service_client_unsubscribe(&s_touch_event_info);
    prv_momentum_stop();
    s_touch.active = false;
  }
}

// Touch is powered (subscribed) only while it can actually be used: the master switch is on and
// either the screen is lit - the user is interacting - or the always-on preference keeps it
// powered with the screen off (faster wake and touch-to-wake, at a standby-battery cost). When the
// screen is off and always-on is off, the sensor sleeps, which is the default.
static bool prv_should_be_subscribed(bool backlight_on) {
  if (!shell_prefs_get_swipe_enabled()) {
    return false;
  }
  return backlight_on || shell_prefs_get_swipe_touch_always_on();
}

static void prv_handle_backlight(PebbleEvent *e, void *context) {
  prv_set_touch_subscribed(prv_should_be_subscribed(e->backlight.is_on));
}

void swipe_navigation_init(void) {
  s_momentum_timer = new_timer_create();
  s_backlight_event_info = (EventServiceInfo) {
    .type = PEBBLE_BACKLIGHT_EVENT,
    .handler = prv_handle_backlight,
  };
  event_service_client_subscribe(&s_backlight_event_info);
  prv_set_touch_subscribed(prv_should_be_subscribed(light_is_on()));
}

#else  // CONFIG_TOUCH

void swipe_navigation_init(void) {}

#endif  // CONFIG_TOUCH
