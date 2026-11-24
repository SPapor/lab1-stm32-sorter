# Лабораторна робота №1 — Warehouse Sorter

Курс: Системне програмування вбудованих систем
Студент: ІО-32, Кекух А.О.

## Збірка

Прошивка збирається в Docker. З ЛР5 (крок 2) основний образ - `stm32-build:gcc14` з `docker/Dockerfile`: Ubuntu 26.04, arm-none-eabi-gcc 14.2.1 (`15:14.2.rel1-1`), newlib 4.6.0, binutils 2.45.50. Версії пакетів зафіксовані в Dockerfile.

```sh
make image                                  # docker build -t stm32-build:gcc14 docker/
make build                                  # ARCH=superloop (ЛР3)
make build ARCH=rtos LTO=1                  # FreeRTOS (ЛР4)
make test && make check-deps
```

Цифри ЛР3 і ЛР4 (теги `lab3`, `lab4`) отримано gcc 10.3.1. Щоб їх відтворити, потрібен старий образ `stm32-build:gcc10` з `docker/gcc10/Dockerfile` (Ubuntu 22.04, `15:10.3-2021.07-4`, newlib 3.3.0):

```sh
make image-gcc10
git checkout lab4
make clean IMAGE=stm32-build:gcc10
make build ARCH=rtos LTO=1 IMAGE=stm32-build:gcc10
# назад
git checkout main
make clean
```

`make clean` тут обов'язковий: на тезі `lab4` версії компілятора в штампах збірки ще немає, тож без нього make узяв би об'єкти і `.gcno` тестів, зібрані іншим gcc (docs/journal.md, проблема 76).

На тезі `lab4` і раніше в `Makefile` стоїть `IMAGE ?= stm32-build`, тож там образ треба вказати явно, як вище (або `docker tag stm32-build:gcc10 stm32-build`).
