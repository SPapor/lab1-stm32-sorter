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
#include "uart_hal.h"
#include "mpu6050.h"
#include "lcd1602.h"
#include "cli.h"
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
#define SERVO_SELFTEST_DEG  90u     /* середина ходу серво під час самоперевірки */
#define TELEMETRY_PERIOD_MS 1000u   /* період рядка accel, поки stream on */
#define CAP_NACK_ADDR       0x69u   /* MPU-6050 з AD0 = 1 — на шині немає, чекаємо NACK */

_Static_assert(CLI_SERVO_MAX_DEG == SERVO_HAL_MAX_DEG, "межа cap servo у cli.h != servo_hal.h");
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
static cli_t cli;
static uint8_t stream_on = 1;   /* періодичний рядок accel (команда stream) */

/* Текст команди help; синтаксис розбирає App/cli.c */
static const char *const help_lines[] = {
  "help                this list",
  "stream on|off       periodic 'accel' line (default on)",
  "cap whoami          MPU-6050: write 0x75, read 1 byte",
  "cap accel           MPU-6050: write 0x3B, read 6 bytes",
  "cap wr2             MPU-6050: 2-byte write PWR_MGMT_1 = 0x00",
  "cap nack            1-byte write to absent 0x69, NACK expected",
  "cap lcd             'CAP' on LCD row 1 (1-byte PCF8574 writes)",
  "cap servo <0..180>  set servo angle",
  "cap adc             one gate + one class conversion",
  "cap uart            send 0x55 and 'UART-TEST'",
};
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
static void telemetry_print(void);
static void console_echo(char ch);
static void cmd_run(const cli_cmd_t *cmd);
static void cap_begin(void);
static void cap_end(void);
static void cap_report(const char *name, const char *st, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static const char *bus_st_name(bus_status_t st);
static void cap_report_bus(const char *name, bus_status_t st);
static bus_status_t cap_mpu_read(uint8_t reg, uint8_t *buf, size_t len);
static void cap_whoami(void);
static void cap_accel(void);
static void cap_wr2(void);
static void cap_nack(void);
static void cap_lcd(void);
static void cap_servo(uint8_t deg);
static void cap_adc(void);
static void cap_uart(void);
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

  cli_init(&cli);
  printf("READY type 'help'\r\n");
  uint32_t t_telemetry = HAL_GetTick();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    int ch = uart_hal_getc();
    if (ch >= 0) {
      cli_cmd_t cmd;

      console_echo((char)ch);
      if (cli_feed(&cli, (char)ch, &cmd)) {
        if (ch != '\r' && ch != '\n')
          printf("\r\n");   /* переповнення: команда завершилась посеред рядка */
        cmd_run(&cmd);
      }
    }

    if (stream_on && HAL_GetTick() - t_telemetry >= TELEMETRY_PERIOD_MS) {
      t_telemetry = HAL_GetTick();
      telemetry_print();
    }
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

static void telemetry_print(void)
{
  mpu6050_accel_raw_t a = {0};
  mpu6050_status_t st_acc = mpu6050_read_accel_raw(&imu, &a);

  printf("accel st=%d raw %6d %6d %6d | mg %5ld %5ld %5ld | gate %4u class %4u\r\n",
         (int)st_acc, a.x, a.y, a.z,
         (long)mpu6050_raw_to_mg(a.x), (long)mpu6050_raw_to_mg(a.y),
         (long)mpu6050_raw_to_mg(a.z),
         adc_hal_read(BOARD_ADC_CH_GATE), adc_hal_read(BOARD_ADC_CH_CLASS));
}

/* Ехо введеного символу. Кінець рядка — завжди "\r\n", пара CR LF дає один перенос. */
static void console_echo(char ch)
{
  static char prev;

  if (ch == '\r' || ch == '\n') {
    if (!(ch == '\n' && prev == '\r'))
      printf("\r\n");
  } else if (ch == 0x08 || ch == 0x7F) {
    printf("\b \b");
  } else {
    putchar(ch);
  }
  prev = ch;
}

static void cmd_run(const cli_cmd_t *cmd)
{
  switch (cmd->id) {
  case CLI_HELP:
    for (size_t i = 0; i < sizeof help_lines / sizeof help_lines[0]; i++)
      printf("%s\r\n", help_lines[i]);
    break;
  case CLI_STREAM:
    stream_on = (uint8_t)cmd->arg;
    printf("%lu STREAM st=OK on=%u\r\n", (unsigned long)HAL_GetTick(), (unsigned)stream_on);
    break;
  case CLI_CAP_WHOAMI: cap_whoami(); break;
  case CLI_CAP_ACCEL:  cap_accel();  break;
  case CLI_CAP_WR2:    cap_wr2();    break;
  case CLI_CAP_NACK:   cap_nack();   break;
  case CLI_CAP_LCD:    cap_lcd();    break;
  case CLI_CAP_SERVO:  cap_servo((uint8_t)cmd->arg); break;
  case CLI_CAP_ADC:    cap_adc();    break;
  case CLI_CAP_UART:   cap_uart();   break;
  default:
    printf("%lu CLI st=BAD\r\n", (unsigned long)HAL_GetTick());
    break;
  }
}

/* Рамка однієї дії для логічного аналізатора: LED_MARK (PA5, канал D4) = 1 рівно
 * на час дії. HAL_UART_Transmit повертається лише після TC, тож ехо команди вже
 * вийшло з TX; I2C_RECOVER друкується тільки після спаду маркера. */
static void cap_begin(void)
{
  i2c_hal_defer_log(&i2c1_ctx, 1);
  HAL_GPIO_WritePin(BOARD_LED_MARK_PORT, BOARD_LED_MARK_PIN, GPIO_PIN_SET);
}

static void cap_end(void)
{
  HAL_GPIO_WritePin(BOARD_LED_MARK_PORT, BOARD_LED_MARK_PIN, GPIO_PIN_RESET);
  i2c_hal_defer_log(&i2c1_ctx, 0);
}

/* Рядок FR-10: "<ms> CAP <name> st=<OK|NACK|TIMEOUT|ERR> key=value..." */
static void cap_report(const char *name, const char *st, const char *fmt, ...)
{
  va_list ap;

  printf("%lu CAP %s st=%s ", (unsigned long)HAL_GetTick(), name, st);
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\r\n");
}

static const char *bus_st_name(bus_status_t st)
{
  switch (st) {
  case BUS_OK:          return "OK";
  case BUS_ERR_NACK:    return "NACK";
  case BUS_ERR_TIMEOUT: return "TIMEOUT";
  default:              return "ERR";
  }
}

/* Невдала транзакція: ISR зафіксовано в i2c_hal до відновлення шини */
static void cap_report_bus(const char *name, bus_status_t st)
{
  cap_report(name, bus_st_name(st), "isr=0x%08lX", (unsigned long)i2c1_ctx.last_isr);
}

/* Напряму через i2c_bus_t, без повторів драйвера: запис адреси регістра (1 байт),
 * STOP, читання len байтів — як у mpu6050.c, але рівно один раз. */
static bus_status_t cap_mpu_read(uint8_t reg, uint8_t *buf, size_t len)
{
  const i2c_bus_t *bus = &i2c1_bus;
  bus_status_t st = bus->write(bus->ctx, BOARD_MPU_ADDR, &reg, 1);

  if (st == BUS_OK)
    st = bus->read(bus->ctx, BOARD_MPU_ADDR, buf, len);
  return st;
}

static void cap_whoami(void)
{
  uint8_t val = 0;

  cap_begin();
  bus_status_t st = cap_mpu_read(MPU6050_REG_WHO_AM_I, &val, 1);
  cap_end();
  if (st == BUS_OK)
    cap_report("whoami", "OK", "val=0x%02X", val);
  else
    cap_report_bus("whoami", st);
}

static void cap_accel(void)
{
  uint8_t b[6] = {0};

  cap_begin();
  bus_status_t st = cap_mpu_read(MPU6050_REG_ACCEL_XOUT_H, b, sizeof b);
  cap_end();
  if (st == BUS_OK)
    cap_report("accel", "OK", "raw=%02X %02X %02X %02X %02X %02X",
               b[0], b[1], b[2], b[3], b[4], b[5]);
  else
    cap_report_bus("accel", st);
}

/* 2-байтовий запис, що у Wokwi C0 обривається TIMEOUT (docs/journal.md, проблема 8) */
static void cap_wr2(void)
{
  const uint8_t b[2] = { MPU6050_REG_PWR_MGMT_1, 0x00u };
  const i2c_bus_t *bus = &i2c1_bus;

  cap_begin();
  bus_status_t st = bus->write(bus->ctx, BOARD_MPU_ADDR, b, sizeof b);
  cap_end();
  if (st == BUS_OK)
    cap_report("wr2", "OK", "reg=0x%02X val=0x%02X", b[0], b[1]);
  else
    cap_report_bus("wr2", st);
}

static void cap_nack(void)
{
  const uint8_t b = 0x00u;
  const i2c_bus_t *bus = &i2c1_bus;

  cap_begin();
  bus_status_t st = bus->write(bus->ctx, CAP_NACK_ADDR, &b, 1);
  cap_end();
  cap_report("nack", bus_st_name(st), "addr=0x%02X", CAP_NACK_ADDR);
}

static void cap_lcd(void)
{
  if (!lcd_ready) {
    cap_report("lcd", "ERR", "ready=0");
    return;
  }

  cap_begin();
  lcd_status_t ls = lcd1602_set_cursor(&lcd, 0, 1);
  if (ls == LCD_OK)
    ls = lcd1602_print(&lcd, "CAP");
  cap_end();
  if (ls == LCD_OK)
    cap_report("lcd", "OK", "row=1 text=CAP");
  else
    cap_report("lcd", "ERR", "isr=0x%08lX", (unsigned long)i2c1_ctx.last_isr);
}

/* Новий CCR набуває чинності з наступного періоду ШИМ (preload увімкнено HAL) */
static void cap_servo(uint8_t deg)
{
  cap_begin();
  servo_hal_set_angle(deg);
  uint16_t pulse = servo_hal_get_pulse_us();
  cap_end();
  cap_report("servo", "OK", "deg=%u pulse_us=%u", (unsigned)deg, (unsigned)pulse);
}

static void cap_adc(void)
{
  cap_begin();
  uint16_t gate = adc_hal_read(BOARD_ADC_CH_GATE);
  uint16_t cls = adc_hal_read(BOARD_ADC_CH_CLASS);
  cap_end();
  cap_report("adc", "OK", "gate=%u class=%u", (unsigned)gate, (unsigned)cls);
}

/* 0x55 разом зі старт- і стоп-бітом (LSB першим) дає на лінії меандр 0101010101 —
 * зручно міряти тривалість біта; далі рядок для розбору символів. */
static void cap_uart(void)
{
  static const uint8_t msg[] = "\x55" "UART-TEST";

  cap_begin();
  HAL_StatusTypeDef s = HAL_UART_Transmit(BOARD_UART, msg, sizeof msg - 1, HAL_MAX_DELAY);
  cap_end();
  printf("\r\n");
  cap_report("uart", s == HAL_OK ? "OK" : "ERR", "bytes=%u", (unsigned)(sizeof msg - 1));
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
