#include "lcd1602.h"

/* Виводи PCF8574 → сигнали HD44780 на модулі LCD1602 I²C */
#define LCD_BIT_RS          0x01u   /* P0: 0 — команда, 1 — дані */
#define LCD_BIT_RW          0x02u   /* P1: 0 — запис; читання з LCD не використовуємо */
#define LCD_BIT_EN          0x04u   /* P2: строб, HD44780 захоплює D4..D7 на спаді */
#define LCD_BIT_BL          0x08u   /* P3: підсвітка */
#define LCD_DATA_SHIFT      4u      /* P4..P7 = D4..D7 */
#define LCD_NIBBLE_MASK     0x0Fu

/* Команди HD44780U (Table 6 «Instructions») */
#define LCD_CMD_CLEAR       0x01u   /* Clear display */
#define LCD_CMD_ENTRY_MODE  0x04u   /* Entry mode set */
#define LCD_ENTRY_INC       0x02u   /*   I/D = 1: адреса DDRAM +1 після запису */
#define LCD_CMD_DISPLAY     0x08u   /* Display on/off control */
#define LCD_DISPLAY_ON      0x04u   /*   D = 1; C = B = 0: курсор не показуємо */
#define LCD_CMD_FUNCTION    0x20u   /* Function set; DL = 0 — 4 біти */
#define LCD_FUNC_2LINE      0x08u   /*   N = 1: 2 рядки; F = 0: шрифт 5x8 */
#define LCD_CMD_SET_DDRAM   0x80u   /* Set DDRAM address */

#define LCD_ROW0_DDRAM      0x00u
#define LCD_ROW1_DDRAM      0x40u

/* Ніббли «Initializing by Instruction» (Figure 24): старша тетрада Function set */
#define LCD_INIT_8BIT       0x3u    /* DL = 1 */
#define LCD_INIT_4BIT       0x2u    /* DL = 0 */

/* Затримки взято з запасом над мінімумами Figure 24: > 15 мс після живлення,
 * > 4,1 мс, > 100 мкс. Clear display — найповільніша команда (порядку 1,5 мс). */
#define LCD_DELAY_POWER_MS  50u
#define LCD_DELAY_INIT1_MS  5u
#define LCD_DELAY_INIT2_MS  1u
#define LCD_DELAY_CLEAR_MS  2u

/* Решта команд виконується ≈ 37 мкс, а запис одного байта на I²C при 100 кГц
 * триває ≈ 90 мкс, тож окремих затримок між ними не потрібно; ширина імпульсу EN
 * так само на порядки більша за мінімум HD44780. */

static uint8_t bl_bit(const lcd1602_t *d)
{
    return d->backlight ? LCD_BIT_BL : 0u;
}

/* Усі записи на шину — рівно по 1 байту: 2-байтові записи у Wokwi C0
 * обриваються TIMEOUT (docs/journal.md, проблема 8). */
static lcd_status_t write_port(const i2c_bus_t *bus, uint8_t addr, uint8_t v)
{
    return bus->write(bus->ctx, addr, &v, 1) == BUS_OK ? LCD_OK : LCD_ERR_BUS;
}

/* Один нібл = два записи: з EN = 1, потім той самий байт з EN = 0. */
static lcd_status_t write_nibble(const lcd1602_t *d, uint8_t nibble, uint8_t rs)
{
    uint8_t v = (uint8_t)((nibble & LCD_NIBBLE_MASK) << LCD_DATA_SHIFT) | rs | bl_bit(d);

    if (write_port(d->bus, d->addr, v | LCD_BIT_EN) != LCD_OK)
        return LCD_ERR_BUS;
    return write_port(d->bus, d->addr, v);
}

/* Байт у 4-бітному режимі: спершу старша тетрада, потім молодша. */
static lcd_status_t write_byte(const lcd1602_t *d, uint8_t b, uint8_t rs)
{
    if (write_nibble(d, b >> LCD_DATA_SHIFT, rs) != LCD_OK)
        return LCD_ERR_BUS;
    return write_nibble(d, b & LCD_NIBBLE_MASK, rs);
}

static lcd_status_t command(const lcd1602_t *d, uint8_t cmd)
{
    return write_byte(d, cmd, 0u);
}

lcd_status_t lcd1602_probe(const i2c_bus_t *bus, uint8_t addr, uint8_t *port_readback)
{
    if (write_port(bus, addr, LCD_BIT_BL) != LCD_OK)
        return LCD_ERR_BUS;
    if (bus->read(bus->ctx, addr, port_readback, 1) != BUS_OK)
        return LCD_ERR_BUS;
    return LCD_OK;
}

/* Послідовність — HD44780U datasheet (Hitachi ADE-207-272), Figure 24
 * «4-Bit Interface», с. 46. Перші три ніббли 0x3 переводять контролер у 8-бітний
 * режим з будь-якого стану, четвертий 0x2 — у 4-бітний; далі команди повними байтами. */
lcd_status_t lcd1602_init(lcd1602_t *d, const i2c_bus_t *bus, uint8_t addr, void (*delay_ms)(uint32_t))
{
    d->bus = bus;
    d->addr = addr;
    d->backlight = 1u;
    d->delay_ms = delay_ms;

    /* Порт у відомий стан: RS = RW = EN = 0, підсвітка ввімкнена */
    if (write_port(bus, addr, bl_bit(d)) != LCD_OK)
        return LCD_ERR_BUS;
    d->delay_ms(LCD_DELAY_POWER_MS);

    if (write_nibble(d, LCD_INIT_8BIT, 0u) != LCD_OK)
        return LCD_ERR_BUS;
    d->delay_ms(LCD_DELAY_INIT1_MS);
    if (write_nibble(d, LCD_INIT_8BIT, 0u) != LCD_OK)
        return LCD_ERR_BUS;
    d->delay_ms(LCD_DELAY_INIT2_MS);
    if (write_nibble(d, LCD_INIT_8BIT, 0u) != LCD_OK)
        return LCD_ERR_BUS;
    d->delay_ms(LCD_DELAY_INIT2_MS);
    if (write_nibble(d, LCD_INIT_4BIT, 0u) != LCD_OK)
        return LCD_ERR_BUS;

    if (command(d, LCD_CMD_FUNCTION | LCD_FUNC_2LINE) != LCD_OK ||
        command(d, LCD_CMD_DISPLAY) != LCD_OK ||   /* display off */
        lcd1602_clear(d) != LCD_OK ||
        command(d, LCD_CMD_ENTRY_MODE | LCD_ENTRY_INC) != LCD_OK ||
        command(d, LCD_CMD_DISPLAY | LCD_DISPLAY_ON) != LCD_OK)
        return LCD_ERR_BUS;
    return LCD_OK;
}

lcd_status_t lcd1602_clear(lcd1602_t *d)
{
    if (command(d, LCD_CMD_CLEAR) != LCD_OK)
        return LCD_ERR_BUS;
    d->delay_ms(LCD_DELAY_CLEAR_MS);
    return LCD_OK;
}

lcd_status_t lcd1602_set_cursor(lcd1602_t *d, uint8_t col, uint8_t row)
{
    uint8_t base = (row == 0u) ? LCD_ROW0_DDRAM : LCD_ROW1_DDRAM;

    if (col >= LCD1602_COLS)
        col = LCD1602_COLS - 1u;
    return command(d, (uint8_t)(LCD_CMD_SET_DDRAM | (base + col)));
}

lcd_status_t lcd1602_print(lcd1602_t *d, const char *s)
{
    for (; *s != '\0'; s++)
        if (write_byte(d, (uint8_t)*s, LCD_BIT_RS) != LCD_OK)
            return LCD_ERR_BUS;
    return LCD_OK;
}
