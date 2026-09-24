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
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MPU_ADDR   0x68
#define REG_WHO    0x75
#define REG_PWR1   0x6B
#define REG_ACCEL  0x3B
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void i2c_scan(void);
static uint16_t adc_read(uint32_t channel);
static HAL_StatusTypeDef mpu_read(uint8_t reg, uint8_t *buf, uint16_t len);
static HAL_StatusTypeDef mpu_write(uint8_t reg, uint8_t val);
static void i2c_recover(const char *where);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int _write(int fd, char *p, int n)
{
  (void)fd;
  HAL_UART_Transmit(&huart2, (uint8_t *)p, n, HAL_MAX_DELAY);
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
  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 1500);

  i2c_scan();

  uint8_t who = 0;
  if (mpu_read(REG_WHO, &who, 1) == HAL_OK)
    printf("WHO_AM_I = 0x%02X\r\n", who);
  else
    printf("WHO_AM_I read FAILED\r\n");

  /* PWR_MGMT_1 = 0x00: clear SLEEP (bit 6, set after reset) */
  HAL_StatusTypeDef st = mpu_write(REG_PWR1, 0x00);
  HAL_Delay(100);
  uint8_t pwr1 = 0xFF;
  HAL_StatusTypeDef st_rb = mpu_read(REG_PWR1, &pwr1, 1);
  printf("PWR_MGMT_1 write st=%d, readback st=%d val=0x%02X%s\r\n",
         st, st_rb, pwr1, (pwr1 & 0x40) ? " (SLEEP!)" : "");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_Delay(1000);

    uint8_t raw[6] = {0};
    HAL_StatusTypeDef st_acc = mpu_read(REG_ACCEL, raw, 6);
    printf("0x3B st=%d raw %02X %02X %02X %02X %02X %02X\r\n",
           st_acc, raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);

    int16_t ax = (int16_t)(raw[0] << 8 | raw[1]);
    int16_t ay = (int16_t)(raw[2] << 8 | raw[3]);
    int16_t az = (int16_t)(raw[4] << 8 | raw[5]);

    printf("accel %6d %6d %6d | gate %4u class %4u\r\n",
          ax, ay, az, adc_read(ADC_CHANNEL_0), adc_read(ADC_CHANNEL_1));
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
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
    if (HAL_I2C_IsDeviceReady(&hi2c1, a << 1, 2, 10) == HAL_OK)
      printf("  found 0x%02X\r\n", a);
}

static uint16_t adc_read(uint32_t channel)
{
  /* ScanConvMode = ADC_SCAN_SEQ_FIXED: ConfigChannel only ORs the channel into
   * CHSELR (MX_ADC1_Init already enabled CH0|CH1), so explicitly drop every
   * other channel with ADC_RANK_NONE to leave exactly one in the sequence. */
  static const uint32_t used[] = { ADC_CHANNEL_0, ADC_CHANNEL_1 };
  ADC_ChannelConfTypeDef c = {0};
  c.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
  for (unsigned i = 0; i < sizeof used / sizeof used[0]; i++) {
    c.Channel = used[i];
    c.Rank = (used[i] == channel) ? ADC_RANK_CHANNEL_NUMBER : ADC_RANK_NONE;
    HAL_ADC_ConfigChannel(&hadc1, &c);
  }
  HAL_ADC_Start(&hadc1);
  HAL_ADC_PollForConversion(&hadc1, 10);
  uint16_t v = HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return v;
}

/* Wokwi C0 I2C model: HAL never sees TXIS for the 2nd byte of a master
 * write (SCL stays low after it, err=TIMEOUT, BUSY stuck). 1-byte writes
 * (TXDR preloaded) and reads work, so no Mem_*; reinit I2C after a failure. */
static void i2c_recover(const char *where)
{
  printf("  I2C fail @%s: err=0x%02lX ISR=0x%08lX\r\n",
         where, hi2c1.ErrorCode, hi2c1.Instance->ISR);
  HAL_I2C_DeInit(&hi2c1);
  MX_I2C1_Init();
}

static HAL_StatusTypeDef mpu_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
  HAL_StatusTypeDef s = HAL_I2C_Master_Transmit(&hi2c1, MPU_ADDR << 1, &reg, 1, 100);
  if (s != HAL_OK) { i2c_recover("rd-tx"); return s; }
  s = HAL_I2C_Master_Receive(&hi2c1, MPU_ADDR << 1, buf, len, 100);
  if (s != HAL_OK) i2c_recover("rd-rx");
  return s;
}

static HAL_StatusTypeDef mpu_write(uint8_t reg, uint8_t val)
{
  uint8_t b[2] = { reg, val };
  HAL_StatusTypeDef s = HAL_I2C_Master_Transmit(&hi2c1, MPU_ADDR << 1, b, 2, 100);
  if (s != HAL_OK) i2c_recover("wr");
  return s;
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
