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
#include "qemu/main-loop.h"
#include "hw/ptimer.h"
#include "hw/timer/bcm2835_armtimer.h"
#include "trace.h"

#define ARM_TIMER_REG_SIZE      0x400

/* Register offsets */
#define ARM_TIMER_LOAD          0x00
#define ARM_TIMER_VALUE         0x04
#define ARM_TIMER_CTRL          0x08
#define ARM_TIMER_INTCLR        0x0C
#define ARM_TIMER_RAW_IRQ       0x10
#define ARM_TIMER_MASK_IRQ      0x14
#define ARM_TIMER_RELOAD        0x18
#define ARM_TIMER_PREDIVIDER    0x1C
#define ARM_TIMER_COUNTER       0x20

/* Control register */
#define CTRL_CNT_PRESCALE       (0xFF << 16)
#define CTRL_CNT_ENABLE         (1 << 9)
#define CTRL_TIMER_ENABLE       (1 << 8)
#define CTRL_INT_ENABLE         (1 << 5)
#define CTRL_TIMER_PRESCALE     (3 << 2)
#define CTRL_TIMER_SIZE_32BIT   (1 << 1)

/* Register reset values */
#define CTRL_CNT_PRESCALE_RESET     (0x3E << 16)
#define ARM_TIMER_CTRL_RESET        (CTRL_CNT_PRESCALE_RESET | CTRL_INT_ENABLE)
#define ARM_TIMER_VALUE_RESET       0xFFFF
#define ARM_TIMER_IE_READ_VALUE     0x544D5241  /* ASCII "ARMT" */
/*
   The system clock refers to a 250 MHz frequency by default.
   This frequency can be changed by setting `core_freq` the `config.txt` file.
   APB clock runs at half the speed of the system clock also called ARM clock.

   The ARM timer's predivider register is 10 bits wide and can be written
   or read from. This register has been added as the SP804 expects a 1MHz clock
   which they do not have. Instead the predivider takes the APB clock
   and divides it down according to:

       timer_clock = apb_clock / (pre_divider + 1)

   The need is a 1MHz timer clock frequency and BCM2835 ARM Peripherals
   documentation mentions the predivider reset value is 0x7D (or 125), so
   the APB clock refers to a 126MHz frequency.

   Also the additional free-running counter runs from the APB clock and has
   its own clock predivider controlled by buts 16-23 of the timer control reg:

        counter_clock = apb_clock / (pre_divider + 1)

   The predivider reset value is 0x3E (or 62), knowing APB clock frequency,
   the FRN clock refers to a 2MHz frequency by default.
*/
#define ARM_APB_FREQ                 126UL /* MHz */
#define ARM_TIMER_PREDIVIDER_RESET   125   /* MHz */

static const uint8_t ctrl_prescale [] = { 1, 16, 256, 1 };

static void bcm2835_armtimer_cb(void *opaque)
{
    BCM2835ARMTimerState *s = (BCM2835ARMTimerState *)opaque;

    s->raw_irq = 1;
    qemu_irq_raise(s->irq);

    trace_bcm2835_armtimer_tick();
}

static uint64_t bcm2835_armtimer_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    BCM2835ARMTimerState *s = (BCM2835ARMTimerState *)opaque;

    switch (offset) {
    case ARM_TIMER_LOAD:
    case ARM_TIMER_RELOAD:
        return s->reload;
    case ARM_TIMER_VALUE:
        return ptimer_get_count(s->timer);
    case ARM_TIMER_CTRL:
        return s->ctrl;
    case ARM_TIMER_INTCLR:
        return ARM_TIMER_IE_READ_VALUE;
    case ARM_TIMER_RAW_IRQ:
        return s->raw_irq;
    case ARM_TIMER_MASK_IRQ:
        return (s->raw_irq && (s->ctrl & CTRL_INT_ENABLE));
    case ARM_TIMER_PREDIVIDER:
        return s->prediv;
    case ARM_TIMER_COUNTER:
        /* FIXME: Counter and its prescaler */
        prescaler = (s->ctrl & CTRL_CNT_PRESCALE) >> 16;
        return 0;

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
    uint32_t limit, div;

    switch (offset) {
    case ARM_TIMER_LOAD:
        s->reload = value;
        /* Recalibration & Reload */
        break;
    case ARM_TIMER_CTRL:
        if (s->ctrl & CTRL_TIMER_ENABLE)
            ptimer_stop(s->timer);

        s->ctrl = value;

        limit = (s->ctrl & CTRL_TIMER_SIZE) ? 0xFFFF : 0XFFFFFFFF;
        ptimer_set_limit(s->timer, limit, (s->ctrl & CTRL_TIMER_ENABLE));

        div = ctrl_prescale[((s->ctrl & CTRL_TIMER_PRESCALE) >> 2)] + s->prediv;
        ptimer_set_freq(s->timer, ARM_APB_FREQ / div);

        if (s->ctrl & CTRL_TIMER_ENABLE)
            ptimer_run(s->timer, 0);
        break;
    case ARM_TIMER_INTCLR:
        qemu_irq_lower(s->irq);
        s->raw_irq = 0;
        break;
    case ARM_TIMER_RELOAD:
        s->reload = value;
        break;
    case ARM_TIMER_PREDIVIDER:
        s->prediv = value;
        break;

    case ARM_TIMER_VALUE:
    case ARM_TIMER_RAW_IRQ:
    case ARM_TIMER_MASK_IRQ:
    case ARM_TIMER_COUNTER:
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
    QEMUBH *bh = qemu_bh_new(bcm2835_armtimer_cb, s);

    s->ctrl = s->reload = 0;
    s->raw_irq = s->msk_irq = 0;
    s->prediv = ARM_TIMER_PREDIVIDER_RESET;
    s->ctrl = ARM_TIMER_CTRL_RESET;

    s->timer = ptimer_init(bh, PTIMER_POLICY_DEFAULT);

    memory_region_init_io(&s->iomem, obj, &bcm2835_armtimer_ops, s,
                          TYPE_BCM2835_ARMTIMER, ARM_TIMER_REG_SIZE);

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
