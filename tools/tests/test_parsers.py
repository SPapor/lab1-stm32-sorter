"""Тести розбору логів і VCD (stats_table, evt_latency, vcd_timing) без Wokwi.

    python3 -m unittest discover -s tools/tests
"""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import evt_latency as el  # noqa: E402
import map_summary as ms  # noqa: E402
import stats_table as st  # noqa: E402
import vcd_timing as vt   # noqa: E402

STATS_BLOCK = """{t} PERF git=0a44968f now_cyc=52 pair_cyc=131 cyc_per_us=48
{t} KNOBS lcd_ms={lcd} lcd_chars=0 imu_ms=10 log=2
{t} STARTUP boot_us=1520 gate_us=391000 class_us=391200 imu_us=402000 lcd_us=905000
{t} STAT name=uart_rx n=3 min_us=90 avg_us=4000 max_us={uart} period_us=0 jit_min_us=0 jit_max_us=0 miss=0
{t} STAT name=gate n=12000 min_us=95 avg_us=120 max_us=2700 period_us=5000 jit_min_us=-900 jit_max_us={gjit} miss={gmiss}
{t} STAT name=imu n=6000 min_us=900 avg_us=950 max_us=1100 period_us=10000 jit_min_us=-800 jit_max_us=31700 miss=120
{t} STAT name=lcd n=120 min_us=31400 avg_us=31500 max_us=31900 period_us={lcdus} jit_min_us=-300 jit_max_us=900 miss=0
{t} STAT name=loop n=900000 min_us=3 avg_us=60 max_us=32100 period_us=0 jit_min_us=0 jit_max_us=0 miss=0
{t} STAT name=parcel_latency n={pn} min_us=2500 avg_us=2600 max_us=2700 period_us=0 jit_min_us=0 jit_max_us=0 miss=0
{t} STAT name=estop_latency n=0 min_us=0 avg_us=0 max_us=0 period_us=0 jit_min_us=0 jit_max_us=0 miss=0
{t} UART rx={rx} ore={ore}
"""


def block(t=60000, lcd=500, uart=9000, gjit=31600, gmiss=118, lcdus=500000, pn=0, rx=6, ore=0):
    return STATS_BLOCK.format(t=t, lcd=lcd, uart=uart, gjit=gjit, gmiss=gmiss, lcdus=lcdus,
                              pn=pn, rx=rx, ore=ore)


S1_LOG = ('# git=0a44968f (з BOOT) scenario=S1 build="LAB2_CAP=0 TEST_I2C_FAULT=0 PERF=1" perf=1 '
          'port=rfc2217://localhost:4000 host_start=2026-11-12T10:00:00\n'
          '15 BOOT fw=lab3-superloop git=0a44968f perf=1\n'
          '15 EVT t_us=15200 name=boot\n'
          '391 EVT t_us=391000 name=ready_gate\n'
          '# > stats reset\n'
          '20100 STATS reset=1\n'
          '# > log on\n'
          '21000 LOAD pct=17 pm=171 win_cyc=48001000 busy_cyc=8200000 windows=20\n'
          '22000 LOAD pct=19 pm=188 win_cyc=48000500 busy_cyc=9000000 windows=21\n'
          '# > stats\n' + block(t=50000, gjit=99999, gmiss=1) +   # старий блок - не береться
          '# > stats\n' + block() +
          '# > stack\n'
          '60100 STACK used=612 reserved=1024 free=412 overflow=0\n'
          '# > heap\n'
          '60200 HEAP arena=0 used=0 free=0\n')

S3_LOG = ('# git=0a44968f (з BOOT) scenario=S3 build="LAB2_CAP=0 TEST_I2C_FAULT=0 PERF=1" perf=1 '
          'port=rfc2217://localhost:4000 host_start=2026-11-12T11:00:00\n'
          '# > stats reset\n'
          '# >> потік "stats" rate_hz=2 duration=60\n'
          '# >> потік завершено: надіслано 120 команд, 720 байтів\n'
          + ''.join(f'{30000 + i} EVT t_us={30000000 + i * 3000000} name=estop_irq\n'
                    f'{30080 + i} EVT t_us={30000000 + i * 3000000 + 21000 + i * 1000} name=estop_react\n'
                    for i in range(40)) +
          '31000 EVT t_us=31000000 name=parcel_in\n'
          '31003 EVT t_us=31002650 name=servo_set\n'
          '40000 LOAD pct=52 pm=521 win_cyc=48000100 busy_cyc=25000000 windows=40\n'
          '40500 CLI st=BAD\n'
          '# > stats\n' + block(t=90000, uart=161000, gjit=158000, gmiss=900, pn=12, rx=700, ore=3))


class EvtLatencyTest(unittest.TestCase):
    def test_pairs_and_percentile(self):
        ev = el.parse_events(S3_LOG.splitlines())
        lat = el.latencies(ev, 'estop_irq', 'estop_react')
        self.assertEqual(len(lat), 40)
        self.assertEqual(min(lat), 21000)
        self.assertEqual(max(lat), 60000)
        s = el.summary(lat)
        self.assertEqual(s['p99'], 60000)                 # n < 100: найближчий ранг = max
        self.assertEqual(el.latencies(ev, 'parcel_in', 'servo_set'), [2650])

    def test_start_without_end_and_boot_pairs(self):
        ev = el.parse_events(['1 EVT t_us=100 name=cmd_rx', '2 EVT t_us=150 name=cmd_rx',
                              '3 EVT t_us=400 name=cmd_reply', '4 EVT t_us=500 name=cmd_reply',
                              '5 EVT t_us=10 name=boot', '6 EVT t_us=900 name=ready_imu'])
        self.assertEqual(el.latencies(ev, 'cmd_rx', 'cmd_reply'), [250])  # береться найближча start
        self.assertEqual(el.latencies(ev, 'boot', 'ready_imu'), [890])

    def test_pair_stats_over_logs(self):
        a = [(0, 'cmd_rx'), (9000, 'cmd_reply')]
        b = [(5, 'cmd_rx'), (4005, 'cmd_reply')]
        s = el.pair_stats([a, b], [('cmd_rx', 'cmd_reply')])[('cmd_rx', 'cmd_reply')]
        self.assertEqual((s['n'], s['min'], s['max']), (2, 4000, 9000))
        self.assertEqual(el.fmt_ms(2650), '2,65 мс')
        self.assertEqual(el.fmt_ms(None), '-')

    def test_percentile_nearest_rank(self):
        vals = list(range(1, 201))
        self.assertEqual(el.percentile(vals, 99), 198)
        self.assertEqual(el.percentile([7], 99), 7)
        self.assertIsNone(el.summary([]))


class StatsTableTest(unittest.TestCase):
    def test_parse_takes_last_block(self):
        run = st.parse_lines(S1_LOG.splitlines(True))
        self.assertEqual(run['scenario'], 'S1')
        self.assertEqual(len(run['blocks']), 2)
        self.assertEqual(st.stat(run, 'gate', 'jit_max_us'), 31600)
        self.assertEqual(st.stat(run, 'gate', 'miss'), 118)
        self.assertEqual(st.stat(run, 'gate', 'jit_min_us'), -900)
        self.assertIsNone(st.stat(run, 'estop_latency', 'max_us'))          # n=0
        self.assertEqual(run['blocks'][-1]['startup']['imu_us'], 402000)
        self.assertEqual(run['knobs']['lcd_ms'], '500')
        self.assertEqual(max(x['pct'] for x in run['load']), 19)
        self.assertEqual(run['stack'][0]['used'], 612)

    def test_lost_bytes_and_fr03(self):
        run = st.parse_lines(S3_LOG.splitlines(True))
        # після stats reset: потік 720 байтів + "stats\r" 6 = 726, прийнято 700
        self.assertEqual(st.lost_bytes(run), 26)
        self.assertEqual(run['bad_cmds'], 1)
        # 5 мс + 158 мс джитера + 2,7 мс + 20 мс
        self.assertEqual(st.fr03_estimate_us(st.last_block(run)), 185700)

    def test_table_verdict_by_s3(self):
        runs = {'S1': st.parse_lines(S1_LOG.splitlines(True)),
                'S3': st.parse_lines(S3_LOG.splitlines(True))}
        t = st.table(runs)
        rows = {r.split('|')[1].strip(): [c.strip() for c in r.split('|')[2:-1]]
                for r in t.splitlines()[2:]}
        fr03 = rows['FR-03: поява посилки -> зміна ШІМ (оцінка), max']
        self.assertEqual(fr03[0], '-')                    # у S1 посилок немає
        self.assertEqual(fr03[2], '185,70 мс')
        self.assertEqual(fr03[-1], 'не виконана в S3')
        self.assertEqual(rows['gate: джитер старту, max'][0], '31,60 мс')
        self.assertEqual(rows['E-STOP: фронт PC13 -> реакція, p99'][2], '60,00 мс')
        self.assertEqual(rows['глибина стека (STACK used)'][0], '612 B')
        self.assertEqual(rows['глибина стека (STACK used)'][-1], 'немає S3')
        self.assertIn('git=0a44968f', st.conditions(runs))

    def test_stack_overflow_fails(self):
        log = S3_LOG + '91000 STACK used=1024 reserved=1024 free=0 overflow=1\n'
        runs = {'S3': st.parse_lines(log.splitlines(True))}
        row = [r for r in st.table(runs).splitlines() if r.startswith('| глибина стека')][0]
        self.assertTrue(row.rstrip(' |').endswith('не виконана в S3'), row)

    def test_retry_bytes_and_unfinished_stream(self):
        log = ('# > stats reset\n'
               '# > log on\n'
               '# > log on (повтор: CR + команда)\n'
               '# > stats\n' + block(rx=12) +
               '# >> потік "stats" rate_hz=2 duration=60 (час МК)\n'
               '# > stats\n' + block(t=70000, rx=400))      # блок посеред потоку
        run = st.parse_lines(log.splitlines(True))
        self.assertEqual(run['blocks'][0]['uart']['sent'], 7 + 8 + 6)   # повтор - ще CR попереду
        self.assertIsNone(st.lost_bytes(run))                            # потік ще не порахований

    def test_blocks_table(self):
        log = ('# > stats reset\n' + block(lcd=500, gjit=31600, gmiss=20) +
               '15000 LOAD pct=43 pm=430 win_cyc=48000000 busy_cyc=20000000 windows=15\n'
               '# > stats reset\n' + block(lcd=100, gjit=31700, gmiss=100))
        run = st.parse_lines(log.splitlines(True))
        t = st.blocks_table(run).splitlines()
        self.assertEqual(len(run['blocks']), 2)
        self.assertIn('lcd_ms=100', t[3])
        self.assertIn('43 %', t[3])
        self.assertIn('59,30 мс*', t[3])                   # 5 + 31,7 + 2,6 (розрахунок) + 20
        self.assertNotIn('без stats reset', st.blocks_table(run))

    def test_blocks_table_marks_block_before_reset(self):
        log = ('# > stats\n' + block(t=9000, gjit=184000) +       # git-хеш на початку: МК не скинуто
               '# > stats reset\n' + block(lcd=500, gjit=35000))
        t = st.blocks_table(st.parse_lines(log.splitlines(True))).splitlines()
        self.assertIn('log=2 (без stats reset) |', t[2])
        self.assertNotIn('без stats reset', t[3])
        self.assertIn('накопичені з попереднього скидання', t[-1])
        # МК стартував у цьому лозі (6.2.7, прогони wokwi-cli): блок чистий і без stats reset
        log = '2 BOOT fw=lab3-superloop git=0a44968f perf=1\n# > stats\n' + block(t=9000)
        self.assertNotIn('без stats reset', st.blocks_table(st.parse_lines(log.splitlines(True))))


def write_vcd(path, changes, names=('D0', 'D1', 'D2', 'D4', 'D5', 'D6', 'D7')):
    """changes: [(t_us, канал, рівень)]; запис у нс, як у Wokwi."""
    ids = {n: chr(ord('!') + i) for i, n in enumerate(names)}
    with open(path, 'w') as f:
        f.write('$timescale 1ns $end\n$scope module logic $end\n')
        for n, i in ids.items():
            f.write(f'$var wire 1 {i} {n} $end\n')
        f.write('$upscope $end\n$enddefinitions $end\n#0\n')
        init = {'D5': 1}
        for n, i in ids.items():
            f.write(f'{init.get(n, 0)}{i}\n')
        for t, n, v in sorted(changes, key=lambda c: c[0]):
            f.write(f'#{int(t * 1000)}\n{v}{ids[n]}\n')


MAP = """Linker script and memory map

.text           0x08000000       0x40
 .text.vTaskStartScheduler
                0x08000000       0x20 build/tasks.o
 .text.app_task
                0x08000020        0x8 build/app_task.o
 .text.main     0x08000028       0x10 build/main.o
 .text          0x08000038        0x8 /tmp/ccAb12.ltrans0.ltrans.o
.data           0x20000000        0x4 load address 0x08000040
 .data.uwTickFreq
                0x20000000        0x4 build/stm32c0xx_hal.o
.bss            0x20000004      0x100
 .bss.ucHeap    0x20000004      0x100 build/heap_4.o
.noinit         0x20000104        0xc
 .noinit        0x20000104        0xc build/rtos_diag.o
"""


class MapSummaryTest(unittest.TestCase):
    def test_modules_prefer_own_layers(self):
        with tempfile.TemporaryDirectory() as fw:
            for f in ('Core/Src/main.c', 'Middlewares/FreeRTOS/examples/demo/main.c',
                      'Middlewares/FreeRTOS/tasks.c', 'Tasks/app_task.c',
                      'Platform/rtos/rtos_diag.c', 'Drivers/STM32C0xx_HAL_Driver/Src/stm32c0xx_hal.c'):
                os.makedirs(os.path.dirname(os.path.join(fw, f)), exist_ok=True)
                open(os.path.join(fw, f), 'w').close()
            mods = ms.source_modules(fw)
        self.assertEqual(mods['main'], 'Core')
        self.assertEqual(mods['tasks'], 'Middlewares')
        self.assertEqual(mods['app_task'], 'Tasks')
        self.assertEqual(mods['rtos_diag'], 'Platform')

    def test_objects_noinit_and_lto(self):
        mods = {'main': 'Core', 'tasks': 'Middlewares', 'heap_4': 'Middlewares',
                'app_task': 'Tasks', 'rtos_diag': 'Platform', 'stm32c0xx_hal': 'Drivers'}
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'firmware.map')
            with open(path, 'w', encoding='utf-8') as f:
                f.write(MAP)
            flash, ram, _, (ld_flash, ld_ram), objs = ms.parse(path, mods)
        self.assertEqual(flash['Middlewares'], 0x20)
        self.assertEqual(ram['Middlewares'], 0x100)
        self.assertEqual(flash['LTO'], 0x8)
        self.assertEqual(ram['Platform'], 0xc)                  # .noinit - лише RAM
        self.assertEqual(objs['tasks.o'][:2], [0x20, 0])
        self.assertEqual(objs['heap_4.o'][:2], [0, 0x100])
        self.assertEqual(objs['stm32c0xx_hal.o'][:2], [4, 4])   # .data - і flash, і RAM
        self.assertEqual((ld_flash, ld_ram), (0x44, 0x110))


RTOS_LOG = """# git=abcd1234 (з BOOT) scenario=MANUAL build="ARCH=rtos OSMETRICS=1" perf=1
2 BOOT fw=lab4-rtos git=abcd1234 perf=1 arch=rtos
229 STACK used=616 reserved=1024 free=408 overflow=0
1000 PERF git=abcd1234 now_cyc=517 pair_cyc=1322 cyc_per_us=48 arch=rtos lto=1 tick_hz=1000
1001 KNOBS lcd_ms=500 lcd_chars=0 imu_ms=10 log=2
1002 STAT name=gate n=10 min_us=195 avg_us=198 max_us=613 period_us=5000 jit_min_us=-5 jit_max_us=5 miss=0
1003 UART rx=6 ore=0
1004 TASK name=estop prio=6 cpu_pm=0 stack_free=152 state=blocked run_ms=3
1005 TASK name=IDLE prio=0 cpu_pm=639 stack_free=108 state=ready run_ms=6390
1006 MTX name=i2c takes=46606 timeouts=0 wait_max_us=1063
1007 LOGQ len=16 max_used=4 lines=43 drop=0 trunc=0 rx_drop=0
1008 SIMCRIT win_ms=20000 task_n=36000 isr_n=18000 deferred_tick=1 deferred_pendsv=2 deferred_irq=3
1009 STAT name=os_tick n=1000 min_us=20 avg_us=30 max_us=90 period_us=0 jit_min_us=0 jit_max_us=0 miss=0
1010 STAT name=os_tick_nosim n=1000 min_us=6 avg_us=16 max_us=60 period_us=0 jit_min_us=0 jit_max_us=0 miss=0
1011 STAT name=os_mutex n=0 min_us=0 avg_us=0 max_us=0 period_us=0 jit_min_us=0 jit_max_us=0 miss=0
1012 STAT name=os_mutex_nosim n=0 min_us=0 avg_us=0 max_us=0 period_us=0 jit_min_us=0 jit_max_us=0 miss=0
2000 STACK used=616 reserved=1024 free=408 overflow=0
2001 STACK name=estop used=360 reserved=512 free=152
2002 STACK name=IDLE used=84 reserved=192 free=108
2003 OSHEAP total=7424 free=480 min_free=480
"""


class RtosLogTest(unittest.TestCase):
    def run_of(self):
        return st.parse_lines(RTOS_LOG.splitlines(True))

    def test_lines_after_uart_stay_in_block(self):
        b = st.last_block(self.run_of())
        self.assertEqual(b['uart']['rx'], 6)
        self.assertEqual(b['stat']['os_tick']['avg_us'], 30)
        self.assertEqual(b['tasks']['estop']['state'], 'blocked')
        self.assertEqual(b['tasks']['IDLE']['cpu_pm'], 639)
        self.assertEqual(b['mtx']['i2c']['wait_max_us'], 1063)
        self.assertEqual(b['simcrit']['task_n'], 36000)
        self.assertEqual(b['logq']['drop'], 0)

    def test_task_stacks_apart_from_msp(self):
        r = self.run_of()
        self.assertEqual([x['used'] for x in r['stack']], [616, 616])
        self.assertEqual(r['task_stack']['estop'], {'used': 360, 'reserved': 512, 'free': 152})
        self.assertEqual(r['osheap'][-1]['min_free'], 480)

    def test_os_table(self):
        t = st.os_table(self.run_of())
        self.assertIn('| обробник тіку: SysTick -> xTaskIncrementTick | 1000 | 30 мкс | 90 мкс | 16 мкс | 60 мкс |', t)
        self.assertIn("| м'ютекс: take + give без конкуренції | 0 | - | - | - | - |", t)
        self.assertIn('| estop | 6 | blocked | 0 | 3 | 152 |', t)
        self.assertIn('1800 задач і 900 ISR на секунду', t)
        self.assertIn('використано 6944 B', t)
        self.assertEqual(st.os_table(st.parse_lines([])), 'у лозі немає блоку stats')


class VcdTimingTest(unittest.TestCase):
    def test_measurements(self):
        ch = []
        # ШІМ 50 Гц, імпульс 1000 мкс; CCR записано на 43,6 мс (preload), тож 2000 мкс -
        # з першого періоду після цього, тобто з імпульсу на 60,1 мс
        for k in range(10):
            t = k * 20000 + 100
            ch += [(t, 'D2', 1), (t + (1000 if t < 43600 else 2000), 'D2', 0)]
        ch += [(41000, 'D4', 1), (43600, 'D4', 0)]       # PARCEL_IN -> CCR за 2,6 мс
        # кнопка: спад на 50 мс з брязкотом 2 мс, відпускання на 120 мс
        ch += [(50000, 'D5', 0), (50500, 'D5', 1), (51000, 'D5', 0), (52000, 'D5', 1),
               (52100, 'D5', 0), (120000, 'D5', 1)]
        # D6: задачі по 100 мкс кожні 5 мс, але з 60 до 95 мс - одна довга (LCD) 31,5 мс
        for t in range(0, 200000, 5000):
            if 60000 < t < 95000:
                continue
            ch += [(t + 10, 'D6', 1), (t + 110, 'D6', 0)]
        ch += [(62000, 'D6', 1), (93500, 'D6', 0), (62000, 'D7', 1), (93500, 'D7', 0)]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'run.vcd')
            write_vcd(path, ch)
            res = vt.measure(path)
        self.assertEqual(res['fr03']['n'], 1)
        self.assertAlmostEqual(res['fr03']['max'], 60100 - 41000)    # перший імпульс 2000 мкс
        self.assertAlmostEqual(res['d4']['max'], 2600)
        self.assertEqual(res['estop']['n'], 1)                       # брязкіт - одне натискання
        # антибрязкіт до 70 мс, але до 93,5 мс цикл зайнятий LCD: перша задача - на 95 мс
        self.assertAlmostEqual(res['estop']['max'], 95010 - 50000)
        self.assertAlmostEqual(res['d7']['max'], 31500)
        self.assertAlmostEqual(res['d6']['max'], 31500)
        self.assertAlmostEqual(res['d6_gap']['max'], 95010 - 62000)

    def test_estop_on_tick_before_full_debounce(self):
        # натискання на 50,7 мс (тік 50): estop_task - на тіку 70, тобто через 19,3 мс
        ch = [(50700, 'D5', 0), (120000, 'D5', 1),
              (69010, 'D6', 1), (69100, 'D6', 0),            # задача на тіку 69 - ще не та
              (70000, 'D6', 1), (70050, 'D6', 0),            # estop_task
              (75010, 'D6', 1), (75100, 'D6', 0)]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'run.vcd')
            write_vcd(path, ch)
            res = vt.measure(path)
        self.assertAlmostEqual(res['estop']['max'], 70000 - 50700)

    def test_estop_from_own_channel_d1(self):
        # ЛР4: у estop свій канал D1. D6 (будь-яка задача) раніше, але міряється D1;
        # D0 - канал gate
        ch = [(50700, 'D5', 0), (120000, 'D5', 1),
              (70000, 'D6', 1), (70050, 'D6', 0),            # інша задача на тіку 70
              (70400, 'D1', 1), (70500, 'D1', 0),            # робота estop
              (5000, 'D0', 1), (5094, 'D0', 0), (10000, 'D0', 1), (10100, 'D0', 0)]
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, 'run.vcd')
            write_vcd(path, ch)
            res = vt.measure(path)
        self.assertAlmostEqual(res['estop']['max'], 70400 - 50700)
        self.assertAlmostEqual(res['d1']['max'], 100)
        self.assertEqual(res['d0']['n'], 2)
        self.assertAlmostEqual(res['d0']['max'], 100)

    def test_press_starts_ignores_bounce(self):
        d5 = [(0, 1), (100000, 0), (100300, 1), (100600, 0), (180000, 1), (180200, 0), (180400, 1),
              (300000, 0)]
        self.assertEqual(vt.press_starts(d5), [100000, 300000])
        # після скидання D5 = 0, доки не ввімкнулася підтяжка: це не натискання
        self.assertEqual(vt.press_starts([(0, 0), (1200, 1), (500000, 0)]), [500000])
        # вікно (vcd_window.py) вирізано посеред брязкоту: спад через 100 мкс після початку - не натискання
        self.assertEqual(vt.press_starts([(0, 1), (100, 0), (400, 1), (200000, 0)]), [200000])


if __name__ == '__main__':
    unittest.main()
