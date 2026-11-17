# Надбудова над згенерованим Makefile. CubeMX перезаписує firmware/Makefile
# при кожній регенерації з firmware.ioc, тож власні каталоги (Platform/, Devices/, App/,
# Tasks/), ядро FreeRTOS і суворіші прапорці для них підключаємо тут: make -f app.mk.

include Makefile

# Бюджет flash: -Og давав 28616 байт text із 32 КБ
OPT = -Os

# Заповнення FLASH/RAM за регіонами скрипту лінкера — у вивід кожної збірки
LDFLAGS += -Wl,--print-memory-usage

# Архітектура (docs/rtos-design.md): обидві прошивки збираються з одного дерева
# з тим самим кодом вимірювань.
#   ARCH=superloop — super loop ЛР3 (за замовчуванням)
#   ARCH=rtos      — FreeRTOS: ядро з Middlewares/FreeRTOS, задачі з Tasks/
#   LTO=1          — -flto для всього образу (обидві ARCH однаково)
#   TICK_HZ=<Гц>   — частота тіку FreeRTOS, лише для ARCH=rtos (методичка 7.2, п. 6)
# Порожнє значення (кореневий Makefile передає змінні завжди) = за замовчуванням.
ARCH_ON    := $(if $(ARCH),$(ARCH),superloop)
LTO_ON     := $(if $(filter 1,$(LTO)),1,0)
TICK_HZ_ON := $(if $(TICK_HZ),$(TICK_HZ),1000)

ifeq ($(filter $(ARCH_ON),superloop rtos),)
$(error ARCH=$(ARCH_ON): має бути superloop або rtos)
endif
# У super loop тік — це HAL_IncTick раз на 1 мс, його частота не налаштовується
ifeq ($(ARCH_ON),superloop)
ifneq ($(TICK_HZ_ON),1000)
$(error TICK_HZ=$(TICK_HZ_ON) лише для ARCH=rtos; super loop має тік HAL 1000 Гц)
endif
endif

# Platform/<arch>/ — те, що в двох архітектурах різне: обробники SVC, PendSV,
# SysTick, база часу HAL, лічильник тіків для perf_now (perf_tick.h)
USER_DIRS = Platform Platform/$(ARCH_ON) Devices App
ifeq ($(ARCH_ON),rtos)
USER_DIRS += Tasks
endif
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

# Ядро FreeRTOS (сабмодуль, версія — docs/rtos-design.md): лише потрібні файли.
# Порт GCC/ARM_CM0 (Cortex-M0+), heap_4. timers.c і event_groups.c не збираються,
# поки немає вимоги; stream_buffer.c — для прийому UART на кроці 2.
# Чужий код, як і Drivers/, — лише -Wall.
ifeq ($(ARCH_ON),rtos)
RTOS_DIR     = Middlewares/FreeRTOS
RTOS_PORT    = $(RTOS_DIR)/portable/GCC/ARM_CM0
RTOS_SOURCES = $(addprefix $(RTOS_DIR)/,tasks.c queue.c list.c stream_buffer.c) \
               $(RTOS_DIR)/portable/MemMang/heap_4.c \
               $(RTOS_PORT)/port.c $(RTOS_PORT)/portasm.c
RTOS_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(RTOS_SOURCES:.c=.o)))

C_SOURCES  += $(RTOS_SOURCES)
C_INCLUDES += -I$(RTOS_DIR)/include -I$(RTOS_PORT)
C_DEFS     += -DTICK_HZ=$(TICK_HZ_ON)
vpath %.c $(sort $(dir $(RTOS_SOURCES)))

$(BUILD_DIR)/$(TARGET).elf: $(RTOS_OBJECTS)

# Вектор SysTick — SysTick_Handler у Platform/rtos/rtos_hal.c: він рахує тіки для
# perf_now з самого HAL_Init і лише після старту ядра викликає обробник порту.
# Порт V11 визначає обробник під іменем CMSIS, тож у port.c його перейменовуємо
$(BUILD_DIR)/port.o: CFLAGS += -DSysTick_Handler=xPortSysTickHandler
endif
C_DEFS += -DARCH_RTOS=$(if $(filter rtos,$(ARCH_ON)),1,0)

# LTO: оптимізація всього образу на етапі лінкування (docs/rtos-design.md, бюджет
# пам'яті). Код генерується вже в лінкері, тож -Os і секції по функціях треба
# повторити й йому: без -ffunction-sections весь код ліг би в одну секцію .text, і
# пул літералів асемблерних вставок порту (ldr r2, =pxCurrentTCB) опинився б далі
# за 1 КБ від інструкції (docs/journal.md). Кадри стека (.su) з LTO пишуться не по
# файлах, тож make stack-usage має сенс лише з LTO=0.
ifeq ($(LTO_ON),1)
CFLAGS  += -flto
LDFLAGS += -flto $(OPT) -ffunction-sections -fdata-sections

# Те саме правило, що в Makefile CubeMX, але без лістингу -Wa,-a,...=build/X.lst:
# з LTO в .o лише байт-код, лістинг порожній, а різні -Wa у кожного об'єкта
# lto-wrapper відкидає з попередженням
$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@
endif
C_DEFS += -DBUILD_LTO=$(LTO_ON)

# Варіанти збірки (за замовчуванням — продукт з вимірювальним модулем):
#   LAB2_CAP=1       — вимірювальний harness ЛР2: команди cap …, I2C-скан, телеметрія stream
#   TEST_I2C_FAULT=1 — тест FR-09 з PRD: періодичне звернення до відсутньої адреси 0x69
#   PERF=0           — без вимірювального модуля ЛР3 (docs/perf.md); за замовчуванням 1
BUILD_OPTS := LAB2_CAP TEST_I2C_FAULT
C_DEFS += $(foreach o,$(BUILD_OPTS),$(if $(filter 1,$($(o))),-D$(o)))

# PERF завжди визначений (#if PERF): 0 лише явно, порожнє значення = 1.
# Harness ЛР2 і вимірювальний модуль разом не вміщаються в 32 КБ flash
# (docs/perf.md, розділ про розмір), тож з LAB2_CAP=1 за замовчуванням PERF=0.
ifeq ($(LAB2_CAP),1)
ifeq ($(PERF),1)
$(error LAB2_CAP=1 разом з PERF=1 не вміщується в 32 КБ flash (docs/perf.md); зберіть з PERF=0)
endif
PERF_ON := 0
else
PERF_ON := $(if $(filter 0,$(PERF)),0,1)
endif
C_DEFS += -DPERF=$(PERF_ON)

# Об'єкти в Makefile залежать лише від .c і Makefile, тож після зміни варіанта
# make узяв би старі .o. Варіант пишемо у файл, лише коли він змінився, і всі
# об'єкти від нього залежать: перемикання варіанта = повна перезбірка.
# Каталог build/ один на всі варіанти: об'єкти іншої ARCH, що лишилися від
# попередньої збірки, не лінкуються — до ELF ідуть лише $(OBJECTS).
BUILD_CFG  = $(BUILD_DIR)/build.cfg
BUILD_LINE = ARCH=$(ARCH_ON) LTO=$(LTO_ON) TICK_HZ=$(TICK_HZ_ON) \
             $(foreach o,$(BUILD_OPTS),$(o)=$(if $(filter 1,$($(o))),1,0)) PERF=$(PERF_ON)

# Версія прошивки в банері й першому рядку журналу (методичка 5.2, 6): git-хеш,
# з «-dirty», якщо збирали з незакоміченими змінами. Теги (lab3, ...) не беремо:
# у логах лишається 8-значний хеш, як у ЛР3. Кореневий Makefile передає його
# з хоста (у Docker git може не бути). Лише main.o, і перезбирається він лише тоді,
# коли хеш змінився (штамп git.cfg).
GIT_HASH ?= $(shell git -C .. describe --always --dirty --abbrev=8 --exclude='*' 2>/dev/null || echo unknown)
GIT_CFG = $(BUILD_DIR)/git.cfg

$(GIT_CFG): FORCE | $(BUILD_DIR)
	@echo '$(GIT_HASH)' | cmp -s - $@ || echo '$(GIT_HASH)' > $@

$(BUILD_DIR)/main.o: CFLAGS += -DGIT_HASH=\"$(GIT_HASH)\"
$(BUILD_DIR)/main.o: $(GIT_CFG)

# Стек власних функцій (методичка 5.4): build/*.su → tools/stack_usage.py; кадр понад
# 512 байт — попередження, з -Werror — помилка. Map з --cref — у Makefile CubeMX.
$(USER_OBJECTS) $(BUILD_DIR)/main.o: CFLAGS += -fstack-usage -Wstack-usage=512

$(BUILD_CFG): FORCE | $(BUILD_DIR)
	@echo '$(BUILD_LINE)' | cmp -s - $@ || echo '$(BUILD_LINE)' > $@

$(OBJECTS): $(BUILD_CFG)

.PHONY: FORCE
FORCE:
