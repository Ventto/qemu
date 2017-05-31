/*
 * BCM2835 System Timer
 *
 * Copyright (C) 2017 Thomas Venries <thomas.venries@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BCM2835_TIMER_H
#define BCM2835_TIMER_H

#include "hw/sysbus.h"

#define TYPE_BCM2835_TIMER "bcm2835-timer"
#define BCM2835_TIMER(obj) \
        OBJECT_CHECK(BCM2835TimerState, (obj), TYPE_BCM2835_TIMER)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;

    uint32_t ctrl;
    uint32_t cnt_lo;
    uint32_t cnt_hi;
    uint32_t cmp0;
    uint32_t cmp1;
    uint32_t cmp2;
    uint32_t cmp3;
} BCM2835TimerState;

#endif
