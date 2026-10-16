/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdarg.h>
#include <stdio.h>
#include "board.h"
#include "platform.h"
#include "adc_hal.h"
#include "servo_hal.h"
#include "i2c_hal.h"
#include "mpu6050.h"
#include "lcd1602.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Результат самоперевірки одного пристрою (рядок FR-10) */
typedef enum { ST_OK = 0, ST_WARN, ST_FAIL } selftest_status_t;

typedef struct {
  unsigned ok, warn, fail;
} selftest_counts_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SERVO_SELFTEST_DEG 90u   /* середина ходу серво під час самоперевірки */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static i2c_hal_ctx_t i2c1_ctx;
static i2c_bus_t i2c1_bus;
static mpu6050_t imu;
static lcd1602_t lcd;
static uint8_t lcd_ready;   /* LCD пройшов probe та init */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void i2c_scan(void);
static selftest_counts_t selftest_run(void);
static void selftest_report(selftest_counts_t *cnt, const char *dev, selftest_status_t st,
                            const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void selftest_ldr(selftest_counts_t *cnt, const char *dev, uint32_t channel);
static const char *mpu_err_name(mpu6050_status_t st);
static void lcd_show_selftest(const selftest_counts_t *cnt);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int _write(int fd, char *p, int n)
{
  (void)fd;
  HAL_UART_Transmit(BOARD_UART, (uint8_t *)p, n, HAL_MAX_DELAY);
  return n;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("hello from STM32C031\r\n");
  HAL_ADCEx_Calibration_Start(BOARD_ADC);
  servo_hal_start();

  i2c_scan();

  i2c_hal_bus_init(&i2c1_bus, &i2c1_ctx, BOARD_I2C, BOARD_I2C_REINIT);
  selftest_counts_t st = selftest_run();
  lcd_show_selftest(&st);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_Delay(1000);

    mpu6050_accel_raw_t a = {0};
    mpu6050_status_t st_acc = mpu6050_read_accel_raw(&imu, &a);
    printf("accel st=%d raw %6d %6d %6d | mg %5ld %5ld %5ld | gate %4u class %4u\r\n",
           (int)st_acc, a.x, a.y, a.z,
           (long)mpu6050_raw_to_mg(a.x), (long)mpu6050_raw_to_mg(a.y),
           (long)mpu6050_raw_to_mg(a.z),
           adc_hal_read(BOARD_ADC_CH_GATE), adc_hal_read(BOARD_ADC_CH_CLASS));
    HAL_GPIO_TogglePin(BOARD_LED_MARK_PORT, BOARD_LED_MARK_PIN);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
static void i2c_scan(void)
{
  printf("I2C scan:\r\n");
  for (uint8_t a = 0x08; a < 0x78; a++)
    if (HAL_I2C_IsDeviceReady(BOARD_I2C, a << 1, 2, 10) == HAL_OK)
      printf("  found 0x%02X\r\n", a);
}

/* Рядок FR-10: "<ms> SELFTEST <пристрій> status=<OK|WARN|FAIL> key=value..." */
static void selftest_report(selftest_counts_t *cnt, const char *dev, selftest_status_t st,
                            const char *fmt, ...)
{
  static const char *const name[] = { "OK", "WARN", "FAIL" };
  va_list ap;

  printf("%lu SELFTEST %s status=%s ", (unsigned long)HAL_GetTick(), dev, name[st]);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\r\n");

  if (st == ST_OK)
    cnt->ok++;
  else if (st == ST_WARN)
    cnt->warn++;
  else
    cnt->fail++;
}

/* LDR: код, що прилип до 0 чи повної шкали, — обрив, замикання або засвітка */
static void selftest_ldr(selftest_counts_t *cnt, const char *dev, uint32_t channel)
{
  uint16_t raw = adc_hal_read(channel);
  selftest_status_t st = (raw > 0u && raw < ADC_HAL_MAX_RAW) ? ST_OK : ST_WARN;

  selftest_report(cnt, dev, st, "raw=%u", (unsigned)raw);
}

static const char *mpu_err_name(mpu6050_status_t st)
{
  switch (st) {
  case MPU6050_ERR_BUS:   return "BUS";
  case MPU6050_ERR_ID:    return "ID";
  case MPU6050_ERR_SLEEP: return "SLEEP";
  default:                return "NONE";
  }
}

/* Самоперевірка при старті; рядок summary — завжди останній */
static selftest_counts_t selftest_run(void)
{
  selftest_counts_t cnt = {0};

  /* MPU-6050: WARN, якщо WHO_AM_I вірний, але SLEEP не знявся —
   * відома поведінка Wokwi (docs/journal.md, проблема 8) */
  mpu6050_status_t ms = mpu6050_init(&imu, &i2c1_bus, BOARD_MPU_ADDR);
  if (ms == MPU6050_OK)
    selftest_report(&cnt, "mpu6050", ST_OK, "who=0x%02X tries=%u",
                    imu.who_am_i, (unsigned)imu.init_tries);
  else
    selftest_report(&cnt, "mpu6050", ms == MPU6050_ERR_SLEEP ? ST_WARN : ST_FAIL,
                    "who=0x%02X tries=%u err=%s",
                    imu.who_am_i, (unsigned)imu.init_tries, mpu_err_name(ms));

  /* LCD1602: ACK + зворотне читання порту PCF8574, потім ініціалізація HD44780 */
  uint8_t rb = 0;
  lcd_status_t ls = lcd1602_probe(&i2c1_bus, BOARD_LCD_ADDR, &rb);
  if (ls == LCD_OK)
    ls = lcd1602_init(&lcd, &i2c1_bus, BOARD_LCD_ADDR, platform_delay_ms);
  lcd_ready = (ls == LCD_OK);
  if (lcd_ready)
    selftest_report(&cnt, "lcd1602", ST_OK, "addr=0x%02X rb=0x%02X", BOARD_LCD_ADDR, rb);
  else
    selftest_report(&cnt, "lcd1602", ST_FAIL, "addr=0x%02X err=BUS", BOARD_LCD_ADDR);

  selftest_ldr(&cnt, "ldr_gate", BOARD_ADC_CH_GATE);
  selftest_ldr(&cnt, "ldr_class", BOARD_ADC_CH_CLASS);

  /* Серво: ставимо 90° і читаємо CCR назад */
  servo_hal_set_angle(SERVO_SELFTEST_DEG);
  uint16_t pulse = servo_hal_get_pulse_us();
  selftest_status_t ss = (pulse >= BOARD_SERVO_MIN_US && pulse <= BOARD_SERVO_MAX_US) ? ST_OK : ST_FAIL;
  selftest_report(&cnt, "servo", ss, "pulse_us=%u", (unsigned)pulse);

  printf("%lu SELFTEST summary ok=%u warn=%u fail=%u\r\n",
         (unsigned long)HAL_GetTick(), cnt.ok, cnt.warn, cnt.fail);
  return cnt;
}

static void lcd_show_selftest(const selftest_counts_t *cnt)
{
  char line[LCD1602_COLS + 1];

  if (!lcd_ready)
    return;
  snprintf(line, sizeof line, "ST ok=%u w=%u f=%u", cnt->ok, cnt->warn, cnt->fail);
  (void)lcd1602_set_cursor(&lcd, 0, 0);
  (void)lcd1602_print(&lcd, "Warehouse Sorter");
  (void)lcd1602_set_cursor(&lcd, 0, 1);
  (void)lcd1602_print(&lcd, line);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
