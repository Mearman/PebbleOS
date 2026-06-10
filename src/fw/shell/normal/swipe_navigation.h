/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! Initialise system-wide swipe navigation. On boards with a touchscreen, a completed swipe is
//! translated into the equivalent button click (up/down/left/right -> UP/DOWN/BACK/SELECT) so the
//! whole UI - watchface, launcher and every menu - responds to swipes exactly as it does to the
//! buttons, without any per-screen wiring. No-op on boards without a touchscreen.
void swipe_navigation_init(void);
