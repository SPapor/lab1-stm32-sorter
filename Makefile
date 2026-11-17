IMAGE ?= stm32-build
# git-хеш збірки (docs/perf.md): рахується на хості, у контейнер іде параметром.
# Теги не беремо (--exclude): на тезі lab3 describe дав би «lab3» замість хеша
GIT_HASH := $(shell git describe --always --dirty --abbrev=8 --exclude='*' 2>/dev/null || echo unknown)
RUN = docker run --rm -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE)

# Варіант збірки: make build LAB2_CAP=1 (harness ЛР2), TEST_I2C_FAULT=1 (тест FR-09),
# PERF=0 (без вимірювального модуля, docs/perf.md).
# Архітектура (docs/rtos-design.md): ARCH=superloop (за замовчуванням) | rtos,
# LTO=0|1, TICK_HZ=<Гц> (лише rtos, за замовчуванням 1000)
build:
	$(RUN) make -C firmware -f app.mk all ARCH=$(ARCH) LTO=$(LTO) TICK_HZ=$(TICK_HZ) \
		LAB2_CAP=$(LAB2_CAP) TEST_I2C_FAULT=$(TEST_I2C_FAULT) PERF=$(PERF) GIT_HASH=$(GIT_HASH)

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

.PHONY: build clean test coverage check-deps stack-usage map-summary stack-graph tools-test shell
