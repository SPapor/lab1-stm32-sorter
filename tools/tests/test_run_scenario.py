"""Тести tools/run_scenario.py без Wokwi: розбір сценарію, запис логу, заголовок.

    python3 -m unittest discover -s tools/tests
"""
import inspect
import os
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import run_scenario as rs  # noqa: E402


class FakeSerial:
    """Віддає наперед задані байти шматками, записане запам'ятовує."""

    def __init__(self, data=b''):
        self.data = data
        self.written = b''
        self.lock = threading.Lock()

    def push(self, data):
        with self.lock:
            self.data += data

    def read(self, n):
        with self.lock:
            chunk, self.data = self.data[:n], self.data[n:]
        if not chunk:
            time.sleep(0.01)
        return chunk

    def write(self, b):
        self.written += b

    def close(self):
        pass


class FakeMcu(FakeSerial):
    """Відповідає, як прошивка: ехо кожного байта, на CR - "\\r\\n", рядок відповіді,
    EVT cmd_rx і cmd_reply. lose - скільки перших байтів загубити (ні ехо, ні в буфер),
    bad - на скільки команд відповісти CLI st=BAD, evt=False - як при log < 2."""
    REPLY = {'stats': ['PERF git=abc now_cyc=52', 'UART rx=6 ore=0'], 'stats reset': ['STATS reset=1'],
             'load': ['LOAD pct=7 pm=72'], 'stack': ['STACK used=612 reserved=1024 free=412 overflow=0'],
             'status': ['STATUS state=READY log=1']}

    def __init__(self, data=b'', lose=0, bad=0, evt=True):
        super().__init__(data)
        self.lose, self.bad, self.evt = lose, bad, evt
        self.buf, self.ms, self.cmds = b'', 100, []

    def write(self, b):
        super().write(b)
        for ch in b:
            if self.lose:
                self.lose -= 1
                continue
            if ch != 0x0D:
                self.buf += bytes([ch])
                self.push(bytes([ch]))
                continue
            self.push(b'\r\n')
            cmd, self.buf = self.buf.decode(), b''
            if not cmd:
                continue
            self.cmds.append(cmd)
            if self.bad:
                self.bad -= 1
                out = ['CLI st=BAD']
            elif cmd.startswith('set '):
                out = ['KNOBS lcd_ms=500 lcd_chars=0 imu_ms=10 log=2']
            else:
                out = self.REPLY.get(cmd, ['CLI st=BAD'])
            if self.evt:
                out = out + ['EVT t_us=1 name=cmd_rx', 'EVT t_us=2 name=cmd_reply']
            self.ms += 10
            self.push(''.join(f'{self.ms} {o}\r\n' for o in out).encode())


def recorder(tmp, ser, clock=time.monotonic):
    meta = {'scenario': 'T', 'build': 'LAB2_CAP=0 TEST_I2C_FAULT=0 PERF=1',
            'port': 'fake://', 'host_start': '2026-11-11T20:00:00'}
    return rs.Recorder(ser, os.path.join(tmp, 'T.log'), meta, clock=clock)


def short_sleep(s):
    time.sleep(min(s, 0.01))


def read_log(rec):
    with open(rec.path, encoding='utf-8') as f:
        return f.read().splitlines()


class ScenarioTest(unittest.TestCase):
    def test_valid_and_expand(self):
        sc = {'name': 'S1', 'setup': ['stats reset'], 'steps': [
            {'send': 'status', 'wait': 1},
            {'prompt': 'натисни B1'},
            {'prompt': 'посунь gate-LDR', 'wait': 5},
            {'wait_mcu': 10},
            {'stream': {'cmd': 'stats', 'rate_hz': 0, 'count': 20}},
            {'repeat': {'times': 3, 'steps': [{'send': 'load'}, {'wait': 0.5}]}},
            {'comment': 'кінець'}]}
        rs.validate(sc)
        flat = rs.expand(sc['steps'])
        self.assertEqual(len(flat), 6 + 3 * 2)      # 6 простих кроків + repeat 3 × 2
        self.assertEqual([s.get('send') for s in flat if 'send' in s], ['status', 'load', 'load', 'load'])

    def test_invalid(self):
        bad = [
            {'steps': []},                                                  # без name
            {'name': 'a b', 'steps': []},                                   # пробіл у name
            {'name': 'S', 'steps': [{'push': 'B1'}]},                       # невідомий крок
            {'name': 'S', 'steps': [{'send': 'x', 'prompt': 'y'}]},         # два кроки в одному
            {'name': 'S', 'steps': [{'wait': -1}]},
            {'name': 'S', 'steps': [{'stream': {'cmd': 'stats'}}]},          # ні duration, ні count
            {'name': 'S', 'steps': [{'stream': {'cmd': 'stats', 'count': 1, 'duration': 1}}]},
            {'name': 'S', 'steps': [{'repeat': {'times': 0, 'steps': []}}]},
            {'name': 'S', 'steps': [], 'final': 'stats'},
            {'name': 'S', 'steps': [], 'drop_part': 'imu'},                  # не список
        ]
        for sc in bad:
            with self.assertRaises(ValueError, msg=sc):
                rs.validate(sc)

    def test_scenario_files_are_valid(self):
        d = os.path.join(os.path.dirname(__file__), '..', 'scenarios')
        names = [n for n in os.listdir(d) if n.endswith(('.yaml', '.json'))]
        self.assertTrue(names)
        for n in names:
            with self.subTest(n):
                sc = rs.load_scenario(os.path.join(d, n))
                self.assertTrue(sc['name'])


class RecorderTest(unittest.TestCase):
    def test_header_from_boot_and_lines_as_is(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, FakeSerial())
            rec.note('# сценарій T')               # до BOOT - у буфер
            rec.feed('15 BOOT fw=lab3-superloop git=0a44968f perf=1')
            rec.feed('16 PERF cpu_hz=48000000 now_cyc=52 pair_cyc=131')
            rec.feed('5017 LOAD pct=7 pm=72 win_cyc=48000123 busy_cyc=3456789 windows=5')
            rec.finish('ffffffff')
            log = read_log(rec)
        self.assertTrue(log[0].startswith('# git=0a44968f (з BOOT) scenario=T'))
        self.assertIn('perf=1', log[0])
        self.assertEqual(log[1], '# сценарій T')
        self.assertEqual(log[2], '15 BOOT fw=lab3-superloop git=0a44968f perf=1')
        self.assertEqual(log[-1], '5017 LOAD pct=7 pm=72 win_cyc=48000123 busy_cyc=3456789 windows=5')
        self.assertEqual(rec.last_ms, 5017)

    def test_git_from_perf_when_boot_missed(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, FakeSerial())
            rec.feed('90210 STATE state=READY')
            rec.feed('90300 PERF git=0a44968f now_cyc=52 pair_cyc=131 cyc_per_us=48')
            rec.finish('ffffffff')
            log = read_log(rec)
        self.assertTrue(log[0].startswith('# git=0a44968f (з PERF)'))
        self.assertIn('perf=1', log[0])

    def test_git_fallback_from_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, FakeSerial())
            rec.feed('100 STATE state=READY')
            rec.finish('deadbeef-dirty')
            log = read_log(rec)
        self.assertTrue(log[0].startswith('# git=deadbeef-dirty (з build/git.cfg)'))
        self.assertIn('perf=?', log[0])

    def test_reader_separates_echo_digit_from_stamp(self):
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeSerial(b'500 LOAD pct=1\r\n')
            rec = recorder(tmp, ser)
            rec.git = 'abc'
            rec.start()
            time.sleep(0.1)
            ser.push(b'5501 LOAD pct=2\r\n')       # ехо '5' впритул до рядка "501 LOAD"
            time.sleep(0.1)
            rec.finish('x')
            log = read_log(rec)
        self.assertEqual(rec.last_ms, 501)
        self.assertEqual(rec.rx[-1], '501 LOAD pct=2')
        self.assertEqual(rec.echo_since(0), b'5')
        self.assertEqual(log[-1], '5501 LOAD pct=2')          # у лозі - як прийшло

    def test_stamps_skip_one_step_back_but_follow_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, FakeSerial())
            rec.feed('90000 LOAD pct=1')
            rec.feed('9001 LOAD pct=1')                   # неправильно розібрана мітка
            self.assertEqual(rec.last_ms, 90000)
            rec.feed('90010 LOAD pct=1')
            self.assertEqual(rec.last_ms, 90010)
            rec.feed('2 BOOT fw=x git=abc perf=1')        # МК скинуто
            self.assertEqual(rec.last_ms, 2)
            rec.finish('x')

    def test_reader_splits_crlf_chunks(self):
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeSerial(b'12 BOOT fw=x git=abc perf=0\r\n13 STA' + b'TE state=READY\r\n')
            rec = recorder(tmp, ser)
            rec.start()
            time.sleep(0.2)
            rec.finish('x')
            log = read_log(rec)
        self.assertEqual(log[1:], ['12 BOOT fw=x git=abc perf=0', '13 STATE state=READY'])
        self.assertIn('perf=0', log[0])


class CommandTest(unittest.TestCase):
    def run_command(self, ser, cmd, **kw):
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, ser)
            rec.git = 'abc'
            rec.start()
            t0 = time.monotonic()
            ok = rs.command(ser, rec, cmd, sleep=short_sleep, **kw)
            took = time.monotonic() - t0
            rec.finish('x')
            return ok, took, read_log(rec)

    def test_reply_events(self):
        self.assertEqual(rs.reply_events('stats reset'), {'STATS'})
        self.assertEqual(rs.reply_events('stats'), {'UART'})
        self.assertEqual(rs.reply_events('set lcd_ms 100'), {'KNOBS', 'SET'})
        self.assertIsNone(rs.reply_events('help'))

    def test_check_reply(self):
        reply = ['stats', '5 PERF git=a', '6 UART rx=1 ore=0', '7 EVT t_us=1 name=cmd_rx']
        end = ['8 EVT t_us=2 name=cmd_reply']
        self.assertEqual(rs.check_reply(reply, {'UART'}), (False, 0))         # ще друкується
        self.assertEqual(rs.check_reply(reply + end, {'UART'}), (True, 0))
        # інша команда (загублено " reset") - не та відповідь
        self.assertEqual(rs.check_reply(reply + end, {'STATS'}), (False, 0))
        # побитий залишок попереднього рядка, потім наша команда
        bad = ['s', '4 CLI st=BAD', '4 EVT t_us=1 name=cmd_rx', '4 EVT t_us=1 name=cmd_reply']
        self.assertEqual(rs.check_reply(bad + reply + end, {'UART'}), (True, 1))
        # log < 2: EVT немає
        self.assertEqual(rs.check_reply(reply, {'UART'}, quiet=True), (True, 0))

    def test_ok_first_time(self):
        ser = FakeMcu()
        ok, _, log = self.run_command(ser, 'stats')
        self.assertTrue(ok)
        self.assertEqual(ser.written, b'stats\r')
        self.assertIn('# > stats', log)

    def test_lost_byte_then_repeat_with_cr(self):
        ser = FakeMcu(lose=1)                        # 's' загублено: прошивка бачить "tats"
        ok, _, log = self.run_command(ser, 'stats', timeout=0.5, char_timeout=0.1)
        self.assertTrue(ok)
        self.assertEqual(ser.written, b'stats\r\rstats\r')
        self.assertEqual(ser.cmds, ['tats', 'stats'])
        self.assertIn('# "stats": без ехо 1 байт', log)
        self.assertIn('# "stats": CLI st=BAD, спроба 1 з 4', log)
        self.assertIn('# > stats ' + rs.RETRY_NOTE, log)

    def test_splitter_lines_and_echo(self):
        sp = rs.StreamSplitter(hold=0.1)
        no_echo = lambda run: 0                             # noqa: E731
        # 's' з "busy_cyc" - частина рядка; ехо - 's' після кінця рядка
        out = list(sp.feed(b'12 LOAD pct=1 busy_cyc=5\r\ns', 0.0, no_echo))
        self.assertEqual(out, [('line', b'12 LOAD pct=1 busy_cyc=5'), ('echo', ord('s'))])
        # ехо літер впритул до рядка, потім ехо CR (CR LF)
        out = list(sp.feed(b'ta55262 LOAD pct=20\r\n\r\n', 0.0, no_echo))
        self.assertEqual(out, [('echo', ord('t')), ('echo', ord('a')),
                               ('line', b'55262 LOAD pct=20'), ('echo', 0x0D)])

    def test_splitter_waits_for_partial_stamp(self):
        sp = rs.StreamSplitter(hold=0.1)
        choose = lambda run: rs.echo_digits(run, 59000)     # noqa: E731
        # шматок закінчився посеред мітки: це ще не ехо
        self.assertEqual(list(sp.feed(b'5', 0.0, choose)), [])
        self.assertEqual(list(sp.feed(b'9123 ', 0.05, choose)), [])
        self.assertEqual(list(sp.feed(b'LOAD x\r\n', 0.06, choose)), [('line', b'59123 LOAD x')])
        # цифра, після якої за hold нічого не прийшло, - ехо
        self.assertEqual(list(sp.feed(b'5', 1.0, choose)), [])
        self.assertEqual(list(sp.feed(b'', 1.2, choose)), [('echo', ord('5'))])

    def test_echo_digits(self):
        # ехо '0' впритул до "298268 LOAD": мітка з 0 не починається
        self.assertEqual(rs.echo_digits(b'0298268', 297268), 1)
        # справжній рядок "298268 LOAD", ехо немає
        self.assertEqual(rs.echo_digits(b'298268', 297268, 297300), 0)
        # ехо '5' + "59123": 59123 ближче до очікуваного часу МК, ніж 559123
        self.assertEqual(rs.echo_digits(b'559123', 59000, 59100), 1)
        # остання мітка давня (log off 60 с): "160000 STATUS" - рядок, а не ехо '1' + 60000
        self.assertEqual(rs.echo_digits(b'160000', 100000), 0)
        self.assertEqual(rs.echo_digits(b'1160000', 100000, 160010), 1)
        # ціле число з команди відлунало раніше за рядок: "100" + "261516"
        self.assertEqual(rs.echo_digits(b'100261516', 261510, 261516), 3)
        # міток ще не було - ехо немає
        self.assertEqual(rs.echo_digits(b'559123', None), 0)

    def test_late_echo_does_not_block_next_bytes(self):
        class LateEcho(FakeSerial):
            """Ехо першого байта приходить лише разом з ехо другого."""
            held = b''

            def write(self, b):
                super().write(b)
                self.held += b
                if len(self.written) >= 2:
                    self.push(self.held)
                    self.held = b''

        with tempfile.TemporaryDirectory() as tmp:
            ser = LateEcho()
            rec = recorder(tmp, ser)
            rec.git = 'abc'
            rec.start()
            t0 = time.monotonic()
            missed = rs.type_line(ser, rec, 'stats', char_timeout=0.3, sleep=short_sleep)
            took = time.monotonic() - t0
            rec.finish('x')
        self.assertEqual(missed, 1)
        self.assertLess(took, 0.6)                   # один тайм-аут, а не по одному на кожен байт

    def test_bad_reply_is_repeated_at_once(self):
        ser = FakeMcu(bad=1)
        ok, took, log = self.run_command(ser, 'load', timeout=5)
        self.assertTrue(ok)
        self.assertLess(took, 2)                     # без очікування тайм-ауту
        self.assertEqual(ser.cmds, ['load', 'load'])
        self.assertIn('# "load": CLI st=BAD, спроба 1 з 4', log)

    def test_gives_up(self):
        ser = FakeMcu(lose=100)
        ok, _, log = self.run_command(ser, 'stack', timeout=0.1, tries=2, char_timeout=0.02)
        self.assertFalse(ok)
        self.assertEqual(ser.written, b'stack\r\rstack\r')
        self.assertEqual(log[-1], '# "stack" так і не виконалась')

    def test_quiet_log_without_evt(self):
        ser = FakeMcu(evt=False)
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, ser)
            rec.feed('90 KNOBS lcd_ms=500 lcd_chars=0 imu_ms=10 log=1')
            rec.start()
            self.assertTrue(rs.command(ser, rec, 'status', timeout=5, sleep=short_sleep))
            rec.finish('x')


class RunTest(unittest.TestCase):
    def test_run_sends_steps_and_final(self):
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeMcu(b'1 BOOT fw=x git=abc perf=1\r\n')
            rec = recorder(tmp, ser)
            rec.start()
            sc = {'name': 'T', 'setup': ['stats reset'], 'steps': [
                {'send': 'status'},
                {'stream': {'cmd': 'load', 'rate_hz': 0, 'count': 3}},
                {'prompt': 'натисни B1'}], 'final': ['stack']}
            asked = []
            rs.run(sc, ser, rec, input_fn=asked.append, sleep=short_sleep, hash_wait=0.5,
                   final_wait=0)
            rec.finish('x')
            log = read_log(rec)
        self.assertEqual(ser.written, b'set log 2\rset lcd_ms 500\rset lcd_chars 0\rset imu_ms 10\r'
                                      b'stats reset\rstatus\rload\rload\rload\rstack\r')
        self.assertEqual(len(asked), 1)
        self.assertIn('# ! натисни B1', log)
        self.assertIn('# >> потік "load" rate_hz=0 count=3 (час МК)', log)
        self.assertIn('# >> потік завершено: надіслано 3 команд, 15 байтів', log)

    def test_auto_markers_and_prompts(self):
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeMcu(b'1 BOOT fw=x git=abc perf=1\r\n')
            rec = recorder(tmp, ser)
            rec.start()
            sc = {'name': 'T', 'knobs_reset': False, 'setup': ['stats reset'],
                  'steps': [{'prompt': 'натисни B1'}], 'final': ['stats']}
            asked = []
            rs.run(sc, ser, rec, input_fn=asked.append, sleep=short_sleep, hash_wait=0.5,
                   final_wait=0, auto=True)
            rec.finish('x')
            log = read_log(rec)
        self.assertEqual(ser.written, b'stats reset\rset go 0\rstats\rset end 0\r')
        self.assertEqual(asked, [])                  # Enter не чекаємо
        self.assertIn('# ! натисни B1 (робить wokwi-cli)', log)

    def test_knobs_reset_off(self):
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeMcu(b'1 BOOT fw=x git=abc perf=1\r\n')
            rec = recorder(tmp, ser)
            rec.start()
            sc = {'name': 'T', 'knobs_reset': False, 'steps': [], 'final': ['stats']}
            rs.run(sc, ser, rec, sleep=short_sleep, hash_wait=0.5, final_wait=0)
            rec.finish('x')
        self.assertEqual(ser.written, b'stats\r')

    def test_mcu_now_and_speed(self):
        t = [100.0]
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, FakeSerial(), clock=lambda: t[0])
            self.assertIsNone(rec.mcu_now())
            for i in range(11):                      # симуляція вдвічі повільніша за реальний час
                t[0] = 100.0 + i
                rec.feed(f'{1000 + i * 500} LOAD pct=1 pm=1')
            self.assertAlmostEqual(rec.speed(), 0.5)
            t[0] = 112.0
            self.assertAlmostEqual(rec.mcu_now(), 6000 + 1000)
            t[0] = 130.0                             # симуляція стала: далі EXTRAP_MS не йдемо
            self.assertAlmostEqual(rec.mcu_now(), 6000 + rs.EXTRAP_MS)
            rec.finish('x')

    def test_stream_paced_by_mcu_time(self):
        t, last = [0.0], [0]
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeSerial()
            rec = recorder(tmp, ser, clock=lambda: t[0])
            rec.feed('0 LOAD pct=1 pm=1')

            def fake_sleep(dt):                      # МК іде вдвічі повільніше, рядок кожні 50 мс МК
                t[0] += dt
                ms = int(t[0] * 500)
                if ms // 50 != last[0]:
                    last[0] = ms // 50
                    rec.feed(f'{ms} LOAD pct=1 pm=1')

            sent = rs.stream(ser, rec, 'stats', 2, duration=5, sleep=fake_sleep, clock=lambda: t[0])
            rec.finish('x')
        self.assertIn(sent, (10, 11))                # 5 с МК × 2 Гц
        self.assertAlmostEqual(t[0], 10, delta=0.5)  # на хості - вдвічі довше

    def test_log_path_is_unique(self):
        import datetime
        now = datetime.datetime(2026, 11, 12, 10, 30)
        with tempfile.TemporaryDirectory() as tmp:
            first = rs.log_path(tmp, 'S1', now)
            self.assertTrue(first.endswith('S1_2026-11-12_1030.log'))
            open(first, 'w').close()
            second = rs.log_path(tmp, 'S1', now)
            self.assertTrue(second.endswith('S1_2026-11-12_1030_2.log'))

    def test_wait_mcu_uses_mcu_time(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, FakeSerial())
            rec.feed('1000 LOAD pct=1 pm=1 win_cyc=1 busy_cyc=1 windows=1')
            ticks = iter(range(2000, 100000, 1000))

            def fake_sleep(_):                      # кожна «пауза» - ще секунда МК
                rec.feed(f'{next(ticks)} LOAD pct=1 pm=1 win_cyc=1 busy_cyc=1 windows=1')

            self.assertTrue(rs.wait_mcu(rec, 5, sleep=fake_sleep))
            self.assertGreaterEqual(rec.last_ms - 1000, 5000)
            rec.finish('x')



class PromptTest(unittest.TestCase):
    def test_ask_enter_drops_typeahead_first(self):
        calls = []
        rs.ask_enter('>>> ', flush=lambda: calls.append('flush'),
                     read=lambda text: calls.append(('read', text)))
        self.assertEqual(calls, ['flush', ('read', '>>> ')])
        self.assertIs(inspect.signature(rs.run).parameters['input_fn'].default, rs.ask_enter)

class CliTest(unittest.TestCase):
    """wokwi-cli, що впав, видно одразу, а не після довгих тайм-аутів."""

    def test_connect_stops_when_cli_died(self):
        t0 = time.monotonic()
        with self.assertRaises(rs.LinkLost) as cm:
            rs.connect('rfc2217://localhost:1', 60, sleep=short_sleep, alive=lambda: False)
        self.assertLess(time.monotonic() - t0, 5)
        self.assertIn('wokwi-cli', str(cm.exception))

    def test_wait_mcu_stops_when_cli_died(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = recorder(tmp, FakeSerial())
            rec.feed('1000 LOAD pct=1')
            rec.alive = lambda: False
            t0 = time.monotonic()
            with self.assertRaises(rs.LinkLost):
                rs.wait_mcu(rec, 54, sleep=short_sleep)
            self.assertLess(time.monotonic() - t0, 1)
            rec.closing = True                     # після set end 0 вихід wokwi-cli - норма
            rec.check()
            rec.finish('x')

    def test_command_stops_when_link_broken(self):
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeSerial()                     # ехо немає
            rec = recorder(tmp, ser)
            rec.git = 'abc'
            rec.broken = 'Connection reset'
            with self.assertRaises(rs.LinkLost):
                rs.command(ser, rec, 'stats', sleep=short_sleep)
            rec.finish('x')

    def test_finish_cli_now_does_not_wait(self):
        import collections
        import subprocess
        proc = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(30)'])
        proc.tail = collections.deque(['wokwi-cli: тест'])
        t0 = time.monotonic()
        rs.finish_cli(proc, wait_s=0)
        self.assertLess(time.monotonic() - t0, 5)
        self.assertIsNotNone(proc.poll())

    def test_vcd_only_with_wokwi(self):
        for extra in (['--vcd'], ['--drop-part', 'imu'], ['--screenshot', 'lcd@3000']):
            with self.assertRaises(SystemExit):
                rs.main(['tools/scenarios/S1.yaml'] + extra)
        with self.assertRaises(SystemExit):                    # сценарій сам прибирає imu
            rs.main(['tools/scenarios/6.2.7-fault.yaml'])

    def test_cli_diagram_drops_part_and_its_wires(self):
        d = {'parts': [{'id': 'nucleo'}, {'id': 'imu'}, {'id': 'lcd'}, {'id': 'logic'}],
             'connections': [['nucleo:PA9', 'imu:SCL', 'green', []],
                             ['nucleo:PA9', 'lcd:SCL', 'green', []],
                             ['nucleo:PA9', 'logic:D0', 'green', []],
                             ['nucleo:PA6', 'logic:D2', 'green', []]]}
        out = rs.cli_diagram(d, ['imu'])
        self.assertEqual([p['id'] for p in out['parts']], ['nucleo', 'lcd', 'logic'])
        self.assertEqual([c[1] for c in out['connections']], ['lcd:SCL', 'logic:D2'])
        self.assertEqual(len(d['parts']), 4)                  # вихідна схема не змінилась
        with self.assertRaises(ValueError):
            rs.cli_diagram(d, ['mpu'])

    def test_cli_args_checked_before_wokwi_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            markers = os.path.join(tmp, 'markers.yaml')
            with open(markers, 'w', encoding='utf-8') as f:
                f.write('steps:\n  - wait-serial: "SET name=go st=BAD"\n')
            accel = os.path.join(tmp, 'accel.yaml')
            with open(accel, 'w', encoding='utf-8') as f:
                f.write('steps:\n  - set-control: {part-id: imu, control: accelX, value: 2}\n')
            self.assertIsNone(rs.cli_args_problem(markers, ['imu'], ('lcd', 3000), 900))
            self.assertIn('mpu', rs.cli_args_problem(markers, ['mpu']))
            self.assertIn('imu', rs.cli_args_problem(accel, ['imu']))      # файл дій крутить imu
            self.assertIn('lcd', rs.cli_args_problem(markers, ['lcd'], ('lcd', 3000)))
            self.assertIn('lcdx', rs.cli_args_problem(markers, [], ('lcdx', 3000)))
            self.assertIn('--sim-limit', rs.cli_args_problem(markers, [], ('lcd', 1000000), 900))
            self.assertIn('немає файлу', rs.cli_args_problem(os.path.join(tmp, 'nope.yaml')))
            self.assertIn('не екран', rs.cli_args_problem(markers, [], ('estop', 3000)))
            self.assertIn('--vcd', rs.cli_args_problem(markers, ['logic'], vcd=True))
            quoted = os.path.join(tmp, 'quoted.yaml')
            with open(quoted, 'w', encoding='utf-8') as f:
                f.write('# part-id: lcd - лише коментар\nsteps:\n'
                        '  - set-control: {part-id: "imu", control: accelX, value: 2}\n')
            self.assertEqual(rs.action_parts(open(quoted, encoding='utf-8').read()), {'imu'})
            self.assertIn('imu', rs.cli_args_problem(quoted, ['imu']))
            self.assertIsNone(rs.cli_args_problem(quoted, ['lcd']))
            for bad in (['--drop-part', 'mpu'], ['--screenshot', 'lcd@\u00b3000']):
                with self.assertRaises(SystemExit):            # помилка аргументів, а не traceback
                    rs.main(['tools/scenarios/S1.yaml', '--wokwi', markers] + bad)

    def test_header_names_dropped_parts(self):
        with tempfile.TemporaryDirectory() as tmp:
            meta = {'scenario': 'T', 'build': 'PERF=1', 'port': 'fake://',
                    'host_start': '2026-11-12T20:00:00', 'drop': ['imu']}
            rec = rs.Recorder(FakeSerial(), os.path.join(tmp, 'T.log'), meta)
            self.assertTrue(rec.header().endswith(' drop=imu'))
            rec.meta['drop'] = []
            self.assertIn('host_start=2026-11-12T20:00:00', rec.header())
            self.assertNotIn('drop=', rec.header())


if __name__ == '__main__':
    unittest.main()
