#ifndef LCD1602_H
#define LCD1602_H

/* Драйвер символьного LCD 16x2 (контролер HD44780) через I²C-розширювач PCF8574
 * поверх абстрактної шини i2c_bus_t. Інтерфейс HD44780 — 4-бітний. */

#include <stdint.h>
#include "i2c_bus.h"

#define LCD1602_COLS 16u
#define LCD1602_ROWS 2u

typedef enum { LCD_OK = 0, LCD_ERR_BUS } lcd_status_t;

typedef struct {
    const i2c_bus_t *bus;
    uint8_t addr;                  /* 7-бітна адреса PCF8574 */
    uint8_t backlight;             /* 1 — підсвітка ввімкнена (біт P3) */
    void (*delay_ms)(uint32_t);    /* затримка від платформи: драйвер сам не чекає */
} lcd1602_t;

/* Перевірка присутності: запис 0x08 (лише підсвітка, EN = 0) і читання 1 байта
 * порту назад. ID-регістру в PCF8574 немає (pinout.md §2). */
lcd_status_t lcd1602_probe(const i2c_bus_t *bus, uint8_t addr, uint8_t *port_readback);

/* Ініціалізація HD44780 у 4-бітний режим, 2 рядки, 5x8, курсор вимкнено. */
lcd_status_t lcd1602_init(lcd1602_t *d, const i2c_bus_t *bus, uint8_t addr, void (*delay_ms)(uint32_t));

/* Очищує екран і ставить курсор у (0, 0). */
lcd_status_t lcd1602_clear(lcd1602_t *d);

/* Курсор у колонку col рядка row (0 — DDRAM 0x00, 1 — DDRAM 0x40). */
lcd_status_t lcd1602_set_cursor(lcd1602_t *d, uint8_t col, uint8_t row);

/* Виводить рядок з поточної позиції курсора. */
lcd_status_t lcd1602_print(lcd1602_t *d, const char *s);

#endif /* LCD1602_H */
