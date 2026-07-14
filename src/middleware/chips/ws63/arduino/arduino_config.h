/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 */
/* *
 * @file arduino_config.h
 * @brief ws63 chip configuration for the Arduino layer (chip porting layer).
 *
 * Decides which Arduino features the ws63 build enables. Arduino.h includes this
 * (via middleware/chips/${CHIP}/arduino/ on the include path) instead of deriving
 * the config inline, so the chip-agnostic Arduino.h stays portable. Another chip
 * supplies its own copy at middleware/chips/<chip>/arduino/arduino_config.h.
 */

#ifndef ARDUINO_CONFIG_H
#define ARDUINO_CONFIG_H

/* ============================================================================
 * Hardware serial ports available (Arduino standard feature macros).
 * ws63 exposes UART0/1/2. (HardwareSerial.cpp gates Serial1/2 on the SDK's
 * UART_BUS_MAX_NUMBER in platform_core.h; these macros are informational.)
 * ============================================================================ */
#define HAVE_HWSERIAL0
#define HAVE_HWSERIAL1
#define HAVE_HWSERIAL2

/* ============================================================================
 * ADC / PWM / GPIO support — derived from ws63 menuconfig (CONFIG_*_USING_V*).
 * These gate the hardware code paths in wiring_analog.cpp / wiring_pulse.cpp /
 * Servo.cpp. Off => the API degrades to a stub / GPIO fallback.
 * ============================================================================ */
#if !defined(CONFIG_PWM_SUPPORT) && defined(CONFIG_PWM_USING_V151)
#define CONFIG_PWM_SUPPORT 1
#endif

#if !defined(CONFIG_ADC_SUPPORT) && defined(CONFIG_ADC_USING_V154)
#define CONFIG_ADC_SUPPORT 1
#endif

#if !defined(CONFIG_GPIO_SUPPORT)
#define CONFIG_GPIO_SUPPORT 1
#endif

/* ============================================================================
 * Interrupt-capable pins. ws63: pins 0-15 are interrupt-capable; others return
 * NOT_AN_INTERRUPT. Used by wiring_interrupts.cpp and digitalPinToInterrupt().
 * ============================================================================ */
#define MAX_INTERRUPT_PINS 16

/* ============================================================================
 * UART (HardwareSerial) TX/RX support. Always on for ws63.
 * ============================================================================ */
#if !defined(CONFIG_UART_SUPPORT_TX)
#define CONFIG_UART_SUPPORT_TX 1
#endif
#if !defined(CONFIG_UART_SUPPORT_RX)
#define CONFIG_UART_SUPPORT_RX 1
#endif

/* ============================================================================
 * I2C master support (Wire). Default on for ws63.
 * ============================================================================ */
#if !defined(CONFIG_I2C_SUPPORT_MASTER)
#define CONFIG_I2C_SUPPORT_MASTER 1
#endif

/* ============================================================================
 * I2S/SIO support (I2SClass). CONFIG_I2S_SUPPORT is not a real SDK Kconfig;
 * derive it from the ws63 SIO driver version so the I2S implementation compiles
 * instead of an empty stub.
 * ============================================================================ */
#if !defined(CONFIG_I2S_SUPPORT) && defined(CONFIG_SIO_USING_V151)
#define CONFIG_I2S_SUPPORT 1
#endif

/* ============================================================================
 * SPI master pin assignment (SPI.cpp). ws63 default pinmux on SPI0:
 *   DI(MISO)=GPIO11  DO(MOSI)=GPIO09  CLK=GPIO07  CS=GPIO10  pinmux mode=3
 * (Physical pin numbers; see SPI.cpp / ws63 IO复用关系表.)
 * ============================================================================ */
#if !defined(CONFIG_SPI_DI_MASTER_PIN)
#define CONFIG_SPI_DI_MASTER_PIN 11
#endif
#if !defined(CONFIG_SPI_DO_MASTER_PIN)
#define CONFIG_SPI_DO_MASTER_PIN 9
#endif
#if !defined(CONFIG_SPI_CLK_MASTER_PIN)
#define CONFIG_SPI_CLK_MASTER_PIN 7
#endif
#if !defined(CONFIG_SPI_CS_MASTER_PIN)
#define CONFIG_SPI_CS_MASTER_PIN 10
#endif
#if !defined(CONFIG_SPI_MASTER_PIN_MODE)
#define CONFIG_SPI_MASTER_PIN_MODE 3
#endif

/* ============================================================================
 * Timer (TimerClass). IRQ priority + which timer index Arduino reserves.
 * ARDUINO_TIMER_RESERVED expands to TIMER_INDEX_0 (enum from timer_porting.h,
 * resolved at point of use in TimerClass.cpp after timer_porting.h is included).
 * ============================================================================ */
#define ARDUINO_TIMER_IRQ_PRIORITY 1
#if !defined(ARDUINO_TIMER_RESERVED)
#define ARDUINO_TIMER_RESERVED TIMER_INDEX_0
#endif

#endif /* ARDUINO_CONFIG_H */
