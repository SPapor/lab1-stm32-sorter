IMAGE ?= stm32-build
# git-хеш збірки (docs/perf.md): рахується на хості, у контейнер іде параметром
GIT_HASH := $(shell git describe --always --dirty --abbrev=8 2>/dev/null || echo unknown)
RUN = docker run --rm -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE)

# Варіант збірки: make build LAB2_CAP=1 (harness ЛР2), TEST_I2C_FAULT=1 (тест FR-09),
# PERF=0 (без вимірювального модуля, docs/perf.md)
build:
	$(RUN) make -C firmware -f app.mk all LAB2_CAP=$(LAB2_CAP) TEST_I2C_FAULT=$(TEST_I2C_FAULT) PERF=$(PERF) GIT_HASH=$(GIT_HASH)

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

shell:
	docker run --rm -it -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE) bash

.PHONY: build clean test coverage check-deps stack-usage map-summary shell
