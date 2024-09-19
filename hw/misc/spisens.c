/*
 * SPI temperature sensor component
 *
 * Copyright (c) 2024 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"
#include "trace.h"

#define TYPE_SPI_SENS "mistra.spisens"

#define SPI_SENS(obj) OBJECT_CHECK(SPISensor, (obj), TYPE_SPI_SENS)

/* Registers */
enum { REG_ID_OFFSET = 0u, REG_CTRL_OFFSET, REG_TEMPERATURE_OFFSET, NR_REGS };

#define REG_CTRL_EN_SHIFT 0x0
#define REG_CTRL_EN_MASK  0x01

#define SPI_SENS_ID_VAL   0x5A

/* A simple SPI slave. */
typedef struct SPISensor {
    /*< private >*/
    SSIPeripheral parent_obj;
    /*< public >*/
    uint8_t regs[NR_REGS]; // peripheral registers
    uint8_t cycle; // cycle counter
    uint8_t ptr; // current register index
    bool write_nread; // indicator if write or read is in progress
} SPISensor;

/* Control-byte bitfields */
#define CB_START     (1 << 7)
#define CB_REG_SHIFT 4
#define CB_REG_MASK  (7 << CB_REG_SHIFT)

/* Reset all counters and load ID register */
static void spi_sens_reset_enter(Object *obj, ResetType type)
{
    SPISensor *s = SPI_SENS(obj);

    s->ptr = 0;
    s->cycle = 0;
    s->write_nread = false;
    memset(s->regs, 0, NR_REGS);
    s->regs[REG_ID_OFFSET] = SPI_SENS_ID_VAL;

    /* random seed */
    srand(time(NULL));
}

/* Generate random temperature value
 * Value is in range 15.0 to 25.0 with 0.5 step.
 */
static uint8_t spi_sens_get_temperature(void)
{
    return (30 + (rand() % 21));
}

/* Return zero on command byte, then return the requested register */
static uint32_t spi_sens_read(SPISensor *s)
{
    if (!s->write_nread) {
        switch (s->cycle) {
        case 0:
            /* Command byte */
            trace_spisens_read_command(s->write_nread, s->ptr);
            /* Generate new temperature sample */
            if (s->regs[REG_CTRL_OFFSET] & REG_CTRL_EN_MASK) {
                s->regs[REG_TEMPERATURE_OFFSET] = spi_sens_get_temperature();
            } else {
                s->regs[REG_TEMPERATURE_OFFSET] = 0xff;
            }
            break;
        case 1:
            switch (s->ptr) {
            case REG_ID_OFFSET:
            case REG_CTRL_OFFSET:
            case REG_TEMPERATURE_OFFSET:
                /* Return register value */
                trace_spisens_read_valid(s->ptr, s->regs[s->ptr]);
                return s->regs[s->ptr];
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Transfer too long\n",
                              TYPE_SPI_SENS, __func__);
            }
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "[%s]%s: Trying to read non-existing register\n",
                          TYPE_SPI_SENS, __func__);
            break;
        }
    }
    return 0;
}

/* Update control values on command byte and update register on second byte */
static void spi_sens_write(SPISensor *s, uint32_t value)
{
    switch (s->cycle) {
    case 0:
        s->write_nread = (value & CB_START) != 0;
        s->ptr = (value & CB_REG_MASK) >> CB_REG_SHIFT;
        trace_spisens_write_command(s->write_nread, s->ptr);
        break;
    case 1:
        if (s->write_nread) {
            switch (s->ptr) {
            case REG_ID_OFFSET:
            case REG_TEMPERATURE_OFFSET:
                qemu_log_mask(
                    LOG_GUEST_ERROR,
                    "[%s]%s: Trying to write to a read-only register\n",
                    TYPE_SPI_SENS, __func__);
                break;
            case REG_CTRL_OFFSET:
                trace_spisens_write_valid(s->ptr, value);
                s->regs[s->ptr] = value;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR,
                              "[%s]%s: Trying to write non-existing register\n",
                              TYPE_SPI_SENS, __func__);
                break;
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s]%s: Transfer too long\n",
                      TYPE_SPI_SENS, __func__);
        break;
    }
}

static uint32_t spi_sens_transfer(SSIPeripheral *dev, uint32_t value)
{
    SPISensor *s = SPI_SENS(dev);
    uint32_t retval = 0;
    spi_sens_write(s, value);
    retval = spi_sens_read(s);
    s->cycle++;
    return retval;
}

static int spi_sens_cs(SSIPeripheral *ss, bool select)
{
    SPISensor *s = SPI_SENS(ss);

    if (select) {
        s->cycle = 0;
        s->ptr = 0;
    }

    trace_spisens_select(s, select ? "de" : "");

    return 0;
}

static const VMStateDescription vmstate_spi_sens = {
    .name = TYPE_SPI_SENS,
    .version_id = 1,
    .fields = (VMStateField[]){ VMSTATE_SSI_PERIPHERAL(parent_obj, SPISensor),
                                VMSTATE_UINT8_ARRAY(regs, SPISensor, NR_REGS),
                                VMSTATE_UINT8(cycle, SPISensor),
                                VMSTATE_UINT8(ptr, SPISensor),
                                VMSTATE_BOOL(write_nread, SPISensor),
                                VMSTATE_END_OF_LIST() }
};

static void spi_sens_init(SSIPeripheral *d, Error **errp)
{
    SPISensor *s = SPI_SENS(d);

    s->cycle = 0;
    s->ptr = 0;
    memset(s->regs, 0, NR_REGS);
    s->regs[REG_ID_OFFSET] = SPI_SENS_ID_VAL;

    trace_spisens_init();

    return;
}

static void spi_sens_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = spi_sens_reset_enter;
    dc->vmsd = &vmstate_spi_sens;
    k->realize = spi_sens_init;
    k->transfer = spi_sens_transfer;
    k->set_cs = spi_sens_cs;
    k->cs_polarity = SSI_CS_LOW;
}

static const TypeInfo spi_sens_info = {
    .name = TYPE_SPI_SENS,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(SPISensor),
    .class_init = spi_sens_class_init,
};

static void spi_sens_register_devices(void)
{
    type_register_static(&spi_sens_info);
}

type_init(spi_sens_register_devices)
