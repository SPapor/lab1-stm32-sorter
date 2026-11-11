#!/usr/bin/env python3
"""Прогін сценарію ЛР3 через UART симулятора Wokwi (RFC2217) із записом логу.

    tools/run_scenario.py tools/scenarios/S1.yaml [--port rfc2217://localhost:4000]
                          [--out docs/metrics] [--build firmware/build]

У wokwi.toml має бути rfc2217ServerPort = 4000, симуляція запущена, а вкладка
симулятора у VS Code - видима (інакше Wokwi ставить симуляцію на паузу).

Лог: docs/metrics/<сценарій>_<дата>.log. Перший рядок - заголовок з git-хешем
прошивки (з рядка BOOT; якщо BOOT уже пропущено - з PERF у відповіді на stats, а
якщо й того немає - з build/git.cfg) і умовами прогону. Далі - рядки МК як є,
без часу хоста: час подій беремо лише з міток МК (<мс>, t_us), методичка 5.2.
Рядки, що починаються з '#', пише сам скрипт (кроки сценарію, підказки).

Сценарій (YAML або JSON):

    name: S1
    title: Простій
    setup: [stats reset, log on]      # команди на початку
    steps:
      - send: status                  # команда
      - wait: 2                       # пауза, секунди хоста
      - wait_mcu: 60                  # пауза за часом МК (по мітках <мс>)
      - prompt: "Натисни B1"          # підказка людині; без wait - чекати Enter
        wait: 30                      #   з wait - просто почекати
      - stream: {cmd: stats, rate_hz: 20, duration: 10}   # потік; rate_hz 0 - без пауз
      - repeat: {times: 5, steps: [{send: status}, {wait: 1}]}
      - comment: "довільна позначка в лозі"
    final: [stats, load, stack, heap]  # за замовчуванням саме ці
"""
import argparse
import datetime
import json
import os
import re
import sys
import threading
import time

FR10 = re.compile(r'^(\d+) ([A-Z][A-Z0-9_]*)\b')
BOOT_GIT = re.compile(r'^\d+ BOOT\b.*\bgit=(\S+)')
BOOT_PERF = re.compile(r'^\d+ BOOT\b.*\bperf=(\d)')
PERF_GIT = re.compile(r'^\d+ PERF\b.*\bgit=(\S+)')
# Дозволені набори ключів кроку
STEP_FORMS = [{'send'}, {'send', 'wait'}, {'wait'}, {'wait_mcu'}, {'prompt'}, {'prompt', 'wait'},
              {'stream'}, {'repeat'}, {'comment'}]
DEFAULT_FINAL = ['stats', 'load', 'stack', 'heap']


# ---- сценарій ----------------------------------------------------------------

def load_scenario(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    if path.endswith('.json'):
        sc = json.loads(text)
    else:
        try:
            import yaml
        except ImportError:
            sys.exit('для .yaml потрібен PyYAML (pip install pyyaml) або візьміть .json')
        sc = yaml.safe_load(text)
    validate(sc)
    return sc


def _num(v, what):
    if not isinstance(v, (int, float)) or isinstance(v, bool) or v < 0:
        raise ValueError(f'{what}: потрібне число ≥ 0, а не {v!r}')


def validate_steps(steps, where='steps'):
    if not isinstance(steps, list):
        raise ValueError(f'{where}: потрібен список кроків')
    for i, st in enumerate(steps):
        w = f'{where}[{i}]'
        if not isinstance(st, dict) or not st:
            raise ValueError(f'{w}: крок - це словник, напр. {{send: status}}')
        if set(st) not in STEP_FORMS:
            raise ValueError(f'{w}: невідомий крок {sorted(st)}; можна: '
                             + ', '.join('+'.join(sorted(f)) for f in STEP_FORMS))
        if 'send' in st and not isinstance(st['send'], str):
            raise ValueError(f'{w}: send - рядок команди')
        if 'wait' in st:
            _num(st['wait'], f'{w}.wait')
        if 'wait_mcu' in st:
            _num(st['wait_mcu'], f'{w}.wait_mcu')
        if 'prompt' in st and not isinstance(st['prompt'], str):
            raise ValueError(f'{w}: prompt - текст підказки')
        if 'stream' in st:
            s = st['stream']
            if not isinstance(s, dict) or not isinstance(s.get('cmd'), str):
                raise ValueError(f'{w}: stream потребує cmd')
            _num(s.get('rate_hz', 0), f'{w}.stream.rate_hz')
            if ('duration' in s) == ('count' in s):
                raise ValueError(f'{w}: stream - або duration (с), або count')
            _num(s.get('duration', s.get('count')), f'{w}.stream.duration/count')
        if 'repeat' in st:
            r = st['repeat']
            if not isinstance(r, dict) or not isinstance(r.get('times'), int) or r['times'] < 1:
                raise ValueError(f'{w}: repeat потребує times ≥ 1')
            validate_steps(r.get('steps'), f'{w}.repeat.steps')


def validate(sc):
    if not isinstance(sc, dict):
        raise ValueError('сценарій - словник з name і steps')
    if not isinstance(sc.get('name'), str) or not re.fullmatch(r'[A-Za-z0-9_.-]+', sc['name']):
        raise ValueError('name: коротка назва для імені файлу, напр. S1 або 6.2.2')
    validate_steps(sc.get('steps'))
    for key in ('setup', 'final'):
        if key in sc and not (isinstance(sc[key], list) and all(isinstance(c, str) for c in sc[key])):
            raise ValueError(f'{key}: список команд')


def expand(steps):
    """repeat розгортається в плоский список кроків."""
    out = []
    for st in steps:
        if 'repeat' in st:
            for _ in range(st['repeat']['times']):
                out.extend(expand(st['repeat']['steps']))
        else:
            out.append(st)
    return out


def read_build_cfg(build_dir):
    """Варіант збірки прошивки, яку крутить Wokwi (firmware/build/build.cfg)."""
    try:
        with open(os.path.join(build_dir, 'build.cfg'), encoding='utf-8') as f:
            return f.read().strip()
    except OSError:
        return 'unknown'


def read_git_cfg(build_dir):
    try:
        with open(os.path.join(build_dir, 'git.cfg'), encoding='utf-8') as f:
            return f.read().strip()
    except OSError:
        return 'unknown'


# ---- запис логу --------------------------------------------------------------

class Recorder:
    """Читає рядки з UART у фоні. Поки git-хеш невідомий, рядки збираються в
    буфер; заголовок пишеться першим рядком, щойно хеш відомий (або на finish)."""

    def __init__(self, ser, path, meta):
        self.ser = ser
        self.path = path
        self.meta = meta                  # scenario, build, port, host_start
        self.git = None
        self.git_from = None
        self.perf = None
        self.pending = []
        self.f = None
        self.last_ms = None
        self.lines = 0
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._reader, daemon=True)

    def start(self):
        self.thread.start()

    def _reader(self):
        buf = b''
        while not self.stop.is_set():
            try:
                chunk = self.ser.read(256)
            except Exception as e:              # з'єднання з Wokwi обірвалося
                self.note(f'# помилка читання: {e}')
                break
            if not chunk:
                continue
            buf += chunk
            while True:
                i = buf.find(b'\n')
                if i < 0:
                    break
                line = buf[:i].rstrip(b'\r').decode('utf-8', 'replace')
                buf = buf[i + 1:]
                self.feed(line)
        if buf:
            self.feed(buf.decode('utf-8', 'replace'))

    def feed(self, line):
        """Один рядок від МК."""
        with self.lock:
            m = FR10.match(line)
            if m:
                self.last_ms = int(m.group(1))
            if self.git is None:
                g = BOOT_GIT.match(line) or PERF_GIT.match(line)
                if g:
                    self.git = g.group(1)
                    self.git_from = 'BOOT' if line.split()[1:2] == ['BOOT'] else 'PERF'
            p = BOOT_PERF.match(line)
            if p:
                self.perf = p.group(1)
            elif self.perf is None and PERF_GIT.match(line):
                self.perf = '1'
            self.lines += 1
            self._write(line)

    def note(self, text):
        """Рядок від скрипта (починається з '#')."""
        with self.lock:
            self._write(text if text.startswith('#') else '# ' + text)

    def _write(self, line):
        if self.f is None:
            self.pending.append(line)
            if self.git is not None:
                self._open()
        else:
            self.f.write(line + '\n')
            self.f.flush()

    def _open(self):
        os.makedirs(os.path.dirname(self.path) or '.', exist_ok=True)
        self.f = open(self.path, 'w', encoding='utf-8')
        self.f.write(self.header() + '\n')
        for line in self.pending:
            self.f.write(line + '\n')
        self.pending = []
        self.f.flush()

    def header(self):
        m = self.meta
        return (f'# git={self.git or "unknown"} (з {self.git_from or "-"}) '
                f'scenario={m["scenario"]} build="{m["build"]}" perf={self.perf or "?"} '
                f'port={m["port"]} host_start={m["host_start"]}')

    def finish(self, fallback_git):
        self.stop.set()
        if self.thread.ident is not None:       # потік міг і не стартувати
            self.thread.join(timeout=2)
        with self.lock:
            if self.f is None:
                if self.git is None:
                    self.git, self.git_from = fallback_git, 'build/git.cfg'
                self._open()
            self.f.close()


# ---- виконання -----------------------------------------------------------------

def send(ser, rec, cmd):
    rec.note(f'# > {cmd}')
    ser.write((cmd + '\r').encode())


def wait_mcu(rec, seconds, timeout_factor=20.0, sleep=time.sleep):
    """Чекати, поки час МК (мітки <мс> у рядках) просунеться на seconds.
    Wokwi іде повільніше за реальний час, тож межа на хості - seconds * timeout_factor."""
    start = rec.last_ms
    deadline = time.monotonic() + seconds * timeout_factor + 5
    while time.monotonic() < deadline:
        cur = rec.last_ms
        if start is None:
            start = cur
        elif cur is not None and cur - start >= seconds * 1000:
            return True
        sleep(0.2)
    rec.note(f'# wait_mcu {seconds} с: час МК не дійшов (немає рядків з <мс>? увімкніть log on)')
    return False


def stream(ser, rec, cmd, rate_hz, duration=None, count=None, sleep=time.sleep):
    """Потік однакових команд. rate_hz = 0 - без пауз, байт за байтом."""
    rec.note(f'# >> потік "{cmd}" rate_hz={rate_hz} '
             + (f'duration={duration}' if duration is not None else f'count={count}'))
    line = (cmd + '\r').encode()
    sent = 0
    t_end = time.monotonic() + duration if duration is not None else None
    while (count is None or sent < count) and (t_end is None or time.monotonic() < t_end):
        ser.write(line)
        sent += 1
        if rate_hz:
            sleep(1.0 / rate_hz)
    rec.note(f'# >> потік завершено: надіслано {sent} команд, {sent * len(line)} байтів')
    return sent


def run(sc, ser, rec, input_fn=input, sleep=time.sleep, hash_wait=3.0, final_wait=3.0):
    rec.note(f'# сценарій {sc["name"]}: {sc.get("title", "")}')
    # BOOT міг пройти до підключення - тоді git-хеш візьмемо з PERF у відповіді на stats
    t_end = time.monotonic() + hash_wait
    while rec.git is None and time.monotonic() < t_end:
        sleep(0.1)
    if rec.git is None:
        send(ser, rec, 'stats')
        sleep(min(hash_wait, 1.0))
    for cmd in sc.get('setup', []):
        send(ser, rec, cmd)
        sleep(0.3)
    for st in expand(sc['steps']):
        if 'send' in st:
            send(ser, rec, st['send'])
            if 'wait' in st:
                sleep(st['wait'])
        elif 'prompt' in st:
            rec.note(f'# ! {st["prompt"]}')
            if 'wait' in st:
                print(f'>>> {st["prompt"]} ({st["wait"]} с)', flush=True)
                sleep(st['wait'])
            else:
                input_fn(f'>>> {st["prompt"]} - Enter, коли готово: ')
        elif 'wait_mcu' in st:
            wait_mcu(rec, st['wait_mcu'], sleep=sleep)
        elif 'wait' in st:
            sleep(st['wait'])
        elif 'stream' in st:
            s = st['stream']
            stream(ser, rec, s['cmd'], s.get('rate_hz', 0), s.get('duration'), s.get('count'),
                   sleep=sleep)
        elif 'comment' in st:
            rec.note(f'# {st["comment"]}')
    for cmd in sc.get('final', DEFAULT_FINAL):
        send(ser, rec, cmd)
        sleep(0.5)
    # дочекатися, поки МК допише відповіді
    last, t_end = -1, time.monotonic() + final_wait
    while time.monotonic() < t_end and rec.lines != last:
        last = rec.lines
        sleep(1.0)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('scenario')
    ap.add_argument('--port', default='rfc2217://localhost:4000')
    ap.add_argument('--out', default='docs/metrics')
    ap.add_argument('--build', default='firmware/build', help='звідки build.cfg і git.cfg')
    ap.add_argument('--hash-wait', type=float, default=3.0)
    args = ap.parse_args(argv)

    sc = load_scenario(args.scenario)
    import serial
    ser = serial.serial_for_url(args.port, baudrate=115200, timeout=0.2)
    now = datetime.datetime.now()
    path = os.path.join(args.out, f'{sc["name"]}_{now:%Y-%m-%d_%H%M}.log')
    rec = Recorder(ser, path, {'scenario': sc['name'], 'build': read_build_cfg(args.build),
                               'port': args.port, 'host_start': f'{now:%Y-%m-%dT%H:%M:%S}'})
    rec.start()
    try:
        run(sc, ser, rec, hash_wait=args.hash_wait)
    except KeyboardInterrupt:
        rec.note('# перервано вручну (Ctrl+C)')
    finally:
        rec.finish(read_git_cfg(args.build))
        ser.close()
    print(f'лог: {path} ({rec.lines} рядків від МК)')
    return path


if __name__ == '__main__':
    main()
