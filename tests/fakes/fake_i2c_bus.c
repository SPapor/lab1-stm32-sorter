#include "fake_i2c_bus.h"
#include <stdio.h>
#include <string.h>
#include "unity.h"

/* Unity друкує не-ASCII як \xNN, тож повідомлення англійською. Рядок — RUN_TEST
 * поточного тесту, а не цього файлу. */
#define FAKE_FAIL(m) UNITY_TEST_FAIL(Unity.CurrentTestLineNumber, (m))

static char msg[512];

static const char *op_name(fake_i2c_op_t op)
{
    return op == FAKE_I2C_WRITE ? "write" : "read";
}

/* "write 0x27 [4D 49]" */
static size_t describe(char *s, size_t n, fake_i2c_op_t op, uint8_t addr, const uint8_t *data, size_t len)
{
    size_t k = (size_t)snprintf(s, n, "%s 0x%02X len=%zu", op_name(op), addr, len);

    if (op == FAKE_I2C_WRITE && k < n) {
        k += (size_t)snprintf(s + k, n - k, " [");
        for (size_t i = 0; i < len && i < FAKE_I2C_MAX_DATA && k < n; i++)
            k += (size_t)snprintf(s + k, n - k, i ? " %02X" : "%02X", data[i]);
        if (k < n)
            k += (size_t)snprintf(s + k, n - k, "]");
    }
    return k;
}

static void fail_mismatch(const fake_i2c_t *f, const fake_i2c_txn_t *exp,
                          fake_i2c_op_t op, uint8_t addr, const uint8_t *data, size_t len)
{
    size_t k = (size_t)snprintf(msg, sizeof msg, "txn %zu: expected ", f->count);

    k += describe(msg + k, sizeof msg - k, exp->op, exp->addr, exp->data, exp->len);
    k += (size_t)snprintf(msg + k, sizeof msg - k, ", got ");
    describe(msg + k, sizeof msg - k, op, addr, data, len);
    FAKE_FAIL(msg);
}

static const fake_i2c_fault_t *fault_for(const fake_i2c_t *f, size_t n)
{
    for (size_t i = 0; i < f->n_faults; i++)
        if (f->faults[i].n == n)
            return &f->faults[i];
    return NULL;
}

/* Спільна частина write/read: лічильник, журнал, перевірка сценарію. */
static bus_status_t transact(fake_i2c_t *f, fake_i2c_op_t op, uint8_t addr, uint8_t *data, size_t len)
{
    if (f->count >= FAKE_I2C_MAX_TXN)
        FAKE_FAIL("fake_i2c: transaction log overflow");
    if (len > FAKE_I2C_MAX_DATA)
        FAKE_FAIL("fake_i2c: transaction longer than FAKE_I2C_MAX_DATA");

    fake_i2c_txn_t *rec = &f->log[f->count++];
    const fake_i2c_fault_t *fault = fault_for(f, f->count);
    bus_status_t st;

    rec->op = op;
    rec->addr = addr;
    rec->len = len;
    if (op == FAKE_I2C_WRITE)
        memcpy(rec->data, data, len);

    if (f->mode == FAKE_I2C_SCRIPT) {
        if (f->pos >= f->n_script) {
            snprintf(msg, sizeof msg, "txn %zu: unexpected, all %zu scripted already done: ",
                     f->count, f->n_script);
            describe(msg + strlen(msg), sizeof msg - strlen(msg), op, addr, data, len);
            FAKE_FAIL(msg);
        }
        const fake_i2c_txn_t *exp = &f->script[f->pos++];
        if (exp->op != op || exp->addr != addr || exp->len != len ||
            (op == FAKE_I2C_WRITE && memcmp(exp->data, data, len) != 0))
            fail_mismatch(f, exp, op, addr, data, len);
        st = fault ? fault->status : exp->status;
        if (op == FAKE_I2C_READ && st == BUS_OK)
            memcpy(data, exp->data, len);
    } else if (fault) {
        st = fault->status;
    } else if (addr != f->dev_addr) {
        st = BUS_ERR_NACK;
    } else {
        st = BUS_OK;
        if (op == FAKE_I2C_WRITE) {
            if (len > 0)
                f->ptr = data[0];
            for (size_t i = 1; i < len; i++)
                f->regs[f->ptr++] = data[i];
        } else {
            for (size_t i = 0; i < len; i++)
                data[i] = f->regs[f->ptr++];
        }
    }

    if (op == FAKE_I2C_READ && st == BUS_OK)
        memcpy(rec->data, data, len);
    rec->status = st;
    return st;
}

static bus_status_t fake_write(void *ctx, uint8_t addr7, const uint8_t *data, size_t len)
{
    return transact(ctx, FAKE_I2C_WRITE, addr7, (uint8_t *)data, len);
}

static bus_status_t fake_read(void *ctx, uint8_t addr7, uint8_t *data, size_t len)
{
    return transact(ctx, FAKE_I2C_READ, addr7, data, len);
}

static void init_common(fake_i2c_t *f, fake_i2c_mode_t mode)
{
    memset(f, 0, sizeof *f);
    f->mode = mode;
    f->bus.write = fake_write;
    f->bus.read = fake_read;
    f->bus.ctx = f;
}

void fake_i2c_init_script(fake_i2c_t *f)
{
    init_common(f, FAKE_I2C_SCRIPT);
}

void fake_i2c_init_regs(fake_i2c_t *f, uint8_t dev_addr)
{
    init_common(f, FAKE_I2C_REGS);
    f->dev_addr = dev_addr;
}

static void expect(fake_i2c_t *f, fake_i2c_op_t op, uint8_t addr, const uint8_t *data, size_t len, bus_status_t st)
{
    if (f->n_script >= FAKE_I2C_MAX_TXN)
        FAKE_FAIL("fake_i2c: script too long");
    if (len > FAKE_I2C_MAX_DATA)
        FAKE_FAIL("fake_i2c: transaction longer than FAKE_I2C_MAX_DATA");

    fake_i2c_txn_t *t = &f->script[f->n_script++];
    t->op = op;
    t->addr = addr;
    t->len = len;
    t->status = st;
    if (len > 0)
        memcpy(t->data, data, len);
}

void fake_i2c_expect_write(fake_i2c_t *f, uint8_t addr, const uint8_t *data, size_t len, bus_status_t st)
{
    expect(f, FAKE_I2C_WRITE, addr, data, len, st);
}

void fake_i2c_expect_read(fake_i2c_t *f, uint8_t addr, const uint8_t *data, size_t len, bus_status_t st)
{
    expect(f, FAKE_I2C_READ, addr, data, len, st);
}

void fake_i2c_fail_at(fake_i2c_t *f, size_t n, bus_status_t st)
{
    if (f->n_faults >= FAKE_I2C_MAX_FAULTS)
        FAKE_FAIL("fake_i2c: too many injected faults");
    f->faults[f->n_faults].n = n;
    f->faults[f->n_faults].status = st;
    f->n_faults++;
}

void fake_i2c_verify(const fake_i2c_t *f)
{
    if (f->pos != f->n_script) {
        snprintf(msg, sizeof msg, "done %zu of %zu scripted transactions", f->pos, f->n_script);
        FAKE_FAIL(msg);
    }
}
