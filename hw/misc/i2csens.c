/*
 * I2C temperature sensor component
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
#include "hw/resettable.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "trace/trace-hw_misc.h"

#define TYPE_I2C_SENS "mistra.i2csens"

#define I2C_SENS(obj) \
    OBJECT_CHECK(I2CSensor, (obj), TYPE_I2C_SENS)

/* registers */
enum {
    REG_ID_OFFSET = 0x0u,
    REG_CTRL_OFFSET,
    REG_TEMPERATURE_OFFSET,
    NR_REGS
};

#define REG_CTRL_EN_SHIFT   (0x0)
#define REG_CTRL_EN_MASK    (0x01)

#define I2C_SENS_ID_VAL     (0x5A)

/* A simple I2C slave which returns values of ID or CNT register. */
typedef struct I2CSensor {
    /*< private >*/
    I2CSlave i2c;
    /*< public >*/
    uint8_t regs[NR_REGS];      // peripheral registers
    uint8_t count;          // counter used for tx/rx
    uint8_t ptr;            // current register index
} I2CSensor;

/* Reset all counters and load ID register */
static void i2c_sens_reset_enter(Object *obj, ResetType type)
{
    I2CSensor *s = I2C_SENS(obj);

    s->ptr = 0;
    s->count = 0;
    memset(s->regs, 0, NR_REGS);
    s->regs[REG_ID_OFFSET] = I2C_SENS_ID_VAL;

    /* random seed */
    srand(time(NULL));
}

/* Generate random temperature value
 * Value is in range 15.0 to 25.0 with 0.5 step.
 */
static uint8_t i2c_sens_get_temperature(void)
{
    return (30 + (rand() % 21));
}

/* Check for read event from master.
 * Once read event is triggered, update value in TEMPERATURE register.
 * If peripheral is enabled, load next value, otherwise load 0xff.
 */
static int i2c_sens_event(I2CSlave *i2c, enum i2c_event event)
{
    I2CSensor *s = I2C_SENS(i2c);

    if (event == I2C_START_RECV) {
        if (s->ptr == REG_TEMPERATURE_OFFSET) {
            if (s->regs[REG_CTRL_OFFSET] & REG_CTRL_EN_MASK) {
                s->regs[REG_TEMPERATURE_OFFSET] = i2c_sens_get_temperature();
            } else {
                s->regs[REG_TEMPERATURE_OFFSET] = 0xff;
            }
        }
    }

    s->count = 0;

    return 0;
}

/* Called when master requests read */
static uint8_t i2c_sens_rx(I2CSlave *i2c)
{
    I2CSensor *s = I2C_SENS(i2c);
    uint8_t ret = 0xff;

    if (s->ptr < NR_REGS) {
        ret = s->regs[s->ptr++];
    }

    trace_i2csens_read(s->ptr, ret);

    return ret;
}

/* Called when master sends write.
 * Update ptr with byte 0, then perform write with second byte.
 */
static int i2c_sens_tx(I2CSlave *i2c, uint8_t data)
{
    I2CSensor *s = I2C_SENS(i2c);

    if (s->count == 0) {
        /* store register address */
        s->ptr = data;
        s->count++;
    } else {
	trace_i2csens_write(s->ptr, data);
        if (s->ptr == REG_CTRL_OFFSET) {
            s->regs[s->ptr++] = data;
        }
    }

    return 0;
}

/* Initialization */
static void i2c_sens_init(Object *obj)
{
    I2CSensor *s = I2C_SENS(obj);

    s->count = 0;
    s->ptr = 0;
    memset(s->regs, 0, NR_REGS);
    s->regs[REG_ID_OFFSET] = I2C_SENS_ID_VAL;

    return;
}

static const VMStateDescription vmstate_i2c_sens = {
    .name = TYPE_I2C_SENS,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, I2CSensor, NR_REGS),
        VMSTATE_UINT8(count, I2CSensor),
        VMSTATE_UINT8(ptr, I2CSensor),
        VMSTATE_END_OF_LIST()
    }
};

static void i2c_sens_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    rc->phases.enter = i2c_sens_reset_enter;
    dc->vmsd = &vmstate_i2c_sens;
    isc->event = i2c_sens_event;
    isc->recv = i2c_sens_rx;
    isc->send = i2c_sens_tx;
}

static TypeInfo i2c_sens_info = {
    .name = TYPE_I2C_SENS,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(I2CSensor),
    .instance_init = i2c_sens_init,
    .class_init = i2c_sens_class_init
};

static void i2c_sens_register_devices(void)
{
    type_register_static(&i2c_sens_info);
}

type_init(i2c_sens_register_devices);
