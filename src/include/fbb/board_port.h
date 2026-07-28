/*
 * Port contract implemented by each fbb-enabled SDK for the market-hosted
 * fbb-board runtime. Applications must use fbb/board.h instead.
 */
#ifndef FBB_BOARD_PORT_H
#define FBB_BOARD_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

int fbb_board_platform_gpio_prepare(int pin);
int fbb_board_platform_gpio_write(int pin, int high);

#ifdef __cplusplus
}
#endif
#endif
