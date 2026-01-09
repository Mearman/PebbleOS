/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "services/normal/notifications/alerts_preferences_private.h"
#include "services/normal/vibes/vibe_intensity.h"
#include "util/attributes.h"

VibeIntensity WEAK alerts_preferences_get_vibe_intensity(void) {
  return DEFAULT_VIBE_INTENSITY;
}

bool WEAK alerts_preferences_get_notification_alternative_design(void) {
  return false;
}

DndNotificationMode WEAK alerts_preferences_dnd_get_show_notifications(void) {
  return DndNotificationModeShow;
}

bool WEAK alerts_preferences_get_notification_vibe_delay(void) {
  return false;
}
