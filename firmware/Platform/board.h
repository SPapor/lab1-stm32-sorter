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

/* Службові піни вимірювань ЛР3 (методичка 5.3, docs/perf.md). Не з CubeMX:
 * їх налаштовує perf_hal_init, щоб не перегенеровувати проєкт
 * (pinout.md §1, примітка; docs/journal.md, проблема 12) */
#define BOARD_PROBE_PORT         GPIOB
#define BOARD_PROBE_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#define BOARD_PROBE_TASK_PIN     GPIO_PIN_13      /* PB13, CN10-30 (morpho), канал D6: працює будь-яка задача */
#define BOARD_PROBE_LCD_PIN      GPIO_PIN_14      /* PB14, CN10-28 (morpho), канал D7: працює lcd_task */
/* Запис у BSRR — одна інструкція str: біти 0..15 ставлять 1, біти 16..31 скидають у 0 */
#define BOARD_PROBE_ON(pin)      (BOARD_PROBE_PORT->BSRR = (pin))
#define BOARD_PROBE_OFF(pin)     (BOARD_PROBE_PORT->BSRR = (uint32_t)(pin) << 16)

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
