/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "swipe.h"

#include "recognizer.h"
#include "recognizer_impl.h"

#include "pbl/services/touch/touch_event.h"

// Minimum travel, in pixels, from touchdown to liftoff for a swipe to be recognized.
#define SWIPE_MIN_DISTANCE_PX (40)

typedef struct SwipeRecognizerData SwipeRecognizerData;

struct SwipeRecognizerData {
  struct {
    uint16_t min_distance_px;
  } config;

  struct {
    int16_t start_x;
    int16_t start_y;
    bool started;
    SwipeDirection direction;
  } state;
};

static void prv_handle_touch_event(Recognizer *recognizer, const TouchEvent *touch_event);
static bool prv_cancel(Recognizer *recognizer);
static void prv_reset(Recognizer *recognizer);

static const RecognizerImpl s_swipe_recognizer_impl = {
  .handle_touch_event = prv_handle_touch_event,
  .cancel = prv_cancel,
  .reset = prv_reset,
};

static SwipeDirection prv_classify(int16_t dx, int16_t dy) {
  const int16_t adx = (dx < 0) ? -dx : dx;
  const int16_t ady = (dy < 0) ? -dy : dy;
  if (adx >= ady) {
    return (dx > 0) ? SwipeDirection_Right : SwipeDirection_Left;
  }
  return (dy > 0) ? SwipeDirection_Down : SwipeDirection_Up;
}

static void prv_handle_touch_event(Recognizer *recognizer, const TouchEvent *touch_event) {
  SwipeRecognizerData *data = recognizer_get_impl_data(recognizer, &s_swipe_recognizer_impl);
  if (!data) {
    return;
  }

  switch (touch_event->type) {
    case TouchEvent_Touchdown:
      data->state.start_x = touch_event->x;
      data->state.start_y = touch_event->y;
      data->state.direction = SwipeDirection_None;
      data->state.started = true;
      break;

    case TouchEvent_PositionUpdate:
      // The direction is classified on liftoff from the net displacement, so intermediate
      // positions don't need to be tracked.
      break;

    case TouchEvent_Liftoff: {
      if (!data->state.started) {
        return;
      }
      const int16_t dx = touch_event->x - data->state.start_x;
      const int16_t dy = touch_event->y - data->state.start_y;
      const int16_t adx = (dx < 0) ? -dx : dx;
      const int16_t ady = (dy < 0) ? -dy : dy;
      const int16_t travel = (adx > ady) ? adx : ady;
      if (travel >= data->config.min_distance_px) {
        data->state.direction = prv_classify(dx, dy);
        recognizer_transition_state(recognizer, RecognizerState_Completed);
      } else {
        recognizer_transition_state(recognizer, RecognizerState_Failed);
      }
      break;
    }

    default:
      break;
  }
}

static bool prv_cancel(Recognizer *recognizer) {
  prv_reset(recognizer);
  return false;
}

static void prv_reset(Recognizer *recognizer) {
  SwipeRecognizerData *data = recognizer_get_impl_data(recognizer, &s_swipe_recognizer_impl);
  if (!data) {
    return;
  }
  data->state.started = false;
  data->state.direction = SwipeDirection_None;
}

Recognizer *swipe_recognizer_create(RecognizerEventCb event_cb, void *user_data) {
  SwipeRecognizerData data = {
    .config = {
      .min_distance_px = SWIPE_MIN_DISTANCE_PX,
    },
  };

  return recognizer_create_with_data(&s_swipe_recognizer_impl, &data, sizeof(data), event_cb,
                                     user_data);
}

SwipeDirection swipe_recognizer_get_direction(const Recognizer *recognizer) {
  const SwipeRecognizerData *data = recognizer_get_impl_data((Recognizer *)recognizer,
                                                             &s_swipe_recognizer_impl);
  return data ? data->state.direction : SwipeDirection_None;
}
