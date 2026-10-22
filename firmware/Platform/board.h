#ifndef BOARD_H
#define BOARD_H

/* Усе знання про плату NUCLEO-C031C6 в одному місці (docs/pinout.md §1, §2, §3.3).
 * Хендли і піни беремо з макросів CubeMX (main.h, adc.h, i2c.h, tim.h, usart.h),
 * тож після перейменування в .ioc змінюється лише цей файл. */

#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"

/* I2C1: PA9 = SCL (D8), PA10 = SDA (D2); MPU-6050 і PCF8574 на одній шині */
#define BOARD_I2C            (&hi2c1)
#define BOARD_I2C_REINIT     MX_I2C1_Init         /* повна переініціалізація після збою шини */

/* ADC1: два фоторезистори */
#define BOARD_ADC            (&hadc1)
#define BOARD_ADC_CH_GATE    ADC_CHANNEL_0        /* PA0, A0 (CN8-1) — LDR gate */
#define BOARD_ADC_CH_CLASS   ADC_CHANNEL_1        /* PA1, A1 (CN8-2) — LDR class */

/* TIM3: ШИМ серво SG92R */
#define BOARD_SERVO_TIM      (&htim3)
#define BOARD_SERVO_CH       TIM_CHANNEL_1        /* PA6, D12 (CN5-5) */

/* USART2: VCP ST-LINK, 115200 8N1 */
#define BOARD_UART           (&huart2)            /* PA2 = TX, PA3 = RX, VCP */

/* Службові маркери для логічного аналізатора */
#define BOARD_LED_MARK_PORT  LED_MARK_GPIO_Port   /* PA5, D13 (CN5-6), LD4, маркер FR-03, канал D4 */
#define BOARD_LED_MARK_PIN   LED_MARK_Pin
#define BOARD_ADC_MARK_PORT  ADC_MARK_GPIO_Port   /* PA15, D7 (CN9-8), маркер АЦП, канал D5 */
#define BOARD_ADC_MARK_PIN   ADC_MARK_Pin

/* Кнопка E-STOP: замикає на GND, pull-up, EXTI за спадом */
#define BOARD_ESTOP_PORT     ESTOP_GPIO_Port
#define BOARD_ESTOP_PIN      ESTOP_Pin            /* PC13, B1 */
#define BOARD_ESTOP_IRQ      EXTI4_15_IRQn        /* лінія EXTI13 */

/* 7-бітні адреси на I2C1 (pinout.md §2) */
#define BOARD_MPU_ADDR       0x68u                /* MPU-6050, AD0 = GND */
#define BOARD_LCD_ADDR       0x27u                /* PCF8574 LCD1602, A2..A0 = 111 */

/* Межі імпульсу серво; TIM3 тікає 1 МГц, тому CCR = мікросекунди */
#define BOARD_SERVO_MIN_US   1000u                /* 0°   — уточнюється вимірюванням (pinout.md §3.3) */
#define BOARD_SERVO_MAX_US   2000u                /* 180° */

#endif /* BOARD_H */
