/*
 *  Allwinner GPIO Emulation
 *
 *  Copyright (C) 2025 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
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

#include "qemu/osdep.h"
#include "hw/gpio/allwinner-gpio.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

/* GPIO ports index (n) */
#define GPIO_PA     0
#define GPIO_PB     1
#define GPIO_PC     2
#define GPIO_PD     3
#define GPIO_PE     4
#define GPIO_PF     5
#define GPIO_PG     6
#define GPIO_PH     7
#define GPIO_PI     8

// /* GPIO ports max pins */
// #define GPIO_PA_PINS    18
// #define GPIO_PB_PINS    24
// #define GPIO_PC_PINS    25
// #define GPIO_PD_PINS    28
// #define GPIO_PE_PINS    12
// #define GPIO_PF_PINS    6
// #define GPIO_PG_PINS    12
// #define GPIO_PH_PINS    28
// #define GPIO_PI_PINS    22

typedef struct AWPortMap {
    uint32_t cfg[4];
    uint32_t dat;
    uint32_t drv[2];
    uint32_t pul[2];
} AWPortMap;

typedef struct AWPortsOverlay {
    AWPortMap ports[AW_GPIO_PORTS_NUM];
} AWPortsOverlay;

static const uint32_t AW_PINS_PER_PORT[AW_GPIO_PORTS_NUM] = {
    18,
    24,
    25,
    28,
    12,
    6,
    12,
    28,
    22
};

// static const uint32_t AW_IRQ_BITMAP[AW_GPIO_PORTS_NUM] = {
//     0x0,
//     0x0,
//     0x0,
//     0x0,
//     0x0,
//     0x0,
//     0x0,
//     0x003fffff,
//     0x000ffc00,
// };

static const uint32_t AW_IRQ_MAP[AW_GPIO_PORTS_NUM][AW_GPIO_PIN_COUNT] =
{
{-1},
{-1},
{-1},
{-1},
{-1},
{-1},
{-1},
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 22, 23, 24, 25, 26, 27,
28, 29, 30, 31, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
};

#define DEFAULT_CFG_MASK                0x77777777
#define DEFAULT_DRV_MASK                0xffffffff
#define DEFAULT_PUL_MASK                0xffffffff

#define CFG_INPUT_MASK     0x0
#define CFG_OUTPUT_MASK    0x1
#define CFG_IO_MASK        0x1
#define CFG_PIN_STRIDE     4
#define CFG_PINS_PER_REG   BITS_PER_LONG / CFG_PIN_STRIDE

/* GPIO masks per number of pins */
#define GPIO_CFG0_PINS_MASK(pins)       ((pins > CFG_PINS_PER_REG) ? \
                                            DEFAULT_CFG_MASK : \
                                            ((1 << (pins * CFG_PIN_STRIDE)) - 1) & DEFAULT_CFG_MASK)
#define GPIO_CFG1_PINS_MASK(pins)       ((pins > 16) ? \
                                            DEFAULT_CFG_MASK : \
                                            ((pins > 8) ? \
                                                ((1 << ((pins - 8) * 4)) - 1) & DEFAULT_CFG_MASK : 0))
#define GPIO_CFG2_PINS_MASK(pins)       ((pins > 24) ? \
                                            DEFAULT_CFG_MASK : \
                                            ((pins > 16) ? \
                                                ((1 << ((pins - 16) * 4)) - 1) & DEFAULT_CFG_MASK : 0))
#define GPIO_CFG3_PINS_MASK(pins)       ((pins > 24) ? \
                                            ((1 << ((pins - 16) * 4)) - 1) & DEFAULT_CFG_MASK : 0)
#define GPIO_DAT_PINS_MASK(pins)        ((1 << pins) - 1)
#define GPIO_DRV0_PINS_MASK(pins)       ((pins > 16) ? DEFAULT_DRV_MASK : ((1 << (pins * 2)) - 1))
#define GPIO_DRV1_PINS_MASK(pins)       ((pins > 16) ? ((1 << ((pins - 16) * 2)) - 1) : 0)
#define GPIO_PUL0_PINS_MASK(pins)       ((pins > 16) ? DEFAULT_PUL_MASK : ((1 << (pins * 2)) - 1))
#define GPIO_PUL1_PINS_MASK(pins)       ((pins > 16) ? ((1 << ((pins - 16) * 2)) - 1) : 0)


#define PORT_STRIDE     0x24

/* Allwinner GPIO memory map */
#define CFG0    0x00 /* Configure register 0 */
#define CFG1    0x04 /* Configure register 1 */
#define CFG2    0x08 /* Configure register 2 */
#define CFG3    0x0c /* Configure register 3 */
#define DAT     0x10 /* Data register */
#define DRV0    0x14 /* Multi-driving register 0 */
#define DRV1    0x18 /* Multi-driving register 1 */
#define PUL0    0x1c /* Pull register 0 */
#define PUL1    0x20 /* Pull register 1 */

/* Port n configure register 0 */
#define GPIO_Pn_CFG0(n)   (n*PORT_STRIDE + CFG0)
/* Port n configure register 1 */
#define GPIO_Pn_CFG1(n)   (n*PORT_STRIDE + CFG1)
/* Port n configure register 2 */
#define GPIO_Pn_CFG2(n)   (n*PORT_STRIDE + CFG2)
/* Port n configure register 3 */
#define GPIO_Pn_CFG3(n)   (n*PORT_STRIDE + CFG3)
/* Port n data register */
#define GPIO_Pn_DAT(n)    (n*PORT_STRIDE + DAT)
/* Port n Multi-driving register 0 */
#define GPIO_Pn_DRV0(n)   (n*PORT_STRIDE + DRV0)
/* Port n Multi-driving register 1 */
#define GPIO_Pn_DRV1(n)   (n*PORT_STRIDE + DRV1)
/* Port n Pull register 0 */
#define GPIO_Pn_PUL0(n)   (n*PORT_STRIDE + PUL0)
/* Port n Pull register 1 */
#define GPIO_Pn_PUL1(n)   (n*PORT_STRIDE + PUL1)
/* PIO interrupt configure register 0 */
#define GPIO_INT_CFG0       0x200
/* PIO interrupt configure register 1 */
#define GPIO_INT_CFG1       0x204
/* PIO interrupt configure register 2 */
#define GPIO_INT_CFG2       0x208
/* PIO interrupt configure register 3 */
#define GPIO_INT_CFG3       0x20c
/* PIO interrupt control register */
#define GPIO_INT_CTL        0x210
/* PIO interrupt status register */
#define GPIO_INT_STA        0x214
/* PIO interrupt debounce register */
#define GPIO_INT_DEB        0x218
/* SDRAM Pad Multi-driving register */
#define SDR_PAD_DRV         0x220
/* SDRAM Pad Pull register */
#define SDR_PAD_PUL         0x224

#define REG_INDEX(offset)         (offset / sizeof(uint32_t))


#define AW_GPIO_SET(port) \
    static inline void allwinner_gpio_set_##port(void *opaque, int line, int level) { \
        allwinner_gpio_set(opaque, port, line, level); \
    }

static void allwinner_gpio_set(void *opaque, int port, int line, int level);

AW_GPIO_SET(GPIO_PA);
AW_GPIO_SET(GPIO_PB);
AW_GPIO_SET(GPIO_PC);
AW_GPIO_SET(GPIO_PD);
AW_GPIO_SET(GPIO_PE);
AW_GPIO_SET(GPIO_PF);
AW_GPIO_SET(GPIO_PG);
AW_GPIO_SET(GPIO_PH);
AW_GPIO_SET(GPIO_PI);

typedef enum AWGPIOLevel {
    AW_GPIO_LEVEL_LOW = 0,
    AW_GPIO_LEVEL_HIGH = 1,
} AWGPIOLevel;

static const char *portname(unsigned index)
{
    char *portname = NULL;

    switch (index)
    {
        case GPIO_PA:
            portname = g_strdup("PA");
            break;
        case GPIO_PB:
            portname = g_strdup("PB");
            break;
        case GPIO_PC:
            portname = g_strdup("PC");
            break;
        case GPIO_PD:
            portname = g_strdup("PD");
            break;
        case GPIO_PE:
            portname = g_strdup("PE");
            break;
        case GPIO_PF:
            portname = g_strdup("PF");
            break;
        case GPIO_PG:
            portname = g_strdup("PG");
            break;
        case GPIO_PH:
            portname = g_strdup("PH");
            break;
        case GPIO_PI:
            portname = g_strdup("PI");
            break;
        default:
            break;
    }
    return portname;
}

static const char *allwinner_gpio_get_regname(unsigned offset)
{
    g_autofree char *regname = NULL;

    switch (offset) {
    case 0 ... GPIO_Pn_PUL1(GPIO_PI):
    {
        switch (offset % 0x24) {
            case CFG0:
                regname = g_strdup("CFG0");
                break;
            case CFG1:
                regname = g_strdup("CFG1");
                break;
            case CFG2:
                regname = g_strdup("CFG2");
                break;
            case CFG3:
                regname = g_strdup("CFG3");
                break;
            case DAT:
                regname = g_strdup("DAT");
                break;
            case DRV0:
                regname = g_strdup("DRV0");
                break;
            case DRV1:
                regname = g_strdup("DRV1");
                break;
            case PUL0:
                regname = g_strdup("PUL0");
                break;
            case PUL1:
                regname = g_strdup("PUL1");
                break;
        }
        return g_strdup_printf("%s:%s", portname(offset / 0x24), regname);
    }
    case GPIO_INT_CFG0:
        return "INT_CFG0";
    case GPIO_INT_CFG1:
        return "INT_CFG1";
    case GPIO_INT_CFG2:
        return "INT_CFG2";
    case GPIO_INT_CFG3:
        return "INT_CFG3";
    case GPIO_INT_CTL:
        return "INT_CTL";
    case GPIO_INT_STA:
        return "INT_STA";
    case GPIO_INT_DEB:
        return "INT_DEB";
    default:
        return "[?]";
    }
}

static bool gpio_is_input(AWPortMap *port, uint32_t pin);

static void allwinner_gpio_update_int(AWGPIOState *s)
{
    qemu_set_irq(s->irq, (s->regs[REG_INDEX(GPIO_INT_CTL)] & s->regs[REG_INDEX(GPIO_INT_STA)]) ? 1 : 0);
}

static void allwinner_gpio_set_int_line(AWGPIOState *s, int port, int line, AWGPIOLevel level)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    int irq_line = AW_IRQ_MAP[port][line];

    /* Check if pin can actually trigger an interrupt */
    if (irq_line == -1)
        return;

    /* if this signal isn't configured as an input signal, nothing to do */
    if (!gpio_is_input(&o->ports[port], line))
        return;

    /* Determine interrupt setting */
    unsigned irq_cfg_index = irq_line / 8;
    unsigned int_cfg_n = s->regs[REG_INDEX(GPIO_INT_CFG0) + irq_cfg_index];
    unsigned int_cfg = extract32(int_cfg_n, (irq_line % 8) * 4, 3);

    /* Get current input value */
    unsigned input_val = extract32(o->ports[port].dat, line, 1);

    switch (int_cfg) {
        case 0: /* Positive edge */
        if (!input_val && level)
            deposit32(s->regs[REG_INDEX(GPIO_INT_STA)], irq_line, 1, 1);
        break;
        case 1: /* Negative edge */
        if (input_val && !level)
            deposit32(s->regs[REG_INDEX(GPIO_INT_STA)], irq_line, 1, 1);
        break;
        case 2: /* High level */
        if (level)
            deposit32(s->regs[REG_INDEX(GPIO_INT_STA)], irq_line, 1, 1);
        break;
        case 3: /* Low level */
        if (!level)
            deposit32(s->regs[REG_INDEX(GPIO_INT_STA)], irq_line, 1, 1);
        break;
        case 4: /* Both edges */
        if (input_val != level)
            deposit32(s->regs[REG_INDEX(GPIO_INT_STA)], irq_line, 1, 1);
        break;
        default:
        /* Unexpected */
        break;
    }
}

static void port_set_all_int_lines(AWGPIOState *s, int port)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    int i;

    for (i = 0; i < AW_PINS_PER_PORT[port]; i++) {
        AWGPIOLevel aw_level = extract32(o->ports[port].dat, i, 1);
        allwinner_gpio_set_int_line(s, port, i, aw_level);
    }

    allwinner_gpio_update_int(s);
}

static void allwinner_set_all_int_lines(AWGPIOState *s)
{
    int i;

    for (i = 0; i < AW_GPIO_PORTS_NUM; i++)
        port_set_all_int_lines(s, i);
}

static void allwinner_gpio_set(void *opaque, int port, int line, int level)
{
    AWGPIOState *s = AW_GPIO(opaque);
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    AWGPIOLevel aw_level = level ? AW_GPIO_LEVEL_HIGH : AW_GPIO_LEVEL_LOW;

    trace_allwinner_gpio_set(portname(port), line, aw_level);

    allwinner_gpio_set_int_line(s, port, line, aw_level);

    /* this is an input signal, so set PSR */
    o->ports[port].dat = deposit32(o->ports[port].dat, line, 1, aw_level);

    allwinner_gpio_update_int(s);
}


static inline bool gpio_is_output(AWPortMap *port, uint32_t pin)
{
    uint32_t cfg_n = pin / CFG_PINS_PER_REG;
    uint32_t pin_shift = (pin % CFG_PINS_PER_REG) * CFG_PIN_STRIDE;
    return (extract32(port->cfg[cfg_n], pin_shift, CFG_PIN_STRIDE - 1) == CFG_OUTPUT_MASK);
}

static inline bool gpio_is_input(AWPortMap *port, uint32_t pin)
{
    uint32_t cfg_n = pin / CFG_PINS_PER_REG;
    uint32_t pin_shift = (pin % CFG_PINS_PER_REG) * CFG_PIN_STRIDE;
    return (extract32(port->cfg[cfg_n], pin_shift, CFG_PIN_STRIDE - 1) == CFG_INPUT_MASK);
}

// static inline bool gpio_is_io(AWPortMap *port, uint32_t pin)
// {
//     uint32_t cfg_n = pin / CFG_PINS_PER_REG;
//     uint32_t pin_shift = (pin % CFG_PINS_PER_REG) * CFG_PIN_STRIDE;
//     return (extract32(port->cfg[cfg_n], pin_shift, CFG_PIN_STRIDE - 1) & 0x6) == 0;
// }

static inline void port_update_output_lines(AWGPIOState *s, uint32_t port)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    int pin;

    for (pin = 0; pin < AW_PINS_PER_PORT[port]; pin++)
    {
        /*
        * if the line is set as output, then forward the line
        * level to its user.
        */
        if (gpio_is_output(&o->ports[port], pin))
            qemu_set_irq(s->output[port][pin], !!(o->ports[port].dat & BIT_MASK(pin)));
        else if (gpio_is_input(&o->ports[port], pin))
            qemu_irq_lower(s->output[port][pin]);
    }

}
// static inline void allwinner_gpio_update_all_output_lines(AWGPIOState *s)
// {
//     int port;
//
//     for (port = 0; port < AW_GPIO_PORTS_NUM; port++) {
//         port_update_output_lines(s, port);
//     }
// }


static uint64_t allwinner_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    AWGPIOState *s = AW_GPIO(opaque);
    uint32_t reg_value = 0;

    switch (offset) {
        case 0 ... GPIO_Pn_PUL1(GPIO_PI):
        case GPIO_INT_CFG0:
        case GPIO_INT_CFG1:
        case GPIO_INT_CFG2:
        case GPIO_INT_CFG3:
        case GPIO_INT_CTL:
        case GPIO_INT_STA:
        case GPIO_INT_DEB:
        reg_value = s->regs[REG_INDEX(offset)];
            break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_AW_GPIO, __func__, offset);
        break;
    }

    trace_allwinner_gpio_read(allwinner_gpio_get_regname(offset), reg_value);

    return reg_value;
}

/* update output values */
/* update interrupts */
static void allwinner_port_write(AWGPIOState *s, hwaddr offset, uint64_t value)
{
    AWPortsOverlay *o = (AWPortsOverlay *)s->regs;
    uint32_t port = offset / PORT_STRIDE;
    uint32_t reg = offset % PORT_STRIDE;

    switch (reg) {
        case CFG0:
            o->ports[port].cfg[0] = value & GPIO_CFG0_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case CFG1:
            o->ports[port].cfg[1] = value & GPIO_CFG1_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case CFG2:
            o->ports[port].cfg[2] = value & GPIO_CFG2_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case CFG3:
            o->ports[port].cfg[3] = value & GPIO_CFG3_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case DAT:
            o->ports[port].dat = value & GPIO_DAT_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case DRV0:
            o->ports[port].drv[0] = value & GPIO_DRV0_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case DRV1:
            o->ports[port].drv[1] = value & GPIO_DRV1_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case PUL0:
            o->ports[port].pul[0] = value & GPIO_PUL0_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
        case PUL1:
            o->ports[port].pul[1] = value & GPIO_PUL1_PINS_MASK(AW_PINS_PER_PORT[port]);
            break;
    }
    port_update_output_lines(s, port);
    port_set_all_int_lines(s, port);
}

static void allwinner_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    AWGPIOState *s = AW_GPIO(opaque);

    trace_allwinner_gpio_write(allwinner_gpio_get_regname(offset), value);

    switch (offset) {
        case 0 ... GPIO_Pn_PUL1(GPIO_PI):
            allwinner_port_write(s, offset, value);
            break;
        case GPIO_INT_CFG0:
        case GPIO_INT_CFG1:
        case GPIO_INT_CFG2:
        case GPIO_INT_CFG3:
        case GPIO_INT_CTL:
        case GPIO_INT_DEB:
            s->regs[REG_INDEX(offset)] = value;
            /* update interrupts */
            allwinner_set_all_int_lines(s);
            break;
        case GPIO_INT_STA:
            /* W1C */
            s->regs[REG_INDEX(offset)] &= ~value;
            allwinner_gpio_update_int(s);
            break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Bad register at offset 0x%"
                      HWADDR_PRIx "\n", TYPE_AW_GPIO, __func__, offset);
        break;
    }
}

static const MemoryRegionOps allwinner_gpio_ops = {
    .read = allwinner_gpio_read,
    .write = allwinner_gpio_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_allwinner_gpio = {
    .name = TYPE_AW_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AWGPIOState, AW_GPIO_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_gpio_reset(DeviceState *dev)
{
    // IMXGPIOState *s = IMX_GPIO(dev);
    //
    // s->dr       = 0;
    // s->gdir     = 0;
    // s->psr      = 0;
    // s->icr      = 0;
    // s->imr      = 0;
    // s->isr      = 0;
    // s->edge_sel = 0;
    //
    // imx_gpio_set_all_output_lines(s);
    // imx_gpio_update_int(s);
}

static void allwinner_gpio_realize(DeviceState *dev, Error **errp)
{
    AWGPIOState *s = AW_GPIO(dev);
    int port;

    memory_region_init_io(&s->iomem, OBJECT(s), &allwinner_gpio_ops, s,
                          TYPE_AW_GPIO, AW_GPIO_IOSIZE);

    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PA, AW_PINS_PER_PORT[GPIO_PA]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PB, AW_PINS_PER_PORT[GPIO_PB]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PC, AW_PINS_PER_PORT[GPIO_PC]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PD, AW_PINS_PER_PORT[GPIO_PD]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PE, AW_PINS_PER_PORT[GPIO_PE]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PF, AW_PINS_PER_PORT[GPIO_PF]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PG, AW_PINS_PER_PORT[GPIO_PG]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PH, AW_PINS_PER_PORT[GPIO_PH]);
    qdev_init_gpio_in(dev, allwinner_gpio_set_GPIO_PI, AW_PINS_PER_PORT[GPIO_PI]);
    for (port = 0; port < AW_GPIO_PORTS_NUM; port++)
    {
        qdev_init_gpio_out_named(dev, s->output[port], portname(port), AW_PINS_PER_PORT[port]);
    }
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void allwinner_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = allwinner_gpio_realize;
    device_class_set_legacy_reset(dc, allwinner_gpio_reset);
    dc->vmsd = &vmstate_allwinner_gpio;
    dc->desc = "Allwinner GPIO controller";
}

static const TypeInfo allwinner_gpio_info = {
    .name = TYPE_AW_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AWGPIOState),
    .class_init = allwinner_gpio_class_init,
};

static void allwinner_gpio_register_types(void)
{
    type_register_static(&allwinner_gpio_info);
}

type_init(allwinner_gpio_register_types)
