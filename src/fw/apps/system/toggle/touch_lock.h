/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "process_management/app_manager.h"

#define TOUCH_LOCK_TOGGLE_UUID {0x7a, 0xc4, 0x10, 0x3f, 0x2b, 0x6d, 0x4e, 0x91, \
                                0xbe, 0x05, 0x8c, 0xf2, 0x1a, 0x73, 0x9d, 0x44}

const PebbleProcessMd *touch_lock_toggle_get_app_info(void);
