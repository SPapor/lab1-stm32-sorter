#include "app.h"
extern volatile uint32_t probe_sink;
#define SINK(x) (probe_sink += (uint32_t)(x))
__attribute__((noinline)) void work_estop(uint8_t *pressed) { *pressed = (uint8_t)probe_sink; }
__attribute__((noinline)) uint16_t work_adc(uint8_t ch) { return (uint16_t)(probe_sink + ch); }
__attribute__((noinline)) int work_gate(uint16_t raw) { SINK(raw); return (int)(probe_sink & 3u); }
__attribute__((noinline)) uint8_t work_sort(uint16_t raw, uint8_t impact, uint16_t *cnt) { cnt[raw & 1u]++; SINK(impact); return 1; }
__attribute__((noinline)) uint8_t work_bus(uint8_t op, uint8_t arg, uint8_t *data) { for (int i = 0; i < 6; i++) data[i] = (uint8_t)(probe_sink + op + arg); return (uint8_t)(probe_sink & 1u); }
__attribute__((noinline)) int work_imu(const uint8_t *d, int16_t *xyz) { xyz[0] = d[0]; xyz[1] = d[2]; xyz[2] = d[4]; return d[1] > 100; }
__attribute__((noinline)) uint8_t work_lcd_byte(uint8_t i, const StatusEvt *s) { return (uint8_t)(i + s->state); }
__attribute__((noinline)) void work_print(const LogEvt *e) { SINK(e->event); SINK(e->ms); }
__attribute__((noinline)) void work_line(const LineEvt *e) { SINK(e->len); }
__attribute__((noinline)) void work_second(void) { SINK(1); }
uint64_t rtos_hal_runtime_now(void) { return probe_sink; }
void rtos_hal_runtime_start(void) { SINK(2); }
void rtos_hal_trace_in(const void *tcb) { SINK(tcb); }
void rtos_hal_assert_failed(uint32_t line) { SINK(line); for (;;) { } }
uint32_t SystemCoreClock = 48000000u;
extern void xPortSysTickHandler(void);
void SysTick_Handler(void) { SINK(3); xPortSysTickHandler(); }
void SystemInit(void) { }
