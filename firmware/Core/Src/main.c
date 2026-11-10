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
#include "period.h"
#include "gate.h"
#include "classify.h"
#include "impact.h"
#include "sorter.h"
#include "health.h"
#if PERF
#include "perf.h"
#include "perf_hal.h"
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Підсумок самоперевірки при старті (FR-08) */
typedef struct {
  unsigned ok, warn, fail;
} selftest_counts_t;

/* Номери задач super loop для вимірювань (docs/perf.md) */
enum {
  PT_UART = 0,
  PT_ESTOP,
  PT_GATE,
  PT_SORT,
  PT_IMU,
  PT_LCD,
#ifdef LAB2_CAP
  PT_TELEMETRY,
#endif
  PT_COUNT
};
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Super loop: періоди задач — усі тут (docs/tasks.md). Кожна задача сама
 * перевіряє HAL_GetTick і повертає керування, якщо її час не настав. */
#define GATE_PERIOD_MS      5u      /* gate_task: відлік АЦП gate, FR-01 */
#define IMU_PERIOD_MS       10u     /* imu_task: прискорення MPU-6050, FR-04 */
#define LCD_PERIOD_MS       500u    /* lcd_task: стан і лічильники на LCD1602, FR-06 */
#define ESTOP_DEBOUNCE_MS   20u     /* estop_task: EXTI кнопки вимкнено на цей час після першого фронту */

#define PERF_WIN_MS         1000u   /* вікно завантаження CPU, docs/perf.md */

#define FW_NAME             "lab3-superloop"
#define SERVO_SELFTEST_DEG  90u     /* середина ходу серво під час самоперевірки */

#ifdef TEST_I2C_FAULT
/* PRD, тест FR-09: make -f app.mk TEST_I2C_FAULT=1 */
#define I2C_FAULT_ADDR      0x69u   /* MPU-6050 з AD0 = 1 — на шині немає */
#define I2C_FAULT_PERIOD_MS 1000u   /* як часто imu_task звертається до I2C_FAULT_ADDR */
#endif

/* Вимірювальний harness ЛР2 (команди cap …, I2C-скан, телеметрія stream) —
 * лише у збірці make -f app.mk LAB2_CAP=1 */
#ifdef LAB2_CAP
#define TELEMETRY_PERIOD_MS 1000u   /* період рядка accel, поки stream on */
#define CAP_NACK_ADDR       0x69u   /* MPU-6050 з AD0 = 1 — на шині немає, чекаємо NACK */
#define CAP_SERVO_HOLD_MS   45u     /* LED_MARK після запису CCR: > 2 періоди ШИМ по 20 мс */
#define CAP_LCD_TEXT        "CAP             "   /* 16 символів: стирає старий рядок */

_Static_assert(CLI_SERVO_MAX_DEG == SERVO_HAL_MAX_DEG, "межа cap servo у cli.h != servo_hal.h");
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* Обгортка роботи задачі (методичка 5.1, приклад 1): такти до й після, джитер
 * старту періодичної задачі, завантаження. Викликається лише тоді, коли задача
 * справді працює, а не тільки перевіряє свій час. PERF=0 — просто виклик. */
#if PERF
#define TASK_RUN(id, call) \
  do { uint32_t t0_ = perf_begin(id); call; perf_end((id), t0_); } while (0)
#else
#define TASK_RUN(id, call) call
#endif
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#if PERF
/* Похибка методу (docs/perf.md): вартість самого виміру, тактів */
static uint32_t perf_now_cyc;    /* порожній інтервал між двома perf_now() */
static uint32_t perf_pair_cyc;   /* t0 = perf_now(); perf_add(&s, perf_now() - t0) */

static uint32_t cyc_per_us;
static perf_load_t load;         /* LOAD раз на PERF_WIN_MS */

/* STAT для кожної задачі — лише запуски з роботою */
static perf_stat_t st_task[PT_COUNT] = {
  [PT_UART]  = { .name = "uart_rx" },
  [PT_ESTOP] = { .name = "estop" },
  [PT_GATE]  = { .name = "gate" },
  [PT_SORT]  = { .name = "sort" },
  [PT_IMU]   = { .name = "imu" },
  [PT_LCD]   = { .name = "lcd" },
#ifdef LAB2_CAP
  [PT_TELEMETRY] = { .name = "telemetry" },
#endif
};
static perf_stat_t st_loop = { .name = "loop" };              /* повний прохід циклу */
static perf_stat_t st_i2c = { .name = "i2c_xfer" };           /* виклик шини, з повторами FR-09 */
static perf_stat_t st_uart_line = { .name = "uart_line" };    /* один рядок журналу в UART */
static perf_stat_t st_lcd_update = { .name = "lcd_update" };  /* обидва рядки LCD */

/* Джитер, пропуски й гістограма періодичних задач */
static perf_period_t per_gate, per_imu, per_lcd;
#ifdef LAB2_CAP
static perf_period_t per_tele;
#endif
static perf_period_t *const task_period[PT_COUNT] = {
  [PT_GATE] = &per_gate,
  [PT_IMU]  = &per_imu,
  [PT_LCD]  = &per_lcd,
#ifdef LAB2_CAP
  [PT_TELEMETRY] = &per_tele,
#endif
};

static i2c_bus_t i2c1_raw;       /* шина i2c_hal; i2c1_bus поверх неї міряє кожну транзакцію */
#endif
static i2c_hal_ctx_t i2c1_ctx;
static i2c_bus_t i2c1_bus;
static mpu6050_t imu;
static uint8_t imu_ready;   /* MPU-6050 відповів з WHO_AM_I = 0x68: imu_task його опитує */
static lcd1602_t lcd;
static uint8_t lcd_ready;   /* LCD пройшов probe та init */
static cli_t cli;
static gate_t gate;
static sorter_t sorter;
static const char *fault_reason;      /* FR-08: перша причина FAULT — ключ reason= у журналі */
static const char *fault_text = "";   /*   і текст для LCD */
static uint8_t log_on = 1;            /* команда log on|off: рядки подій FR-10 */

/* Момент останнього запуску періодичних задач (HAL_GetTick) */
static uint32_t t_gate, t_imu, t_lcd;

/* Подія PARCEL_IN: виставляє gate_task, обробляє sort_task */
static uint8_t parcel_pending;
static uint32_t parcel_ms;    /* HAL_GetTick відліку, на якому виявлено посилку */

static volatile uint8_t estop_pending;      /* був фронт PC13, EXTI вимкнено до кінця брязкоту */
static volatile uint32_t estop_edge_ms;     /* час першого фронту */

#ifdef LAB2_CAP
static uint8_t stream_on = 1;   /* періодичний рядок accel (команда stream) */
#endif

/* Текст команди help; синтаксис розбирає App/cli.c */
static const char *const help_lines[] = {
  "help                this list",
  "status              state, counters A/B/REJECT",
  "reset               zero the counters",
  "log on|off          event lines on UART (default on)",
#ifdef LAB2_CAP
  "stream on|off       periodic 'accel' line (default on)",
  "cap whoami          MPU-6050: write 0x75, read 1 byte",
  "cap accel           MPU-6050: write 0x3B, read 6 bytes",
  "cap wr2             MPU-6050: 2-byte write PWR_MGMT_1 = 0x00",
  "cap nack            1-byte write to absent 0x69, NACK expected",
  "cap lcd             'CAP' on LCD row 1 (1-byte PCF8574 writes)",
  "cap servo <0..180>  set servo angle",
  "cap adc             one gate + one class conversion",
  "cap uart            send 0x55 and 'UART-TEST'",
#endif
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void uart_rx_task(void);
static void estop_task(void);
static void gate_task(void);
static void sort_task(void);
static void imu_task(void);
static void lcd_task(void);
static void ev_vprint(const char *event, const char *fmt, va_list ap);
static void ev_log(const char *event, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void cli_reply(const char *event, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void led_mark(uint8_t on);
static selftest_counts_t selftest_run(void);
static void selftest_report(selftest_counts_t *cnt, const char *dev, health_t h,
                            const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void selftest_ldr(selftest_counts_t *cnt, const char *dev, uint32_t channel);
static void fault_note(const char *reason, const char *text);
static const char *mpu_err_name(mpu6050_status_t st);
static void lcd_show_selftest(const selftest_counts_t *cnt);
static void console_echo(char ch);
static void cmd_run(const cli_cmd_t *cmd);
static void uart_rx_run(int ch);
static void estop_run(void);
static void gate_run(void);
static void sort_run(void);
static void imu_run(void);
static void lcd_run(void);
#if PERF
static void perf_calibrate(void);
static void perf_setup(void);
static uint32_t perf_begin(unsigned id);
static void perf_end(unsigned id, uint32_t t0);
static bus_status_t meas_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len);
static bus_status_t meas_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len);
#endif
#if defined(LAB2_CAP) || defined(TEST_I2C_FAULT)
static const char *bus_st_name(bus_status_t st);
#endif
#ifdef TEST_I2C_FAULT
static void i2c_fault_probe(void);
#endif
#ifdef LAB2_CAP
static void i2c_scan(void);
static void telemetry_task(void);
static void telemetry_run(void);
static void cap_begin(void);
static void cap_end(void);
static void cap_report(const char *name, const char *st, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
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
#endif
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
#if PERF
  perf_hal_init();
  perf_setup();
#endif
  ev_log("BOOT", "fw=%s", FW_NAME);
#if PERF
  perf_calibrate();
#endif
  HAL_ADCEx_Calibration_Start(BOARD_ADC);
  servo_hal_start();

#ifdef LAB2_CAP
  i2c_scan();
#endif

  /* FR-08: самоперевірка; FAIL хоча б одного пристрою — FAULT до перезапуску.
   * На час самоперевірки — 1 спроба I2C: mpu6050_init сам повторює запис
   * PWR_MGMT_1 3 рази, а в Wokwi кожен 2-байтовий запис — TIMEOUT 100 мс
   * (docs/journal.md, проблема 8), інакше було б 3 × 3 тайм-аути. FR-09
   * (до 3 спроб) — лише в робочому циклі. */
#if PERF
  i2c_hal_bus_init(&i2c1_raw, &i2c1_ctx, BOARD_I2C, BOARD_I2C_REINIT);
  i2c1_bus.write = meas_write;
  i2c1_bus.read = meas_read;
  i2c1_bus.ctx = &i2c1_raw;
#else
  i2c_hal_bus_init(&i2c1_bus, &i2c1_ctx, BOARD_I2C, BOARD_I2C_REINIT);
#endif
  i2c_hal_set_tries(&i2c1_ctx, 1);
  selftest_counts_t st = selftest_run();
  i2c_hal_set_tries(&i2c1_ctx, I2C_HAL_MAX_TRIES);
  lcd_show_selftest(&st);
  sorter_init(&sorter, st.fail != 0u);
  if (sorter.state == SORTER_FAULT)
    ev_log("FAULT", "reason=%s", fault_reason);

  gate_init(&gate, GATE_ON_RAW, GATE_OFF_RAW);
  cli_init(&cli);
  t_gate = t_imu = t_lcd = HAL_GetTick();   /* підсумок самоперевірки на LCD — ще LCD_PERIOD_MS */
  ev_log("STATE", "state=%s", sorter_state_name(sorter.state));
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Super loop: задачі по черзі; кожна сама перевіряє свій час і повертає
     * керування. Блокувальні виклики всередині задач — docs/tasks.md. */
#if PERF
    uint32_t t_loop = perf_now();
    if (perf_load_poll(&load, t_loop))
      ev_log("LOAD", "pct=%lu pm=%lu win_cyc=%lu busy_cyc=%lu",
             (unsigned long)load.last_pct, (unsigned long)load.last_pm,
             (unsigned long)load.last_win, (unsigned long)load.last_busy);
#endif
    uart_rx_task();
    estop_task();
    gate_task();
    sort_task();
    imu_task();
    lcd_task();
#ifdef LAB2_CAP
    telemetry_task();
#endif
#if PERF
    perf_add(&st_loop, perf_now() - t_loop);
#endif
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
/* ---------------------------------------------------------------------------
 * Журнал подій FR-10: один рядок "<мс> <ПОДІЯ> key=value ..." на подію.
 * printf → _write → HAL_UART_Transmit: виклик блокує цикл до кінця передачі,
 * ≈ 87 мкс на байт при 115200 8N1.
 * ------------------------------------------------------------------------- */
static void ev_vprint(const char *event, const char *fmt, va_list ap)
{
#if PERF
  uint32_t t0 = perf_now();
#endif
  printf("%lu %s ", (unsigned long)HAL_GetTick(), event);
  vprintf(fmt, ap);
  printf("\r\n");
#if PERF
  perf_add(&st_uart_line, perf_now() - t0);
#endif
}

/* Подія: друкується, лише поки log on */
static void ev_log(const char *event, const char *fmt, ...)
{
  va_list ap;

  if (!log_on)
    return;
  va_start(ap, fmt);
  ev_vprint(event, fmt, ap);
  va_end(ap);
}

/* Відповідь на команду CLI: друкується завжди */
static void cli_reply(const char *event, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  ev_vprint(event, fmt, ap);
  va_end(ap);
}

/* Маркер FR-03 на LD4 (PA5, канал D4 аналізатора): фронт — посилку виявлено
 * (gate_task), спад — рішення прийнято і CCR серво записано (sort_task).
 * Ширина імпульсу — програмна затримка реакції; від фронту D4 до першого
 * імпульсу нової ширини на D2 (PA6) — повна затримка FR-03. */
static void led_mark(uint8_t on)
{
  HAL_GPIO_WritePin(BOARD_LED_MARK_PORT, BOARD_LED_MARK_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ---------------------------------------------------------------------------
 * Задачі super loop (docs/tasks.md)
 * ------------------------------------------------------------------------- */

/* Кожен прохід: усі прийняті байти → cli; завершена команда виконується тут же
 * разом з друком відповіді. USART2 без FIFO, прийом опитуванням: якщо між двома
 * викликами приходить більше одного байта (цикл заблоковано довше ≈ 87 мкс при
 * безперервному потоці), решта губиться (ORE). */
static void uart_rx_task(void)
{
  int ch = uart_hal_getc();

  if (ch < 0)
    return;
  TASK_RUN(PT_UART, uart_rx_run(ch));
}

/* Перший байт уже прочитано; решту, що встигла прийти, забираємо тут же */
static void uart_rx_run(int ch)
{
  do {
    cli_cmd_t cmd;

    console_echo((char)ch);
    if (cli_feed(&cli, (char)ch, &cmd)) {
      if (ch != '\r' && ch != '\n')
        printf("\r\n");   /* переповнення: команда завершилась посеред рядка */
      cmd_run(&cmd);
    }
  } while ((ch = uart_hal_getc()) >= 0);
}

/* Кнопка E-STOP (PC13, EXTI за спадом, pull-up). ISR лише фіксує перший фронт і
 * вимикає EXTI: кнопка Wokwi брязкотить до 2,4 мс і дає до ~200 фронтів на перехід,
 * а клік тримає кнопку від 33 мс (docs/journal.md, проблема 14). */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin != BOARD_ESTOP_PIN)
    return;
  HAL_NVIC_DisableIRQ(BOARD_ESTOP_IRQ);
  estop_edge_ms = HAL_GetTick();
  estop_pending = 1;
}

/* Подія від ISR. Через ESTOP_DEBOUNCE_MS після фронту: PC13 = 0 — натискання,
 * стан перемикається (FR-07); PC13 = 1 — брязкіт відпускання. Фронти за цей час
 * скидаються, EXTI вмикається знову — ще до друку рядка. */
static void estop_task(void)
{
  if (!estop_pending || HAL_GetTick() - estop_edge_ms < ESTOP_DEBOUNCE_MS)
    return;
  TASK_RUN(PT_ESTOP, estop_run());
}

static void estop_run(void)
{
  static uint16_t presses;

  estop_pending = 0;
  uint8_t pressed = (HAL_GPIO_ReadPin(BOARD_ESTOP_PORT, BOARD_ESTOP_PIN) == GPIO_PIN_RESET);
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(BOARD_ESTOP_PIN);
  HAL_NVIC_ClearPendingIRQ(BOARD_ESTOP_IRQ);
  HAL_NVIC_EnableIRQ(BOARD_ESTOP_IRQ);

  if (pressed) {
    sorter_state_t st = sorter_estop_toggle(&sorter);
    presses++;
    ev_log("ESTOP", "n=%u state=%s", (unsigned)presses, sorter_state_name(st));
  }
}

/* Кожні GATE_PERIOD_MS: відлік АЦП gate → поріг з гістерезисом (FR-01).
 * PARCEL_IN — подія для sort_task; PARCEL_OUT — посилка пройшла, SORTING → READY. */
static void gate_task(void)
{
  if (!period_due(&t_gate, HAL_GetTick(), GATE_PERIOD_MS))
    return;
  TASK_RUN(PT_GATE, gate_run());
}

static void gate_run(void)
{
  uint16_t raw = adc_hal_read(BOARD_ADC_CH_GATE);
  switch (gate_update(&gate, raw)) {
  case GATE_EV_IN:
    led_mark(1);
    parcel_ms = HAL_GetTick();
    parcel_pending = 1;
    ev_log("PARCEL_IN", "gate=%u", (unsigned)raw);
    break;
  case GATE_EV_OUT:
    sorter_parcel_out(&sorter);
    ev_log("PARCEL_OUT", "gate=%u state=%s", (unsigned)raw, sorter_state_name(sorter.state));
    break;
  default:
    break;
  }
}

/* Подія PARCEL_IN: клас (FR-02), прапорець удару (FR-04), лоток (FR-05),
 * заслінка (FR-03), лічильники (FR-06). Спершу дія, потім рядок журналу.
 * Нова ширина імпульсу на PA6 з'являється з наступного періоду ШИМ (≤ 20 мс). */
static void sort_task(void)
{
  if (!parcel_pending)
    return;
  TASK_RUN(PT_SORT, sort_run());
}

static void sort_run(void)
{
  sorter_decision_t d;

  parcel_pending = 0;

  uint16_t raw = adc_hal_read(BOARD_ADC_CH_CLASS);
  parcel_class_t cls = classify(raw);
  int accepted = sorter_parcel_in(&sorter, cls, &d);
  if (accepted)
    servo_hal_set_angle(d.angle);
  led_mark(0);
  uint32_t lat_ms = HAL_GetTick() - parcel_ms;

  if (accepted)
    ev_log("SORT", "n=%lu class=%s raw=%u impact=%u route=%s angle=%u lat_ms=%lu",
           (unsigned long)d.n, classify_name(cls), (unsigned)raw, (unsigned)d.impact,
           sorter_bin_name(d.bin), (unsigned)d.angle, (unsigned long)lat_ms);
  else
    ev_log("SORT_SKIP", "state=%s class=%s",
           sorter_state_name(sorter.state), classify_name(cls));
}

/* Кожні IMU_PERIOD_MS: прискорення MPU-6050; |a| > 2 g — прапорець удару для
 * посилки, що зараз їде до gate (FR-04). Рядок IMPACT — один на посилку. */
static void imu_task(void)
{
  if (!imu_ready || !period_due(&t_imu, HAL_GetTick(), IMU_PERIOD_MS))
    return;
  TASK_RUN(PT_IMU, imu_run());
}

static void imu_run(void)
{
  mpu6050_accel_raw_t a;

#ifdef TEST_I2C_FAULT
  i2c_fault_probe();
#endif

  mpu6050_status_t st = mpu6050_read_accel_raw(&imu, &a);
  if (st != MPU6050_OK) {
    ev_log("IMU_ERR", "st=%s", mpu_err_name(st));
    return;
  }

  int32_t x = mpu6050_raw_to_mg(a.x);
  int32_t y = mpu6050_raw_to_mg(a.y);
  int32_t z = mpu6050_raw_to_mg(a.z);
  uint32_t m2 = impact_mag2(x, y, z);
  if (impact_over(m2) && sorter_impact(&sorter))
    ev_log("IMPACT", "amag_mg=%lu x_mg=%ld y_mg=%ld z_mg=%ld",
           (unsigned long)impact_isqrt(m2), (long)x, (long)y, (long)z);
}

/* Кожні LCD_PERIOD_MS: стан і лічильники A/B/REJECT (FR-06) або причина FAULT
 * (FR-08). Обидва рядки переписуються повністю: 2 × (курсор + 16 символів) =
 * 34 байти HD44780 по ≈ 926 мкс — ≈ 31,5 мс блокування за запуск. */
static void lcd_task(void)
{
  if (!lcd_ready || !period_due(&t_lcd, HAL_GetTick(), LCD_PERIOD_MS))
    return;
  TASK_RUN(PT_LCD, lcd_run());
}

static void lcd_run(void)
{
  char row0[LCD1602_COLS + 1];
  char row1[LCD1602_COLS + 1];

  if (sorter.state == SORTER_FAULT) {
    snprintf(row0, sizeof row0, "%-16s", "FAULT:");
    snprintf(row1, sizeof row1, "%-16s", fault_text);
  } else {
    snprintf(row0, sizeof row0, "%-8s n=%-5lu", sorter_state_name(sorter.state),
             (unsigned long)sorter_total(&sorter));
    snprintf(row1, sizeof row1, "A%-4lu B%-4lu R%-3lu",
             (unsigned long)sorter.count[BIN_A], (unsigned long)sorter.count[BIN_B],
             (unsigned long)sorter.count[BIN_REJECT]);
  }

#if PERF
  uint32_t t0 = perf_now();
#endif
  lcd_status_t st = lcd1602_set_cursor(&lcd, 0, 0);
  if (st == LCD_OK)
    st = lcd1602_print(&lcd, row0);
  if (st == LCD_OK)
    st = lcd1602_set_cursor(&lcd, 0, 1);
  if (st == LCD_OK)
    st = lcd1602_print(&lcd, row1);
#if PERF
  perf_add(&st_lcd_update, perf_now() - t0);
#endif
  if (st != LCD_OK)
    ev_log("LCD_ERR", "isr=0x%08lX", (unsigned long)i2c1_ctx.last_isr);
}

#if PERF
/* ---------------------------------------------------------------------------
 * Вимірювання (docs/perf.md)
 * ------------------------------------------------------------------------- */

/* Похибка методу. now_cyc — порожній інтервал між двома perf_now(): на стільки
 * завищений кожен вимір. pair_cyc — скільки тактів циклу забирає пара
 * «t0 = perf_now(); perf_add(&s, perf_now() - t0)» понад now_cyc. Мінімум із 16
 * спроб: у будь-яку може влізти SysTick. */
static void perf_calibrate(void)
{
  perf_stat_t cal = { .name = "cal" };
  uint32_t cpu_mhz = perf_hal_cpu_hz() / 1000000u;

  perf_now_cyc = UINT32_MAX;
  perf_pair_cyc = UINT32_MAX;
  for (int i = 0; i < 16; i++) {
    uint32_t a = perf_now();
    uint32_t b = perf_now();
    uint32_t t0 = perf_now();
    perf_add(&cal, perf_now() - t0);
    uint32_t c = perf_now();

    if (b - a < perf_now_cyc)
      perf_now_cyc = b - a;
    if (c - b - (b - a) < perf_pair_cyc)
      perf_pair_cyc = c - b - (b - a);
  }
  ev_log("PERF", "cpu_hz=%lu now_cyc=%lu now_ns=%lu pair_cyc=%lu pair_ns=%lu",
         (unsigned long)perf_hal_cpu_hz(),
         (unsigned long)perf_now_cyc, (unsigned long)(perf_now_cyc * 1000u / cpu_mhz),
         (unsigned long)perf_pair_cyc, (unsigned long)(perf_pair_cyc * 1000u / cpu_mhz));
}

static void perf_setup(void)
{
  cyc_per_us = perf_hal_cpu_hz() / 1000000u;
  perf_load_init(&load, perf_hal_cpu_hz() / 1000u * PERF_WIN_MS);   /* 1 с = SystemCoreClock тактів */
  perf_period_init(&per_gate, GATE_PERIOD_MS * 1000u);
  perf_period_init(&per_imu, IMU_PERIOD_MS * 1000u);
  perf_period_init(&per_lcd, LCD_PERIOD_MS * 1000u);
#ifdef LAB2_CAP
  perf_period_init(&per_tele, TELEMETRY_PERIOD_MS * 1000u);
#endif
}

static uint32_t perf_begin(unsigned id)
{
  uint32_t t0 = perf_now();

  if (task_period[id] != NULL)
    perf_period_start(task_period[id], t0, cyc_per_us);
  return t0;
}

static void perf_end(unsigned id, uint32_t t0)
{
  uint32_t cyc = perf_now() - t0;

  perf_add(&st_task[id], cyc);
  perf_load_busy(&load, cyc);
}

/* Вимірювальна шина поверх i2c_hal: кожен виклик write/read драйвера —
 * один вимір i2c_xfer, разом з повторами й відновленням FR-09 */
static bus_status_t meas_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
  const i2c_bus_t *raw = ctx;
  uint32_t t0 = perf_now();
  bus_status_t st = raw->write(raw->ctx, addr7, data, len);

  perf_add(&st_i2c, perf_now() - t0);
  return st;
}

static bus_status_t meas_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len)
{
  const i2c_bus_t *raw = ctx;
  uint32_t t0 = perf_now();
  bus_status_t st = raw->read(raw->ctx, addr7, data, len);

  perf_add(&st_i2c, perf_now() - t0);
  return st;
}
#endif

#ifdef TEST_I2C_FAULT
/* PRD, тест FR-09: раз на I2C_FAULT_PERIOD_MS — читання 1 байта з відсутньої
 * адреси. Читання, а не запис: 1-байтовий запис на відсутню адресу у Wokwi
 * повертає HAL_OK (docs/journal.md, проблема 9). Очікується I2C_RECOVER
 * try=1/3 … 3/3, st=NACK, а наступне опитування 0x68 — штатне. */
static void i2c_fault_probe(void)
{
  static uint32_t t_fault;
  uint8_t b;

  if (!period_due(&t_fault, HAL_GetTick(), I2C_FAULT_PERIOD_MS))
    return;
  bus_status_t st = i2c1_bus.read(i2c1_bus.ctx, I2C_FAULT_ADDR, &b, 1);
  ev_log("I2C_FAULT_TEST", "addr=0x%02X st=%s", (unsigned)I2C_FAULT_ADDR, bus_st_name(st));
}
#endif

/* ---------------------------------------------------------------------------
 * Самоперевірка при старті (FR-08)
 * ------------------------------------------------------------------------- */

/* Рядок FR-10: "<мс> SELFTEST dev=<пристрій> status=<OK|WARN|FAIL> key=value..." */
static void selftest_report(selftest_counts_t *cnt, const char *dev, health_t h,
                            const char *fmt, ...)
{
  va_list ap;

  printf("%lu SELFTEST dev=%s status=%s ", (unsigned long)HAL_GetTick(), dev, health_name(h));
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\r\n");

  if (h == HEALTH_OK)
    cnt->ok++;
  else if (h == HEALTH_WARN)
    cnt->warn++;
  else
    cnt->fail++;
}

/* Перша причина FAULT лишається: її показує LCD */
static void fault_note(const char *reason, const char *text)
{
  if (fault_reason == NULL) {
    fault_reason = reason;
    fault_text = text;
  }
}

static void selftest_ldr(selftest_counts_t *cnt, const char *dev, uint32_t channel)
{
  uint16_t raw = adc_hal_read(channel);

  selftest_report(cnt, dev, health_ldr(raw, ADC_HAL_MAX_RAW), "raw=%u", (unsigned)raw);
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

/* Самоперевірка; рядок SELFTEST_END — завжди останній */
static selftest_counts_t selftest_run(void)
{
  selftest_counts_t cnt = {0};

  /* MPU-6050: WHO_AM_I = 0x68. SLEEP, що не знімається у Wokwi (docs/journal.md,
   * проблема 8), — WARN: прискорення читається, сортувальник працює */
  mpu6050_status_t ms = mpu6050_init(&imu, &i2c1_bus, BOARD_MPU_ADDR);
  health_t h = health_mpu(ms);
  imu_ready = (h != HEALTH_FAIL);
  if (ms == MPU6050_OK)
    selftest_report(&cnt, "mpu6050", h, "who=0x%02X tries=%u",
                    imu.who_am_i, (unsigned)imu.init_tries);
  else
    selftest_report(&cnt, "mpu6050", h, "who=0x%02X tries=%u err=%s",
                    imu.who_am_i, (unsigned)imu.init_tries, mpu_err_name(ms));
  /* Немає відповіді чи чужий WHO_AM_I (без пристрою може прочитатися 0xFF) —
   * MPU-6050 не знайдено; деталь — err= у рядку SELFTEST */
  if (h == HEALTH_FAIL)
    fault_note("MPU_NOT_FOUND", "MPU not found");

  /* LCD1602: ACK + зворотне читання порту PCF8574, потім ініціалізація HD44780 */
  uint8_t rb = 0;
  lcd_status_t ls = lcd1602_probe(&i2c1_bus, BOARD_LCD_ADDR, &rb);
  if (ls == LCD_OK)
    ls = lcd1602_init(&lcd, &i2c1_bus, BOARD_LCD_ADDR, platform_delay_ms);
  lcd_ready = (ls == LCD_OK);
  if (lcd_ready) {
    selftest_report(&cnt, "lcd1602", HEALTH_OK, "addr=0x%02X rb=0x%02X", BOARD_LCD_ADDR, rb);
  } else {
    selftest_report(&cnt, "lcd1602", HEALTH_FAIL, "addr=0x%02X err=BUS", BOARD_LCD_ADDR);
    fault_note("LCD_NOT_FOUND", "LCD not found");
  }

  selftest_ldr(&cnt, "ldr_gate", BOARD_ADC_CH_GATE);
  selftest_ldr(&cnt, "ldr_class", BOARD_ADC_CH_CLASS);

  /* Серво: ставимо 90° і читаємо CCR назад */
  servo_hal_set_angle(SERVO_SELFTEST_DEG);
  uint16_t pulse = servo_hal_get_pulse_us();
  h = health_servo(pulse, BOARD_SERVO_MIN_US, BOARD_SERVO_MAX_US);
  selftest_report(&cnt, "servo", h, "pulse_us=%u", (unsigned)pulse);
  if (h == HEALTH_FAIL)
    fault_note("SERVO_RANGE", "Servo range");

  printf("%lu SELFTEST_END ok=%u warn=%u fail=%u\r\n",
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

/* ---------------------------------------------------------------------------
 * CLI
 * ------------------------------------------------------------------------- */

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
  case CLI_STATUS:
    cli_reply("STATUS", "state=%s a=%lu b=%lu reject=%lu total=%lu impact=%u log=%u fault=%s",
              sorter_state_name(sorter.state),
              (unsigned long)sorter.count[BIN_A], (unsigned long)sorter.count[BIN_B],
              (unsigned long)sorter.count[BIN_REJECT], (unsigned long)sorter_total(&sorter),
              (unsigned)sorter.impact, (unsigned)log_on,
              fault_reason != NULL ? fault_reason : "NONE");
    break;
  case CLI_RESET:
    sorter_reset_counters(&sorter);
    cli_reply("RESET", "total=%lu", (unsigned long)sorter_total(&sorter));
    break;
  case CLI_LOG:
    log_on = (uint8_t)cmd->arg;
    cli_reply("LOG", "on=%u", (unsigned)log_on);
    break;
#ifdef LAB2_CAP
  case CLI_STREAM:
    stream_on = (uint8_t)cmd->arg;
    cli_reply("STREAM", "st=OK on=%u", (unsigned)stream_on);
    break;
  case CLI_CAP_WHOAMI: cap_whoami(); break;
  case CLI_CAP_ACCEL:  cap_accel();  break;
  case CLI_CAP_WR2:    cap_wr2();    break;
  case CLI_CAP_NACK:   cap_nack();   break;
  case CLI_CAP_LCD:    cap_lcd();    break;
  case CLI_CAP_SERVO:  cap_servo((uint8_t)cmd->arg); break;
  case CLI_CAP_ADC:    cap_adc();    break;
  case CLI_CAP_UART:   cap_uart();   break;
#endif
  default:
    cli_reply("CLI", "st=BAD");
    break;
  }
}

#if defined(LAB2_CAP) || defined(TEST_I2C_FAULT)
static const char *bus_st_name(bus_status_t st)
{
  switch (st) {
  case BUS_OK:          return "OK";
  case BUS_ERR_NACK:    return "NACK";
  case BUS_ERR_TIMEOUT: return "TIMEOUT";
  default:              return "ERR";
  }
}
#endif

/* ---------------------------------------------------------------------------
 * Вимірювальний harness ЛР2 (make -f app.mk LAB2_CAP=1)
 * ------------------------------------------------------------------------- */
#ifdef LAB2_CAP
static void i2c_scan(void)
{
  printf("I2C scan:\r\n");
  for (uint8_t a = 0x08; a < 0x78; a++)
    if (HAL_I2C_IsDeviceReady(BOARD_I2C, a << 1, 2, 10) == HAL_OK)
      printf("  found 0x%02X\r\n", a);
}

/* Кожні TELEMETRY_PERIOD_MS, поки stream on: прискорення й обидва АЦП */
static void telemetry_task(void)
{
  static uint32_t t_tele;

  if (!stream_on || !period_due(&t_tele, HAL_GetTick(), TELEMETRY_PERIOD_MS))
    return;
  TASK_RUN(PT_TELEMETRY, telemetry_run());
}

static void telemetry_run(void)
{
  mpu6050_accel_raw_t a = {0};
  mpu6050_status_t st_acc = mpu6050_read_accel_raw(&imu, &a);
  printf("accel st=%d raw %6d %6d %6d | mg %5ld %5ld %5ld | gate %4u class %4u\r\n",
         (int)st_acc, a.x, a.y, a.z,
         (long)mpu6050_raw_to_mg(a.x), (long)mpu6050_raw_to_mg(a.y),
         (long)mpu6050_raw_to_mg(a.z),
         adc_hal_read(BOARD_ADC_CH_GATE), adc_hal_read(BOARD_ADC_CH_CLASS));
}

/* Рамка однієї дії для логічного аналізатора: LED_MARK (PA5, канал D4) = 1 рівно
 * на час дії. HAL_UART_Transmit повертається лише після TC, тож ехо команди вже
 * вийшло з TX; I2C_RECOVER друкується тільки після спаду маркера. Повтори FR-09
 * на час дії вимкнено: на записі — рівно одна транзакція. */
static void cap_begin(void)
{
  i2c_hal_set_tries(&i2c1_ctx, 1);
  i2c_hal_defer_log(&i2c1_ctx, 1);
  HAL_GPIO_WritePin(BOARD_LED_MARK_PORT, BOARD_LED_MARK_PIN, GPIO_PIN_SET);
}

static void cap_end(void)
{
  HAL_GPIO_WritePin(BOARD_LED_MARK_PORT, BOARD_LED_MARK_PIN, GPIO_PIN_RESET);
  i2c_hal_defer_log(&i2c1_ctx, 0);
  i2c_hal_set_tries(&i2c1_ctx, I2C_HAL_MAX_TRIES);
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
    cap_report("accel", "OK", "raw=%02X%02X%02X%02X%02X%02X",
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
    ls = lcd1602_print(&lcd, CAP_LCD_TEXT);
  cap_end();
  if (ls == LCD_OK)
    cap_report("lcd", "OK", "row=1 text=CAP");
  else
    cap_report("lcd", "ERR", "isr=0x%08lX", (unsigned long)i2c1_ctx.last_isr);
}

/* HAL вмикає preload CCR, тож нова ширина з'являється лише з наступного
 * періоду ШИМ (до 20 мс після запису). LED_MARK тримаємо CAP_SERVO_HOLD_MS:
 * у вікно маркера гарантовано потрапляє щонайменше один повний новий імпульс. */
static void cap_servo(uint8_t deg)
{
  cap_begin();
  servo_hal_set_angle(deg);
  uint16_t pulse = servo_hal_get_pulse_us();
  HAL_Delay(CAP_SERVO_HOLD_MS);
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
#endif /* LAB2_CAP */
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
