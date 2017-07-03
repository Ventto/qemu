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
#include "hw/timer/bcm2835_systimer.h"

#define TIMER_M0        (1 << 0)
#define TIMER_M1        (1 << 1)
#define TIMER_M2        (1 << 2)
#define TIMER_M3        (1 << 3)
#define TIMER_MATCH(n)  (1 << n)

static void bcm2835_systimer_update(void *opaque, unsigned timer)
{
    BCM2835SysTimerState *s = (BCM2835SysTimerState *)opaque;

    s->ctrl |= TIMER_MATCH(timer);
    qemu_irq_raise((timer == 1) ? s->irq[0] : s->irq[1]);

    uint64_t now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    s->cnt_lo = now & 0xffffffff;
    s->cnt_hi = now >> 32;

    trace_bcm2835_systimer_update(timer);
}

static void bcm2835_systimer_tick1(void *opaque)
{
    bcm2835_systimer_update(opaque, 1);
}

static void bcm2835_systimer_tick3(void *opaque)
{
    bcm2835_systimer_update(opaque, 3);
}

static uint64_t bcm2835_systimer_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    BCM2835SysTimerState *s = (BCM2835SysTimerState *)opaque;

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
                      "bcm2835_systimer_read: Bad offset - [%x]\n",
                      (int)offset);
        return 0;
    }
}

static void bcm2835_systimer_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    BCM2835SysTimerState *s = (BCM2835SysTimerState *)opaque;

    switch (offset) {
    case 0x00:
        value &= 0x0000000FUL;
        if ((s->ctrl & TIMER_M1) && (value & TIMER_M1))
            qemu_irq_lower(s->irq[0]);
        if ((s->ctrl & TIMER_M3) && (value & TIMER_M3))
            qemu_irq_lower(s->irq[1]);
        s->ctrl &= ~value;
        break;
    case 0x0c:
        s->cmp0 = value;
        break;
    case 0x10:
        timer_mod(s->timers[0], value);
        s->cmp1 = value;
        break;
    case 0x14:
        s->cmp2 = value;
        break;
    case 0x18:
        timer_mod(s->timers[1], value);
        s->cmp3 = value;
        break;

    case 0x04:
    case 0x08:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_systimer_write: Read-only offset %x\n",
                      (int)offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_systimer_write: Bad offset %x\n",
                      (int)offset);
    }
}

static const MemoryRegionOps bcm2835_systimer_ops = {
    .read = bcm2835_systimer_read,
    .write = bcm2835_systimer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_systimer = {
    .name = TYPE_BCM2835_SYSTIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, BCM2835SysTimerState),
        VMSTATE_UINT32(cnt_lo, BCM2835SysTimerState),
        VMSTATE_UINT32(cnt_hi, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp0, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp1, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp2, BCM2835SysTimerState),
        VMSTATE_UINT32(cmp3, BCM2835SysTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_systimer_init(Object *obj)
{
    BCM2835SysTimerState *s = BCM2835_SYSTIMER(obj);

    s->ctrl = 0;
    s->cmp0 = s->cmp1 = s->cmp2 = s->cmp3 = 0;

    s->timers[0] = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_systimer_tick1, s);
    s->timers[1] = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_systimer_tick3, s);

    memory_region_init_io(&s->iomem, obj, &bcm2835_systimer_ops, s,
                          TYPE_BCM2835_SYSTIMER, 0x20);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[1]);
}

static void bcm2835_systimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_TIMER, dc->categories);
    dc->desc = "BCM2835 System Timer";
    dc->vmsd = &vmstate_bcm2835_systimer;
}

static TypeInfo bcm2835_systimer_info = {
    .name          = TYPE_BCM2835_SYSTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835SysTimerState),
    .class_init    = bcm2835_systimer_class_init,
    .instance_init = bcm2835_systimer_init,
};

static void bcm2835_systimer_register_types(void)
{
    type_register_static(&bcm2835_systimer_info);
}

type_init(bcm2835_systimer_register_types)
