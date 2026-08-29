#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/gpio/allwinner-gpio.h"
#include "hw/gpio/allwinner-gpio-regs.h"

extern void allwinner_gpio_set(void *opaque, int port, int line, int level);
void qmp_x_gpio_set(const char *id, int64_t port, int64_t pin, bool level, Error **errp);


void qmp_x_gpio_set(const char *id, int64_t port, int64_t pin, bool level, Error **errp)
{
    Object *obj = object_resolve_path(id, NULL);
    if (!obj) {
        printf("Device '%s' not found", id);
        return;
    }

    /* Safely check type without triggering an abort on type mismatch */
    AWGPIOState *s = (AWGPIOState *)object_dynamic_cast(obj, TYPE_AW_GPIO);
    if (!s) {
        printf("Object at '%s' is not an Allwinner GPIO device", id);
        return;
    }   // OBJECT_CHECK cast; error if not allwinner.gpio
    if (port < 0 || port >= AW_GPIO_PORTS_NUM ||
        pin  < 0 || pin  >= AW_PINS_PER_PORT[port]) {
        error_setg(errp, "pin out of range"); return;
    }
    allwinner_gpio_set(s, port, pin, level);  // drive input line
}
