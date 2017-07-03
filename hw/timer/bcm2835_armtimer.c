/*
 * BCM2835 Armtem Timer
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
#include "hw/timer/bcm2835_armtimer.h"

#define TIMERCLK_DIVIDER            0x7DUL

#define TIMER_CTRL_COUNTER_PRESCALE (0x3E << 16)
#define TIMER_CTRL_COUNTER_ENABLE   (1 << 9)
#define TIMER_CTRL_ENABLE           (1 << 8)
#define TIMER_CTRL_INT_ENABLE       (1 << 6)
#define TIMER_CTRL_DIV1             (3 << 2)
#define TIMER_CTRL_DIV256           (2 << 2)
#define TIMER_CTRL_DIV16            (1 << 2)
#define TIMER_CTRL_DIV1_NOPRESCALE  (0 << 2)
#define TIMER_CTRL_COUNTER_32BIT    (1 << 1)

static void bcm2835_armtimer_tick(void *opaque)
{
    BCM2835ARMTimerState *s = (BCM2835ARMTimerState *)opaque;

    s->raw_irq = 1;
    qemu_irq_raise(s->irq);

    uint64_t now = qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    s->val = (now & 0xffffffff)  + s->reload;
    timer_mod(s->timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + s->reload);

    trace_bcm2835_armtimer_tick();
}

static uint64_t bcm2835_armtimer_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    BCM2835ARMTimerState *s = (BCM2835ARMTimerState *)opaque;

    switch (offset) {
    case 0x00:
        return s->reload;
    case 0x04:
        return s->val - ((uint64_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL)
                           & 0xffffffff);
    case 0x08:
        return s->ctrl;
    case 0x0c:
        return 0x544D5241;  /* ASCII reversed value for "ARMT" */
    case 0x10:
        return s->raw_irq;
    case 0x14:
        return (s->raw_irq && (s->ctrl & TIMER_CTRL_INT_ENABLE));
    case 0x18:
        return s->reload;
    case 0x1C:
        return s->prediv;
    case 0x20:
        if (s->ctrl & TIMER_CTRL_COUNTER_ENABLE) {
            return ((uint64_t)qemu_clock_get_us(QEMU_CLOCK_VIRTUAL)
                    & 0xffffffff) / s->prediv;
        } else {
            return 0;
        }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_armtimer_read: Bad offset - [%x]\n",
                      (int)offset);
        return 0;
    }
}

static void bcm2835_armtimer_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    BCM2835ARMTimerState *s = (BCM2835ARMTimerState *)opaque;

    switch (offset) {
    case 0x00:
        s->reload = value;
        s->val = (qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) & 0xffffffff) + value;
        timer_mod(s->timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + value);
        break;
    case 0x08:
        /* FIXME: Enable/disable timer */
        /* FIXME: Restrict counter to 16-bit */
        /* FIXME: Prescaler & divider */
        s->ctrl = value;
        break;
    case 0x0C:
        if (s->raw_irq) {
            qemu_irq_lower(s->irq);
            s->raw_irq = 0;
        }
        break;
    case 0x18:
        s->reload = value;
        break;
    case 0x1C:
        s->prediv = value;
        break;

    case 0x04:
    case 0x10:
    case 0x14:
    case 0x20:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_armtimer_write: Read only offset - [%x]\n",
                      (int)offset);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_armtimer_write: Bad offset - [%x]\n",
                      (int)offset);
    }
}

static const MemoryRegionOps bcm2835_armtimer_ops = {
    .read = bcm2835_armtimer_read,
    .write = bcm2835_armtimer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_armtimer = {
    .name = TYPE_BCM2835_ARMTIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, BCM2835ARMTimerState),
        VMSTATE_UINT32(reload, BCM2835ARMTimerState),
        VMSTATE_UINT32(raw_irq, BCM2835ARMTimerState),
        VMSTATE_UINT32(msk_irq, BCM2835ARMTimerState),
        VMSTATE_UINT32(prediv, BCM2835ARMTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_armtimer_init(Object *obj)
{
    BCM2835ARMTimerState *s = BCM2835_ARMTIMER(obj);

    s->ctrl = s->reload = 0;
    s->raw_irq = s->msk_irq = 0;
    s->prediv = TIMERCLK_DIVIDER;
    s->ctrl |= TIMER_CTRL_COUNTER_PRESCALE;

    s->timer = timer_new_us(QEMU_CLOCK_VIRTUAL, bcm2835_armtimer_tick, s);

    memory_region_init_io(&s->iomem, obj, &bcm2835_armtimer_ops, s,
                          TYPE_BCM2835_ARMTIMER, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void bcm2835_armtimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    //set_bit(DEVICE_CATEGORY_TIMER, dc->categories);
    dc->desc = "BCM2835 Armtem Timer";
    dc->vmsd = &vmstate_bcm2835_armtimer;
}

static TypeInfo bcm2835_armtimer_info = {
    .name          = TYPE_BCM2835_ARMTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835ARMTimerState),
    .class_init    = bcm2835_armtimer_class_init,
    .instance_init = bcm2835_armtimer_init,
};

static void bcm2835_armtimer_register_types(void)
{
    type_register_static(&bcm2835_armtimer_info);
}

type_init(bcm2835_armtimer_register_types)
