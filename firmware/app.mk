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
#   SIM_CRIT=1|0   — лише rtos: 1 (за замовчуванням) - порт з обходом для Wokwi
#                    (Platform/rtos/port_sim), 0 - порт із сабмодуля без змін
#   ASSERT=1       — лише rtos: configASSERT і перевірка обробників SVC/PendSV
#                    (у продукті їх немає, docs/rtos-design.md, розділ 4)
# Порожнє значення (кореневий Makefile передає змінні завжди) = за замовчуванням.
ARCH_ON    := $(if $(ARCH),$(ARCH),superloop)
LTO_ON     := $(if $(filter 1,$(LTO)),1,0)
TICK_HZ_ON := $(if $(TICK_HZ),$(TICK_HZ),1000)
SIM_CRIT_ON := $(if $(filter 0,$(SIM_CRIT)),0,1)
# configASSERT ядра: лише ASSERT=1 або стрес-тест (він перевіряє саме ядро)
ASSERT_ON   := $(if $(filter 1,$(ASSERT) $(TEST_KSTRESS) $(TEST_QSTRESS)),1,0)

ifeq ($(filter $(ARCH_ON),superloop rtos qpc),)
$(error ARCH=$(ARCH_ON): має бути superloop, rtos або qpc)
endif
# У super loop тік — це HAL_IncTick раз на 1 мс, його частота не налаштовується
ifeq ($(ARCH_ON),superloop)
ifneq ($(TICK_HZ_ON),1000)
$(error TICK_HZ=$(TICK_HZ_ON) лише для ARCH=rtos; super loop має тік HAL 1000 Гц)
endif
endif

ifeq ($(TEST_APIPROBE)$(ARCH_ON),1superloop)
$(error TEST_APIPROBE=1 лише для ARCH=rtos)
endif
ifeq ($(TEST_KSTRESS)$(ARCH_ON),1superloop)
$(error TEST_KSTRESS=1 лише для ARCH=rtos)
endif
ifeq ($(TEST_CRITCOST)$(ARCH_ON),1superloop)
$(error TEST_CRITCOST=1 лише для ARCH=rtos)
endif
INV_ON := $(if $(filter 1 sem,$(TEST_INVERSION)),1,$(if $(filter mutex,$(TEST_INVERSION)),2,0))
INV_NAME := $(if $(filter 1,$(INV_ON)),sem,$(if $(filter 2,$(INV_ON)),mutex))
ifneq ($(INV_ON)$(ARCH_ON),0superloop)
ifeq ($(ARCH_ON),superloop)
$(error TEST_INVERSION лише для ARCH=rtos)
endif
endif
ifeq ($(OSMETRICS)$(ARCH_ON),1superloop)
$(error OSMETRICS=1 лише для ARCH=rtos)
endif
TEST_OS_DEF_heap               := -DTEST_HEAP=1
TEST_OS_DEF_heap_nohook        := -DTEST_HEAP=2
TEST_OS_DEF_stack              := -DTEST_STACKOVF=1
TEST_OS_DEF_stack_nocheck      := -DTEST_STACKOVF=2
TEST_OS_DEF_stack_near         := -DTEST_STACKOVF=3
TEST_OS_DEF_stack_near_nocheck := -DTEST_STACKOVF=4
TEST_OS_DEF_starve             := -DTEST_STARVE=1
TEST_OS_DEF_qflood_drop        := -DTEST_QFLOOD=1
TEST_OS_DEF_qflood_wait        := -DTEST_QFLOOD=2
TEST_OS_ON := $(if $(TEST_OS),$(TEST_OS),0)
TEST_OS_DEFS :=
ifneq ($(TEST_OS_ON),0)
ifneq ($(words $(TEST_OS_ON)),1)
$(error TEST_OS=$(TEST_OS_ON): лише одна назва)
endif
TEST_OS_DEFS := $(TEST_OS_DEF_$(TEST_OS_ON))
ifeq ($(TEST_OS_DEFS),)
$(error TEST_OS=$(TEST_OS_ON): має бути одне з $(patsubst TEST_OS_DEF_%,%,$(filter TEST_OS_DEF_%,$(.VARIABLES))))
endif
ifeq ($(ARCH_ON),superloop)
$(error TEST_OS лише для ARCH=rtos)
endif
ifneq ($(filter 1,$(TEST_KSTRESS) $(TEST_CRITCOST) $(TEST_APIPROBE) $(OSMETRICS))$(filter-out 0,$(INV_ON)),)
$(error TEST_OS не поєднується з TEST_KSTRESS, TEST_CRITCOST, TEST_APIPROBE, OSMETRICS і TEST_INVERSION)
endif
endif
ifeq ($(ARCH_ON),qpc)
ifneq ($(filter 1,$(TEST_APIPROBE) $(TEST_KSTRESS) $(TEST_CRITCOST) $(OSMETRICS))$(filter-out 0,$(INV_ON) $(TEST_OS_ON)),)
$(error TEST_APIPROBE, TEST_KSTRESS, TEST_CRITCOST, OSMETRICS, TEST_INVERSION і TEST_OS - лише для ARCH=rtos)
endif
ifeq ($(TEST_QSTRESS)$(TEST_QPCHECK),11)
$(error TEST_QSTRESS і TEST_QPCHECK - окремі збірки)
endif
else
ifneq ($(filter 1,$(TEST_QSTRESS) $(TEST_QPCHECK)),)
$(error TEST_QSTRESS і TEST_QPCHECK - лише для ARCH=qpc)
endif
endif

# Platform/<arch>/ — те, що в двох архітектурах різне: обробники SVC, PendSV,
# SysTick, база часу HAL, лічильник тіків для perf_now (perf_tick.h)
ifeq ($(ARCH_ON),qpc)
USER_DIRS = Platform Platform/qpc Platform/rtos Devices App Active
else
USER_DIRS = Platform Platform/$(ARCH_ON) Devices App
endif
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
ifneq ($(filter rtos qpc,$(ARCH_ON)),)
RTOS_DIR     = Middlewares/FreeRTOS
RTOS_PORT    = $(RTOS_DIR)/portable/GCC/ARM_CM0
# SIM_CRIT=1 (за замовчуванням): копія порту з Platform/rtos/port_sim/ - критична
# секція відкладає переривання, тік ядра і PendSV, бо Wokwi не враховує PRIMASK
# (docs/rtos-design.md, розділ 2; відмінності - docs/port_sim-V11.3.3.patch).
# SIM_CRIT=0 - порт із сабмодуля без змін. portmacro.h в обох - із сабмодуля
RTOS_PORT_SRC = $(if $(filter 1,$(SIM_CRIT_ON)),Platform/rtos/port_sim,$(RTOS_PORT))
# У збірці TEST_KSTRESS port.c і portasm.c включаються у Platform/rtos/kstress/
# (той самий порт плюс лічильники критичних секцій), тож окремо не збираються
RTOS_PORT_C  = $(if $(filter 1,$(TEST_KSTRESS) $(TEST_QSTRESS)),Platform/rtos/kstress/port_kstress.c,$(RTOS_PORT_SRC)/port.c)
RTOS_PORT_S  = $(if $(filter 1,$(TEST_KSTRESS) $(TEST_QSTRESS)),Platform/rtos/kstress/port_kstress_asm.c,$(RTOS_PORT_SRC)/portasm.c)
ifeq ($(ARCH_ON),qpc)
QPC_DIR      = Middlewares/qpc
QP_SOURCES   = $(addprefix $(QPC_DIR)/src/qf/,qep_hsm.c qep_msm.c qf_act.c qf_defer.c qf_dyn.c \
               qf_mem.c qf_ps.c qf_qact.c qf_qeq.c qf_qmact.c qf_time.c) $(QPC_DIR)/ports/freertos/qf_port.c
RTOS_SOURCES = $(addprefix $(RTOS_DIR)/,tasks.c queue.c list.c) $(QP_SOURCES) \
               $(RTOS_PORT_C) $(RTOS_PORT_S)
C_INCLUDES  += -I$(QPC_DIR)/include -I$(QPC_DIR)/ports/freertos -ITasks
ifeq ($(filter 1,$(TEST_QSTRESS) $(TEST_QPCHECK)),)
C_DEFS      += -DQ_UNSAFE
endif
else
RTOS_SOURCES = $(addprefix $(RTOS_DIR)/,tasks.c queue.c list.c stream_buffer.c) \
               $(RTOS_DIR)/portable/MemMang/heap_4.c \
               $(RTOS_PORT_C) $(RTOS_PORT_S)
endif
RTOS_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(RTOS_SOURCES:.c=.o)))

C_SOURCES  += $(RTOS_SOURCES)
# port_sim раніше за порт сабмодуля: port_kstress.c включає "port.c" звідти
C_INCLUDES += -I$(RTOS_DIR)/include $(if $(filter 1,$(SIM_CRIT_ON)),-IPlatform/rtos/port_sim) -I$(RTOS_PORT)
C_DEFS     += -DTICK_HZ=$(TICK_HZ_ON)
vpath %.c $(sort $(dir $(RTOS_SOURCES)))

$(BUILD_DIR)/$(TARGET).elf: $(RTOS_OBJECTS)

# Вектор SysTick — SysTick_Handler у Platform/rtos/rtos_hal.c: він рахує тіки для
# perf_now з самого HAL_Init і лише після старту ядра викликає обробник порту.
# Порт V11 визначає обробник під іменем CMSIS, тож у port.c його перейменовуємо
$(BUILD_DIR)/port.o $(BUILD_DIR)/port_kstress.o: CFLAGS += -DSysTick_Handler=xPortSysTickHandler
ifneq ($(filter 1,$(TEST_KSTRESS) $(TEST_QSTRESS)),)
C_INCLUDES += -IPlatform/rtos/kstress
endif
endif
C_DEFS += -DARCH_RTOS=$(if $(filter rtos qpc,$(ARCH_ON)),1,0)
C_DEFS += -DARCH_QPC=$(if $(filter qpc,$(ARCH_ON)),1,0)
C_DEFS += -DSIM_CRIT=$(if $(filter rtos qpc,$(ARCH_ON)),$(SIM_CRIT_ON),0)
C_DEFS += -DRTOS_ASSERT=$(ASSERT_ON)

# LTO: оптимізація всього образу на етапі лінкування (docs/rtos-design.md, бюджет
# пам'яті). Код генерується вже в лінкері, тож -Os і секції по функціях треба
# повторити й йому: без -ffunction-sections весь код ліг би в одну секцію .text, і
# пул літералів асемблерних вставок порту (ldr r2, =pxCurrentTCB) опинився б далі
# за 1 КБ від інструкції (docs/journal.md). Кадри стека (.su) з LTO пишуться не по
# файлах, тож make stack-usage має сенс лише з LTO=0. І -Werror та -Wstack-usage=512
# з CFLAGS у лінкері вже не діють: їх перевіряє ціль objects (лише компіляція, CI).
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
#   TEST_PRIMASK=1   — дослід: чи маскує PRIMASK переривання (рядок PRIMASK після READY)
#   TEST_BOOTMARK=1  — дослід: лічильник perf у точках старту до BOOT (рядки BOOTMARK)
#   TEST_APIPROBE=1  — зонд бюджету flash: посилання на все API ядра кроків 2-3 (лише rtos)
#   TEST_KSTRESS=1   — стрес-тест ядра в Wokwi замість задачі app (лише rtos)
#   TEST_CRITCOST=1  — ціна входу й виходу критичної секції ядра (лише rtos, PERF=1)
#   PERF=0           — без вимірювального модуля ЛР3 (docs/perf.md); за замовчуванням 1
# TEST_* у продукт не входять; docs/rtos-design.md, розділи 2 і 5
BUILD_OPTS := LAB2_CAP TEST_I2C_FAULT TEST_I2C_DIAG TEST_PRIMASK TEST_BOOTMARK TEST_APIPROBE TEST_KSTRESS TEST_QSTRESS TEST_QPCHECK \
              TEST_CRITCOST OSMETRICS
C_DEFS += $(foreach o,$(BUILD_OPTS),$(if $(filter 1,$($(o))),-D$(o)))
C_DEFS += $(if $(filter-out 0,$(INV_ON)),-DTEST_INVERSION=$(INV_ON))
C_DEFS += $(TEST_OS_DEFS)

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
ifeq ($(TEST_CRITCOST)$(PERF_ON),10)
$(error TEST_CRITCOST=1 міряє perf_now, потрібен PERF=1)
endif
ifeq ($(OSMETRICS)$(PERF_ON),10)
$(error OSMETRICS=1 міряє perf_now, потрібен PERF=1)
endif
ifneq ($(filter qflood_drop qflood_wait,$(TEST_OS_ON)),)
ifeq ($(PERF_ON),0)
$(error TEST_OS=$(TEST_OS_ON) міряє perf_now, потрібен PERF=1)
endif
endif

# Об'єкти в Makefile залежать лише від .c і Makefile, тож після зміни варіанта
# make узяв би старі .o. Варіант пишемо у файл, лише коли він змінився, і всі
# об'єкти від нього залежать: перемикання варіанта = повна перезбірка.
# Каталог build/ один на всі варіанти: об'єкти іншої ARCH, що лишилися від
# попередньої збірки, не лінкуються — до ELF ідуть лише $(OBJECTS).
BUILD_CFG  = $(BUILD_DIR)/build.cfg
BUILD_LINE = ARCH=$(ARCH_ON) LTO=$(LTO_ON) TICK_HZ=$(TICK_HZ_ON) SIM_CRIT=$(SIM_CRIT_ON) ASSERT=$(ASSERT_ON) \
             $(foreach o,$(BUILD_OPTS),$(o)=$(if $(filter 1,$($(o))),1,0)) \
             TEST_INVERSION=$(if $(INV_NAME),$(INV_NAME),0) TEST_OS=$(TEST_OS_ON) PERF=$(PERF_ON) \
             GCC=$(shell $(CC) -dumpfullversion) \
             NEWLIB=$(shell printf '\043include <newlib.h>\n' | $(CC) -dM -E -x c - | sed -n 's/.*_NEWLIB_VERSION "\(.*\)"/\1/p')

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

ifdef GCC_PATH
NM = $(GCC_PATH)/$(PREFIX)nm
else
NM = $(PREFIX)nm
endif
empty :=
space := $(empty) $(empty)
HEAP_ALLOC = _malloc_r _calloc_r _realloc_r _memalign_r _free_r

all: $(BUILD_DIR)/heap.chk
$(BUILD_DIR)/heap.chk: $(BUILD_DIR)/$(TARGET).elf
	@$(NM) $< > $@.nm
	@if grep -wE '$(subst $(space),|,$(HEAP_ALLOC))' $@.nm; then rm -f $@.nm; \
	  echo "heap: в образі є виділення з купи newlib (docs/qp-design.md, крок 2)" >&2; exit 1; fi
	@rm -f $@.nm
	@touch $@

# Лише компіляція, без лінкування: -Wextra -Werror і -Wstack-usage=512 для власного
# коду, коли образ не лінкується (ARCH=rtos LTO=0 не влазить у flash)
objects: $(OBJECTS)

.PHONY: FORCE objects
FORCE:
