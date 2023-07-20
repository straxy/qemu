/*
 * Allwinner A10 PS2 Module emulation
 *
 * Copyright (C) 2023 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/input/allwinner-a10-ps2.h"
#include "hw/input/ps2.h"
#include "hw/irq.h"

/* PS2 register offsets */
enum {
    REG_PLL1_CFG             = 0x0000, /* PLL1 Control */
    REG_PLL1_TUN             = 0x0004, /* PLL1 Tuning */
    REG_PLL2_CFG             = 0x0008, /* PLL2 Control */
    REG_PLL2_TUN             = 0x000C, /* PLL2 Tuning */
    REG_PLL3_CFG             = 0x0010, /* PLL3 Control */
    REG_PLL4_CFG             = 0x0018, /* PLL4 Control */
    REG_PLL5_CFG             = 0x0020, /* PLL5 Control */
    REG_PLL5_TUN             = 0x0024, /* PLL5 Tuning */
    REG_PLL6_CFG             = 0x0028, /* PLL6 Control */
    REG_PLL6_TUN             = 0x002C, /* PLL6 Tuning */
    REG_PLL7_CFG             = 0x0030, /* PLL7 Control */
    REG_PLL1_TUN2            = 0x0038, /* PLL1 Tuning2 */
    REG_PLL5_TUN2            = 0x003C, /* PLL5 Tuning2 */
    REG_PLL8_CFG             = 0x0040, /* PLL8 Control */
    REG_OSC24M_CFG           = 0x0050, /* OSC24M Control */
    REG_CPU_AHB_APB0_CFG     = 0x0054, /* CPU, AHB and APB0 Divide Ratio */
};

#define REG_INDEX(offset)    (offset / sizeof(uint32_t))

/* PS2 register reset values */
enum {
    REG_PLL1_CFG_RST         = 0x21005000,
    REG_PLL1_TUN_RST         = 0x0A101000,
    REG_PLL2_CFG_RST         = 0x08100010,
    REG_PLL2_TUN_RST         = 0x00000000,
    REG_PLL3_CFG_RST         = 0x0010D063,
    REG_PLL4_CFG_RST         = 0x21009911,
    REG_PLL5_CFG_RST         = 0x11049280,
    REG_PLL5_TUN_RST         = 0x14888000,
    REG_PLL6_CFG_RST         = 0x21009911,
    REG_PLL6_TUN_RST         = 0x00000000,
    REG_PLL7_CFG_RST         = 0x0010D063,
    REG_PLL1_TUN2_RST        = 0x00000000,
    REG_PLL5_TUN2_RST        = 0x00000000,
    REG_PLL8_CFG_RST         = 0x21009911,
    REG_OSC24M_CFG_RST       = 0x00138013,
    REG_CPU_AHB_APB0_CFG_RST = 0x00010010,
};

static void allwinner_a10_ps2_update_irq(AwA10PS2State *s)
{
    int level = /*(s->pending && (s->cr & 0x10) != 0)
                 || (s->cr & 0x08) !=*/ 0;

    qemu_set_irq(s->irq, level);
}

static void allwinner_a10_ps2_set_irq(void *opaque, int n, int level)
{
    AwA10PS2State *s = (AwA10PS2State *)opaque;

    s->pending = level;
    allwinner_a10_ps2_update_irq(s);
}

static uint64_t allwinner_a10_ps2_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    const AwA10PS2State *s = AW_A10_PS2(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_PLL1_CFG:
    case REG_PLL1_TUN:
    case REG_PLL2_CFG:
    case REG_PLL2_TUN:
    case REG_PLL3_CFG:
    case REG_PLL4_CFG:
    case REG_PLL5_CFG:
    case REG_PLL5_TUN:
    case REG_PLL6_CFG:
    case REG_PLL6_TUN:
    case REG_PLL7_CFG:
    case REG_PLL1_TUN2:
    case REG_PLL5_TUN2:
    case REG_PLL8_CFG:
    case REG_OSC24M_CFG:
    case REG_CPU_AHB_APB0_CFG:
        break;
    case 0x158 ... AW_A10_PS2_IOSIZE:
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

static void allwinner_a10_ps2_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    AwA10PS2State *s = AW_A10_PS2(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_PLL1_CFG:
    case REG_PLL1_TUN:
    case REG_PLL2_CFG:
    case REG_PLL2_TUN:
    case REG_PLL3_CFG:
    case REG_PLL4_CFG:
    case REG_PLL5_CFG:
    case REG_PLL5_TUN:
    case REG_PLL6_CFG:
    case REG_PLL6_TUN:
    case REG_PLL7_CFG:
    case REG_PLL1_TUN2:
    case REG_PLL5_TUN2:
    case REG_PLL8_CFG:
    case REG_OSC24M_CFG:
    case REG_CPU_AHB_APB0_CFG:
        break;
    case 0x158 ... AW_A10_PS2_IOSIZE:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t)offset);
        break;
    }

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps allwinner_a10_ps2_ops = {
    .read = allwinner_a10_ps2_read,
    .write = allwinner_a10_ps2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static const VMStateDescription allwinner_a10_ps2_vmstate = {
    .name = "allwinner-a10-ps2",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AwA10PS2State, AW_A10_PS2_REGS_NUM),
        VMSTATE_INT32(pending, AwA10PS2State),
        VMSTATE_END_OF_LIST()
    }
};

static void allwinner_a10_ps2_realize(DeviceState *dev, Error **errp)
{
    AwA10PS2State *s = AW_A10_PS2(dev);

    qdev_connect_gpio_out(DEVICE(s->ps2dev), PS2_DEVICE_IRQ,
                          qdev_get_gpio_in_named(dev, "ps2-input-irq", 0));
}

static void allwinner_a10_ps2_kbd_realize(DeviceState *dev, Error **errp)
{
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_GET_CLASS(dev);
    AwA10PS2KbdState *s = AW_A10_PS2_KBD_DEVICE(dev);
    AwA10PS2State *ps = AW_A10_PS2(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->kbd), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->kbd);
    pdc->parent_realize(dev, errp);
}

static void allwinner_a10_ps2_kbd_init(Object *obj)
{
    AwA10PS2KbdState *s = AW_A10_PS2_KBD_DEVICE(obj);
    AwA10PS2State *ps = AW_A10_PS2(obj);

    ps->is_mouse = false;
    object_initialize_child(obj, "kbd", &s->kbd, TYPE_PS2_KBD_DEVICE);
}

static void allwinner_a10_ps2_mouse_realize(DeviceState *dev, Error **errp)
{
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_GET_CLASS(dev);
    AwA10PS2MouseState *s = AW_A10_PS2_MOUSE_DEVICE(dev);
    AwA10PS2State *ps = AW_A10_PS2(dev);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mouse), errp)) {
        return;
    }

    ps->ps2dev = PS2_DEVICE(&s->mouse);
    pdc->parent_realize(dev, errp);
}

static void allwinner_a10_ps2_mouse_init(Object *obj)
{
    AwA10PS2MouseState *s = AW_A10_PS2_MOUSE_DEVICE(obj);
    AwA10PS2State *ps = AW_A10_PS2(obj);

    ps->is_mouse = true;
    object_initialize_child(obj, "mouse", &s->mouse, TYPE_PS2_MOUSE_DEVICE);
}

static void allwinner_a10_ps2_kbd_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_CLASS(oc);

    device_class_set_parent_realize(dc, allwinner_a10_ps2_kbd_realize,
                                    &pdc->parent_realize);
}

static const TypeInfo allwinner_a10_ps2_kbd_info = {
    .name          = TYPE_AW_A10_PS2_KBD_DEVICE,
    .parent        = TYPE_AW_A10_PS2,
    .instance_init = allwinner_a10_ps2_kbd_init,
    .instance_size = sizeof(AwA10PS2KbdState),
    .class_init    = allwinner_a10_ps2_kbd_class_init,
};

static void allwinner_a10_ps2_mouse_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    AwA10PS2DeviceClass *pdc = AW_A10_PS2_CLASS(oc);

    device_class_set_parent_realize(dc, allwinner_a10_ps2_mouse_realize,
                                    &pdc->parent_realize);
}

static const TypeInfo allwinner_a10_ps2_mouse_info = {
    .name          = TYPE_AW_A10_PS2_MOUSE_DEVICE,
    .parent        = TYPE_AW_A10_PS2,
    .instance_init = allwinner_a10_ps2_mouse_init,
    .instance_size = sizeof(AwA10PS2MouseState),
    .class_init    = allwinner_a10_ps2_mouse_class_init,
};

static void allwinner_a10_ps2_init(Object *obj)
{
    AwA10PS2State *s = AW_A10_PS2(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &allwinner_a10_ps2_ops, s, "allwinner-a10-ps2", AW_A10_PS2_IOSIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_in_named(DEVICE(obj), allwinner_a10_ps2_set_irq, "ps2-input-irq", 1);
}

static void allwinner_a10_ps2_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = allwinner_a10_ps2_realize;
    dc->vmsd = &allwinner_a10_ps2_vmstate;
}

static const TypeInfo allwinner_a10_ps2_type_info = {
    .name          = TYPE_AW_A10_PS2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = allwinner_a10_ps2_init,
    .instance_size = sizeof(AwA10PS2State),
    .class_init    = allwinner_a10_ps2_class_init,
    .class_size    = sizeof(AwA10PS2DeviceClass),
    .abstract      = true,
    .class_init    = allwinner_a10_ps2_class_init,
};

static void allwinner_a10_ps2_register_types(void)
{
    type_register_static(&allwinner_a10_ps2_type_info);
    type_register_static(&allwinner_a10_ps2_kbd_info);
    type_register_static(&allwinner_a10_ps2_mouse_info);
}

type_init(allwinner_a10_ps2_register_types)
