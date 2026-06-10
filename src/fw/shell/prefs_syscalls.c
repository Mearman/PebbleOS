/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "prefs_syscalls.h"

#include "syscall/syscall_internal.h"

DEFINE_SYSCALL(UnitsDistance, sys_shell_prefs_get_units_distance, void) {
  return shell_prefs_get_units_distance();
}

DEFINE_SYSCALL(bool, sys_shell_prefs_get_swipe_tap_to_open, void) {
  return shell_prefs_get_swipe_tap_to_open();
}
