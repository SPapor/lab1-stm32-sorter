IMAGE ?= stm32-build
RUN = docker run --rm -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE)

build:
	$(RUN) make -C firmware -f app.mk all

clean:
	$(RUN) make -C firmware -f app.mk clean

# Перевірка залежностей шарів, на хості без Docker
check-deps:
	sh tools/check-deps.sh

shell:
	docker run --rm -it -v $(PWD):/workspace -w /workspace --user $$(id -u):$$(id -g) $(IMAGE) bash

.PHONY: build clean check-deps shell
