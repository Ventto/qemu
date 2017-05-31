/*
 * BCM2835 System Timer
 *
 * Copyright (C) 2017 Thomas Venries <thomas.venries@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/timer/bcm2835_timer.h"

#define TIMER_M0   (1 << 0)
#define TIMER_M1   (1 << 1)
#define TIMER_M2   (1 << 2)
#define TIMER_M3   (1 << 3)

static uint64_t bcm2835_timer_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    BCM2835TimerState *s = (BCM2835TimerState *)opaque;

    assert(size == 4);

    switch (offset) {
    case 0x00:
        return s->ctrl;
    case 0x04:
        return s->cnt_lo;
    case 0x08:
        return s->cnt_hi;
    case 0x0c:
        return s->cmp0;
    case 0x10:
        return s->cmp1;
    case 0x14:
        return s->cmp2;
    case 0x18:
        return s->cmp3;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_timer_read: Bad offset - [%x]\n",
                      (int)offset);
        return 0;
    }
}

static void bcm2835_timer_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    BCM2835TimerState *s = (BCM2835TimerState *)opaque;

    assert(size == 4);

    switch (offset) {
    case 0x00:
        s->ctrl = value;
        break;
    case 0x04:
        s->cnt_lo = value;
        break;
    case 0x08:
        s->cnt_hi = value;
        break;
    case 0x0c:
        s->cmp0 = value;
        break;
    case 0x10:
        s->cmp1 = value;
        break;
    case 0x14:
        s->cmp2 = value;
        break;
    case 0x18:
        s->cmp3 = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_timer_write: Bad offset %x\n",
                      (int)offset);
    }
}

static const MemoryRegionOps bcm2835_timer_ops = {
    .read = bcm2835_timer_read,
    .write = bcm2835_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_timer = {
    .name = TYPE_BCM2835_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, BCM2835TimerState),
        VMSTATE_UINT32(cnt_lo, BCM2835TimerState),
        VMSTATE_UINT32(cnt_hi, BCM2835TimerState),
        VMSTATE_UINT32(cmp0, BCM2835TimerState),
        VMSTATE_UINT32(cmp1, BCM2835TimerState),
        VMSTATE_UINT32(cmp2, BCM2835TimerState),
        VMSTATE_UINT32(cmp3, BCM2835TimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_timer_init(Object *obj)
{
    BCM2835TimerState *s = BCM2835_TIMER(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_timer_ops, s,
                          TYPE_BCM2835_TIMER, 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void bcm2835_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_TIMER, dc->categories);
    dc->desc = "BCM2835 System Timer";
    dc->vmsd = &vmstate_bcm2835_timer;
}

static TypeInfo bcm2835_timer_info = {
    .name          = TYPE_BCM2835_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835TimerState),
    .class_init    = bcm2835_timer_class_init,
    .instance_init = bcm2835_timer_init,
};

static void bcm2835_timer_register_types(void)
{
    type_register_static(&bcm2835_timer_info);
}

type_init(bcm2835_timer_register_types)
