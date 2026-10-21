#ifndef FAKE_I2C_BUS_H
#define FAKE_I2C_BUS_H

/* Тестовий дубль i2c_bus_t для хостових тестів драйверів Devices/.
 *
 * Режими:
 *  - FAKE_I2C_SCRIPT (mock): тест задає очікувану послідовність транзакцій
 *    {write|read, адреса, байти, статус}; перша розбіжність або зайва транзакція
 *    валить тест, fake_i2c_verify перевіряє, що виконано всі;
 *  - FAKE_I2C_REGS: модель пристрою з регістрами (MPU-6050). Перший байт запису —
 *    вказівник регістра, далі запис і читання з автоінкрементом; чужа адреса — NACK.
 * В обох режимах fake_i2c_fail_at змушує N-ту транзакцію повернути помилку
 * (у REGS вона не змінює регістрів), а всі транзакції пишуться в log. */

#include <stddef.h>
#include <stdint.h>
#include "i2c_bus.h"

#define FAKE_I2C_MAX_DATA    16u
#define FAKE_I2C_MAX_TXN     128u
#define FAKE_I2C_MAX_FAULTS  8u

typedef enum { FAKE_I2C_WRITE, FAKE_I2C_READ } fake_i2c_op_t;
typedef enum { FAKE_I2C_SCRIPT, FAKE_I2C_REGS } fake_i2c_mode_t;

typedef struct {
    fake_i2c_op_t op;
    uint8_t addr;
    uint8_t data[FAKE_I2C_MAX_DATA];   /* write — очікувані байти, read — що віддати */
    size_t len;
    bus_status_t status;
} fake_i2c_txn_t;

typedef struct {
    size_t n;                /* номер транзакції, з 1 */
    bus_status_t status;
} fake_i2c_fault_t;

typedef struct {
    i2c_bus_t bus;           /* передається драйверу */
    fake_i2c_mode_t mode;

    fake_i2c_txn_t script[FAKE_I2C_MAX_TXN];
    size_t n_script;
    size_t pos;

    uint8_t dev_addr;
    uint8_t regs[256];
    uint8_t ptr;

    fake_i2c_fault_t faults[FAKE_I2C_MAX_FAULTS];
    size_t n_faults;

    fake_i2c_txn_t log[FAKE_I2C_MAX_TXN];   /* status — те, що повернуто драйверу */
    size_t count;
} fake_i2c_t;

void fake_i2c_init_script(fake_i2c_t *f);
void fake_i2c_init_regs(fake_i2c_t *f, uint8_t dev_addr);

void fake_i2c_expect_write(fake_i2c_t *f, uint8_t addr, const uint8_t *data, size_t len, bus_status_t st);
void fake_i2c_expect_read(fake_i2c_t *f, uint8_t addr, const uint8_t *data, size_t len, bus_status_t st);

/* Транзакція номер n (з 1) поверне st замість звичайного результату. */
void fake_i2c_fail_at(fake_i2c_t *f, size_t n, bus_status_t st);

/* SCRIPT: виконано рівно всі очікувані транзакції. */
void fake_i2c_verify(const fake_i2c_t *f);

#endif /* FAKE_I2C_BUS_H */
