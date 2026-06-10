/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "tap.h"

#include "recognizer.h"
#include "recognizer_impl.h"

// Finger travel (px, per axis) beyond which the touch is treated as a drag/swipe, not a tap. The
// kernel scroll shim handles those; the tap recogniser fails so it doesn't also fire.
#define TAP_MOVEMENT_THRESHOLD_PX 10

struct TapRecognizerData {
  // Recognizer config
  struct {
    uint16_t taps_required;
    uint16_t fingers_required;
    GPoint movement_threshold;
  } config;

  // Gesture state
  struct {
    GPoint start_point;  // where the finger went down
    GPoint tap_point;    // reported tap location, valid once the gesture completes
    bool finger_down;
  } state;
};

static void prv_handle_touch_event(Recognizer *recognizer, const TouchEvent *touch_event);
static void prv_reset(Recognizer *recognizer);
static bool prv_cancel(Recognizer *recognizer);

static const RecognizerImpl s_tap_recognizer_impl = {
  .handle_touch_event = prv_handle_touch_event,
  .reset = prv_reset,
  .cancel = prv_cancel
};

static void prv_handle_touch_event(Recognizer *recognizer, const TouchEvent *touch_event) {
  TapRecognizerData *data = recognizer_get_impl_data(recognizer, &s_tap_recognizer_impl);

  switch (touch_event->type) {
    case TouchEvent_Touchdown:
      data->state.start_point = GPoint(touch_event->x, touch_event->y);
      data->state.finger_down = true;
      break;
    case TouchEvent_PositionUpdate: {
      if (!data->state.finger_down) {
        break;
      }
      const int16_t dx = touch_event->x - data->state.start_point.x;
      const int16_t dy = touch_event->y - data->state.start_point.y;
      const int16_t adx = (dx < 0) ? -dx : dx;
      const int16_t ady = (dy < 0) ? -dy : dy;
      if ((adx > data->config.movement_threshold.x) ||
          (ady > data->config.movement_threshold.y)) {
        // Moved too far: this is a drag/swipe, not a tap.
        recognizer_transition_state(recognizer, RecognizerState_Failed);
      }
      break;
    }
    case TouchEvent_Liftoff:
      if (data->state.finger_down) {
        data->state.finger_down = false;
        data->state.tap_point = GPoint(touch_event->x, touch_event->y);
        recognizer_transition_state(recognizer, RecognizerState_Completed);
      }
      break;
  }
}

static void prv_reset(Recognizer *recognizer) {
  TapRecognizerData *data = recognizer_get_impl_data(recognizer, &s_tap_recognizer_impl);
  data->state.finger_down = false;
}

static bool prv_cancel(Recognizer *recognizer) {
  prv_reset(recognizer);
  return false;
}

Recognizer *tap_recognizer_create(RecognizerEventCb event_cb, void *user_data) {
  TapRecognizerData data = {
    .config = {
      .taps_required = 1,
      .fingers_required = 1,
      .movement_threshold = GPoint(TAP_MOVEMENT_THRESHOLD_PX, TAP_MOVEMENT_THRESHOLD_PX),
    },
  };

  return recognizer_create_with_data(&s_tap_recognizer_impl, &data, sizeof(data), event_cb,
                                     user_data);
}

const TapRecognizerData *tap_recognizer_get_data(const Recognizer *recognizer) {
  return recognizer_get_impl_data((Recognizer *)recognizer, &s_tap_recognizer_impl);
}

GPoint tap_recognizer_get_tap_point(const Recognizer *recognizer) {
  const TapRecognizerData *data = tap_recognizer_get_data(recognizer);
  return data ? data->state.tap_point : GPointZero;
}

void tap_recognizer_set_num_taps_required(Recognizer *recognizer, int num_taps) {
  TapRecognizerData *data = recognizer_get_impl_data(recognizer, &s_tap_recognizer_impl);
  if (!data) {
    return;
  }
  data->config.taps_required = num_taps;
}
