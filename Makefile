IMAGE ?= stm32-build
RUN = docker run --rm -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE)

build:
	$(RUN) make -C firmware -f app.mk all

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

shell:
	docker run --rm -it -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE) bash

.PHONY: build clean test coverage check-deps shell
