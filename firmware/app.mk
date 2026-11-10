# Надбудова над згенерованим Makefile. CubeMX перезаписує firmware/Makefile
# при кожній регенерації з firmware.ioc, тож власні каталоги (Platform/, Devices/, App/)
# і суворіші прапорці для них підключаємо тут: make -f app.mk.

include Makefile

# Бюджет flash: -Og давав 28616 байт text із 32 КБ
OPT = -Os

# Заповнення FLASH/RAM за регіонами скрипту лінкера — у вивід кожної збірки
LDFLAGS += -Wl,--print-memory-usage

USER_DIRS    = Platform Devices App
USER_SOURCES = $(foreach d,$(USER_DIRS),$(wildcard $(d)/*.c))

C_SOURCES   += $(USER_SOURCES)
C_INCLUDES  += $(addprefix -I,$(USER_DIRS))
vpath %.c $(USER_DIRS)

USER_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(USER_SOURCES:.c=.o)))
$(USER_OBJECTS): CFLAGS += -Wextra -Werror

# main.c генерує CubeMX, але в секціях USER CODE — власний код (composition root),
# тож і для нього -Wextra -Werror. Решта Core/ і Drivers/ — лише -Wall.
$(BUILD_DIR)/main.o: CFLAGS += -Wextra -Werror

# Пререквізити правила лінкування в Makefile розкриваються в момент читання
# (до C_SOURCES +=), тому власні об'єкти додаємо явно — інакше ld їх не знайде.
$(BUILD_DIR)/$(TARGET).elf: $(USER_OBJECTS)

# Варіанти збірки (за замовчуванням — лише продукт):
#   LAB2_CAP=1       — вимірювальний harness ЛР2: команди cap …, I2C-скан, телеметрія stream
#   TEST_I2C_FAULT=1 — тест FR-09 з PRD: періодичне звернення до відсутньої адреси 0x69
BUILD_OPTS := LAB2_CAP TEST_I2C_FAULT
C_DEFS += $(foreach o,$(BUILD_OPTS),$(if $(filter 1,$($(o))),-D$(o)))

# Об'єкти в Makefile залежать лише від .c і Makefile, тож після зміни варіанта
# make узяв би старі .o. Варіант пишемо у файл, лише коли він змінився, і всі
# об'єкти від нього залежать: перемикання варіанта = повна перезбірка.
BUILD_CFG  = $(BUILD_DIR)/build.cfg
BUILD_LINE = $(foreach o,$(BUILD_OPTS),$(o)=$(if $(filter 1,$($(o))),1,0))

$(BUILD_CFG): FORCE | $(BUILD_DIR)
	@echo '$(BUILD_LINE)' | cmp -s - $@ || echo '$(BUILD_LINE)' > $@

$(OBJECTS): $(BUILD_CFG)

.PHONY: FORCE
FORCE:
