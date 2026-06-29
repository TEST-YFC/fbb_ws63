/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file wiring.cpp
 * @brief Time functions implementation for Arduino compatibility layer
 * @version 3.0
 * @date 2026-04-21
 */

#include "Arduino.h"
#include "los_tick.h"
#include "los_task.h"
#include "hal_timer.h"
#include "los_config.h"
#include "systick.h"
#include "chip_io.h"
#include "driver/security_unified/trng.h"
#include <stdlib.h>

#if (CHIP_WS63 == 1)
#define SYS_CPU_FREQ_HZ (160 * 1000000UL)
#else
#define SYS_CPU_FREQ_HZ (GET_SYS_CLOCK())
#endif

/* *
 * @brief Get milliseconds since system startup
 * @return Unsigned long - milliseconds
 */
unsigned long millis()
{
    return LOS_Tick2MS(LOS_TickCountGet());
}

/* *
 * @brief Get microseconds since system startup
 * @return Unsigned long - microseconds
 */
unsigned long micros()
{
    return uapi_systick_get_us();
}

/* *
 * @brief Delay for specified milliseconds
 * @param ms - Milliseconds to delay
 */
void delay(unsigned long ms)
{
    if (ms == 0) {
        return;
    }

    LOS_TaskDelay(LOS_MS2Tick(ms));
}

/* *
 * @brief Delay for specified microseconds
 * @param us - Microseconds to delay
 */
void delayMicroseconds(unsigned int us)
{
    if (us == 0) {
        return;
    }

    uapi_systick_delay_us(us);
}

/* *
 * @brief Yield function for cooperative multitasking
 */
void yield(void)
{
    // In LiteOS, this can trigger a task switch if needed
    // For now, it's a no-op as LiteOS handles scheduling automatically
}

/* *
 * @brief Map a value from one range to another
 * @param value - Value to map
 * @param fromLow/fromHigh - Source range
 * @param toLow/toHigh - Target range
 * @return Mapped value (integer arithmetic, truncated)
 */
long map(long value, long fromLow, long fromHigh, long toLow, long toHigh)
{
    if (fromHigh == fromLow) {
        return toLow;  // Guard against divide-by-zero
    }
    return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}

/* *
 * @brief Seed the pseudo-random number generator
 * @param seed - Seed value (0 is ignored per Arduino spec)
 */
void randomSeed(unsigned long seed)
{
    if (seed != 0) {
        srand((unsigned int)seed);
    }
}

/* *
 * @brief Generate a random number using hardware TRNG
 * @param max - Exclusive upper bound
 * @return Random value in [0, max)
 */
long random(long max)
{
    if (max <= 0) {
        return 0;
    }
    uint32_t rnd = 0;
    if (uapi_drv_cipher_trng_get_random(&rnd) == ERRCODE_SUCC) {
        return (long)(rnd % (unsigned int)max);
    }
    // Fallback to rand() if TRNG fails
    return (long)(rand() % (unsigned int)max);
}

/* *
 * @brief Generate a random number in a range using hardware TRNG
 * @param min - Inclusive lower bound
 * @param max - Exclusive upper bound
 * @return Random value in [min, max)
 */
long random(long min, long max)
{
    if (min >= max) {
        return min;
    }
    unsigned long range = (unsigned long)(max - min);
    uint32_t rnd = 0;
    if (uapi_drv_cipher_trng_get_random(&rnd) == ERRCODE_SUCC) {
        return (long)(rnd % range) + min;
    }
    // Fallback to rand() if TRNG fails
    return (long)(rand() % range) + min;
}

/* *
 * @brief Construct a 16-bit word from a single value
 */
unsigned int makeWord(unsigned int w)
{
    return w;
}

/* *
 * @brief Construct a 16-bit word from high and low bytes
 */
unsigned int makeWord(unsigned char h, unsigned char l)
{
    return ((unsigned int)h << 8) | l;
}
