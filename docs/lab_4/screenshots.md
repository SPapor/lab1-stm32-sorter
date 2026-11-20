# Скріншоти ЛР4

Куди: `docs/lab_4/img/NN-....png`. Записи аналізатора - у PulseView (як у ЛР3: File -> Import Value Change Dump, канали D0-D7, курсори A і B, у підписі - Δ між ними). Вікна VCD вирізані `tools/vcd_window.py` у `docs/lab_4/captures/`, час на них - від початку вікна. Wokwi - Wokwi для VS Code, збірка `make build ARCH=rtos LTO=1` (або вказана).

| № | Файл | Звідки | Канали | Що позначити | Коли |
|---|---|---|---|---|---|
| 1 | `01-wokwi-rtos-stand.png` | Wokwi для VS Code, продукт rtos, після 3 посилок (ldr_gate 10 -> 500 -> 10 лк, ldr_class 500 / 100 / 10 лк) | - | стенд цілком; у терміналі - `STATE state=READY`, рядки `SORT` трьох класів; на LCD - `READY n=3`, `A1 B1 R1` | готово |
| 2 | `02-tasks-stack.png` | той самий запуск: команди `tasks`, `stack`, `heap` | - | термінал: рядки `TASK` (cpu_pm кожної задачі, IDLE 645 ‰), `STACK name=...` (запас кожного стека), `OSHEAP free=480` | готово |
| 3 | `03-estop-superloop-6.2.2.png` | `docs/lab_4/captures/03-estop-superloop-6.2.2.vcd` (super loop ЛР3, 6.2.2) | D5, D6, D7, D3 | A - спад D5 на 34,200 мс, B - підйом D6 на 94,733 мс (старт estop_task), Δ = 60,5 мс; видно D7 (перемальовка з NACK) між ними | зараз |
| 4 | `04-estop-rtos-6.2.2.png` | 6.2.2 на rtos з `--vcd` (`wokwi-cli`) | D5, D1 (`estop`), D6, D7 | натискання, коли D7 високий: A - спад D5, B - підйом D1, Δ ≈ 20 мс (антибрязкіт) - задача `estop` перебиває перемальовку LCD | **частина Б** |
| 5 | `05-gate-superloop-s1.png` | `docs/lab_4/captures/05-gate-superloop-s1.vcd` (super loop, S1 кроку 1, `LTO=1`) | D6, D7 | A і B на фронтах D7: 37,4 мс перемальовки, весь цей час D6 суцільно 1 - `gate` стоїть | зараз |
| 6 | `06-preemption-rtos-s3.png` | S3 на rtos з `--vcd` (`wokwi-cli`) | D0 (`gate`), D1 (`estop`), D6, D7 | фрагмент перемальовки LCD (D7): посеред неї імпульси D0 кожні 5 мс і D6 - витіснення `lcd`; A і B на двох сусідніх фронтах D0, Δ = 5,00 мс. Це хронологія задач для звіту (розділ 10 методички) | **частина Б** |
| 7 | `07-stack-overflow.png` | Wokwi для VS Code, збірка `TEST_OS=stack_near`, команди `status`, `stats` | - | термінал: `STATUS` пройшов, на `stats` - `FATAL stack overflow in cli` | готово |
| 8 | `08-starve-tasks.png` | Wokwi для VS Code, збірка `TEST_OS=starve`, сценарій `tools/scenarios/7.2.3.yaml` (або вручну: почекати ~70 с, `tasks`) | - | термінал: `TASK name=spin cpu_pm=269`, `IDLE cpu_pm=355`, рядки `LOAD pct=92`, `pct=100` одразу після вікна, пачка `I2C_RECOVER` під час доганяння `imu` | готово |

Числа на 2 і 8 - з тих запусків, що на знімках, а не з логів частини А: на 8 вікно `spin` лягло на секундні інтервали `LOAD` інакше, ніж у `7.2.3_2026-11-20_1414` (там 252 ‰, 380 ‰, LOAD 91 %). Для звіту беру числа з логів.

Для 4 і 6 записи з'являться в грудні разом з логами частини Б; тоді ж їх вікна ляжуть у `docs/lab_4/captures/`.
