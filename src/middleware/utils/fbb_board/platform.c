#include "driver/gpio.h"
#include "fbb/board_port.h"
#include "pinctrl.h"

int fbb_board_platform_gpio_prepare(int pin)
{
    if (uapi_pin_set_mode((pin_t)pin,
                          (pin_mode_t)HAL_PIO_FUNC_GPIO) != ERRCODE_SUCC ||
        uapi_gpio_set_dir((pin_t)pin,
                          GPIO_DIRECTION_OUTPUT) != ERRCODE_SUCC) {
        return -1;
    }
    return 0;
}

int fbb_board_platform_gpio_write(int pin, int high)
{
    gpio_level_t level = high ? GPIO_LEVEL_HIGH : GPIO_LEVEL_LOW;
    return uapi_gpio_set_val((pin_t)pin, level) == ERRCODE_SUCC ? 0 : -1;
}
