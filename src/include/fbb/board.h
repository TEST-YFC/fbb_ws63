/*
 * Stable Board Binding API shared by fbb-enabled SDKs.
 * Physical pin and generated-table details remain private to the SDK.
 */
#ifndef FBB_BOARD_H
#define FBB_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FBB_BOARD_API_VERSION 0x00010000u

typedef enum {
    FBB_STATUS_OK = 0,
    FBB_STATUS_NOT_FOUND = -1,
    FBB_STATUS_INVALID_ARGUMENT = -2,
    FBB_STATUS_PLATFORM_ERROR = -3
} fbb_status_t;

typedef struct fbb_board_gpio fbb_board_gpio_t;

uint32_t fbb_board_api_version(void);
const fbb_board_gpio_t *fbb_board_gpio(const char *capability,
                                       const char *instance);
fbb_status_t fbb_gpio_write(const fbb_board_gpio_t *gpio, bool active);

#ifdef __cplusplus
}
#endif
#endif
