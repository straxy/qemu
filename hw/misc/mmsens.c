/*
 * Memory mapped sensor component
 *
 * Written by Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/misc/mmsens.h"
#include "trace/trace-hw_misc.h"

REG32(CTRL, 0x00)
    FIELD(CTRL,     EN,     0,  1)      /* component enable */
    FIELD(CTRL,     IEN,    1,  1)      /* interrupt enable */
    FIELD(CTRL,     FREQ,   2,  1)      /* sampling frequency setting */

REG32(STATUS, 0x04)
    FIELD(STATUS,   INITW,  0,  1)      /* initial value warning */
    FIELD(STATUS,   IFG,    1,  1)      /* interrupt flag */

REG32(INITVAL, 0x08)
    FIELD(INITVAL,  VALUE,  0,  16)     /* initial counter value */

REG32(DATA, 0x0C)
    FIELD(DATA,     SAMPLE, 0,  16)     /* current value */

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

#define DATA_UPDATE_NORMAL_FREQ     (1)
#define DATA_UPDATE_FAST_FREQ       (2)

#define FREQ_NORMAL     (0)
#define FREQ_FAST       (1)

/*
 * IRQ generator
 *
 * If alarm is enabled and is set, trigger interrupt.
 */
static void mm_sens_update_irq(const MMSensorState *s)
{
    bool pending = s->regs[R_CTRL] & s->regs[R_STATUS] & R_CTRL_IEN_MASK;

    trace_mm_sens_update_irq(pending);

    qemu_set_irq(s->irq, pending);
}

/*
 * Update measured data
 *
 * Update current measurement.
 */
static void mm_sens_update_data(void *opaque)
{
    MMSensorState *s = MM_SENS(opaque);

    s->regs[R_DATA] = s->regs[R_DATA] + 1;
    if ((s->regs[R_DATA] & 0x000fu) > 0x0009u) {
        s->regs[R_DATA] += 0x0006u;
        if ((s->regs[R_DATA] & 0x00f0u) > 0x0090u) {
            s->regs[R_DATA] += 0x0060u;
            if ((s->regs[R_DATA] & 0x0f00u) > 0x0900u) {
                s->regs[R_DATA] += 0x0600u;
                if ((s->regs[R_DATA] & 0xf000u) > 0x9000u) {
                    s->regs[R_DATA] += 0x6000u;
                }
            }
        }
    }

    s->regs[R_STATUS] |= R_STATUS_IFG_MASK;

    mm_sens_update_irq(s);
}

/*
 * Reset component registers and variables
 */
static void mm_sens_reset_enter(Object *obj, ResetType type)
{
    MMSensorState *s = MM_SENS(obj);

    s->sampling_frequency = FREQ_NORMAL;

    memset(s->regs, 0, MM_SENS_IOSIZE);
}

/*
 * CTRL register updates
 *
 * If component is enabled, start timer, else stop timer.
 * If interrupt is enabled, check if interrupt needs to be generated.
 */
static void r_ctrl_pre_write(MMSensorState *s, uint64_t val)
{
    uint8_t new_sfreq = (val & R_CTRL_FREQ_MASK) >> R_CTRL_FREQ_SHIFT;


    ptimer_transaction_begin(s->timer);

    if (new_sfreq != s->sampling_frequency) {
        s->sampling_frequency = new_sfreq;
        switch (s->sampling_frequency) {
            case FREQ_NORMAL:
                ptimer_set_freq(s->timer, DATA_UPDATE_NORMAL_FREQ);
                break;
            case FREQ_FAST:
                ptimer_set_freq(s->timer, DATA_UPDATE_FAST_FREQ);
                break;
            default:
                trace_mm_sens_unknown_frequency(s->sampling_frequency);
                break;
        }
    }

    if (val & R_CTRL_EN_MASK) {
        /* start timer if not started. */
        if (!(s->regs[R_CTRL] & R_CTRL_EN_MASK)) {
            ptimer_run(s->timer, 0);

            /* load initial value to DATA register. */
            if (s->regs[R_STATUS] & R_STATUS_INITW_MASK) {
                s->regs[R_DATA] = 0;
            } else {
                s->regs[R_DATA] = s->regs[R_INITVAL];
            }
        }
    } else {
        /* stop timer */
        ptimer_stop(s->timer);
    }

    ptimer_transaction_commit(s->timer);
}

/*
 * INITVAL register updates
 *
 * Check if value is valid and update STATUS.INITW bit.
 */
static void r_initval_pre_write(MMSensorState *s, uint64_t val)
{
    uint8_t is_wrong = 0;
    uint32_t bcd_val = val;

    while (bcd_val > 0) {
        if ((bcd_val & 0x0000000fu) > 9) {
            is_wrong = 1;
            break;
        }
        bcd_val >>= 4;
    }

    trace_mm_sens_initval(val, is_wrong);

    s->regs[R_STATUS] &= ~R_STATUS_INITW_MASK;
    s->regs[R_STATUS] |= is_wrong << R_STATUS_INITW_SHIFT;
}

static uint64_t mm_sens_read(void *opaque, hwaddr offset,
                             unsigned size) {
    const MMSensorState *s = MM_SENS(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case A_CTRL:
    case A_STATUS:
    case A_INITVAL:
    case A_DATA:
        break;
    case 0x010 ... MM_SENS_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void mm_sens_write(void *opaque, hwaddr offset,
                          uint64_t val, unsigned size) {
    MMSensorState *s = MM_SENS(opaque);
    const uint32_t idx = REG_INDEX(offset);

    /* Pre-write handlers */
    switch (offset) {
    case A_CTRL:
        r_ctrl_pre_write(s, val);
        break;
    case A_STATUS:
        /* STATUS.INITW should not be affected by written value */
        val = val & (~R_STATUS_INITW_MASK);
        val |= s->regs[R_STATUS] & R_STATUS_INITW_MASK;
        break;
    case A_INITVAL:
        r_initval_pre_write(s, val);
        break;
    case A_DATA:
        /* Data is read-only register. */
        return;
    case 0x010 ... MM_SENS_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    s->regs[idx] = (uint32_t)val;

    /* Post-write handlers */
    switch (offset) {
        case A_CTRL:
            trace_mm_sens_ctrl_post_write(val);
            if (s->regs[A_CTRL] & (R_CTRL_EN_MASK | R_CTRL_IEN_MASK)) {
                mm_sens_update_irq(s);
            }
            break;
        case A_STATUS:
            trace_mm_sens_status_post_write(val);
            mm_sens_update_irq(s);
            break;
        case A_INITVAL:
        default:
            break;

    }
}

static const MemoryRegionOps mm_sens_ops = {
    .read = mm_sens_read,
    .write = mm_sens_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const VMStateDescription vmstate_mm_sens = {
    .name = "mm_sens_cmd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(sampling_frequency, MMSensorState),
        VMSTATE_UINT32_ARRAY(regs, MMSensorState, MM_SENS_REGS_NUM),
        VMSTATE_PTIMER(timer, MMSensorState),
        VMSTATE_END_OF_LIST()
    }
};

static void mm_sens_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MMSensorState *s = MM_SENS(obj);

    sysbus_init_irq(sbd, &s->irq);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &mm_sens_ops, s,
                          TYPE_MM_SENS, MM_SENS_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    s->timer = ptimer_init(mm_sens_update_data, s, PTIMER_POLICY_CONTINUOUS_TRIGGER);
    ptimer_transaction_begin(s->timer);
    ptimer_set_freq(s->timer, DATA_UPDATE_NORMAL_FREQ);
    ptimer_transaction_commit(s->timer);
}

static void mm_sens_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = mm_sens_reset_enter;
    dc->vmsd = &vmstate_mm_sens;
}

static const TypeInfo mm_sens_info = {
    .name           = TYPE_MM_SENS,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(MMSensorState),
    .instance_init  = mm_sens_init,
    .class_init     = mm_sens_class_init,
};

static void mm_sens_register_types(void)
{
    type_register_static(&mm_sens_info);
}

type_init(mm_sens_register_types)
