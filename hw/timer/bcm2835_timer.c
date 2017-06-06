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
#include "qemu/timer.h"
#include "trace.h"
#include "hw/timer/bcm2835_timer.h"

#define TIMER_M1   (1 << 1)
#define TIMER_M3   (1 << 3)

static void bcm2835_timer_tick(void *opaque)
{
    BCM2835TimerState *s = (BCM2835TimerState *)opaque;

    s->ctrl |= TIMER_M3;
    trace_bcm2835_timer_tick(TIMER_M3);
    qemu_irq_raise(s->irq);
}

static uint64_t bcm2835_timer_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    BCM2835TimerState *s = (BCM2835TimerState *)opaque;

    switch (offset) {
    case 0x00:
        return s->ctrl;
    case 0x04:
        return (uint64_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) & 0xffffffff;
    case 0x08:
        return (uint64_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) >> 32;
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

    switch (offset) {
    case 0x00:
        s->ctrl = value;
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
        timer_mod(s->timer, value);
        s->cmp3 = value;
        s->ctrl &= ~TIMER_M3;
        break;

    case 0x04:
    case 0x08:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_timer_write: Read-only offset %x\n",
                      (int)offset);
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
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
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

    s->ctrl = 0;
    s->cmp0 = s->cmp1 = s->cmp2 = s->cmp3 = 0;

    s->timer = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_timer_tick, s);

    memory_region_init_io(&s->iomem, obj, &bcm2835_timer_ops, s,
                          TYPE_BCM2835_TIMER, 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
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
