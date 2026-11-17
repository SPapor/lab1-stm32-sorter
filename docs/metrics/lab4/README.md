# Логи ЛР4

Логи прогонів ЛР4 (`tools/run_scenario.py ... --out docs/metrics/lab4`). У заголовку кожного - git-хеш прошивки, `build.cfg` збірки і `arch=`, `lto=`, `tick_hz=` з рядка `BOOT`. Логи ЛР3 лежать рівнем вище, docs/metrics/.

| Лог | Сценарій | Прошивка | Примітка |
|---|---|---|---|
| `S1_2026-11-17_1850.log` | S1 | `cc2c3a84`, super loop, `LTO=1` | до виправлення `perf_now` (журнал, проблема 36) |
| `S1_2026-11-17_1852.log` | S1 | `cc2c3a84`, rtos, `LTO=1` | до виправлення `perf_now`: `gate max_us=89477591` |
| `S1_2026-11-17_1856.log` | S1 | `cedabba3`, super loop, `LTO=1` | smoke кроку 1, docs/rtos-design.md, розділ 5 |
| `S1_2026-11-17_1857.log` | S1 | `cedabba3`, rtos (одна задача `app`), `LTO=1` | smoke кроку 1; 13 NACK на I²C (Wokwi) |

Записи аналізатора до `_1856` і `_1857` - docs/captures/.
