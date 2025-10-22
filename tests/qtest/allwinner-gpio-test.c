#include "qemu/osdep.h"

#include "qemu/bitops.h"

#include "libqtest-single.h"

// add commands to index gpios, select PH13 or similar
// reuse the map structure
#define AW_A10_GPIO_BASE        0x01c20800

typedef struct AWPortMap {
    uint32_t cfg[4];
    uint32_t dat;
    uint32_t drv[2];
    uint32_t pul[2];
} AWPortMap;

typedef struct AWPortsOverlay {
    AWPortMap ports[AW_GPIO_PORTS_NUM];
} AWPortsOverlay;

static void test_reset_values(const void *data) {

}

// Set pin to output, check that it propagates
// static void test_set_output_pins(const void *data) {}

// Set pin to input, check that the value is written but it does not propagate
// static void test_set_output_pins(const void *data) {}

// Make variations for different IRQ trigger strategies
static void test_set_input_pins(const void *data) {
    /*
     * Test that setting a line high/low externally sets the
     * corresponding GPIO line high/low : it should set the
     * right bit in IDR and send an irq to syscfg.
     */
    // unsigned int pin = test_pin(data);
    // uint32_t gpio = test_gpio_addr(data);
    // unsigned int gpio_id = get_gpio_id(gpio);

    qtest_irq_intercept_in(global_qtest, "/machine/soc");

    /* Configure a line as input, raise it, and check that the pin is high */
    // gpio_set_2bits(gpio, MODER, pin, MODER_INPUT);
    // gpio_set_irq(gpio, pin, 1);
    // g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) | (1 << pin));
    // g_assert_true(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Lower the line and check that the pin is low */
    // gpio_set_irq(gpio, pin, 0);
    // g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));
    // g_assert_false(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Clean the test */
    // gpio_writel(gpio, MODER, reset(gpio, MODER));
    // disconnect_all_pins(gpio);
    // g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR));
}

int main(int argc, char **argv) {
  QTestState *s;
  int r;

  g_test_init(&argc, &argv, NULL);

  s = qtest_init("-machine cubieboard");
  qtest_add_data_func("/allwinner-cubieboard/gpio/set_input_pins", s,
                      test_set_input_pins);
  r = g_test_run();
  qtest_quit(s);

  return r;
}
