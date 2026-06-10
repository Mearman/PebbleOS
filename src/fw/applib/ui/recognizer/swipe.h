/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "recognizer.h"

//! Direction of a recognized swipe gesture.
typedef enum SwipeDirection {
  SwipeDirection_None = 0,
  SwipeDirection_Up,
  SwipeDirection_Down,
  SwipeDirection_Left,
  SwipeDirection_Right,
} SwipeDirection;

//! Create a swipe recognizer. The recognizer completes when a touch travels at least the
//! configured minimum distance from its starting point before liftoff, reporting the
//! dominant-axis direction of the travel.
//! @param event_cb callback invoked on recognizer state changes
//! @param user_data data passed to the callback
//! @return the new recognizer, or NULL on failure
Recognizer *swipe_recognizer_create(RecognizerEventCb event_cb, void *user_data);

//! Get the direction of the recognized swipe. Meaningful once the recognizer has completed.
//! @return the swipe direction, or SwipeDirection_None if no swipe was recognized
SwipeDirection swipe_recognizer_get_direction(const Recognizer *recognizer);
