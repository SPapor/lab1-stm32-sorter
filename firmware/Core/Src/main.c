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
#include <stddef.h>
#include <string.h>
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
#include "lcdfmt.h"
#if PERF
#include "perf.h"
#include "perf_hal.h"
#include "memmark.h"
#include "mem_hal.h"
#include <malloc.h>
#endif
#if ARCH_RTOS
#include "rtos_hal.h"
#include "sorter_tasks.h"
#endif
#ifdef OSMETRICS
#include "rtos_osm.h"
#endif
#if defined(TEST_PRIMASK) || defined(TEST_BOOTMARK)
#include "perf_hal.h"
#include "perf_tick.h"
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* Підсумок самоперевірки при старті (FR-08) */
typedef struct {
  unsigned ok, warn, fail;
} selftest_counts_t;

/* Сенсори для сценарію старту (методичка 6.2.7, docs/scenarios.md) */
enum { RDY_GATE = 0, RDY_CLASS, RDY_IMU, RDY_LCD, RDY_COUNT };

/* Номери задач super loop для вимірювань (docs/perf.md) */
enum {
  PT_UART = 0,
  PT_ESTOP,
  PT_GATE,
  PT_SORT,
  PT_IMU,
  PT_LCD,
  PT_LCD_PART,      /* дописування змінених символів LCD між періодами (set lcd_chars) */
#if ARCH_RTOS
  PT_LOGGER,        /* задача logger: запис черги журналу -> UART */
#endif
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
#ifdef OSMETRICS
#define OSBENCH_N           1000u
#endif
#if defined(OSMETRICS) || defined(TEST_APIPROBE) || RTOS_ASSERT || defined(TEST_L5)
#define HIST_CMD            0
#else
#define HIST_CMD            1
#endif
#define IMU_READY_MG        500     /* |a| від 0,5 g — MPU справді міряє (у SLEEP на залізі дає нулі) */

/* Архітектура (app.mk: ARCH, LTO, TICK_HZ) — у рядках BOOT і PERF */
#if ARCH_QPC
#define FW_NAME             "lab5-qpc"
#define ARCH_NAME           "qpc"
#elif ARCH_RTOS
#define FW_NAME             "lab4-rtos"
#define ARCH_NAME           "rtos"
#else
#define FW_NAME             "lab3-superloop"
#define ARCH_NAME           "superloop"
#define TICK_HZ             1000u       /* тік HAL; у rtos — тік ядра з app.mk */
#endif
/* Ширина тексту рядка BOOT після "BOOT ": коротший доповнюється пробілами, тож друк
 * BOOT займає той самий час в обох ARCH і не входить у різницю STARTUP між ними
 * (docs/rtos-design.md, розділ 5). 96 - найдовший рядок rtos: git-хеш з -dirty,
 * TICK_HZ до 4 цифр, os_heap до 4 цифр; довший друкується без доповнення. */
#define BOOT_WIDTH          96u
#ifndef GIT_HASH
#define GIT_HASH            "unknown"   /* app.mk передає -DGIT_HASH з git describe */
#endif
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
/* Перший коректний вимір сенсора після скидання (методичка 6.2.7). Без PERF
 * умова READY_IF не обчислюється */
#if PERF
#define READY(what) ready_mark(what)
#define READY_IF(cond, what) \
  do { if (ready_us[what] == 0u && (cond)) ready_mark(what); } while (0)
#else
#define READY(what) ((void)0)
#define READY_IF(cond, what) ((void)0)
#endif

#if PERF
#define TASK_RUN(id, call) \
  do { uint32_t t0_ = perf_begin(id); call; perf_end((id), t0_); } while (0)
#else
#define TASK_RUN(id, call) call
#endif

/* Спільний ресурс (АЦП, шина I2C, стан сортувальника) у rtos - під м'ютексом
 * (Tasks/sorter_tasks.c), у super loop задачі й так по черзі */
#if ARCH_RTOS
#define LOCK(what)   tasks_lock(TASKS_LOCK_##what)
#define UNLOCK(what) tasks_unlock(TASKS_LOCK_##what)
#else
#define LOCK(what)   ((void)0)
#define UNLOCK(what) ((void)0)
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

/* STAT для кожної задачі — лише запуски з роботою. Назви ставить perf_setup:
 * з ініціалізатором уся статистика лягла б у .data і мала б копію у флеші */
static perf_stat_t st_task[PT_COUNT];
static const char *const task_name[PT_COUNT] = {
  [PT_UART] = "uart_rx", [PT_ESTOP] = "estop", [PT_GATE] = "gate",
  [PT_SORT] = "sort", [PT_IMU] = "imu", [PT_LCD] = "lcd", [PT_LCD_PART] = "lcd_part",
#if ARCH_RTOS
  [PT_LOGGER] = "logger",
#endif
#ifdef LAB2_CAP
  [PT_TELEMETRY] = "telemetry",
#endif
};
static perf_stat_t st_loop;          /* loop: повний прохід циклу */
static perf_stat_t st_i2c;           /* i2c_xfer: виклик шини, з повторами FR-09 */
static perf_stat_t st_uart_line;     /* uart_line: один рядок журналу в UART */
static perf_stat_t st_lcd_update;    /* lcd_update: обидва рядки LCD */
#if ARCH_QPC
static perf_stat_t st_bus_wait;
#endif

/* Затримки реакції (методичка 5.1, приклад 2) */
static perf_stat_t st_estop_lat;     /* estop_latency: фронт PC13 → реакція */
static perf_stat_t st_parcel_lat;    /* parcel_latency: PARCEL_IN → запис CCR, FR-03 */

/* Старт: мкс від скидання (uwTick = 0 у HAL_Init) до BOOT і до першого
 * коректного виміру кожного сенсора; 0 — ще не було */
static uint32_t boot_us;
static uint32_t ready_us[RDY_COUNT];
static volatile uint32_t estop_irq_cyc;   /* perf_now() у EXTI-колбеку */
static uint8_t i2c_failed;                /* остання транзакція впала: наступна вдала — EVT i2c_ok */

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
/* Ручки для пошуку точки зламу (команда set, docs/scenarios.md, 6.2.5) */
static uint16_t lcd_period_ms = LCD_PERIOD_MS;
static uint16_t imu_period_ms = IMU_PERIOD_MS;
static uint8_t lcd_chars;             /* 0 — повна перемальовка; N — лише змінені, ≤ N символів за прохід */
static uint8_t log_level = 2;         /* 0 — нічого, 1 — події продукту, 2 — ще EVT і LOAD (log on|off = 2|0) */

static char lcd_want[2][LCD1602_COLS + 1];   /* що має бути на LCD */
static char lcd_shown[2][LCD1602_COLS];      /* що там зараз (для lcd_chars > 0; 0 — невідомо) */
static uint8_t lcd_pending;                  /* lcd_chars > 0: ще є незаписані зміни */

#if !ARCH_RTOS
/* Момент останнього запуску періодичних задач (HAL_GetTick) */
static uint32_t t_gate, t_imu, t_lcd;

/* Подія PARCEL_IN: виставляє gate_task, обробляє sort_task (у rtos - черга) */
static uint8_t parcel_pending;
static uint32_t parcel_ms;    /* HAL_GetTick відліку, на якому виявлено посилку */
static uint32_t parcel_cyc;   /* perf_now() у той самий момент (PERF=1) */
static volatile uint32_t estop_edge_ms;     /* час першого фронту */
#else
/* Робота задач для Tasks/sorter_tasks.c; imu і lcd - лише якщо пристрій пройшов самоперевірку */
#if !defined(TEST_KSTRESS) && !defined(TEST_CRITCOST) && !defined(TEST_QSTRESS)
static tasks_ops_t task_ops;
#endif
#if !ARCH_QPC
static i2c_bus_t i2c1_inner;     /* шина під м'ютексом: i2c1_bus бере його на кожну транзакцію */
#endif
#endif

static volatile uint8_t estop_pending;      /* був фронт PC13, EXTI вимкнено до кінця брязкоту */

#ifdef LAB2_CAP
static uint8_t stream_on = 1;   /* періодичний рядок accel (команда stream) */
#endif

/* Текст команди help: імена команд одним рядком (бюджет flash, docs/qp-design.md,
 * крок 3); синтаксис розбирає App/cli.c */
static const char help_line[] = "help status reset log"
#if PERF
  " stats load stack heap"
#if ARCH_RTOS
  " tasks"
#endif
#if HIST_CMD
  " hist"
#endif
  " set"
#endif
  ;
#ifdef LAB2_CAP
static const char *const help_lines[] = {
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
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
#if !ARCH_RTOS
static void loop_pass(void);
static void uart_rx_task(void);
static void estop_task(void);
static void gate_task(void);
static void sort_task(void);
static void imu_task(void);
static void lcd_task(void);
#else
#if ARCH_QPC
static uint8_t estop_work(void);
#else
static void estop_work(void);
#endif
#if ARCH_QPC
static void qpc_estop_apply(void);
static void qpc_parcel_out(uint16_t raw);
static void qpc_impact(int16_t x, int16_t y, int16_t z);
static uint8_t qpc_sorter_state(void);
static void qpc_imu_begin(void);
static uint8_t qpc_bus_imu_read(uint8_t raw[6]);
static void qpc_imu_end(uint8_t st, const uint8_t raw[6]);
static void qpc_lcd_begin(char rows[2][TASKS_LCD_ROW]);
static uint8_t qpc_bus_lcd_row(uint8_t part, const char *text);
static void qpc_lcd_end(uint8_t st);
static void qpc_bus_wait(uint32_t cyc);
#endif
#if ARCH_QPC
static uint8_t gate_work(void);
#else
static void gate_work(void);
#endif
static void sort_work(uint32_t in_ms, uint32_t in_cyc);
static void imu_work(void);
static void lcd_work(void);
static void cli_work(int ch);
static void log_line(uint32_t ms, const char *event, const char *fmt, const fmt_arg_t *args,
                     unsigned n);
static void rtos_second(void);
static void ev_i2c(const char *event, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#if ARCH_QPC
#if SIM_CRIT
static void i2c_try_hold(uint8_t on);
#endif
#else
static bus_status_t locked_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len);
static bus_status_t locked_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len);
#endif
#if PERF
static void ev_now(const char *event, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
#endif
static uint16_t adc_read(uint32_t channel);
#ifdef TEST_PRIMASK
static void primask_test(void);
#endif
#ifdef TEST_BOOTMARK
#define BOOTMARK(at) perf_bootmark(at)
#else
#define BOOTMARK(at) ((void)0)
#endif
static void ev_vprint(const char *event, unsigned width, const char *fmt, va_list ap);
static void ev_log(const char *event, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void boot_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void cli_reply(const char *event, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void led_mark(uint8_t on);
static selftest_counts_t selftest_run(void);
static void selftest_report(selftest_counts_t *cnt, const char *dev, health_t h,
                            const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static health_t selftest_ldr(selftest_counts_t *cnt, const char *dev, uint32_t channel);
static void fault_note(const char *reason, const char *text);
static const char *mpu_err_name(mpu6050_status_t st);
static void lcd_show_selftest(const selftest_counts_t *cnt);
static void console_echo(char ch);
static void cmd_run(const cli_cmd_t *cmd);
static void con_write(const char *s, int n);
static void uart_rx_run(int ch);
#if !ARCH_QPC
static void estop_run(void);
#endif
static gate_event_t gate_run(void);
static void sort_run(uint32_t in_ms, uint32_t in_cyc);
static void imu_run(void);
static void lcd_run(uint8_t due);
static lcd_status_t lcd_write_changed(void);
#if PERF
static void perf_calibrate(void);
static void perf_setup(void);
static uint32_t perf_begin(unsigned id);
static void perf_end(unsigned id, uint32_t t0);
static bus_status_t meas_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len);
static bus_status_t meas_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len);
static void i2c_measured(uint32_t t0, bus_status_t st);
static void evt(const char *name, uint64_t t_us);
static void stack_report(void);
static void heap_report(void);
static void stats_report(void);
static void stats_reset(void);
static void load_line(void (*out)(const char *, const char *, ...));
static void load_report(void);
#if HIST_CMD
static void hist_report(const char *name);
#endif
static void knobs_line(void);
static void set_knob(const char *name, int32_t v);
static void ready_mark(unsigned what);
static void ready_flush(void);
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
/* Сирі байти в UART: ехо CLI і переноси рядків */
static void con_write(const char *s, int n)
{
  uart_hal_write(s, (size_t)n);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
#if PERF
  /* Stack painting першим рядком main (методичка 5.4): резерв стека
   * [_estack - _Min_Stack_Size, SP - 16 слів) — шаблоном; поточний кадр не чіпаємо */
  memmark_paint(mem_hal_stack_bottom(), mem_hal_sp() - 16, MEMMARK_PATTERN);
#endif
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  BOOTMARK('h');               /* дослід TEST_BOOTMARK: після HAL_Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  BOOTMARK('c');               /* після перемикання тактування */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  BOOTMARK('m');               /* після MX_*_Init */
#if ARCH_RTOS
  HAL_NVIC_SetPriority(BOARD_ESTOP_IRQ, 3, 0);
#endif
#if PERF
  perf_hal_init();
  perf_setup();
#endif
  BOOTMARK('b');               /* перед рядком BOOT */
#if ARCH_RTOS
  boot_log("fw=%s git=%s perf=%u arch=%s lto=%u tick_hz=%u kernel=%s os_heap=%lu",
           FW_NAME, GIT_HASH, (unsigned)PERF, ARCH_NAME, (unsigned)BUILD_LTO, (unsigned)TICK_HZ,
           rtos_hal_kernel_version(), (unsigned long)rtos_hal_heap_total());
  {
    uint32_t line, pc;

    /* configASSERT до скидання: рядок і адреса виклику збережені в .noinit */
    if (rtos_hal_assert_take(&line, &pc))
      ev_log("ASSERT", "line=%lu pc=0x%08lX", (unsigned long)line, (unsigned long)pc);
  }
#else
  boot_log("fw=%s git=%s perf=%u arch=%s lto=%u tick_hz=%u", FW_NAME, GIT_HASH,
           (unsigned)PERF, ARCH_NAME, (unsigned)BUILD_LTO, (unsigned)TICK_HZ);
#endif
  BOOTMARK('e');               /* після рядка BOOT */
#if PERF
  {
    uint64_t t = perf_now_us64();
    boot_us = (uint32_t)t;
    evt("boot", t);
  }
  knobs_line();
  perf_calibrate();
#endif
  HAL_ADCEx_Calibration_Start(BOARD_ADC);
  servo_hal_start();

#ifdef LAB2_CAP
  i2c_scan();
#endif

  /* FR-08: самоперевірка; FAIL хоча б одного пристрою — FAULT до перезапуску.
   * На час самоперевірки — 1 спроба I2C: mpu6050_init сам повторює запис
   * PWR_MGMT_1 3 рази, а в Wokwi кожен 2-байтовий запис — TIMEOUT
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
#if ARCH_RTOS
  /* Шина спільна для imu і lcd: кожна транзакція - під м'ютексом (до старту
   * ядра - без нього). I2C_RECOVER - через чергу журналу */
#if ARCH_QPC
#if SIM_CRIT
  i2c1_ctx.hold = i2c_try_hold;
#endif
#else
  i2c1_inner = i2c1_bus;
  i2c1_bus.write = locked_write;
  i2c1_bus.read = locked_read;
  i2c1_bus.ctx = &i2c1_inner;
#endif
  i2c_hal_set_log(&i2c1_ctx, ev_i2c);
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
#if !ARCH_RTOS
  t_gate = t_imu = t_lcd = HAL_GetTick();   /* підсумок самоперевірки на LCD — ще LCD_PERIOD_MS */
#endif
  ev_log("STATE", "state=%s", sorter_state_name(sorter.state));
#if PERF
  stack_report();               /* глибина стека й купа після старту (методичка 5.4) */
  heap_report();
#endif
#ifdef TEST_BOOTMARK
  perf_bootmark_report(cli_reply);
#endif
#ifdef TEST_PRIMASK
  primask_test();
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
#if ARCH_RTOS
  /* FreeRTOS: сім задач (Tasks/sorter_tasks.c, docs/rtos-design.md, розділ 6); сюди не
   * повертається. Робота задач - ті самі функції, що в super loop. Тут перший
   * виклик API ядра: з нього до старту ядра переривання вимкнені
   * (ulCriticalNesting = 0xaaaaaaaa у порту), HAL_GetTick стоїть. Тому вся
   * ініціалізація з HAL_Delay і тайм-аутами HAL (LCD, самоперевірка, I2C) — вище.
   * LCD підсумку самоперевірки - ще перший період lcd (xTaskDelayUntil) */
#if defined(TEST_KSTRESS) || defined(TEST_CRITCOST) || defined(TEST_QSTRESS)
  /* тестові збірки ядра запускають свої задачі: робота продукту не лінкується */
  (void)estop_work;
  (void)gate_work;
  (void)sort_work;
  (void)imu_work;
  (void)lcd_work;
  (void)cli_work;
  (void)log_line;
  (void)rtos_second;
#if ARCH_QPC
  (void)qpc_imu_begin;
  (void)qpc_bus_imu_read;
  (void)qpc_imu_end;
  (void)qpc_lcd_begin;
  (void)qpc_bus_lcd_row;
  (void)qpc_lcd_end;
  (void)qpc_bus_wait;
  (void)qpc_estop_apply;
  (void)qpc_parcel_out;
  (void)qpc_impact;
  (void)qpc_sorter_state;
#endif
  tasks_start(NULL);
#else
  task_ops.estop = estop_work;
  task_ops.gate = gate_work;
  task_ops.sort = sort_work;
  task_ops.imu = imu_ready ? imu_work : NULL;
  task_ops.lcd = lcd_ready ? lcd_work : NULL;
  task_ops.cli = cli_work;
  task_ops.log_line = log_line;
  task_ops.second = rtos_second;
  task_ops.estop_debounce_ms = ESTOP_DEBOUNCE_MS;
  task_ops.gate_ms = GATE_PERIOD_MS;
  task_ops.imu_ms = &imu_period_ms;
  task_ops.lcd_ms = &lcd_period_ms;
#if ARCH_QPC
  task_ops.imu_begin = qpc_imu_begin;
  task_ops.bus_imu_read = qpc_bus_imu_read;
  task_ops.imu_end = qpc_imu_end;
  task_ops.lcd_begin = qpc_lcd_begin;
  task_ops.bus_lcd_row = qpc_bus_lcd_row;
  task_ops.lcd_end = qpc_lcd_end;
  task_ops.bus_wait = qpc_bus_wait;
  task_ops.estop_apply = qpc_estop_apply;
  task_ops.parcel_out = qpc_parcel_out;
  task_ops.impact = qpc_impact;
  task_ops.sorter_state = qpc_sorter_state;
#endif
  tasks_start(&task_ops);
#endif
#endif
  while (1)
  {

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if !ARCH_RTOS
    loop_pass();
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
 * Рядок форматується в буфер (App/fmt.c) і йде в UART одним HAL_UART_Transmit,
 * що блокує цикл до кінця передачі: ≈ 87 мкс на байт при 115200 8N1.
 * ------------------------------------------------------------------------- */
static void ev_vprint(const char *event, unsigned width, const char *fmt, va_list ap)
{
  size_t text;
  /* UART - разом з обліком uart_line: у rtos друкують cli і logger */
  uart_hal_lock();
#if PERF
  uint32_t t0 = perf_now();
#endif
  uart_hal_line_add("%lu %s ", (unsigned long)HAL_GetTick(), event);
  text = uart_hal_line_len();
  uart_hal_line_vadd(fmt, ap);
  /* Доповнення до width символів тексту (лише BOOT, решта - width = 0) */
  uart_hal_line_pad(text + width);
  uart_hal_line_end();
#if PERF
  perf_add(&st_uart_line, perf_now() - t0);
#endif
  uart_hal_unlock();
}

/* Подія: друкується, лише поки log on. У rtos після старту ядра - через чергу
 * журналу (Tasks/sorter_tasks.c): друкує задача logger з міткою часу події */
static void ev_log(const char *event, const char *fmt, ...)
{
  va_list ap;

  if (log_level == 0)
    return;
  va_start(ap, fmt);
#if ARCH_RTOS
  if (tasks_running())
    tasks_log(event, fmt, ap);
  else
#endif
    ev_vprint(event, 0u, fmt, ap);
  va_end(ap);
}

#if ARCH_RTOS
/* I2C_RECOVER з i2c_hal: як ev_log, але й при log off (так було й у super loop) */
static void ev_i2c(const char *event, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  if (tasks_running())
    tasks_log(event, fmt, ap);
  else
    ev_vprint(event, 0u, fmt, ap);
  va_end(ap);
}

#if PERF
/* Подія з самої задачі logger (LOAD): одразу в UART, як ev_log у super loop */
static void ev_now(const char *event, const char *fmt, ...)
{
  va_list ap;

  if (log_level == 0)
    return;
  va_start(ap, fmt);
  ev_vprint(event, 0u, fmt, ap);
  va_end(ap);
}
#endif

/* Запис черги журналу: рядок FR-10 з міткою часу події */
static void log_print(uint32_t ms, const char *event, const char *fmt, const fmt_arg_t *args,
                      unsigned n)
{
  uart_hal_lock();
#if PERF
  uint32_t t0 = perf_now();
#endif
  uart_hal_line_add("%lu %s ", (unsigned long)ms, event);
  uart_hal_line_aadd(fmt, args, n);
  uart_hal_line_end();
#if PERF
  perf_add(&st_uart_line, perf_now() - t0);
#endif
  uart_hal_unlock();
}

static void log_line(uint32_t ms, const char *event, const char *fmt, const fmt_arg_t *args,
                     unsigned n)
{
  TASK_RUN(PT_LOGGER, log_print(ms, event, fmt, args, n));
}
#endif

/* Рядок BOOT: як ev_log, але завжди BOOT_WIDTH символів тексту */
static void boot_log(const char *fmt, ...)
{
  va_list ap;

  if (log_level == 0)
    return;
  va_start(ap, fmt);
  ev_vprint("BOOT", BOOT_WIDTH, fmt, ap);
  va_end(ap);
}

/* Відповідь на команду CLI: друкується завжди */
static void cli_reply(const char *event, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  ev_vprint(event, 0u, fmt, ap);
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

#ifdef TEST_PRIMASK
/* Дослід (docs/rtos-design.md, розділ 2): чи маскує PRIMASK переривання. З
 * вимкненими перериваннями - 10 повних обертів SysTick, які рахуємо опитуванням
 * COUNTFLAG. На залізі uwTick і лічильник тіків perf за цей час не змінюються, а
 * тік чекає в PENDSTSET. У super loop обидва лічильники - uwTick, у rtos uwTick
 * рахує TIM17, а тіки perf - SysTick_Handler. */
static void primask_test(void)
{
  uint32_t uw0, pt0, uw1, pt1, pend, pm;
  unsigned wraps = 0;

  __disable_irq();
  pm = __get_PRIMASK();
  uw0 = uwTick;
  pt0 = perf_tick_count();
  (void)SysTick->CTRL;                 /* скинути COUNTFLAG */
  while (wraps < 10u)
    if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)
      wraps++;
  uw1 = uwTick;
  pt1 = perf_tick_count();
  pend = (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0u;
  __enable_irq();
  cli_reply("PRIMASK", "uwtick_delta=%lu perf_tick_delta=%lu pendst=%lu wraps=%u primask=%lu",
            (unsigned long)(uw1 - uw0), (unsigned long)(pt1 - pt0), (unsigned long)pend,
            wraps, (unsigned long)pm);
}
#endif

/* ---------------------------------------------------------------------------
 * Задачі super loop (docs/tasks.md)
 * ------------------------------------------------------------------------- */

#if !ARCH_RTOS
/* Один прохід циклу: задачі по черзі; кожна сама перевіряє свій час і повертає
 * керування. Блокувальні виклики всередині задач — docs/tasks.md. Його викликає
 * while (1) у main; у ARCH=rtos ті самі роботи (*_run) викликають задачі
 * (*_work нижче, Tasks/sorter_tasks.c) */
static void loop_pass(void)
{
#if PERF
  uint32_t t_loop = perf_now();
  if (perf_load_poll(&load, t_loop) && log_level >= 2)
    load_line(ev_log);
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
  ready_flush();
  perf_add(&st_loop, perf_now() - t_loop);
#endif
}

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
#endif

/* Наступний прийнятий байт або -1: у super loop - опитування USART2, у rtos -
 * потоковий буфер, який наповнює переривання прийому */
static int con_getc(void)
{
#if ARCH_RTOS
  return tasks_rx_getc();
#else
  return uart_hal_getc();
#endif
}

/* Перший байт уже прочитано; решту, що встигла прийти, забираємо тут же */
static void uart_rx_run(int ch)
{
  do {
    cli_cmd_t cmd;

    console_echo((char)ch);
    if (cli_feed(&cli, (char)ch, &cmd)) {
#if PERF
      uint64_t us_rx = perf_now_us64();   /* цикл дійшов до кінця рядка команди */
#endif
      if (ch != '\r' && ch != '\n')
        con_write("\r\n", 2);   /* переповнення: команда завершилась посеред рядка */
      cmd_run(&cmd);
#if PERF
      uint64_t us_reply = perf_now_us64();
      evt("cmd_rx", us_rx);
      evt("cmd_reply", us_reply);
#endif
#if ARCH_QPC
      return;
#endif
    }
  } while ((ch = con_getc()) >= 0);
}

/* Кнопка E-STOP (PC13, EXTI за спадом, pull-up). ISR лише фіксує перший фронт і
 * вимикає EXTI: кнопка Wokwi брязкотить до 2,4 мс і дає до ~200 фронтів на перехід,
 * а клік тримає кнопку від 33 мс (docs/journal.md, проблема 14). */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin != BOARD_ESTOP_PIN)
    return;
#if !defined(TEST_Q93) || TEST_Q93 > 2
  HAL_NVIC_DisableIRQ(BOARD_ESTOP_IRQ);
#endif
#if PERF
  estop_irq_cyc = perf_now();
#endif
  estop_pending = 1;
#if ARCH_RTOS
  tasks_estop_from_isr();     /* задача estop: антибрязкіт, потім estop_run */
#else
  estop_edge_ms = HAL_GetTick();
#endif
}

/* Подія від ISR. Через ESTOP_DEBOUNCE_MS після фронту: PC13 = 0 — натискання,
 * стан перемикається (FR-07); PC13 = 1 — брязкіт відпускання. Фронти за цей час
 * скидаються, EXTI вмикається знову — ще до друку рядка. */
#if !ARCH_RTOS
static void estop_task(void)
{
  if (!estop_pending || HAL_GetTick() - estop_edge_ms < ESTOP_DEBOUNCE_MS)
    return;
  TASK_RUN(PT_ESTOP, estop_run());
}
#endif

#if !ARCH_QPC
static void estop_run(void)
{
  static uint16_t presses;

  estop_pending = 0;
#if PERF
  /* Мітку ISR читаємо до EnableIRQ: поки переривання вимкнене, колбек її не
   * перезапише. Час реакції — момент, коли цикл дійшов до рішення. */
  uint32_t t_irq = estop_irq_cyc;
  uint32_t t_react = perf_now();
  uint64_t us_react = perf_now_us64();
#endif
  uint8_t pressed = (HAL_GPIO_ReadPin(BOARD_ESTOP_PORT, BOARD_ESTOP_PIN) == GPIO_PIN_RESET);
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(BOARD_ESTOP_PIN);
  HAL_NVIC_ClearPendingIRQ(BOARD_ESTOP_IRQ);
  HAL_NVIC_EnableIRQ(BOARD_ESTOP_IRQ);

  if (pressed) {
    LOCK(SORTER);
    sorter_state_t st = sorter_estop_toggle(&sorter);
    UNLOCK(SORTER);
    presses++;
    ev_log("ESTOP", "n=%u state=%s", (unsigned)presses, sorter_state_name(st));
#if PERF
    uint32_t lat = t_react - t_irq;   /* включно з ESTOP_DEBOUNCE_MS */
    perf_add(&st_estop_lat, lat);
    evt("estop_irq", us_react - perf_cyc_to_us(lat, cyc_per_us));
    evt("estop_react", us_react);
#endif
  }
}
#else
#if PERF
static uint32_t qpc_estop_t_irq;
#endif

static uint8_t qpc_estop_check(void)
{
  estop_pending = 0;
#if PERF
  qpc_estop_t_irq = estop_irq_cyc;
#endif
  uint8_t pressed = (HAL_GPIO_ReadPin(BOARD_ESTOP_PORT, BOARD_ESTOP_PIN) == GPIO_PIN_RESET);
  __HAL_GPIO_EXTI_CLEAR_FALLING_IT(BOARD_ESTOP_PIN);
  HAL_NVIC_ClearPendingIRQ(BOARD_ESTOP_IRQ);
  HAL_NVIC_EnableIRQ(BOARD_ESTOP_IRQ);
  return pressed;
}

static void qpc_estop_apply(void)
{
  static uint16_t presses;
#if PERF
  uint32_t t_react = perf_now();
  uint64_t us_react = perf_now_us64();
#endif

  LOCK(SORTER);
  sorter_state_t st = sorter_estop_toggle(&sorter);
  UNLOCK(SORTER);
  presses++;
  ev_log("ESTOP", "n=%u state=%s", (unsigned)presses, sorter_state_name(st));
#if PERF
  uint32_t lat = t_react - qpc_estop_t_irq;
  perf_add(&st_estop_lat, lat);
  evt("estop_irq", us_react - perf_cyc_to_us(lat, cyc_per_us));
  evt("estop_react", us_react);
#endif
}

static void qpc_parcel_out(uint16_t raw)
{
  LOCK(SORTER);
  sorter_parcel_out(&sorter);
  sorter_state_t st = sorter.state;
  UNLOCK(SORTER);
  ev_log("PARCEL_OUT", "gate=%u state=%s", (unsigned)raw, sorter_state_name(st));
}

static void qpc_impact(int16_t x, int16_t y, int16_t z)
{
  LOCK(SORTER);
  int first = sorter_impact(&sorter);
  UNLOCK(SORTER);
  if (first)
    ev_log("IMPACT", "amag_mg=%lu x_mg=%ld y_mg=%ld z_mg=%ld",
           (unsigned long)impact_isqrt(impact_mag2(x, y, z)), (long)x, (long)y, (long)z);
}

static uint8_t qpc_sorter_state(void)
{
  return (uint8_t)sorter.state;
}

_Static_assert(SORTER_READY == TASKS_ST_READY && SORTER_SORTING == TASKS_ST_SORTING &&
               SORTER_ESTOP == TASKS_ST_ESTOP && SORTER_FAULT == TASKS_ST_FAULT,
               "стани sorter у sorter_tasks.h");
#endif

/* Кожні GATE_PERIOD_MS: відлік АЦП gate → поріг з гістерезисом (FR-01).
 * PARCEL_IN — подія для sort_task; PARCEL_OUT — посилка пройшла, SORTING → READY. */
#if !ARCH_RTOS
static void gate_task(void)
{
  if (!period_due(&t_gate, HAL_GetTick(), GATE_PERIOD_MS))
    return;
  TASK_RUN(PT_GATE, gate_run());
}
#endif

/* Відлік АЦП: у rtos АЦП ділять gate і sort */
static uint16_t adc_read(uint32_t channel)
{
  uint16_t raw;

  LOCK(ADC);
  raw = adc_hal_read(channel);
  UNLOCK(ADC);
  return raw;
}

static gate_event_t gate_run(void)
{
  uint16_t raw = adc_read(BOARD_ADC_CH_GATE);
  READY_IF(health_ldr(raw, ADC_HAL_MAX_RAW) == HEALTH_OK, RDY_GATE);
  gate_event_t ev = gate_update(&gate, raw);

  switch (ev) {
  case GATE_EV_IN: {
    uint32_t cyc = 0;
#if PERF
    cyc = perf_now();
#endif
    led_mark(1);
#if ARCH_RTOS
    tasks_parcel(HAL_GetTick(), cyc);
#else
    parcel_cyc = cyc;
    parcel_ms = HAL_GetTick();
    parcel_pending = 1;
#endif
    ev_log("PARCEL_IN", "gate=%u", (unsigned)raw);
    break;
  }
  case GATE_EV_OUT: {
#if ARCH_QPC
    tasks_parcel_out(raw);
#else
    LOCK(SORTER);
    sorter_parcel_out(&sorter);
    sorter_state_t st = sorter.state;
    UNLOCK(SORTER);
    ev_log("PARCEL_OUT", "gate=%u state=%s", (unsigned)raw, sorter_state_name(st));
#endif
    break;
  }
  default:
    break;
  }
  return ev;
}

/* Подія PARCEL_IN: клас (FR-02), прапорець удару (FR-04), лоток (FR-05),
 * заслінка (FR-03), лічильники (FR-06). Спершу дія, потім рядок журналу.
 * Нова ширина імпульсу на PA6 з'являється з наступного періоду ШИМ (≤ 20 мс). */
#if !ARCH_RTOS
static void sort_task(void)
{
  if (!parcel_pending)
    return;
  parcel_pending = 0;
  TASK_RUN(PT_SORT, sort_run(parcel_ms, parcel_cyc));
}
#endif

/* in_ms, in_cyc - HAL_GetTick і perf_now моменту PARCEL_IN (від gate) */
static void sort_run(uint32_t in_ms, uint32_t in_cyc)
{
  sorter_decision_t d;
  sorter_state_t st;

  (void)in_cyc;   /* лише PERF=1 */

  uint16_t raw = adc_read(BOARD_ADC_CH_CLASS);
  READY_IF(health_ldr(raw, ADC_HAL_MAX_RAW) == HEALTH_OK, RDY_CLASS);
  parcel_class_t cls = classify(raw);
  LOCK(SORTER);
  int accepted = sorter_parcel_in(&sorter, cls, &d);
  st = sorter.state;
  UNLOCK(SORTER);
  if (accepted)
    servo_hal_set_angle(d.angle);
#if PERF
  uint32_t t_dec = perf_now();          /* CCR записано (або посилку пропущено) */
  uint64_t us_dec = perf_now_us64();
#endif
  led_mark(0);
  uint32_t lat_ms = HAL_GetTick() - in_ms;

  if (accepted)
    ev_log("SORT", "n=%lu class=%s raw=%u impact=%u route=%s angle=%u lat_ms=%lu",
           (unsigned long)d.n, classify_name(cls), (unsigned)raw, (unsigned)d.impact,
           sorter_bin_name(d.bin), (unsigned)d.angle, (unsigned long)lat_ms);
  else
    ev_log("SORT_SKIP", "state=%s class=%s", sorter_state_name(st), classify_name(cls));
#if PERF
  /* EVT друкуємо після дії: рядок журналу (~3 мс) не входить у затримку */
  uint32_t lat = t_dec - in_cyc;
  evt("parcel_in", us_dec - perf_cyc_to_us(lat, cyc_per_us));
  if (accepted) {
    perf_add(&st_parcel_lat, lat);
    evt("servo_set", us_dec);
  }
#endif
}

/* Кожні IMU_PERIOD_MS: прискорення MPU-6050; |a| > 2 g — прапорець удару для
 * посилки, що зараз їде до gate (FR-04). Рядок IMPACT — один на посилку. */
#if !ARCH_RTOS
static void imu_task(void)
{
  if (!imu_ready || !period_due(&t_imu, HAL_GetTick(), imu_period_ms))
    return;
  TASK_RUN(PT_IMU, imu_run());
}
#endif

static void imu_process(mpu6050_status_t st, const mpu6050_accel_raw_t *a)
{
  if (st != MPU6050_OK) {
    ev_log("IMU_ERR", "st=%s", mpu_err_name(st));
    return;
  }

  int32_t x = mpu6050_raw_to_mg(a->x);
  int32_t y = mpu6050_raw_to_mg(a->y);
  int32_t z = mpu6050_raw_to_mg(a->z);
  uint32_t m2 = impact_mag2(x, y, z);
  READY_IF(m2 >= (uint32_t)IMU_READY_MG * IMU_READY_MG, RDY_IMU);
  int first = 0;
  if (impact_over(m2)) {
#if ARCH_QPC
    tasks_impact((int16_t)x, (int16_t)y, (int16_t)z);
#else
    LOCK(SORTER);
    first = sorter_impact(&sorter);
    UNLOCK(SORTER);
#endif
  }
  if (first)
    ev_log("IMPACT", "amag_mg=%lu x_mg=%ld y_mg=%ld z_mg=%ld",
           (unsigned long)impact_isqrt(m2), (long)x, (long)y, (long)z);
}

static mpu6050_status_t imu_read(mpu6050_accel_raw_t *a)
{
#ifdef TEST_I2C_FAULT
  i2c_fault_probe();
#endif
  return mpu6050_read_accel_raw(&imu, a);
}

static void imu_run(void)
{
  mpu6050_accel_raw_t a;
  mpu6050_status_t st = imu_read(&a);

  imu_process(st, &a);
}

/* Кожні LCD_PERIOD_MS: стан і лічильники A/B/REJECT (FR-06) або причина FAULT
 * (FR-08). Обидва рядки переписуються повністю: 2 × (курсор + 16 символів) =
 * 34 байти HD44780 по ≈ 926 мкс — ≈ 31,5 мс блокування за запуск. */
#if !ARCH_RTOS
static void lcd_task(void)
{
  uint8_t due;

  if (!lcd_ready)
    return;
  due = (uint8_t)period_due(&t_lcd, HAL_GetTick(), lcd_period_ms);
  if (!due && !lcd_pending)
    return;
#if PERF
  BOARD_PROBE_ON(BOARD_PROBE_LCD_PIN);    /* D7: головний блокувальник циклу */
#endif
  if (due)
    TASK_RUN(PT_LCD, lcd_run(1));
  else
    TASK_RUN(PT_LCD_PART, lcd_run(0));    /* лише при lcd_chars > 0 */
#if PERF
  BOARD_PROBE_OFF(BOARD_PROBE_LCD_PIN);
#endif
}
#endif

/* due = 1 — настав період: новий текст рядків. lcd_chars = 0 — обидва рядки
 * повністю (34 записи HD44780, ~31,5 мс); N > 0 — лише символи, що змінилися,
 * не більше N за прохід, решта — на наступних проходах циклу (STAT lcd_part) */
static void lcd_format(void)
{
  char *row0 = lcd_want[0];
  char *row1 = lcd_want[1];
  const size_t cap = sizeof lcd_want[0];

  /* Той самий текст, що давали "%-16s", "%-8s n=%-5lu" і "A%-4lu B%-4lu R%-3lu" */
  LOCK(SORTER);
  if (sorter.state == SORTER_FAULT) {
    lcdfmt_str(row0, 0, cap, "FAULT:", LCD1602_COLS);
    lcdfmt_str(row1, 0, cap, fault_text, LCD1602_COLS);
  } else {
    size_t n = lcdfmt_str(row0, 0, cap, sorter_state_name(sorter.state), 8);
    n = lcdfmt_str(row0, n, cap, " n=", 0);
    lcdfmt_u32(row0, n, cap, sorter_total(&sorter), 5);
    n = lcdfmt_str(row1, 0, cap, "A", 0);
    n = lcdfmt_u32(row1, n, cap, sorter.count[BIN_A], 4);
    n = lcdfmt_str(row1, n, cap, " B", 0);
    n = lcdfmt_u32(row1, n, cap, sorter.count[BIN_B], 4);
    n = lcdfmt_str(row1, n, cap, " R", 0);
    lcdfmt_u32(row1, n, cap, sorter.count[BIN_REJECT], 3);
  }
  UNLOCK(SORTER);
}

static lcd_status_t lcd_write_row(uint8_t row, const char *text)
{
  lcd_status_t st = lcd1602_set_cursor(&lcd, 0, row);

  if (st == LCD_OK)
    st = lcd1602_print(&lcd, text);
  return st;
}

static void lcd_finish(lcd_status_t st)
{
  READY_IF(st == LCD_OK, RDY_LCD);
  if (st != LCD_OK) {
    memset(lcd_shown, 0, sizeof lcd_shown);   /* що на екрані — невідомо: наступний період перепише все */
    lcd_pending = 0;
    ev_log("LCD_ERR", "isr=0x%08lX", (unsigned long)i2c1_ctx.last_isr);
  }
}

static void lcd_run(uint8_t due)
{
  lcd_status_t st;

  if (due)
    lcd_format();
#if PERF
  uint32_t t0 = perf_now();
#endif
  /* У rtos - завжди повна перемальовка: порції lcd_chars - обхід обмеження 1 super
   * loop, а задачу lcd і так витісняють (docs/rtos-design.md, розділ 6) */
  if (lcd_chars == 0u || ARCH_RTOS) {
    st = lcd_write_row(0, lcd_want[0]);
    if (st == LCD_OK)
      st = lcd_write_row(1, lcd_want[1]);
  } else {
    st = lcd_write_changed();
  }
#if PERF
  perf_add(&st_lcd_update, perf_now() - t0);
#endif
  lcd_finish(st);
}

/* lcd_chars > 0: не більше lcd_chars символів, що відрізняються від показаних.
 * Курсор ставиться лише перед «стрибком»: після запису символу HD44780 сам
 * переходить на наступну позицію (крім кінця рядка). Команда курсора — ще
 * ~0,93 мс, у ліміт символів вона не входить. */
static lcd_status_t lcd_write_changed(void)
{
  unsigned left = lcd_chars;
  int cur = -1;                 /* позиція курсора HD44780 після останнього запису */
  lcd_status_t st = LCD_OK;

  lcd_pending = 0;
  for (unsigned i = 0; i < 2u * LCD1602_COLS; i++) {
    unsigned r = i / LCD1602_COLS, c = i % LCD1602_COLS;
    char ch[2] = { lcd_want[r][c], '\0' };

    if (lcd_shown[r][c] == ch[0])
      continue;
    if (left == 0u) {
      lcd_pending = 1;          /* решта — на наступному проході циклу */
      break;
    }
    if (cur != (int)i)
      st = lcd1602_set_cursor(&lcd, (uint8_t)c, (uint8_t)r);
    if (st == LCD_OK)
      st = lcd1602_print(&lcd, ch);
    if (st != LCD_OK)
      break;
    lcd_shown[r][c] = ch[0];
    cur = (c == LCD1602_COLS - 1u) ? -1 : (int)i + 1;
    left--;
  }
  return st;
}

#if ARCH_RTOS
/* ---------------------------------------------------------------------------
 * Робота задач rtos (Tasks/sorter_tasks.c): ті самі *_run, що в super loop; час і
 * чекання задає задача, тож тут лише облік TASK_RUN
 * ------------------------------------------------------------------------- */
#if ARCH_QPC
#if PERF
static uint32_t t0_imu, t0_lcd, t0_lcd_update;
#endif

static void qpc_imu_begin(void)
{
#if PERF
  t0_imu = perf_begin(PT_IMU);
#endif
}

static uint8_t qpc_bus_imu_read(uint8_t raw[6])
{
  mpu6050_accel_raw_t a;
  mpu6050_status_t st = imu_read(&a);

  memcpy(raw, &a, sizeof a);
  return (uint8_t)st;
}

static void qpc_imu_end(uint8_t st, const uint8_t raw[6])
{
  mpu6050_accel_raw_t a;

  memcpy(&a, raw, sizeof a);
  imu_process((mpu6050_status_t)st, &a);
#if PERF
  perf_end(PT_IMU, t0_imu);
#endif
}

static void qpc_lcd_begin(char rows[2][TASKS_LCD_ROW])
{
#if PERF
  t0_lcd = perf_begin(PT_LCD);
#endif
  lcd_format();
  memcpy(rows, lcd_want, sizeof lcd_want);
#if PERF
  t0_lcd_update = perf_now();
#endif
}

static uint8_t qpc_bus_lcd_row(uint8_t part, const char *text)
{
  char buf[TASKS_LCD_PART + 1u];
  uint8_t col = (uint8_t)((part % TASKS_LCD_PARTS) * TASKS_LCD_PART);
  lcd_status_t st = LCD_OK;

  memcpy(buf, text, TASKS_LCD_PART);
  buf[TASKS_LCD_PART] = '\0';
  if (col == 0u)
    st = lcd1602_set_cursor(&lcd, 0, (uint8_t)(part / TASKS_LCD_PARTS));
  if (st == LCD_OK)
    st = lcd1602_print(&lcd, buf);
  return (uint8_t)st;
}

static void qpc_lcd_end(uint8_t st)
{
#if PERF
  perf_add(&st_lcd_update, perf_now() - t0_lcd_update);
#endif
  lcd_finish((lcd_status_t)st);
#if PERF
  perf_end(PT_LCD, t0_lcd);
#endif
}

static void qpc_bus_wait(uint32_t cyc)
{
#if PERF
  perf_add(&st_bus_wait, cyc);
#else
  (void)cyc;
#endif
}

_Static_assert(sizeof(mpu6050_accel_raw_t) == 6u, "BusDoneEvt.data - 6 байтів MPU");
_Static_assert(sizeof lcd_want == 2u * TASKS_LCD_ROW, "рядки LCD у BusReqEvt");
_Static_assert(TASKS_LCD_PART * TASKS_LCD_PARTS == LCD1602_COLS, "рядок LCD - ціле число шматків");
#endif

#if ARCH_QPC
static uint8_t estop_work(void)
{
  uint8_t pressed = 0;

  if (estop_pending)
    TASK_RUN(PT_ESTOP, pressed = qpc_estop_check());
  return pressed;
}
#else
static void estop_work(void)
{
  if (estop_pending)
    TASK_RUN(PT_ESTOP, estop_run());
}
#endif

#if ARCH_QPC
_Static_assert(GATE_EV_IN == TASKS_GATE_IN && GATE_EV_OUT == TASKS_GATE_OUT, "події gate у sorter_tasks.h");

static uint8_t gate_work(void)
{
  gate_event_t ev;

  TASK_RUN(PT_GATE, ev = gate_run());
  return (uint8_t)ev;
}
#else
static void gate_work(void)
{
  TASK_RUN(PT_GATE, gate_run());
}
#endif

static void sort_work(uint32_t in_ms, uint32_t in_cyc)
{
  TASK_RUN(PT_SORT, sort_run(in_ms, in_cyc));
}

static void imu_work(void)
{
  TASK_RUN(PT_IMU, imu_run());
}

static void lcd_work(void)
{
  TASK_RUN(PT_LCD, lcd_run(1));
}

static void cli_work(int ch)
{
  TASK_RUN(PT_UART, uart_rx_run(ch));
}

#if ARCH_QPC
#if SIM_CRIT
static void i2c_try_hold(uint8_t on)
{
  if (on)
    LOCK(I2C);
  else
    UNLOCK(I2C);
}
#endif
#else
/* Шина I2C під м'ютексом: транзакція (з повторами FR-09) - одне взяття */
static bus_status_t locked_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
  const i2c_bus_t *in = ctx;
  bus_status_t st;

  LOCK(I2C);
  st = in->write(in->ctx, addr7, data, len);
  UNLOCK(I2C);
  return st;
}

static bus_status_t locked_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len)
{
  const i2c_bus_t *in = ctx;
  bus_status_t st;

  LOCK(I2C);
  st = in->read(in->ctx, addr7, data, len);
  UNLOCK(I2C);
  return st;
}
#endif

/* Задача logger раз на секунду. LOAD = 100 % - частка idle (run-time stats ядра
 * в тактах perf_now) у тому самому вікні perf_load, що й у super loop. Вікно -
 * щонайменше на 1 мс коротше за секунду (perf_setup): logger будиться за тіками
 * ядра, і секунда за perf_now може вийти на кілька тактів коротшою */
static void rtos_second(void)
{
#if PERF
  static uint32_t prev_now;
  static uint64_t prev_idle;
  static uint8_t started;
  uint32_t now = perf_now();
  uint64_t idle = tasks_idle_cyc();

  if (started) {
    uint32_t win = now - prev_now;
    uint32_t idle_cyc = (uint32_t)(idle - prev_idle);

    perf_load_busy(&load, win > idle_cyc ? win - idle_cyc : 0u);
  }
  started = 1;
  prev_now = now;
  prev_idle = idle;
  if (perf_load_poll(&load, now) && log_level >= 2)
    load_line(ev_now);
  ready_flush();
#endif
}
#endif

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
  ev_log("PERF", "cpu_hz=%lu now_cyc=%lu pair_cyc=%lu",
         (unsigned long)perf_hal_cpu_hz(), (unsigned long)perf_now_cyc,
         (unsigned long)perf_pair_cyc);
}

static void perf_setup(void)
{
  for (unsigned i = 0; i < PT_COUNT; i++)
    st_task[i].name = task_name[i];
  st_loop.name = "loop";
  st_i2c.name = "i2c_xfer";
  st_uart_line.name = "uart_line";
  st_lcd_update.name = "lcd_update";
#if ARCH_QPC
  st_bus_wait.name = "bus_wait";
#endif
  st_estop_lat.name = "estop_latency";
  st_parcel_lat.name = "parcel_latency";
  cyc_per_us = perf_hal_cpu_hz() / 1000000u;
#if ARCH_RTOS
  perf_load_init(&load, perf_hal_cpu_hz() / 1000u * (PERF_WIN_MS - 1u));   /* rtos_second */
#else
  perf_load_init(&load, perf_hal_cpu_hz() / 1000u * PERF_WIN_MS);   /* 1 с = SystemCoreClock тактів */
#endif
  perf_period_init(&per_gate, GATE_PERIOD_MS * 1000u);
  perf_period_init(&per_imu, IMU_PERIOD_MS * 1000u);
  perf_period_init(&per_lcd, LCD_PERIOD_MS * 1000u);
#ifdef LAB2_CAP
  perf_period_init(&per_tele, TELEMETRY_PERIOD_MS * 1000u);
#endif
}

static const uint16_t probe_pin[PT_COUNT] = {
#ifndef TEST_INVERSION
  [PT_ESTOP] = BOARD_PROBE_ESTOP_PIN, [PT_GATE] = BOARD_PROBE_GATE_PIN,
#endif
};

/* D6 (PB13) = 1, поки працює будь-яка задача: на аналізаторі ширина імпульсу —
 * час задачі, відстань між фронтами — період і джитер (методичка 5.3). У rtos D6
 * веде перемикання задач (rtos_hal_trace_in), тут - лише D0 і D1 */
static uint32_t perf_begin(unsigned id)
{
#if !ARCH_RTOS
  BOARD_PROBE_ON(BOARD_PROBE_TASK_PIN);
#endif
  if (probe_pin[id] != 0u)
    BOARD_PROBE_ON(probe_pin[id]);
  /* джитер — до t0: облік старту (там ділення) не входить у час задачі */
  if (task_period[id] != NULL)
    perf_period_start(task_period[id], perf_now(), cyc_per_us);
  return perf_now();
}

static void perf_end(unsigned id, uint32_t t0)
{
  uint32_t cyc = perf_now() - t0;

  if (probe_pin[id] != 0u)
    BOARD_PROBE_OFF(probe_pin[id]);
  perf_add(&st_task[id], cyc);
#if ARCH_RTOS
  (void)cyc;    /* LOAD у rtos - з часу idle (rtos_second) */
#else
  BOARD_PROBE_OFF(BOARD_PROBE_TASK_PIN);
  perf_load_busy(&load, cyc);
#endif
}

/* Вимірювальна шина поверх i2c_hal: кожен виклик write/read драйвера —
 * один вимір i2c_xfer, разом з повторами й відновленням FR-09 */
static bus_status_t meas_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
  const i2c_bus_t *raw = ctx;
  uint32_t t0 = perf_now();
  bus_status_t st = raw->write(raw->ctx, addr7, data, len);

  i2c_measured(t0, st);
  return st;
}

static bus_status_t meas_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len)
{
  const i2c_bus_t *raw = ctx;
  uint32_t t0 = perf_now();
  bus_status_t st = raw->read(raw->ctx, addr7, data, len);

  i2c_measured(t0, st);
  return st;
}

/* EVT i2c_fail — кожна транзакція, що впала після всіх спроб FR-09;
 * EVT i2c_ok — перша вдала після збою (шина ожила) */
static void i2c_measured(uint32_t t0, bus_status_t st)
{
  perf_add(&st_i2c, perf_now() - t0);
  if (st != BUS_OK) {
    i2c_failed = 1;
    evt("i2c_fail", perf_now_us64());
  } else if (i2c_failed) {
    i2c_failed = 0;
    evt("i2c_ok", perf_now_us64());
  }
}

/* Глибина стека за stack painting. used = reserved — шаблон зіпсовано до самого
 * низу: стек, найімовірніше, вийшов за резерв _Min_Stack_Size і щось перезаписав */
static void stack_report(void)
{
  uint32_t *bottom = mem_hal_stack_bottom();
  uint32_t *top = mem_hal_stack_top();
  uint32_t used = memmark_used_bytes(bottom, top, MEMMARK_PATTERN);
  uint32_t reserved = (uint32_t)((top - bottom) * (int)sizeof(uint32_t));

  cli_reply("STACK", "used=%lu reserved=%lu free=%lu overflow=%u",
            (unsigned long)used, (unsigned long)reserved, (unsigned long)(reserved - used),
            (unsigned)(used >= reserved));
#if ARCH_RTOS
  /* у rtos це стек MSP (main до старту ядра і переривання), далі - стеки задач */
  if (tasks_running())
    tasks_stack_report(cli_reply);
#endif
}

/* Купа newlib (printf і stdio беруть пам'ять через malloc): arena — скільки
 * забрано в sbrk (не зменшується, тож це пік), used / free — усередині арени */
static void heap_report(void)
{
  /* _mallinfo_r — те саме ядро, що в mallinfo(), але без mstats.o: той тягне
   * malloc_stats, fprintf і strlen (~450 Б флешу), які тут не потрібні */
  struct mallinfo mi = _mallinfo_r(_REENT);

  cli_reply("HEAP", "arena=%u used=%u free=%u",
            (unsigned)mi.arena, (unsigned)mi.uordblks, (unsigned)mi.fordblks);
#if ARCH_RTOS
  if (tasks_running())
    tasks_heap_report(cli_reply);
#endif
}

/* Один рядок STAT (методичка 5.1), час у мкс. Ключі однакові для всіх рядків:
 * у задач за подією і блокувальних викликів period_us=0 і нулі джитера.
 * Джитер старту знаковий, miss — інтервал між стартами понад 2 періоди */
static void stat_line(const perf_stat_t *sp, const perf_period_t *pp)
{
  const perf_stat_t *s = sp;
  const perf_period_t *p = pp;

#if ARCH_RTOS
  /* Їх пишуть інші задачі: копія за одну критичну секцію, щоб рядок був
   * узгоджений. Копії статичні (stat_line кличе лише cli), а з perf_period_t -
   * лише поля до гістограми: на стеку cli вони не вмістилися б */
  static perf_stat_t sc;
  static perf_period_t pc;

  if (tasks_running()) {
    tasks_snapshot(&sc, sp, sizeof sc);
    s = &sc;
    if (pp != NULL) {
      tasks_snapshot(&pc, pp, offsetof(perf_period_t, hist));
      p = &pc;
    }
  }
#endif
  cli_reply("STAT", "name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu period_us=%lu "
            "jit_min_us=%ld jit_max_us=%ld miss=%lu", s->name, (unsigned long)s->count,
            (unsigned long)perf_cyc_to_us(s->min, cyc_per_us),
            (unsigned long)perf_cyc_to_us(perf_avg(s), cyc_per_us),
            (unsigned long)perf_cyc_to_us(s->max, cyc_per_us),
            (unsigned long)(p ? p->period_us : 0u), (long)(p ? p->jit_min_us : 0),
            (long)(p ? p->jit_max_us : 0), (unsigned long)(p ? p->missed : 0u));
}

/* Команда stats: шапка з версією і похибкою методу, далі всі STAT і лічильники UART */
static void stats_report(void)
{
  static const perf_stat_t *const other[] = {
    &st_loop, &st_i2c, &st_uart_line, &st_lcd_update, &st_estop_lat, &st_parcel_lat,
#if ARCH_QPC
    &st_bus_wait,
#endif
  };
  uint32_t rx, ore;

  cli_reply("PERF", "git=%s now_cyc=%lu pair_cyc=%lu cyc_per_us=%lu arch=%s lto=%u tick_hz=%u",
            GIT_HASH, (unsigned long)perf_now_cyc, (unsigned long)perf_pair_cyc,
            (unsigned long)cyc_per_us, ARCH_NAME, (unsigned)BUILD_LTO, (unsigned)TICK_HZ);
  knobs_line();
#if ARCH_RTOS
  {
    uint32_t os_api_us, os_start_us;

    /* ще й старт ядра: перший виклик API і запуск першої задачі */
    rtos_hal_startup_us(&os_api_us, &os_start_us);
    cli_reply("STARTUP", "boot_us=%lu gate_us=%lu class_us=%lu imu_us=%lu lcd_us=%lu "
              "os_api_us=%lu os_start_us=%lu",
              (unsigned long)boot_us, (unsigned long)ready_us[RDY_GATE],
              (unsigned long)ready_us[RDY_CLASS], (unsigned long)ready_us[RDY_IMU],
              (unsigned long)ready_us[RDY_LCD], (unsigned long)os_api_us,
              (unsigned long)os_start_us);
  }
#else
  cli_reply("STARTUP", "boot_us=%lu gate_us=%lu class_us=%lu imu_us=%lu lcd_us=%lu",
            (unsigned long)boot_us, (unsigned long)ready_us[RDY_GATE],
            (unsigned long)ready_us[RDY_CLASS], (unsigned long)ready_us[RDY_IMU],
            (unsigned long)ready_us[RDY_LCD]);
#endif
  for (unsigned i = 0; i < PT_COUNT; i++)
    stat_line(&st_task[i], task_period[i]);
  for (size_t i = 0; i < sizeof other / sizeof other[0]; i++)
    stat_line(other[i], NULL);
  uart_hal_counters(&rx, &ore);
  cli_reply("UART", "rx=%lu ore=%lu", (unsigned long)rx, (unsigned long)ore);
#if ARCH_RTOS
  if (tasks_running())
    tasks_report(cli_reply);
#endif
#ifdef OSMETRICS
  for (unsigned i = 0; i < OSM_COUNT; i++) {
    const perf_stat_t *raw, *nosim;

    rtos_osm_stats((rtos_osm_id_t)i, &raw, &nosim);
    stat_line(raw, NULL);
    if (nosim != NULL)
      stat_line(nosim, NULL);
  }
#endif
#ifdef TEST_INVERSION
  stat_line(tasks_inv_wait(), NULL);
#endif
}

/* stats reset: усі STAT, джитер, гістограми й лічильники UART у нуль.
 * Вікно завантаження не чіпаємо — воно й так оновлюється щосекунди */
static void stats_reset(void)
{
  static perf_stat_t *const other[] = {
    &st_loop, &st_i2c, &st_uart_line, &st_lcd_update, &st_estop_lat, &st_parcel_lat,
#if ARCH_QPC
    &st_bus_wait,
#endif
  };

  for (unsigned i = 0; i < PT_COUNT; i++) {
    perf_stat_reset(&st_task[i]);
    if (task_period[i] != NULL)
      perf_period_reset(task_period[i]);
  }
  for (size_t i = 0; i < sizeof other / sizeof other[0]; i++)
    perf_stat_reset(other[i]);
  uart_hal_counters_reset();
#if ARCH_RTOS
  if (tasks_running())
    tasks_stats_reset();
#endif
  cli_reply("STATS", "reset=1");
}

/* LOAD за останнє закрите вікно (методичка 5.1, приклад 4). Щосекунди з циклу —
 * як подія (log off вимикає), командою load — завжди */
static void load_line(void (*out)(const char *, const char *, ...))
{
  out("LOAD", "pct=%lu pm=%lu win_cyc=%lu busy_cyc=%lu windows=%lu",
      (unsigned long)load.last_pct, (unsigned long)load.last_pm,
      (unsigned long)load.last_win, (unsigned long)load.last_busy,
      (unsigned long)load.windows);
}

static void load_report(void)
{
  load_line(cli_reply);
}

#if HIST_CMD
/* Команда hist <задача>: шапка і по рядку на кожен непорожній кошик 100 мкс.
 * Крайні кошики — «нижче за -1000 мкс» і «від 2000 мкс» */
static void hist_report(const char *name)
{
  const perf_period_t *p = NULL;

  for (unsigned i = 0; i < PT_COUNT; i++)
    if (task_period[i] != NULL && strcmp(st_task[i].name, name) == 0)
      p = task_period[i];
  if (p == NULL) {
    cli_reply("HIST", "name=%s st=BAD", name);
    return;
  }
  cli_reply("HIST", "name=%s period_us=%lu n=%lu step_us=%d",
            name, (unsigned long)p->period_us, (unsigned long)p->n, PERF_HIST_STEP_US);
  for (unsigned b = 0; b < PERF_HIST_BINS; b++) {
    long lo = perf_hist_lo_us(b);

    if (p->hist[b] == 0u)
      continue;
    if (b == 0u)
      cli_reply("HIST", "name=%s lo_us=-inf hi_us=%d n=%lu", name, PERF_HIST_LO_US,
                (unsigned long)p->hist[b]);
    else if (b == PERF_HIST_BINS - 1u)
      cli_reply("HIST", "name=%s lo_us=%ld hi_us=inf n=%lu", name, lo, (unsigned long)p->hist[b]);
    else
      cli_reply("HIST", "name=%s lo_us=%ld hi_us=%ld n=%lu", name, lo,
                lo + PERF_HIST_STEP_US, (unsigned long)p->hist[b]);
  }
}
#endif

/* Перший коректний вимір сенсора (лише раз): мітка часу для рядка STARTUP у
 * stats і EVT ready_<сенсор>. STARTUP не загубиться, навіть якщо скрипт підключився до
 * UART уже після старту. 32 біти мкс вистачає на 71 хв від скидання */
static void ready_mark(unsigned what)
{
  uint32_t t;

  if (ready_us[what] != 0u)
    return;
  t = (uint32_t)perf_now_us64();
  ready_us[what] = t ? t : 1u;
}

/* EVT ready_<сенсор> друкується наприкінці проходу циклу, поза задачами: рядок
 * (~3,5 мс) не потрапляє ні в час задачі, ні в шлях PARCEL_IN -> CCR */
static void ready_flush(void)
{
  static const char *const name[RDY_COUNT] = {
    [RDY_GATE] = "ready_gate", [RDY_CLASS] = "ready_class",
    [RDY_IMU] = "ready_imu", [RDY_LCD] = "ready_lcd",
  };
  static uint8_t printed;

  if (printed == (1u << RDY_COUNT) - 1u)
    return;
  for (unsigned i = 0; i < RDY_COUNT; i++) {
    if (ready_us[i] != 0u && !(printed & (1u << i))) {
      printed |= (uint8_t)(1u << i);
      evt(name[i], ready_us[i]);
    }
  }
}

/* Поточні значення ручок: після BOOT, у stats і у відповідь на set */
static void knobs_line(void)
{
  cli_reply("KNOBS", "lcd_ms=%u lcd_chars=%u imu_ms=%u log=%u", (unsigned)lcd_period_ms,
            (unsigned)lcd_chars, (unsigned)imu_period_ms, (unsigned)log_level);
}

/* Команда set (docs/scenarios.md, 6.2.5). Зміна періоду обнуляє джитер задачі:
 * старий номінальний період уже не той. lcd_chars — що на екрані, вже невідомо,
 * тож наступний період перепише всі символи (порціями по lcd_chars) */
static void set_knob(const char *name, int32_t v)
{
  if (strcmp(name, "lcd_ms") == 0 && v >= 10) {
    lcd_period_ms = (uint16_t)v;
    perf_period_init(&per_lcd, (uint32_t)v * 1000u);
  } else if (strcmp(name, "imu_ms") == 0 && v >= 1 && v <= 1000) {
    imu_period_ms = (uint16_t)v;
    perf_period_init(&per_imu, (uint32_t)v * 1000u);
  } else if (strcmp(name, "lcd_chars") == 0 && v <= (int32_t)(2u * LCD1602_COLS)) {
    lcd_chars = (uint8_t)v;
    memset(lcd_shown, 0, sizeof lcd_shown);
    lcd_pending = 0;
  } else if (strcmp(name, "log") == 0 && v <= 2) {
    log_level = (uint8_t)v;
  } else {
    cli_reply("SET", "name=%s st=BAD", name);
    return;
  }
  knobs_line();
}

/* Журнал подій (методичка 5.2): "<мс> EVT t_us=<64-біт мкс> name=<подія>".
 * Форматер (App/fmt.c) не має %llu, тому 64 біти — двома частинами по 10^9.
 * Як і решта подій, вимикається командою log off. */
static void evt(const char *name, uint64_t t_us)
{
  uint32_t lo, hi;

  if (log_level < 2)
    return;
  hi = perf_udiv64(t_us, 1000000000u, &lo);

  if (hi != 0u)
    ev_log("EVT", "t_us=%lu%09lu name=%s", (unsigned long)hi, (unsigned long)lo, name);
  else
    ev_log("EVT", "t_us=%lu name=%s", (unsigned long)lo, name);
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

  uart_hal_line_add("%lu SELFTEST dev=%s status=%s ", (unsigned long)HAL_GetTick(), dev,
                    health_name(h));
  va_start(ap, fmt);
  uart_hal_line_vadd(fmt, ap);
  va_end(ap);
  uart_hal_line_end();

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

static health_t selftest_ldr(selftest_counts_t *cnt, const char *dev, uint32_t channel)
{
  uint16_t raw = adc_hal_read(channel);
  health_t h = health_ldr(raw, ADC_HAL_MAX_RAW);

  selftest_report(cnt, dev, h, "raw=%u", (unsigned)raw);
  return h;
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

  if (selftest_ldr(&cnt, "ldr_gate", BOARD_ADC_CH_GATE) == HEALTH_OK)
    READY(RDY_GATE);
  if (selftest_ldr(&cnt, "ldr_class", BOARD_ADC_CH_CLASS) == HEALTH_OK)
    READY(RDY_CLASS);

  /* Серво: ставимо 90° і читаємо CCR назад */
  servo_hal_set_angle(SERVO_SELFTEST_DEG);
  uint16_t pulse = servo_hal_get_pulse_us();
  h = health_servo(pulse, BOARD_SERVO_MIN_US, BOARD_SERVO_MAX_US);
  selftest_report(&cnt, "servo", h, "pulse_us=%u", (unsigned)pulse);
  if (h == HEALTH_FAIL)
    fault_note("SERVO_RANGE", "Servo range");

  uart_hal_printf("%lu SELFTEST_END ok=%u warn=%u fail=%u",
                  (unsigned long)HAL_GetTick(), cnt.ok, cnt.warn, cnt.fail);
  return cnt;
}

static void lcd_show_selftest(const selftest_counts_t *cnt)
{
  char line[LCD1602_COLS + 1];

  if (!lcd_ready)
    return;
  size_t n = lcdfmt_str(line, 0, sizeof line, "ST ok=", 0);   /* "ST ok=%u w=%u f=%u" */
  n = lcdfmt_u32(line, n, sizeof line, cnt->ok, 0);
  n = lcdfmt_str(line, n, sizeof line, " w=", 0);
  n = lcdfmt_u32(line, n, sizeof line, cnt->warn, 0);
  n = lcdfmt_str(line, n, sizeof line, " f=", 0);
  lcdfmt_u32(line, n, sizeof line, cnt->fail, 0);
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
      con_write("\r\n", 2);
  } else if (ch == 0x08 || ch == 0x7F) {
    con_write("\b \b", 3);
  } else {
    con_write(&ch, 1);
  }
  prev = ch;
}

static void cmd_run(const cli_cmd_t *cmd)
{
  switch (cmd->id) {
  case CLI_HELP:
    uart_hal_printf("%s", help_line);
#ifdef LAB2_CAP
    for (size_t i = 0; i < sizeof help_lines / sizeof help_lines[0]; i++)
      uart_hal_printf("%s", help_lines[i]);
#endif
    break;
  case CLI_STATUS: {
    sorter_t s;

    LOCK(SORTER);           /* копія: рядок у UART довгий, а м'ютекс потрібен задачам вище */
    s = sorter;
    UNLOCK(SORTER);
    cli_reply("STATUS", "state=%s a=%lu b=%lu reject=%lu total=%lu impact=%u log=%u fault=%s",
              sorter_state_name(s.state),
              (unsigned long)s.count[BIN_A], (unsigned long)s.count[BIN_B],
              (unsigned long)s.count[BIN_REJECT], (unsigned long)sorter_total(&s),
              (unsigned)s.impact, (unsigned)(log_level != 0u),
              fault_reason != NULL ? fault_reason : "NONE");
    break;
  }
  case CLI_RESET: {
    uint32_t total;

    LOCK(SORTER);
    sorter_reset_counters(&sorter);
    total = sorter_total(&sorter);
    UNLOCK(SORTER);
    cli_reply("RESET", "total=%lu", (unsigned long)total);
    break;
  }
  case CLI_LOG:
    log_level = cmd->arg ? 2u : 0u;
    cli_reply("LOG", "on=%u", (unsigned)(log_level != 0u));
    break;
#if PERF
  case CLI_STATS:       stats_report(); break;
  case CLI_STATS_RESET: stats_reset(); break;
  case CLI_LOAD:        load_report(); break;
  case CLI_STACK:       stack_report(); break;
  case CLI_HEAP:        heap_report(); break;
#if HIST_CMD
  case CLI_HIST:        hist_report(cmd->name); break;
#endif
  case CLI_SET:         set_knob(cmd->name, cmd->arg); break;
#if ARCH_RTOS
  case CLI_TASKS:       tasks_list(cli_reply); break;
#endif
#ifdef OSMETRICS
  case CLI_OSBENCH:
    tasks_osbench(OSBENCH_N);
    cli_reply("OSBENCH", "n=%u", (unsigned)OSBENCH_N);
    break;
#endif
#endif
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
  uart_hal_printf("I2C scan:");
  for (uint8_t a = 0x08; a < 0x78; a++)
    if (HAL_I2C_IsDeviceReady(BOARD_I2C, a << 1, 2, 10) == HAL_OK)
      uart_hal_printf("  found 0x%02X", a);
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
  uart_hal_printf("accel st=%d raw %6d %6d %6d | mg %5ld %5ld %5ld | gate %4u class %4u",
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

  uart_hal_line_add("%lu CAP %s st=%s ", (unsigned long)HAL_GetTick(), name, st);
  va_start(ap, fmt);
  uart_hal_line_vadd(fmt, ap);
  va_end(ap);
  uart_hal_line_end();
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
  con_write("\r\n", 2);
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
