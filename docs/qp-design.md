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

### 2. Схема AO (чернетка)

#### Активні об'єкти і пріоритети

Сім AO, як і сім задач у ЛР4, але склад інший: `cli` і `logger` стали одним AO Console (власник UART TX), а м'ютекс `i2c` став AO Bus. Пріоритет AO - він же пріоритет задачі FreeRTOS (методичка 5.5), тож `QF_MAX_ACTIVE` = 7, `configMAX_PRIORITIES` = 8 (з idle).

| AO | Пріор. | З чого в ЛР4 | Чим володіє | Чому такий пріоритет |
|---|---|---|---|---|
| Estop | 7 | `estop` | лінія EXTI PC13 | FR-07: аварійна зупинка не має чекати жодного іншого кроку; у ЛР4 теж найвищий |
| Gate | 6 | `gate` | АЦП (обидва канали) | FR-01 і FR-03: відлік кожні 5 мс, затримка виявлення входить у 50 мс FR-03 |
| Sorter | 5 | `sort` + м'ютекс `sorter` | `sorter_t`, ШІМ серво | FR-03: від `PARCEL_IN` до запису CCR; у ЛР4 `parcel_latency` 0,9 мс (S2) |
| Bus | 4 | м'ютекс `i2c` | шина I²C1 | власник має бути вищим за клієнтів Imu (3) і Lcd (1) (методичка 5.8); нижчим за Gate і Sorter, бо ті шину не чіпають, а обмін I²C - до 0,7 мс, з тайм-аутом до 5 мс |
| Imu | 3 | `imu` | - | FR-04: удар, поки посилка їде; період 10 мс, жорсткого дедлайну немає |
| Console | 2 | `cli` + `logger` | передавач UART, рядок команди | FR-10 і відповідь на команду; у ЛР4 обидві задачі мали пріоритет 2 і ділили м'ютекс `uart` - один власник UART TX робить їх одним AO і заощаджує стек (640 B) |
| Lcd | 1 | `lcd` | текст на екрані | FR-06: оновлення раз на 500 мс, найменш термінове |

Відмінність від рисунка 2 методички: там Bus найвищий (6), бо всі, хто вище, - його клієнти. У нас Estop, Gate і Sorter шину не використовують, тож правило «власник вищий за клієнтів» виконується і з Bus = 4, а FR-01 і FR-03 не чекають обміну I²C.

АЦП стає ресурсом Gate без окремого AO: канал class Gate читає в той самий крок, коли бачить посилку, і передає відлік у `PARCEL_IN`. У ЛР4 class читала `sort` через ~0,5 мс після `PARCEL_IN` (черга `gate` -> `sort` 559 мкс, `OSMETRICS_2026-11-19.log`); тепер відлік на цей час раніше. На класифікацію це не впливає (LDR міняється за сотні мілісекунд), але це зміна, яку я опишу у звіті.

#### Події

`QEvt` у QP/C 8.1.6 - 8 B (`include/qp.h`). Розміри подій - з `sizeof` у зонді (`docs/metrics/lab5/budget/probe/app.h`).

| Сигнал | Параметри | Вид, B | Хто надсилає | Хто отримує | Як |
|---|---|---|---|---|---|
| `ESTOP_EDGE` | - | статична | ISR EXTI | Estop | `QACTIVE_POST_FROM_ISR` |
| `DEBOUNCE` | - | часова, одноразова 20 мс | QF (хук тіку) | Estop | `QTimeEvt` |
| `ESTOP_PRESS` | - | статична | Estop | Sorter | `QACTIVE_POST` |
| `GATE_TICK` | - | часова, періодична 5 мс | QF | Gate | `QTimeEvt` |
| `PARCEL_IN` | `in_ms`, `in_cyc`, `gate_raw`, `class_raw` | динамічна, 20 | Gate | Sorter | `QACTIVE_POST` |
| `PARCEL_OUT` | `gate_raw` | динамічна, 20 | Gate | Sorter | `QACTIVE_POST` |
| `IMPACT` | `x`, `y`, `z`, `amag` | динамічна, 16 | Imu | Sorter | `QACTIVE_POST` |
| `STATUS` | стан, лічильники A/B/REJECT, текст FAULT | динамічна, 20 | Sorter (на кожну зміну) | Lcd | `QACTIVE_PUBLISH` (або `POST`, див. бюджет) |
| `STATUS_REQ`, `RESET_REQ` | - | статичні | Console (`status`, `reset`) | Sorter | `QACTIVE_POST`; відповідь - `LOG` у Console |
| `BUS_REQ` | `reply_to`, операція (`MPU_READ`, `LCD_CMD`, `LCD_DATA`), байт | динамічна, 16 | Imu, Lcd | Bus | `QACTIVE_POST` |
| `BUS_DONE` | статус, операція, 6 байтів даних | динамічна, 16 | Bus | `reply_to` | `QACTIVE_POST` |
| `BUS_RETRY` | - | часова, одноразова | QF | Bus | `QTimeEvt` (повтор після відновлення, FR-09) |
| `IMU_TICK` | - | часова, періодична `imu_ms` | QF | Imu | `QTimeEvt` |
| `LCD_TICK` | - | часова, періодична `lcd_ms` | QF | Lcd | `QTimeEvt` |
| `SET_PERIOD` | нове значення, мс | динамічна, 16 | Console (`set imu_ms`, `set lcd_ms`) | Imu, Lcd | `QACTIVE_POST` |
| `LINE` | довжина, рядок до 32 символів | динамічна, 44 (пул 52) | ISR USART2 | Console | `Q_NEW_X_FROM_ISR` + `QACTIVE_POST_X_FROM_ISR`, втрата з лічильником |
| `LOG` | `event`, `fmt`, `ms`, до 7 аргументів, ознака відповіді | динамічна, 52 | усі AO (`ev_log`), Sorter (відповіді на `STATUS_REQ`/`RESET_REQ`) | Console | `Q_NEW_X` + `QACTIVE_POST_X`, втрата з лічильником (як `LOGQ drop`) |
| `SECOND` | - | часова, періодична 1 с | QF | Console | `QTimeEvt` (`LOAD`, `EVT ready_*`) |

Пули (за зростанням блоку, методичка 5.10): 16 B × 10 (`BUS_REQ`/`BUS_DONE` для двох клієнтів, `IMPACT`, `SET_PERIOD`), 20 B × 8 (`PARCEL_*`, `STATUS`), 52 B × 18 (16 `LOG`, як `qLog`, і 2 `LINE`). Це стартові числа; підганятиму за `QF_getPoolMin` після S3 і 6.2.4.

Відкриті питання для кроку 2:
- `log_level` у ЛР4 - спільна змінна (пише `cli`, читають усі). У ЛР5 нею володіє Console: AO завжди шлють `LOG`, а Console при `log off` їх відкидає. Ціна - виділення і пересилання подій, які не друкуються.
- Рядок у ISR збирається в `LINE` до `\r`, тож луну Console друкує цілим рядком, а не по символу. Байти в UART ті самі, змінюється лише момент. `run_scenario.py` шле команди цілими рядками, тож для скриптів різниці не має бути; перевірю на 6.2.3.

#### Машини станів

Діаграми з усіма переходами - на кроці 2, тут лише стани і ключові переходи.

- **Estop** (2 стани): `armed` --`ESTOP_EDGE`--> `debouncing` (entry: `DEBOUNCE` на 20 мс; exit: disarm) --`DEBOUNCE` / прочитати PC13, скинути і ввімкнути EXTI, [натиснуто] `ESTOP_PRESS` у Sorter--> `armed`. `estop_pending` зникає: його роль грає стан `debouncing`.
- **Gate** (2 стани): `clear` --`GATE_TICK` [нижче порогу ON] / `PARCEL_IN`--> `occupied` --`GATE_TICK` [вище порогу OFF] / `PARCEL_OUT`--> `clear`. Логіка порогів - `App/gate.c` без змін.
- **Sorter** (ієрархічна): суперстан `sorter` обробляє `STATUS_REQ`, `RESET_REQ` і `PARCEL_IN` у станах, де посилку не приймають (`SORT_SKIP`). Усередині - `operational` { `ready`, `sorting` } (тут `ESTOP_PRESS` -> `estop`, `IMPACT` - позначка удару один раз на посилку), `estop` (`ESTOP_PRESS` -> `ready`) і `fault` (після невдалої самоперевірки, FR-08). `ready` --`PARCEL_IN` / клас, лоток, серво, лічильники, `SORT`--> `sorting` --`PARCEL_OUT`--> `ready`. Entry кожного стану публікує `STATUS`. Стан `sorter_t.state` і стан машини мають бути одним цілим: або `App/sorter.c` лишається з рішеннями і лічильниками, а стан - лише в QP, або навпаки. Вирішу на кроці 2, бо від цього залежать хостові тести `sorter`.
- **Bus** (2 стани): `serving` --`BUS_REQ` / одна спроба обміну, `BUS_DONE`--> `serving`; [помилка] / відновлення шини -> `recovering` (запити відкладаються, `QActive_defer`; entry: `BUS_RETRY`) --`BUS_RETRY` / повторити той самий запит--> `serving` або, після третьої спроби (FR-09), `BUS_DONE` з помилкою. Так одна спроба - один крок: у ЛР4 три спроби з тайм-аутом 5 мс ішли одним викликом, `i2c_xfer` max 54 450 мкс у 6.2.4 (`6.2.4_2026-11-22_1620.log`).
- **Imu** (2 стани): `idle` --`IMU_TICK` / `BUS_REQ(MPU_READ)`--> `reading` --`BUS_DONE` [ок] / модуль прискорення, [> 2 g] `IMPACT`; [помилка] `IMU_ERR`--> `idle`. `IMU_TICK` у `reading` - пропуск (лічильник).
- **Lcd** (2 стани): `idle` --`LCD_TICK` / текст рядків з останнього `STATUS`--> `drawing` (entry: перший `BUS_REQ`) --`BUS_DONE` [ще є байти] / наступний `BUS_REQ`; [34-й байт] -> `idle`; [помилка] `LCD_ERR` -> `idle`. `STATUS` в обох станах лише оновлює копію.
- **Console** (1 стан, плоска): `LINE` -> луна, `cli_feed`, команда (події іншим AO або вимірювальний звіт); `LOG` -> рядок FR-10; `SECOND` -> `LOAD`.

Вимоги методички (розділ 3): взаємодіють щонайменше три машини (Gate -> Sorter -> Lcd, Imu <-> Bus <-> Lcd, Estop -> Sorter); ієрархічна - Sorter; два і більше стани - Estop, Gate, Sorter, Bus, Imu, Lcd (шість).

#### Найризикованіше місце: LCD через Bus

Числа - з S2 у super loop (`S2_2026-11-22_1642.log`, `6f5fd394`), бо там транзакцію ніхто не витісняє:

| Що | Скільки | Звідки |
|---|---|---|
| одна транзакція PCF8574 (запис 1 байта) | min 242 мкс (`i2c_xfer`), у середньому 273 мкс | `STAT i2c_xfer`; 37 114 мкс / 136 |
| байт HD44780 | 2 ніббли × 2 записи (EN = 1, EN = 0) = 4 транзакції ≈ 1,09 мс | `Devices/lcd1602.c`, `write_byte` |
| повна перемальовка | 2 команди курсора + 32 символи = 34 байти = 136 транзакцій = 37,1 мс | `STAT lcd_update` 37 114 мкс |
| читання MPU-6050 | запис регістра + 6 байтів, `i2c_xfer` max 710 мкс, крок `imu` 1 018 мкс | `STAT i2c_xfer`, `STAT imu` |

Скільки кроків на перемальовку - залежить від того, що Bus робить за один `BUS_REQ`:

| Крок Bus | Кроків на перемальовку | Крок, мс | Найдовше очікування Imu на Bus | Пересилань подій на перемальовку |
|---|---|---|---|---|
| байт HD44780 | 34 | ~1,09 | ~1,1 мс + витіснення Estop/Gate/Sorter | 68 |
| нібл | 68 | ~0,55 | ~0,55 мс | 136 |
| транзакція PCF8574 | 136 | ~0,27 | ~0,27 мс, як дрібність м'ютекса в ЛР4 | 272 |

Для порівняння: м'ютекс `i2c` у ЛР4 Imu чекала до 18,8 мс у S1 і 33,4 мс у S2 (`MTX name=i2c wait_max_us`), хоча м'ютекс брався на одну транзакцію: власника (`lcd`, пріоритет 1) витісняли всі інші задачі.

Ціна пересилань у Wokwi велика: «черга gate -> sort» у ЛР4 - 559 мкс у середньому (`OSMETRICS_2026-11-19.log`, з перемиканням задачі). Якщо пара `BUS_REQ` + `BUS_DONE` коштує стільки ж, то при кроці «байт HD44780» це ще ~38 мс на перемальовку (68 × 559 мкс), тобто ~7,6 % процесора при 500 мс. Я беру крок «байт HD44780» (34 кроки, трохи довші за 1 мс з рекомендації методички, розділ 3) і на кроці 2 перевірю ціну пересилання на прошивці. Якщо вона близька до 559 мкс - «нібл» не беру.

#### Що чим замінюється (ЛР4 -> ЛР5)

| ЛР4 | ЛР5 |
|---|---|
| 7 задач з нескінченним циклом, `xTaskCreate` з купи | 7 AO, задачі і черги створює `QActive_start` статично |
| `xTaskDelayUntil` у `gate`, `imu`, `lcd` | періодичні `QTimeEvt`: `GATE_TICK`, `IMU_TICK`, `LCD_TICK` |
| сповіщення з EXTI + `xTaskDelayUntil` 20 мс у `estop` | `ESTOP_EDGE` (`QACTIVE_POST_FROM_ISR`, статична) + одноразова `DEBOUNCE` |
| `estop_pending` (volatile) | стан `debouncing` |
| черга `qParcel` | динамічна подія `PARCEL_IN` |
| черга `qLog` і задача `logger` | динамічна подія `LOG` у Console |
| потоковий буфер `sb_rx` | подія `LINE` з ISR |
| очікування `qLog` з тайм-аутом до кінця секунди | періодична `SECOND` у Console |
| м'ютекс `i2c` | AO Bus: `BUS_REQ` / `BUS_DONE` |
| м'ютекс `adc` | АЦП читає лише Gate, class - у `PARCEL_IN` |
| м'ютекс `sorter` | `sorter_t` - дані Sorter; інші шлють `ESTOP_PRESS`, `PARCEL_*`, `IMPACT`, `STATUS_REQ`, `RESET_REQ`, Lcd отримує `STATUS` |
| м'ютекс `uart` | у UART пише лише Console |
| `imu_period_ms`, `lcd_period_ms` через вказівник | подія `SET_PERIOD` |
| `log_level` (глобальна) | поле Console, фільтр на боці Console |
| три спроби I²C в одному виклику | стан `recovering` у Bus, одна спроба - один крок |
| купа ОС `heap_4` (7 424 B) | немає: AO, стеки, черги, пули - статичні масиви |
| `tasks_snapshot` (критична секція для `stats`) | лишається: вимірювальний код (виняток п. 4.1, правило 4) |

### 3. QP/C 8.1.6 і приклад dpp_nucleo-c031c6

Склонував у `/tmp`, у репозиторій не додавав: `qpc` на тезі `v8.1.6` (`1a078e8`, 2026-11-11), `qpc-examples` (`cda413c`, коміт «8.1.6»; саме на нього вказує сабмодуль `examples` тегу) і сабмодуль `3rd_party` тегу (`ae18ffa`) - у ньому FreeRTOS V11.2.0, CMSIS і файли плати для прикладу.

**Файли з методички 7.5 на тезі.** У `src/qf/` 12 файлів: 11 зі списку методички (`qep_hsm`, `qep_msm`, `qf_act`, `qf_defer`, `qf_dyn`, `qf_mem`, `qf_ps`, `qf_qact`, `qf_qeq`, `qf_qmact`, `qf_time`) і `qf_actq.c`, який у порт FreeRTOS не додають. `ports/freertos/` - `qf_port.c`, `qp_port.h`, `qs_port.h`, `syscalls.c`. У `src/qs/` лише `qstamp.c`: QS у GPL-версії справді немає. Список методички збігається з тегом. `qep_msm.c` і `qf_qmact.c` потрібні лише для `QMsm`/`QMActive` (код із QM); у зонді, де їх не кличуть, `--gc-sections` викидає їх повністю (0 B у map, `logs/03_probe.txt`).

**Що в прикладі** (`freertos/arm-cm/dpp_nucleo-c031c6`):

| Що | У прикладі | У нас (чернетка) |
|---|---|---|
| FreeRTOS | V11.2.0 з `3rd_party`, порт `GCC/ARM_CM0` | V11.3.3, копія порту `port_sim` (SIM_CRIT) |
| `configMAX_PRIORITIES` | 32 (списки готовності 32 × 20 B = 640 B) | 8 (160 B) |
| `configSUPPORT_STATIC_ALLOCATION` / `DYNAMIC` | 1 / 0, `configTOTAL_HEAP_SIZE` 0 | так само |
| `configUSE_TICK_HOOK` / `IDLE_HOOK` | 1 / 1 (в idle - вивід QS у UART) | 1 / 0 |
| м'ютекси, рекурсивні, лічильні семафори | 1, 1, 1 | 0, 0, 0 |
| `configMINIMAL_STACK_SIZE` | 128 слів | 48 слів (як у ЛР4) |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 2 |
| тік | 1000 Гц | 1000 Гц (`TICK_HZ`) |
| `qp_config.h` | `QF_MAX_ACTIVE` = `configMAX_PRIORITIES`, `QF_MAX_EPOOL` 3, `QF_MAX_TICK_RATE` 1, `QF_TIMEEVT_CTR_SIZE` 4, `QF_EQUEUE_CTR_SIZE` 1, `QF_MPOOL_CTR_SIZE` 2 | 7, 3, 1, 2, 1, 1 (`probe/qp_config.h`) |
| AO | 6: 5 Philo (черга 10) і Table (черга 5) | 7 |
| стеки AO | `configMINIMAL_STACK_SIZE` = 512 B кожен, 3 072 B | 3 968 B (розділ 4) |
| пули | один, `TableEvt` × 10 | три (розділ 2) |
| `QF_onStartup` | лише пріоритет SysTick | пріоритети і ввімкнення EXTI та USART2 |

Приклад я зібрав його ж Makefile (`make CONF=rel`, -O2, `-DNDEBUG`) локальним gcc 14.2.1 (`logs/06_dpp_example.txt`): text 14 200 B, data 8 B, bss 8 312 B (з них 2 048 B - стек MSP у секції `.stack`). По об'єктах: QP разом 5 723 B (`qep_hsm` 1 240, `qf_port` 1 324, `qep_msm` 684, `qf_ps` 542, `qf_dyn` 483, `qf_mem` 455, `qf_qact` 412, `qf_time` 376, `qf_act` 119, `qf_qmact` 88), ядро 5 208 B (`tasks` 2 584, `queue` 1 620, `port` 632, `portasm` 216, `list` 156); RAM - `bsp.o` 4 048 B (стеки, черги, пул), `philo.o` 1 020 B (5 AO), `tasks.o` 752 B. Що я з цього беру: AO з вбудованими `StaticTask_t` і `StaticQueue_t` займає ~190 B (у зонді рівно 192 B), а 32 пріоритети без потреби коштують 480 B RAM.
