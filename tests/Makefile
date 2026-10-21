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

TESTS = test_mpu6050 test_lcd1602 test_cli

test_mpu6050_SRC = test_mpu6050.c fakes/fake_i2c_bus.c $(FW)/Devices/mpu6050.c
test_lcd1602_SRC = test_lcd1602.c fakes/fake_i2c_bus.c $(FW)/Devices/lcd1602.c
test_cli_SRC     = test_cli.c $(FW)/App/cli.c

vpath %.c . fakes unity $(FW)/Devices $(FW)/App

define TEST_rules
$(1)_OBJ = $$(addprefix $(BUILD)/$(1)/,$$(notdir $$($(1)_SRC:.c=.o)) unity.o)
$(BUILD)/$(1)/$(1): $$($(1)_OBJ)
	$$(CC) $$(LDFLAGS) -o $$@ $$^
$(BUILD)/$(1)/%.o: %.c | $(BUILD)/$(1)
	$$(CC) $$(CFLAGS) $$(CPPFLAGS) -c -o $$@ $$<
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
