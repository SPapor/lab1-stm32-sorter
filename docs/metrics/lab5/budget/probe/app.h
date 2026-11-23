#ifndef APP_H
#define APP_H
#include "qpc.h"
#include <stdint.h>
enum Signals {
    STATUS_SIG = Q_USER_SIG,
    MAX_PUB_SIG,
    ESTOP_EDGE_SIG = MAX_PUB_SIG, DEBOUNCE_SIG, ESTOP_PRESS_SIG,
    GATE_TICK_SIG, PARCEL_IN_SIG, PARCEL_OUT_SIG,
    IMPACT_SIG, STATUS_REQ_SIG, RESET_REQ_SIG,
    BUS_REQ_SIG, BUS_DONE_SIG, BUS_RETRY_SIG,
    IMU_TICK_SIG, SET_PERIOD_SIG,
    LCD_TICK_SIG,
    LINE_SIG, LOG_SIG, SECOND_SIG,
    MAX_SIG
};
typedef struct { QEvt super; uint32_t in_ms; uint32_t in_cyc; uint16_t class_raw; uint16_t gate_raw; } ParcelEvt;
typedef struct { QEvt super; int16_t x, y, z; uint16_t amag; } ImpactEvt;
typedef struct { QEvt super; QActive *reply_to; uint8_t op; uint8_t arg; } BusReqEvt;
typedef struct { QEvt super; uint8_t st; uint8_t op; uint8_t data[6]; } BusDoneEvt;
typedef struct { QEvt super; uint8_t state; uint16_t cnt[3]; const char *fault; } StatusEvt;
typedef struct { QEvt super; uint16_t value; } SetEvt;
typedef struct { QEvt super; const char *event; const char *fmt; uint32_t ms; uint8_t n; uint8_t reply; uintptr_t arg[7]; } LogEvt;
typedef struct { QEvt super; uint8_t len; char buf[32]; } LineEvt;
extern QActive * const AO_Estop;
extern QActive * const AO_Gate;
extern QActive * const AO_Sorter;
extern QActive * const AO_Bus;
extern QActive * const AO_Imu;
extern QActive * const AO_Console;
extern QActive * const AO_Lcd;
void aos_ctor(void);
void work_estop(uint8_t *pressed);
uint16_t work_adc(uint8_t ch);
int work_gate(uint16_t raw);
uint8_t work_sort(uint16_t raw, uint8_t impact, uint16_t *cnt);
uint8_t work_bus(uint8_t op, uint8_t arg, uint8_t *data);
int work_imu(const uint8_t *d, int16_t *xyz);
uint8_t work_lcd_byte(uint8_t i, const StatusEvt *s);
void work_print(const LogEvt *e);
void work_line(const LineEvt *e);
void work_second(void);
void log_ev(QActive *me, const char *event, const char *fmt, uintptr_t a0);
#endif
