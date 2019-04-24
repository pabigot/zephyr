/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __INC_BOARD_H
#define __INC_BOARD_H

/* GPIO and ADC channel selectors for external connectors on
 * Feather-based Particle mesh boards.
 *
 * NOTE: This file is replicated in particle_{argon,boron,xenon}.
 * Changes should be made in all instances. */
#define EXT_P0_GPIO_PIN 26	   /* P0 SDA */
#define EXT_P1_GPIO_PIN 27	   /* P1 SCL */
#define EXT_P2_GPIO_PIN (32 + 1)   /* P2 SDA1 UART1_RTS SPI1_SCK PWM3 */
#define EXT_P3_GPIO_PIN (32 + 2)   /* P3 SCL1 UART1_CTS SPI1_MOSI PWM3 */
#define EXT_P4_GPIO_PIN (32 + 8)   /* P4 UART2_TX SPI1_MISO PWM1 */
#define EXT_P5_GPIO_PIN (32 + 10)  /* P5 UART2_RX PWM1 */
#define EXT_P6_GPIO_PIN (32 + 11)  /* P6 UART2_CTS PWM1 */
#define EXT_P7_GPIO_PIN (32 + 12)  /* P7 PWM0 (Blue User LED) */
#define EXT_P8_GPIO_PIN (32 + 3)   /* P8 UART2_RTS PWM1 */
#define EXT_P9_GPIO_PIN 6	   /* P9 UART1_RX */
#define EXT_P10_GPIO_PIN 8	   /* P10 UART1_TX */
#define EXT_P11_GPIO_PIN (32 + 14) /* P11 SPI_MISO */
#define EXT_P12_GPIO_PIN (32 + 13) /* P12 SPI_MOSI */
#define EXT_P13_GPIO_PIN (32 + 15) /* P13 SPI_SCK */
#define EXT_P14_GPIO_PIN 31	   /* P14 ADC5 SPI_SS PWM3 */
#define EXT_P14_ADC_CHANNEL 7	   /* ADC5 = AIN7 */
#define EXT_P15_GPIO_PIN 30	   /* P15 ADC4 PWM3 */
#define EXT_P15_ADC_CHANNEL 6	   /* ADC4 = AIN6 */
#define EXT_P16_GPIO_PIN 29	   /* P16 ADC3 PWM2 */
#define EXT_P16_ADC_CHANNEL 5	   /* ADC3 = AIN5 */
#define EXT_P17_GPIO_PIN 28	   /* P17 ADC2 PWM2 */
#define EXT_P17_ADC_CHANNEL 4	   /* ADC2 = AIN4 */
#define EXT_P18_GPIO_PIN 4	   /* P18 ADC1 PWM2 */
#define EXT_P18_ADC_CHANNEL 1	   /* ADC1 = AIN2 */
#define EXT_P19_GPIO_PIN 3	   /* P19 ADC0 PWM2 */
#define EXT_P19_ADC_CHANNEL 1	   /* ADC1 = AIN1 */
#define EXT_P20_GPIO_PIN 11	   /* P20 MODEn */
#define EXT_P21_GPIO_PIN 18	   /* P21 RESETn */

#endif /* __INC_BOARD_H */
