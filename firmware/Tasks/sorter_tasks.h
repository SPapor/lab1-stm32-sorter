#ifndef SORTER_TASKS_H
#define SORTER_TASKS_H

/* Задачі продукту в ARCH=rtos (docs/rtos-design.md, розділ 6): estop, gate,
 * sort, imu, cli, logger, lcd, черги, м'ютекси, прийом UART перериванням.
 * Сама робота задач - функції main.c (ті самі, що в super loop), вони приходять
 * сюди в tasks_ops_t. Заголовок без FreeRTOS: його включає main.c. */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "fmt.h"

/* Скільки аргументів формату несе запис черги журналу (найбільше - рядок SORT) */
#define TASKS_LOG_ARGS 7u
#define TASKS_LCD_ROW 17u
#define TASKS_GATE_IN 1u
#define TASKS_GATE_OUT 2u

typedef void (*tasks_out_fn)(const char *event, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

typedef struct {
    void (*estop)(void);                     /* після антибрязкоту; сама перевіряє, чи був фронт */
#if ARCH_QPC
    uint8_t (*gate)(void);
#else
    void (*gate)(void);                      /* кожні gate_ms */
#endif
    void (*sort)(uint32_t in_ms, uint32_t in_cyc);   /* PARCEL_IN з черги */
    void (*imu)(void);                       /* кожні *imu_ms; NULL - MPU немає, задача спить */
    void (*lcd)(void);                       /* кожні *lcd_ms; NULL - LCD немає, задача спить */
    void (*cli)(int ch);                     /* перший байт; решту - tasks_rx_getc */
    void (*log_line)(uint32_t ms, const char *event, const char *fmt,
                     const fmt_arg_t *args, unsigned n);   /* запис qLog -> UART */
    void (*second)(void);                    /* logger, раз на секунду (LOAD, EVT ready_*) */
    uint16_t estop_debounce_ms;
    uint16_t gate_ms;
    const volatile uint16_t *imu_ms;         /* ручки set imu_ms і set lcd_ms */
    const volatile uint16_t *lcd_ms;
#if ARCH_QPC
    void (*imu_begin)(void);
    uint8_t (*bus_imu_read)(uint8_t raw[6]);
    void (*imu_end)(uint8_t st, const uint8_t raw[6]);
    void (*lcd_begin)(char rows[2][TASKS_LCD_ROW]);
    uint8_t (*bus_lcd_row)(uint8_t row, const char *text);
    void (*lcd_end)(uint8_t st);
    void (*bus_wait)(uint32_t cyc);
#endif
} tasks_ops_t;

typedef enum {
    TASKS_LOCK_ADC = 0,
    TASKS_LOCK_I2C,
    TASKS_LOCK_SORTER,
    TASKS_LOCK_UART,
    TASKS_LOCK_COUNT
} tasks_lock_t;

/* Створює черги, м'ютекси й задачі і запускає ядро. Не повертається.
 * Збірки TEST_KSTRESS і TEST_CRITCOST замість задач продукту запускають свої
 * (Tasks/app_task.c), ops не використовується. */
void tasks_start(const tasks_ops_t *ops) __attribute__((noreturn));

/* 1 - ядро запущено: події йдуть через tasks_log, а м'ютекси справжні.
 * До того (BOOT, самоперевірка) усе друкується одразу, а tasks_lock нічого не робить */
int tasks_running(void);

/* Подія FR-10 у чергу qLog з міткою HAL_GetTick: аргументи копіюються (рядки %s
 * мають жити довше - літерали), друкує задача logger. Без очікування: черга
 * повна - запис губиться (лічильник drop у рядку LOGQ). Лише з задач */
void tasks_log(const char *event, const char *fmt, va_list ap)
    __attribute__((format(printf, 2, 0)));

/* PARCEL_IN від gate для sort (черга qParcel): мс і такти perf_now моменту виявлення */
void tasks_parcel(uint32_t in_ms, uint32_t in_cyc);

/* З колбека EXTI E-STOP: будить задачу estop */
void tasks_estop_from_isr(void);

/* Наступний прийнятий байт без очікування або -1 (потоковий буфер з ISR USART2) */
int tasks_rx_getc(void);

/* М'ютекс ресурсу. Чекає з тайм-аутом і повторює: кожен тайм-аут - лічильник у
 * рядку MTX (довге очікування видно, а операція все одно виконається) */
void tasks_lock(tasks_lock_t l);
void tasks_unlock(tasks_lock_t l);

/* Копія структури, яку пише інша задача, за одну критичну секцію (STAT, джитер) */
void tasks_snapshot(void *dst, const void *src, size_t n);

#if PERF
/* Рядки для команд stats, stack і heap */
void tasks_list(tasks_out_fn out);
void tasks_report(tasks_out_fn out);
void tasks_stack_report(tasks_out_fn out);  /* STACK name=<задача> за high water mark */
void tasks_heap_report(tasks_out_fn out);   /* OSHEAP: купа ядра */
void tasks_stats_reset(void);
/* Такти, які idle мала від старту ядра (run-time stats) */
uint64_t tasks_idle_cyc(void);
#ifdef TEST_INVERSION
#include "perf.h"
const perf_stat_t *tasks_inv_wait(void);
#endif
#ifdef OSMETRICS
void tasks_osbench(unsigned n);
#endif
#endif

#endif /* SORTER_TASKS_H */
