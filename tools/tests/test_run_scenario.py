"""Тести tools/run_scenario.py без Wokwi: розбір сценарію, запис логу, заголовок.

    python3 -m unittest discover -s tools/tests
"""
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

    def test_find_echo_skips_letters_inside_lines(self):
        # 's' з "busy_cyc" не ехо; ехо - 's' після кінця рядка
        data = b'12 LOAD pct=1 busy_cyc=5\r\ns'
        self.assertEqual(rs.find_echo(None, data, ord('s'), False), len(data) - 1)
        self.assertEqual(rs.find_echo(ord('x'), b'st', ord('s'), True), 0)
        self.assertEqual(rs.find_echo(ord('x'), b'st', ord('s'), False), -1)
        # '1' з мітки "102407 LOAD" - не ехо; ехо - '1' після кінця рядка
        data = b'102407 LOAD pct=20\r\n1'
        self.assertEqual(rs.find_echo(ord(' '), data, ord('1'), True), len(data) - 1)
        # ехо '0' впритул до рядка "298268 LOAD": остання мітка МК 297268
        data = b'0298268 LOAD pct=20\r\n'
        self.assertEqual(rs.find_echo(ord('0'), data, ord('0'), True, 297268), 0)
        # справжній рядок "298268 LOAD", а чекаємо '2': не ехо
        data = b'298268 LOAD pct=20\r\n'
        self.assertEqual(rs.find_echo(ord(' '), data, ord('2'), True, 297268), -1)

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


if __name__ == '__main__':
    unittest.main()
