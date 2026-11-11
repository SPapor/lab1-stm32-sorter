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


def recorder(tmp, ser):
    meta = {'scenario': 'T', 'build': 'LAB2_CAP=0 TEST_I2C_FAULT=0 PERF=1',
            'port': 'fake://', 'host_start': '2026-11-11T20:00:00'}
    return rs.Recorder(ser, os.path.join(tmp, 'T.log'), meta)


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


class RunTest(unittest.TestCase):
    def test_run_sends_steps_and_final(self):
        with tempfile.TemporaryDirectory() as tmp:
            ser = FakeSerial(b'1 BOOT fw=x git=abc perf=1\r\n')
            rec = recorder(tmp, ser)
            rec.start()
            sc = {'name': 'T', 'setup': ['stats reset'], 'steps': [
                {'send': 'status'},
                {'stream': {'cmd': 'load', 'rate_hz': 0, 'count': 3}},
                {'prompt': 'натисни B1'}], 'final': ['stack']}
            asked = []
            rs.run(sc, ser, rec, input_fn=asked.append, sleep=lambda s: None,
                   hash_wait=0.5, final_wait=0)
            rec.finish('x')
            log = read_log(rec)
        self.assertEqual(ser.written, b'stats reset\rstatus\rload\rload\rload\rstack\r')
        self.assertEqual(len(asked), 1)
        self.assertIn('# ! натисни B1', log)
        self.assertIn('# >> потік завершено: надіслано 3 команд, 15 байтів', log)

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
