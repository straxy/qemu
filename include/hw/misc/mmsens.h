/*
 * Memory Mapped Sensor component
 *
 * Copyright (C) 2024 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
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

#ifndef HW_MISC_MM_SENS_H
#define HW_MISC_MM_SENS_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/ptimer.h"

/**
 * @name Constants
 * @{
 */

/** Size of register I/O address space used by MM Sens device */
#define MM_SENS_IOSIZE        (0x400)

/** Total number of known registers */
#define MM_SENS_REGS_NUM      (MM_SENS_IOSIZE / sizeof(uint32_t))

/** @} */

/**
 * @name Object model
 * @{
 */

#define TYPE_MM_SENS    "mistra.mmsens"
OBJECT_DECLARE_SIMPLE_TYPE(MMSensorState, MM_SENS)

/** @} */

/**
 * Memory mapped Sensor object instance state.
 */
struct MMSensorState {
    /*< private >*/
    SysBusDevice parent_obj;
    /** Timer pointer for periodic execution */
    ptimer_state *timer;
    /** Sampling frequency flag - 0 or 1 */
    unsigned char sampling_frequency;
    /*< public >*/

    /** Maps I/O registers in physical memory */
    MemoryRegion iomem;
    /** Interrupt line */
    qemu_irq irq;

    /** Array of hardware registers */
    uint32_t regs[MM_SENS_REGS_NUM];
};

#endif /* HW_MISC_MM_SENS_H */
