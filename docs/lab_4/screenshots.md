# Скріншоти ЛР4

Куди: `docs/lab_4/img/NN-....png`. Записи аналізатора - у PulseView (як у ЛР3: File -> Import Value Change Dump, канали D0-D7, курсори A і B, у підписі - Δ між ними). Вікна VCD вирізані `tools/vcd_window.py` у `docs/lab_4/captures/`, час на них - від початку вікна. Wokwi - Wokwi для VS Code, збірка `make build ARCH=rtos LTO=1` (або вказана).

| № | Файл | Звідки | Канали | Що позначити | Коли |
|---|---|---|---|---|---|
| 1 | `01-wokwi-rtos-stand.png` | Wokwi для VS Code, продукт rtos, після 3 посилок (ldr_gate 10 -> 500 -> 10 лк, ldr_class 500 / 100 / 10 лк) | - | стенд цілком; у терміналі - `STATE state=READY`, рядки `SORT` трьох класів; на LCD - `READY n=3`, `A1 B1 R1` | готово |
| 2 | `02-tasks-stack.png` | той самий запуск: команди `tasks`, `stack`, `heap` | - | термінал: рядки `TASK` (cpu_pm кожної задачі, IDLE 645 ‰), `STACK name=...` (запас кожного стека), `OSHEAP free=480` | готово |
| 3 | `03-estop-superloop-6.2.2.png` | `docs/lab_4/captures/03-estop-superloop-6.2.2.vcd` (super loop ЛР3, 6.2.2) | D5, D6, D7 | A - спад D5 на 34,200 мс, B - підйом D6 на 94,733 мс (старт estop_task), Δ = 60,5 мс; видно D7 (перемальовка з NACK) між ними | зараз |
| 4 | `04-estop-rtos-6.2.2.png` | `docs/lab_4/captures/04-estop-rtos-6.2.2.vcd` (6.2.2 на rtos у VS Code, `docs/captures/6.2.2_2026-11-22_1624_rtos.vcd`, вікно 18,770-18,880 с) | D0, D1 (`estop`), D5, D7 | натискання посеред перемальовки (D7 шматками): A - спад D5 на 30,187 мс, B - підйом D1 на 49,959 мс, Δ = 19,8 мс (антибрязкіт) - `estop` витісняє `lcd` | готово (частина Б у VS Code) |
| 5 | `05-gate-superloop-s1.png` | `docs/lab_4/captures/05-gate-superloop-s1.vcd` (super loop, S1 кроку 1, `LTO=1`) | D6, D7 | A і B на фронтах D7: 37,4 мс перемальовки, весь цей час D6 суцільно 1 - `gate` стоїть | зараз |
| 6 | `06-preemption-rtos-s2.png` | `docs/lab_4/captures/06-preemption-rtos-s2.vcd` (S2 на rtos у VS Code, `docs/captures/S2_2026-11-22_1640_rtos.vcd`, вікно 20,250-20,340 с) | D0 (`gate`), D6, D7 | перемальовка LCD 74,6 мс шматками, посеред неї D0 кожні 5 мс; A і B на сусідніх фронтах D0 (38,928 і 43,943 мс), Δ = 5,02 мс. Хронологія задач (розділ 10 методички); на S3 - за бажанням у частині Б | готово (S2) |
| 7 | `07-stack-overflow.png` | Wokwi для VS Code, збірка `TEST_OS=stack_near`, команди `status`, `stats` | - | термінал: `STATUS` пройшов, на `stats` - `FATAL stack overflow in cli` | готово |
| 8 | `08-starve-tasks.png` | Wokwi для VS Code, збірка `TEST_OS=starve`, сценарій `tools/scenarios/7.2.3.yaml` (або вручну: `tasks` одразу після вікна `spin` - 20-25, 40-45, 60-65 або 80-85 с; на знімку - 87,7 с, після четвертого) | - | термінал: `TASK name=spin cpu_pm=269`, `IDLE cpu_pm=355`, рядки `LOAD pct=92`, `pct=100` одразу після вікна, пачка `I2C_RECOVER` під час доганяння `imu` | готово |

Числа на 2 і 8 - з тих запусків, що на знімках, а не з логів частини А: на 8 вікно `spin` лягло на секундні інтервали `LOAD` інакше, ніж у `7.2.3_2026-11-20_1414` (там 252 ‰, 380 ‰, LOAD 91 %). Для звіту беру числа з логів.

4 і 6 зняті 22.11 із записів частини Б у Wokwi для VS Code (6.2.2 і S2).
