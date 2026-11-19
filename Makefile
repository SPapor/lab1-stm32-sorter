IMAGE ?= stm32-build
# git-хеш збірки (docs/perf.md): рахується на хості, у контейнер іде параметром.
# Теги не беремо (--exclude): на тезі lab3 describe дав би «lab3» замість хеша
GIT_HASH := $(shell git describe --always --dirty --abbrev=8 --exclude='*' 2>/dev/null || echo unknown)
RUN = docker run --rm -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE)

# ARCH, LTO, TICK_HZ - лише з командного рядка: ARCH у середовищі буває чужим
# (ARCH=arm для збірки ядра Linux), а make бере змінні середовища як свої
$(foreach v,ARCH LTO TICK_HZ SIM_CRIT ASSERT,$(if $(filter environment%,$(origin $(v))),$(eval $(v) :=)))

# Варіант збірки: make build LAB2_CAP=1 (harness ЛР2), TEST_I2C_FAULT=1 (тест FR-09),
# PERF=0 (без вимірювального модуля, docs/perf.md).
# Архітектура (docs/rtos-design.md): ARCH=superloop (за замовчуванням) | rtos,
# LTO=0|1, TICK_HZ=<Гц> (лише rtos, за замовчуванням 1000)
FW_VARS = ARCH=$(ARCH) LTO=$(LTO) TICK_HZ=$(TICK_HZ) SIM_CRIT=$(SIM_CRIT) ASSERT=$(ASSERT) \
	LAB2_CAP=$(LAB2_CAP) TEST_I2C_FAULT=$(TEST_I2C_FAULT) TEST_PRIMASK=$(TEST_PRIMASK) \
	TEST_BOOTMARK=$(TEST_BOOTMARK) TEST_APIPROBE=$(TEST_APIPROBE) TEST_KSTRESS=$(TEST_KSTRESS) TEST_CRITCOST=$(TEST_CRITCOST) TEST_INVERSION=$(TEST_INVERSION) OSMETRICS=$(OSMETRICS) PERF=$(PERF) GIT_HASH=$(GIT_HASH)

build:
	$(RUN) make -C firmware -f app.mk all $(FW_VARS)

# Лише компіляція з -Werror і -Wstack-usage по файлах, без лінкування: для варіантів,
# що не лінкуються (ARCH=rtos LTO=0) або лінкуються з LTO (там ці прапорці не діють)
compile:
	$(RUN) make -C firmware -f app.mk objects $(FW_VARS)

clean:
	$(RUN) make -C firmware -f app.mk clean
	$(RUN) make -C tests clean

# Хостові тести Devices/ і App/ (gcc, Unity) і звіт покриття в build/coverage/
test:
	$(RUN) make -C tests test

coverage:
	$(RUN) make -C tests coverage

# Перевірка залежностей шарів, на хості без Docker
check-deps:
	sh tools/check-deps.sh

# Після make build: найбільші кадри стека (.su) і flash/RAM за модулями (map), на хості
stack-usage:
	python3 tools/stack_usage.py firmware/build

map-summary:
	python3 tools/map_summary.py firmware/build/firmware.map

# Статична глибина стека за дизасемблером: main і переривання разом з HAL і newlib
stack-graph:
	$(RUN) arm-none-eabi-objdump -d firmware/build/firmware.elf > firmware/build/firmware.dis
	python3 tools/stack_graph.py firmware/build/firmware.dis

# Тести хост-скриптів (розбір сценаріїв, логів, VCD) - без Wokwi, на хості
tools-test:
	python3 -m unittest discover -s tools/tests

shell:
	docker run --rm -it -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE) bash

.PHONY: build compile clean test coverage check-deps stack-usage map-summary stack-graph tools-test shell
