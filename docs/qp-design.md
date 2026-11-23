# Прошивка на QP/C поверх FreeRTOS (ЛР5)

Той самий продукт, що в ЛР3 (super loop) і ЛР4 (FreeRTOS), але на активних об'єктах QP/C 8.1.6. Ядро - FreeRTOS V11.3.3 з ЛР4, вбудовані ядра QV/QK/QXK не використовуються. Методичка - LR5-metodychka.pdf, далі «методичка».

## Крок 1: аналіз, схема AO (чернетка), бюджет пам'яті, ризики

Прошивку на main на цьому кроці я не міняв. Усі числа взято з логів ЛР4 (`docs/metrics/lab4/`), з map-файлів і зі збірок кроку 1 (`docs/metrics/lab5/budget/`). Біля кожного числа вказано, звідки воно. Часи - симуляційні (Wokwi), як і в ЛР3-ЛР4.

### 1. Інвентаризація ЛР4

Код: `firmware/Tasks/sorter_tasks.c` (задачі, черги, м'ютекси, ISR USART2), `firmware/Core/Src/main.c` (робота задач `*_run`, ті самі функції, що в super loop), `firmware/Platform/rtos/` (база часу, хуки, обхід SIM_CRIT). Коміт `a7a4b9f`. Прошивка в `firmware/` не мінялась з `1a03385`.

#### Задачі

Стек і найдовша ділянка - з S2 на rtos (`S2_2026-11-22_1640.log`, `6f5fd394`): `STACK` і `STAT max_us`. Для `estop` - ще 6.2.2 (`6.2.2_2026-11-22_1624.log`), бо в S2 кнопку не натискали.

| Задача | Пріор. | Стек, B (резерв / зайнято) | Період або тригер | Що робить | Найдовша ділянка, max |
|---|---|---|---|---|---|
| `estop` | 6 | 512 / 108 (S2), 392 (6.2.2) | сповіщення з EXTI, потім `xTaskDelayUntil` 20 мс (антибрязкіт) | `estop_run`: читає PC13, знову вмикає EXTI, під м'ютексом `sorter` перемикає E-STOP, рядок `ESTOP` | 2 676 мкс (6.2.2) |
| `gate` | 5 | 512 / 360 | 5 мс, `xTaskDelayUntil` | `gate_run`: АЦП gate під м'ютексом `adc`, гістерезис; `PARCEL_IN` - у `qParcel` і в журнал; `PARCEL_OUT` - під м'ютексом `sorter` | 629 мкс |
| `sort` | 4 | 640 / 448 | `qParcel` (`xQueueReceive`, без тайм-ауту) | `sort_run`: АЦП class (`adc`), клас, рішення (`sorter`), серво, рядок `SORT` | 3 426 мкс |
| `imu` | 3 | 768 / 568 | `imu_ms` (10 мс), `xTaskDelayUntil` | `imu_run`: MPU-6050 через шину, м'ютекс `i2c` на кожну транзакцію; удар - під `sorter`, рядок `IMPACT` | 7 666 мкс (з очікуванням м'ютекса) |
| `cli` | 2 | 768 / 608 | потоковий буфер `sb_rx` | `uart_rx_run`: луна, `cli_feed`, `cmd_run`; відповіді і звіти (`stats` ~1,7 КБ) друкує сама під м'ютексом `uart` | 15 515 мкс (`uart_rx`) |
| `logger` | 2 | 640 / 472 | `qLog` з тайм-аутом до кінця секунди | рядки журналу FR-10 (`log_line`), раз на секунду `rtos_second` (`LOAD`, `EVT ready_*`) | 13 072 мкс |
| `lcd` | 1 | 640 / 536 | `lcd_ms` (500 мс), `xTaskDelayUntil` | `lcd_run`: текст рядків під `sorter`, 34 байти HD44780 = 136 записів PCF8574, кожен під м'ютексом `i2c` | 115 755 мкс (з витісненням); у super loop 37 114 мкс (`S2_2026-11-22_1642.log`) |
| IDLE | 0 | 192 / 84 | - | - | - |

Разом стеки задач 4 480 B (у купі ОС 7 424 B), idle - ще 192 B.

#### Об'єкти синхронізації

| Об'єкт | Розмір | Хто кладе | Хто бере | Примітка |
|---|---|---|---|---|
| черга `qLog` | 16 × `log_rec_t` 44 B = 704 B | усі задачі (`tasks_log` з `ev_log`, `ev_i2c`) | `logger` | `xQueueSend(..., 0)`, втрата - `LOGQ drop` (проблема 70: 327 втрат у 6.2.4) |
| черга `qParcel` | 2 × 8 B | `gate` (`tasks_parcel`) | `sort` | тайм-аут 0, втрата не рахується (проблема 63) |
| потоковий буфер `sb_rx` | 64 B | ISR USART2 | `cli` | втрата - `rx_drop` |
| сповіщення | - | ISR EXTI (`vTaskNotifyGiveFromISR`) | `estop` | |
| м'ютекс `adc` | - | `gate`, `sort` | | wait_max 96 мкс (S2) |
| м'ютекс `i2c` | - | `imu`, `lcd` (через `locked_write/read`, одна транзакція - одне взяття) | | wait_max 18 825 мкс (S1 `S1_2026-11-19_1903`), 33 369 мкс (S2) |
| м'ютекс `sorter` | - | `estop`, `gate`, `sort`, `imu`, `lcd`, `cli` (`status`, `reset`) | | wait_max 68 мкс |
| м'ютекс `uart` | - | `cli`, `logger` (через `uart_hal_set_lock`); разом з ним захищено буфер рядка `line[]` в `uart_hal.c` | | wait_max 606 мкс (S1) |
| критична секція | - | `tasks_snapshot` (копія `perf_stat_t` для `stats`), `log_drop++` | | вимірювальний код |

Змінні, які ділять задачі між собою або з перериванням (без вимірювальних лічильників):

| Змінна | Пише | Читає | Захист |
|---|---|---|---|
| `estop_pending` (volatile) | ISR EXTI | `estop` | немає (прапорець) |
| `sorter` (`sorter_t`) | `estop`, `gate`, `sort`, `imu`, `cli` | `lcd`, `cli` | м'ютекс `sorter` |
| `i2c1_ctx`, `hi2c1` | `imu`, `lcd` | | м'ютекс `i2c` |
| `hadc1` | `gate`, `sort` | | м'ютекс `adc` |
| `line[]` (`uart_hal.c`), `huart2` | `cli`, `logger` | | м'ютекс `uart` |
| `imu_period_ms`, `lcd_period_ms` | `cli` (`set`) | `imu`, `lcd` (через `const volatile *`) | немає |
| `log_level` | `cli` (`log on/off`) | усі (`ev_log`) | немає |
| `lcd_chars` | `cli` | `lcd` (у rtos не впливає) | немає |
| `fault_reason`, `fault_text` | `main` до старту ядра | `lcd`, `cli` | не потрібен (пишуться до старту) |

Разом: 4 м'ютекси, 0 семафорів (двійковий - лише в збірці `TEST_INVERSION=sem`), 1 сповіщення, 2 черги, 1 потоковий буфер; 9 спільних змінних без `fault_*`, з них 4 під м'ютексами і 5 без захисту.

#### Переривання

| ISR | NVIC | Що робить | Що надсилає |
|---|---|---|---|
| `EXTI4_15` (PC13, E-STOP) | 3 | `rtos_hal_irq_defer`, далі колбек: вимикає свою лінію, `estop_irq_cyc = perf_now()`, `estop_pending = 1` | `vTaskNotifyGiveFromISR(estop)`, `portYIELD_FROM_ISR` |
| `USART2` (прийом) | 3, вмикає `cli` | `rtos_hal_irq_defer`, усі байти з RDR | `xStreamBufferSendFromISR(sb_rx)`, `portYIELD_FROM_ISR` |
| `TIM17` (база часу HAL) | `TickPriority` | `rtos_hal_irq_defer`, `HAL_IncTick` | нічого (ОС не викликає) |
| `SysTick` (тік ядра) | порт | `rtos_hal_ticks++` для `perf_now`, `port_sim_defer_tick`, `xPortSysTickHandler` | тік ядра |
| `PendSV`, `SVC` | порт | перемикання задач (порт ARM_CM0, копія `port_sim`) | - |
| `HardFault` | - | `rtos_hal_hardfault`: рядок `FATAL` і зупинка | - |

На Cortex-M0+ немає BASEPRI, критична секція порту - `cpsid i`, тож `FromISR` можна викликати з переривання будь-якого пріоритету (rtos-design.md, розділ 2).

#### Спільні ресурси

| Ресурс | Клієнти | Захист у ЛР4 |
|---|---|---|
| шина I²C1 (MPU-6050 0x68, PCF8574 0x27) | `imu`, `lcd`; до старту ядра - самоперевірка | м'ютекс `i2c` на транзакцію (`locked_write/read`) |
| передавач UART (USART2 TX) | `cli`, `logger`; до старту - `BOOT` і самоперевірка; `rtos_hal_raw_puts` з хуків (`FATAL`) | м'ютекс `uart` |
| АЦП (ADC1, канали gate і class) | `gate`, `sort` | м'ютекс `adc` |
| ШІМ серво (TIM3) | лише `sort` (і самоперевірка до старту) | не потрібен |
| стан сортувальника `sorter_t` | `estop`, `gate`, `sort`, `imu`, `lcd`, `cli` | м'ютекс `sorter` |

**Хто пише в UART (методичка 4.1).** Текст у UART у ЛР4 пишуть дві задачі: `logger` (усі події FR-10 з `qLog` і `LOAD` через `ev_now`) і `cli` (луна, відповіді `cli_reply`, звіти `stats`, `stack`, `tasks`, `uart_hal_printf` у `help`). Обидві йдуть через форматер `App/fmt.c` у спільний буфер `line[]` і `HAL_UART_Transmit` з опитуванням (~87 мкс на байт), тому м'ютекс `uart` захищає і буфер, і передавач. До старту ядра друкує `main` напряму (`BOOT`, самоперевірка). Окремо - `rtos_hal_raw_puts` (`FATAL` з хуків і HardFault): він пише в TDR опитуванням TXE в обхід м'ютекса і може перемежатися з рядком задачі; це аварійний шлях, у ЛР5 він лишається поза Console.
