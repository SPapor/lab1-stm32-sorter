# Надбудова над згенерованим Makefile. CubeMX перезаписує firmware/Makefile
# при кожній регенерації з firmware.ioc, тож власні каталоги (Platform/, Devices/, App/)
# і суворіші прапорці для них підключаємо тут: make -f app.mk.

include Makefile

USER_DIRS    = Platform Devices App
USER_SOURCES = $(foreach d,$(USER_DIRS),$(wildcard $(d)/*.c))

C_SOURCES   += $(USER_SOURCES)
C_INCLUDES  += $(addprefix -I,$(USER_DIRS))
vpath %.c $(USER_DIRS)

USER_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(USER_SOURCES:.c=.o)))
$(USER_OBJECTS): CFLAGS += -Wextra -Werror

# Пререквізити правила лінкування в Makefile розкриваються в момент читання
# (до C_SOURCES +=), тому власні об'єкти додаємо явно — інакше ld їх не знайде.
$(BUILD_DIR)/$(TARGET).elf: $(USER_OBJECTS)
