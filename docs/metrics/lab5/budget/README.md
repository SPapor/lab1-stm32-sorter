# Бюджет пам'яті ЛР5, крок 1

Звідки числа розділу 4 у docs/qp-design.md. Усе зібрано 23.11 образом `stm32-build` (arm-none-eabi-gcc 10.3.1), як і прошивки ЛР4, крім `05_gcc14.txt` і `06_dpp_example.txt` (локальний gcc 14.2.1). Прапорці - як у `firmware/app.mk`: `-mcpu=cortex-m0plus -mthumb -Os -ffunction-sections -fdata-sections`, `--gc-sections`, `LTO=1` - ще `-flto` і в лінкері.

| Лог | Що |
|---|---|
| `logs/01_lr4_rtos_lto1.txt` | ЛР4 rtos як є (`a7a4b9f`), `make build ARCH=rtos LTO=1` |
| `logs/02_lr4_rtos_lto0_objects.txt` | те саме з `LTO=0`; FLASH у скрипті лінкера 64K, бо в 32K не лінкується; `tools/map_summary.py --objects` і функції ядра з map (`fnsum.py`) |
| `logs/03_probe.txt` | зонд `probe/`: QP/C 8.1.6 + FreeRTOS V11.3.3 у статичній конфігурації + 7 AO-скелетів схеми з розділу 2 (`ao.c`), ISR і хуки (`bsp.c`), заглушки роботи (`work.c`); `LTO=0/1`, з `Q_UNSAFE` і без; `arm-none-eabi-size` по об'єктах, кадри стека, `nm` |
| `logs/04_shim.txt` | справжній образ: дерево `a7a4b9f`, але `Tasks/sorter_tasks.c` замінено тонким шаром на QP з тим самим API (`shim/sorter_tasks.c`: AO на кожну задачу ЛР4, без Bus, кроків LCD і подій Sorter), конфіг ядра - `probe/FreeRTOSConfig.patch`, `app.mk` - `shim/app.mk.patch`; ще варіант з `HIST_CMD 0` |
| `logs/05_gcc14.txt` | ЛР4 rtos `LTO=1` gcc 14.2.1, `-Os` і `-Oz` |
| `logs/06_dpp_example.txt` | приклад `freertos/arm-cm/dpp_nucleo-c031c6` (qpc-examples `cda413c`), його Makefile, `CONF=rel` |

Як повторити зонд: QP/C на тезі `v8.1.6` скопіювати в `probe/qpc/` (`include`, `src`, `ports`), у `probe/fw/` - `Middlewares/FreeRTOS`, `Platform/rtos/port_sim`, `startup_stm32c031xx.s`, `STM32C031xx_FLASH.ld` і CMSIS (`cmsis/dev`, `cmsis/core`), `FreeRTOSConfig.h` з ЛР4 з `probe/FreeRTOSConfig.patch`; потім `./build.sh 0` або `./build.sh 1 -DQ_UNSAFE` у контейнері `stm32-build`. `objsum.py <map>` - flash і RAM по об'єктах (як `map_summary.py`, але для будь-якого дерева), `fnsum.py <map> <об'єкти>` - розміри функцій.
