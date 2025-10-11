/*
 *  Allwinner GPIO registers definition
 *
 *  Copyright (C) 2025 Strahinja Jankovic. <strahinja.p.jankovic@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ALLWINNER_GPIO_H
#define ALLWINNER_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"

/** Size of register I/O address space used by GPIO device */
#define AW_GPIO_IOSIZE (0x400)

/** Total number of known registers */
#define AW_GPIO_REGS_NUM    (AW_GPIO_IOSIZE / sizeof(uint32_t))

/** Max number of ports */
#define AW_GPIO_PORTS_NUM   9
/** Max number of pins per ports */
#define AW_GPIO_PIN_COUNT 32

#define TYPE_AW_GPIO        "allwinner.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(AWGPIOState, AW_GPIO)

struct AWGPIOState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;
    qemu_irq output[AW_GPIO_PORTS_NUM][AW_GPIO_PIN_COUNT];

    uint32_t regs[AW_GPIO_REGS_NUM];
};

#endif /* ALLWINNER_GPIO_H */
