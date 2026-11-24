# Лабораторна робота №1 — Warehouse Sorter

Курс: Системне програмування вбудованих систем
Студент: ІО-32, Кекух А.О.

## Збірка

Прошивка збирається в Docker, образом `stm32-build:gcc10` з `docker/gcc10/Dockerfile`: Ubuntu 22.04, arm-none-eabi-gcc 10.3.1 (`15:10.3-2021.07-4`), newlib 3.3.0, binutils 2.38. Версії пакетів зафіксовані в Dockerfile. Цим самим компілятором зібрано й виміряно ЛР3 і ЛР4 (теги `lab3`, `lab4`).

```sh
make image-gcc10                            # docker build -t stm32-build:gcc10 docker/gcc10/
make build                                  # ARCH=superloop (ЛР3)
make build ARCH=rtos LTO=1                  # FreeRTOS (ЛР4)
make test && make check-deps
```

Образ з gcc 14.2.1 (`docker/Dockerfile`, `make image`, `IMAGE=stm32-build:gcc14`) лишається для порівняння: у ЛР5 я пробував перейти на нього і повернувся (docs/journal.md, проблема 79).

Відтворити lab4:

```sh
git checkout lab4
make clean IMAGE=stm32-build:gcc10
make build ARCH=rtos LTO=1 IMAGE=stm32-build:gcc10
# назад
git checkout main
make clean
```

`make clean` тут обов'язковий: на тезі `lab4` версії компілятора в штампах збірки ще немає, тож без нього make узяв би об'єкти і `.gcno` тестів, зібрані іншим gcc (docs/journal.md, проблема 76). На тезі `lab4` і раніше в `Makefile` стоїть `IMAGE ?= stm32-build`, тож там образ треба вказати явно, як вище (або `docker tag stm32-build:gcc10 stm32-build`).
