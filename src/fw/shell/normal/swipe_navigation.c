/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "swipe_navigation.h"

#ifdef CONFIG_TOUCH

#include "applib/event_service_client.h"
#include "drivers/button_id.h"
#include "kernel/events.h"
#include "pbl/services/touch/touch_event.h"
#include "shell/prefs.h"

#include <stdbool.h>
#include <stdint.h>

// Finger travel before the gesture commits to a vertical or horizontal axis.
#define AXIS_LOCK_PX 12
// Minimum travel for a discrete swipe (horizontal navigation, or a stepped vertical swipe).
#define MIN_SWIPE_PX 40
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
static SwipeTouch s_touch;

static int16_t prv_abs16(int16_t v) {
  return (v < 0) ? -v : v;
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

static void prv_handle_touch(PebbleEvent *e, void *context) {
  const TouchEvent *te = &e->touch.event;
  switch (te->type) {
    case TouchEvent_Touchdown:
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
      if (s_touch.axis == SwipeAxis_Vertical && shell_prefs_get_swipe_continuous_scroll()) {
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
        if (shell_prefs_get_swipe_continuous_scroll()) {
          // Flick: keep scrolling a few more steps, scaled to the speed at release.
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
          const SwipeDirection dir =
              (s_touch.velocity > 0) ? SwipeDirection_Down : SwipeDirection_Up;
          for (int16_t i = 0; i < steps; i++) {
            prv_inject_for_direction(dir);
          }
        } else if (prv_abs16(dy) >= MIN_SWIPE_PX) {
          prv_inject_for_direction((dy > 0) ? SwipeDirection_Down : SwipeDirection_Up);
        }
      }
      break;
    }

    default:
      break;
  }
}

void swipe_navigation_init(void) {
  s_touch_event_info = (EventServiceInfo) {
    .type = PEBBLE_TOUCH_EVENT,
    .handler = prv_handle_touch,
  };
  event_service_client_subscribe(&s_touch_event_info);
}

#else  // CONFIG_TOUCH

void swipe_navigation_init(void) {}

#endif  // CONFIG_TOUCH
