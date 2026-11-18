# Логи ЛР4

Логи прогонів ЛР4 (`tools/run_scenario.py ... --out docs/metrics/lab4`). У заголовку кожного - git-хеш прошивки, `build.cfg` збірки і `arch=`, `lto=`, `tick_hz=` з рядка `BOOT`. Логи ЛР3 лежать рівнем вище, docs/metrics/.

| Лог | Сценарій | Прошивка | Примітка |
|---|---|---|---|
| `S1_2026-11-17_1850.log` | S1 | `cc2c3a84`, super loop, `LTO=1` | до виправлення `perf_now` (журнал, проблема 36) |
| `S1_2026-11-17_1852.log` | S1 | `cc2c3a84`, rtos, `LTO=1` | до виправлення `perf_now`: `gate max_us=89477591` |
| `S1_2026-11-17_1856.log` | S1 | `cedabba3`, super loop, `LTO=1` | smoke кроку 1, docs/rtos-design.md, розділ 5 |
| `S1_2026-11-17_1857.log` | S1 | `cedabba3`, rtos (одна задача `app`), `LTO=1` | smoke кроку 1; 13 NACK на I²C (Wokwi) |
| `S1_2026-11-17_1942.log` | S1 | `a84b069e`, super loop, `LTO=1` | після виправлень code review; збігається з `_1856` до 3 мкс. Пари rtos з `wokwi-cli` немає: скінчилися хвилини CI Wokwi (журнал, проблема 41) |
| `S1_2026-11-18_0011.log` | S1 | `cad75f82`, super loop, `LTO=1` | wokwi.com у браузері, вікно 69,4 с; основний smoke кроку 1 на поточній історії (docs/rtos-design.md, розділ 5) |
| `S1_2026-11-18_1147.log` | S1 | `cad75f82`, rtos (одна задача `app`), `LTO=1` | wokwi.com у браузері, вікно 66,7 с; `os_api_us` / `os_start_us` у STARTUP; 11 NACK на I²C (Wokwi) |

Досліди кроку 1 (Wokwi у браузері, wokwi.com, «Upload Firmware and Start Simulation»: хвилини CI скінчилися, а Wokwi для VS Code не запустився; лише плата NUCLEO-C031C6, тож MPU і LCD - FAIL, на досліди це не впливає). Текст скопійовано з монітора порту, заголовок у тому ж форматі, що пише `run_scenario.py`:

| Лог | Збірка | Що |
|---|---|---|
| `PRIMASK_superloop_2026-11-18_0004.log` | `79f4919e`, super loop, `TEST_PRIMASK=1` | 10 обертів SysTick з PRIMASK = 1: обробники виконуються (журнал, проблема 35) |
| `PRIMASK_rtos_2026-11-18_0005.log` | `cad75f82`, rtos, `TEST_PRIMASK=1` | те саме в rtos |
| `BOOTMARK_superloop_2026-11-18_0007.log`, `BOOTMARK_rtos_2026-11-18_0008.log` | `cad75f82`, `TEST_BOOTMARK=1` | стан лічильника perf у точках старту до `BOOT` (docs/rtos-design.md, розділ 5) |
| `BOOTWIDTH_superloop_2026-11-18_1213.log`, `BOOTWIDTH_rtos_2026-11-18_1209.log` | `50939149`, `LTO=1` | рядок `BOOT` однієї довжини (103 символи) в обох ARCH, STARTUP з `stats`; повна схема, як у S1 (docs/rtos-design.md, розділ 5) |
| `FMT_BOOTMARK_superloop_2026-11-18_1228.log`, `FMT_BOOTMARK_rtos_2026-11-18_1233.log` | `8c895f3d`, `LTO=1`, `TEST_BOOTMARK=1` | ціна символу після форматера App/fmt.c і перевірка парсерів на новому виводі (docs/rtos-design.md, розділ 4) |
| `KSTRESS_2026-11-18_1411.log` | `3439b99b`, rtos, `TEST_KSTRESS=1` | стрес-тест ядра: зависання планувальника після перемикання в критичній секції (журнал, проблема 43), зупинено на 15,2 с |
| `KSTRESS_2026-11-18_1528.log` | `1baa13f6`, rtos, `TEST_KSTRESS=1` | повтор з лічильниками критичних секцій задач і ISR окремо: та сама картина, 3 перемикання, одне в `taskENTER_CRITICAL` |

Записи аналізатора до `_1856`, `_1857` і `_1942` - docs/captures/.

Хеші `cc2c3a84`, `cedabba3` і `a84b069e` у цих логах - з історії до переписування дат комітів 17.11 (дати розподілено, вміст не змінився). Ті коміти лишилися під локальним тегом `lab4-step1-orig`, у гілці `lab4/kernel` той самий код - `ccc0299`, `04e7809` і `38f2289`.
