# Хостові тести Devices/ і App/ (методичка ЛР2, п. 4.6). Запускається в Docker
# з кореневого Makefile: make test, make coverage. Unity v2.7.0 — tests/unity/.
# Кожен test_*.c — окремий виконуваний файл зі своїм каталогом об'єктів,
# тож .gcda спільного fake_i2c_bus.c не перетинаються.

FW      = ../firmware
BUILD   = ../build/tests
COV_DIR = ../build/coverage

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -Werror -O0 -g --coverage -fprofile-abs-path -MMD -MP
CPPFLAGS = -I$(FW)/Devices -I$(FW)/App -Iunity -Ifakes
LDFLAGS  = --coverage

TESTS = test_mpu6050 test_lcd1602 test_cli test_cli_noperf test_cli_lab2 \
        test_period test_gate test_classify test_impact test_sorter test_health \
        test_perf test_memmark test_lcdfmt

test_mpu6050_SRC = test_mpu6050.c fakes/fake_i2c_bus.c $(FW)/Devices/mpu6050.c
test_lcd1602_SRC = test_lcd1602.c fakes/fake_i2c_bus.c $(FW)/Devices/lcd1602.c
test_cli_SRC     = test_cli.c $(FW)/App/cli.c
test_cli_DEFS    = -DPERF=1

# Той самий cli.c і test_cli.c у варіантах прошивки: без вимірювань (PERF=0) і з
# harness ЛР2 (LAB2_CAP=1, а з ним app.mk завжди ставить PERF=0)
test_cli_noperf_SRC  = $(test_cli_SRC)
test_cli_noperf_DEFS = -DPERF=0
test_cli_lab2_SRC    = $(test_cli_SRC)
test_cli_lab2_DEFS   = -DLAB2_CAP -DPERF=0

# Чиста логіка super loop (ЛР3)
test_period_SRC   = test_period.c $(FW)/App/period.c
test_gate_SRC     = test_gate.c $(FW)/App/gate.c
test_classify_SRC = test_classify.c $(FW)/App/classify.c
test_impact_SRC   = test_impact.c $(FW)/App/impact.c
test_sorter_SRC   = test_sorter.c $(FW)/App/sorter.c
test_health_SRC   = test_health.c $(FW)/App/health.c

# Вимірювальний модуль (ЛР3, крок 2)
test_perf_SRC     = test_perf.c $(FW)/App/perf.c
test_memmark_SRC  = test_memmark.c $(FW)/App/memmark.c
# порівнює з snprintf хоста, тож попередження про обрізання там навмисні
test_lcdfmt_SRC   = test_lcdfmt.c $(FW)/App/lcdfmt.c
test_lcdfmt_DEFS  = -Wno-format-truncation

vpath %.c . fakes unity $(FW)/Devices $(FW)/App

define TEST_rules
$(1)_OBJ = $$(addprefix $(BUILD)/$(1)/,$$(notdir $$($(1)_SRC:.c=.o)) unity.o)
$(BUILD)/$(1)/$(1): $$($(1)_OBJ)
	$$(CC) $$(LDFLAGS) -o $$@ $$^
$(BUILD)/$(1)/%.o: %.c | $(BUILD)/$(1)
	$$(CC) $$(CFLAGS) $$(CPPFLAGS) $$($(1)_DEFS) -c -o $$@ $$<
$(BUILD)/$(1):
	mkdir -p $$@
-include $$($(1)_OBJ:.o=.d)
endef
$(foreach t,$(TESTS),$(eval $(call TEST_rules,$(t))))

BINS = $(foreach t,$(TESTS),$(BUILD)/$(t)/$(t))

# Лічильники покриття — лише від цього запуску. Падіння будь-якого тесту → код 1.
test: $(BINS)
	@find $(BUILD) -name '*.gcda' -delete
	@fail=0; for t in $(TESTS); do echo "== $$t"; $(BUILD)/$$t/$$t || fail=1; done; exit $$fail

# Звіт по Devices/ і App/: рядки і гілки текстом, HTML у build/coverage/.
GCOVR = cd .. && gcovr --root . --object-directory build/tests \
        --filter 'firmware/Devices/' --filter 'firmware/App/'

coverage: test
	mkdir -p $(COV_DIR)
	$(GCOVR)
	$(GCOVR) --branches
	$(GCOVR) --html-details build/coverage/index.html --print-summary \
	         --fail-under-line 80 --fail-under-branch 80

clean:
	rm -rf $(BUILD) $(COV_DIR)

.PHONY: test coverage clean
