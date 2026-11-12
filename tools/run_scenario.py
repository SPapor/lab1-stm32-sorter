#!/usr/bin/env python3
"""Прогін сценарію ЛР3 через UART симулятора Wokwi (RFC2217) із записом логу.

    tools/run_scenario.py tools/scenarios/S1.yaml [--port rfc2217://localhost:4000]
                          [--out docs/metrics] [--build firmware/build]
    tools/run_scenario.py tools/scenarios/S2.yaml --wokwi tools/wokwi/S2.yaml [--vcd]

Без --wokwi скрипт підключається до симуляції у VS Code, а руками діє людина.
З --wokwi він сам запускає wokwi-cli (проєкт tools/wokwi/, RFC2217 на 4001) зі
сценарієм дій: LDR, кнопки (set-control) за часом симуляції. Обидва сценарії
синхронізуються маркерами в UART: після setup скрипт шле set go 0 (на рядку
відповіді SET name=go починаються дії), після final - set end 0 (на SET name=end
wokwi-cli закінчує і пише VCD). Маркер - саме рядок відповіді: прошивка друкує
його цілим, а ехо набраної команди може перемежовуватися іншими рядками.
Безкоштовний тариф Wokwi обриває сеанс wokwi-cli через 5 хв. Схема для wokwi-cli -
копія diagram.json з більшим буфером аналізатора (ANALYZER_BUFFER змін; за
замовчуванням Wokwi тримає 1 млн, а I2C на D0/D1 заповнює їх за ~24 с).
Підказки людині в цьому режимі - лише позначки в лозі, а замість Enter скрипт
чекає auto секунд часу МК (поле кроку prompt; без нього - wait, без нього - 0).
Потрібен WOKWI_CLI_TOKEN у середовищі.

У wokwi.toml має бути rfc2217ServerPort = 4000, симуляція запущена, а вкладка
симулятора у VS Code - видима (інакше Wokwi ставить симуляцію на паузу).

Лог: docs/metrics/<сценарій>_<дата>_<час>.log (якщо такий уже є - з _2, _3 ...).
Перший рядок - заголовок з git-хешем прошивки (з рядка BOOT; якщо BOOT уже
пропущено - з PERF у відповіді на stats, а якщо й того немає - з build/git.cfg) і
умовами прогону. Далі - рядки МК як є, без часу хоста: час подій беремо лише з
міток МК (<мс>, t_us), методичка 5.2. Рядки, що починаються з '#', пише сам скрипт.

Команди. Прошивка приймає UART опитуванням без FIFO: тримає один прийнятий байт,
а поки друкує відповідь, перемальовує LCD (~40 мс) чи читає MPU (~1 мс), наступні
губляться. Тому команди з setup, send і final скрипт набирає по байту, як людина
в терміналі: наступний байт - лише після ехо попереднього. Потім чекає на відповідь:
рядок відповіді саме цієї команди і EVT cmd_reply після нього. Якщо відповіді немає
або прийшло CLI st=BAD (наприклад, у буфері CLI лишився чужий байт), команда йде ще
раз з CR попереду, до TRIES спроб. У лозі це "# > cmd (повтор ...)".
Потік (stream) шле команди без очікування: це навантаження, втрати там і міряємо.

Перед setup скрипт повертає ручки set до значень після скидання (KNOBS_DEFAULT):
вони живуть в ОЗП МК і лишаються від попереднього прогону. knobs_reset: false у
сценарії це вимикає (6.2.7 - МК щойно скинуто, зайві команди заважали б старту).

Час. wait_mcu і stream рахують час МК за мітками <мс>: Wokwi може йти повільніше
за реальний час. Між мітками час МК оцінюється за швидкістю симуляції.

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
        auto: 5                       #   з --wokwi - чекати 5 с часу МК
      - stream: {cmd: stats, rate_hz: 2, duration: 10}   # потік за часом МК; rate_hz 0 - без пауз
      - repeat: {times: 5, steps: [{send: status}, {wait: 1}]}
      - comment: "довільна позначка в лозі"
    final: [stats, load, stack, heap]  # за замовчуванням саме ці
    knobs_reset: true                  # за замовчуванням
"""
import argparse
import collections
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
KNOBS_LOG = re.compile(r'^\d+ KNOBS\b.*\blog=(\d)')
LOG_ON = re.compile(r'^\d+ LOG on=(\d)')
LINE_START = re.compile(rb'\d+ [A-Z]')
DIGITS = re.compile(rb'\d+')
# Дозволені набори ключів кроку
STEP_FORMS = [{'send'}, {'send', 'wait'}, {'wait'}, {'wait_mcu'}, {'prompt'}, {'prompt', 'wait'},
              {'prompt', 'auto'}, {'prompt', 'wait', 'auto'}, {'stream'}, {'repeat'}, {'comment'}]
DEFAULT_FINAL = ['stats', 'load', 'stack', 'heap']
KNOBS_DEFAULT = ['set log 2', 'set lcd_ms 500', 'set lcd_chars 0', 'set imu_ms 10']
# За яким рядком видно, що виконалась саме ця команда (перший збіг за початком)
REPLY_EVENTS = [('stats reset', {'STATS'}), ('stats', {'UART'}), ('load', {'LOAD'}),
                ('stack', {'STACK'}), ('heap', {'HEAP'}), ('hist', {'HIST'}),
                ('set', {'KNOBS', 'SET'}), ('log', {'LOG'}), ('status', {'STATUS'}),
                ('reset', {'RESET'})]
RETRY_NOTE = '(повтор: CR + команда)'
REPLY_TIMEOUT = 3.0          # с хоста на відповідь при швидкості симуляції 1:1
TRIES = 4
TIMEOUT_FACTOR = 20.0        # у скільки разів Wokwi може бути повільнішим за реальний час
SPEED_WINDOW_S = 20.0        # за скільки останніх секунд хоста оцінюється швидкість симуляції
EXTRAP_MS = 1500             # далі останньої мітки час МК не екстраполюється (пауза симуляції)
ECHO_TIMEOUT = 2.0           # с хоста на ехо одного байта (цикл може стояти на I2C до 3 × 100 мс)
RAW_KEEP = 1 << 20           # скільки останніх сирих байтів UART тримати для пошуку ехо
CLI_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wokwi')
CLI_PORT = 'rfc2217://localhost:4001'
CLI_DIAGRAM = '.diagram.json'                      # у tools/wokwi, будується з diagram.json
ANALYZER_BUFFER = 10_000_000                       # змін рівня, ~4 хв з I2C
MARK_GO, MARK_END = 'set go 0', 'set end 0'      # відповідь: SET name=go|end st=BAD


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
        if 'auto' in st:
            _num(st['auto'], f'{w}.auto')
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
    if 'knobs_reset' in sc and not isinstance(sc['knobs_reset'], bool):
        raise ValueError('knobs_reset: true або false')


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


def read_cfg(build_dir, name):
    """build.cfg - варіант збірки, яку крутить Wokwi; git.cfg - її git-хеш."""
    try:
        with open(os.path.join(build_dir, name), encoding='utf-8') as f:
            return f.read().strip()
    except OSError:
        return 'unknown'


def log_path(out_dir, name, now):
    """docs/metrics/S1_2026-11-12_1030.log; якщо вже є - S1_2026-11-12_1030_2.log."""
    base = os.path.join(out_dir, f'{name}_{now:%Y-%m-%d_%H%M}')
    path, k = base + '.log', 2
    while os.path.exists(path):
        path, k = f'{base}_{k}.log', k + 1
    return path


# ---- запис логу --------------------------------------------------------------

class Recorder:
    """Читає рядки з UART у фоні. Поки git-хеш невідомий, рядки збираються в
    буфер; заголовок пишеться першим рядком, щойно хеш відомий (або на finish)."""

    def __init__(self, ser, path, meta, clock=time.monotonic):
        self.ser = ser
        self.path = path
        self.meta = meta                  # scenario, build, port, host_start
        self.clock = clock
        self.git = None
        self.git_from = None
        self.perf = None
        self.log_level = None             # з KNOBS log= і LOG on=
        self.pending = []
        self.f = None
        self.last_ms = None
        self.stamps = collections.deque(maxlen=512)   # (час хоста, мітка МК)
        self.rx = []                      # усі рядки МК - для пошуку відповідей
        self.raw = bytearray()            # сирі байти - для пошуку ехо
        self.raw_base = 0                 # номер першого байта raw від підключення
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
            self.feed_raw(chunk)
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
                self.stamps.append((self.clock(), self.last_ms))
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
            k = KNOBS_LOG.match(line)
            if k:
                self.log_level = int(k.group(1))
            else:
                k = LOG_ON.match(line)
                if k:
                    self.log_level = 2 if k.group(1) == '1' else 0
            self.rx.append(line)
            self.lines += 1
            self._write(line)

    def feed_raw(self, chunk):
        with self.lock:
            self.raw += chunk
            if len(self.raw) > RAW_KEEP:
                drop = len(self.raw) - RAW_KEEP // 2
                del self.raw[:drop]
                self.raw_base += drop

    def raw_pos(self):
        with self.lock:
            return self.raw_base + len(self.raw)

    def raw_since(self, pos):
        """(байт перед pos або None, байти від pos)."""
        with self.lock:
            i = max(pos - self.raw_base, 0)
            return (self.raw[i - 1] if i > 0 else None), bytes(self.raw[i:])

    def note(self, text):
        """Рядок від скрипта (починається з '#')."""
        with self.lock:
            self._write(text if text.startswith('#') else '# ' + text)

    def mark(self):
        with self.lock:
            return len(self.rx)

    def since(self, mark):
        with self.lock:
            return self.rx[mark:]

    def _speed(self):
        if len(self.stamps) < 2:
            return 1.0
        h1, m1 = self.stamps[-1]
        h0, m0 = next((s for s in self.stamps if h1 - s[0] <= SPEED_WINDOW_S), self.stamps[-1])
        if h1 - h0 < 1.0:
            return 1.0
        return min(max((m1 - m0) / ((h1 - h0) * 1000.0), 0.05), 2.0)

    def speed(self):
        """Швидкість симуляції: мс МК за мс хоста (1.0 - реальний час)."""
        with self.lock:
            return self._speed()

    def mcu_now(self):
        """Час МК зараз, мс: остання мітка + час хоста після неї × швидкість симуляції,
        але не далі ніж на EXTRAP_MS. None, поки не було жодної мітки."""
        with self.lock:
            if not self.stamps:
                return None
            h1, m1 = self.stamps[-1]
            return m1 + min((self.clock() - h1) * 1000.0 * self._speed(), EXTRAP_MS)

    def is_quiet(self):
        """log < 2: EVT (і cmd_reply теж) прошивка не друкує."""
        with self.lock:
            return self.log_level is not None and self.log_level < 2

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
        self.f = open(self.path, 'x', encoding='utf-8')
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


# ---- команди -----------------------------------------------------------------

def reply_events(cmd):
    for key, events in REPLY_EVENTS:
        if cmd == key or cmd.startswith(key + ' '):
            return events
    return None


def check_reply(lines, want, quiet=False):
    """Чи виконалась команда: (так/ні, скільки прийшло CLI st=BAD).
    Відповідь закінчується рядком EVT cmd_reply - прошивка друкує його після кожної
    команди, і після побитої теж. quiet (log < 2, EVT немає) - досить рядка з want."""
    events = [(m.group(2), line) for line in lines for m in [FR10.match(line)] if m]
    bad = sum(1 for ev, line in events if ev == 'CLI' and 'st=BAD' in line)
    if quiet:
        return (any(ev in want for ev, _ in events) if want else bool(lines) and not bad), bad
    seg = []
    for ev, line in events:
        if ev != 'EVT':
            seg.append((ev, line))
        elif 'name=cmd_reply' in line:
            if (not any(e == 'CLI' and 'st=BAD' in s for e, s in seg)
                    and (want is None or any(e in want for e, _ in seg))):
                return True, bad
            seg = []
    return False, bad


def find_echo(before, data, ch, at_start, ref_ms=None):
    """Індекс ехо байта ch у data або -1. Ехо прошивка друкує між рядками, тож
    байт рахується ехо, лише якщо перед ним кінець рядка або попереднє ехо
    (at_start - data починається одразу після нього). Так літера всередині
    чужого рядка (LOAD, EVT) не сприймається як ехо. Цифра перед "<мс> ПОДІЯ"
    може бути і ехо, і першою цифрою мітки: що з двох чисел (з нею чи без неї)
    ближче до ref_ms - останньої мітки МК, - те й мітка."""
    for i, b in enumerate(data):
        if b != ch:
            continue
        prev = data[i - 1] if i > 0 else before
        if not ((i == 0 and at_start) or prev == 0x0A):
            continue
        if LINE_START.match(data, i) and not echo_before_line(data, i, ref_ms):
            continue
        return i
    return -1


def echo_before_line(data, i, ref_ms):
    run = DIGITS.match(data, i).group()
    if ref_ms is None or len(run) < 2:
        return False
    return abs(int(run[1:]) - ref_ms) <= abs(int(run) - ref_ms)   # мітка МК не починається з 0


def type_line(ser, rec, text, char_timeout=ECHO_TIMEOUT, sleep=time.sleep, clock=time.monotonic):
    """Набрати text по байту: наступний - після ехо попереднього. Без ехо за
    char_timeout байт, можливо, загублено - решта все одно йде, а що вийшло,
    покаже відповідь. Повертає, скільки байтів так і не дали ехо."""
    pos = rec.raw_pos()
    before, _ = rec.raw_since(pos)
    at_start = before in (None, 0x0A)
    missed = 0
    for ch in text.encode():
        ser.write(bytes([ch]))
        deadline = clock() + char_timeout
        while True:
            before, data = rec.raw_since(pos)
            i = find_echo(before, data, ch, at_start, rec.last_ms)
            if i >= 0:
                pos += i + 1
                at_start = True
                break
            if clock() >= deadline:
                missed += 1                     # ехо наступного - знову одразу за pos
                break
            sleep(0.002)
    return missed


def command(ser, rec, cmd, timeout=REPLY_TIMEOUT, tries=TRIES, sleep=time.sleep,
            clock=time.monotonic, char_timeout=ECHO_TIMEOUT, done=None):
    """Набрати команду і дочекатися відповіді; повторити, якщо її загублено.
    done(рядки) - своя ознака успіху замість відповіді з EVT cmd_reply."""
    want = reply_events(cmd)
    for attempt in range(1, tries + 1):
        mark = rec.mark()
        slow = min(rec.speed(), 1.0)
        if attempt == 1:
            rec.note(f'# > {cmd}')
            missed = type_line(ser, rec, cmd + '\r', char_timeout / slow, sleep, clock)
        else:
            rec.note(f'# > {cmd} {RETRY_NOTE}')
            missed = type_line(ser, rec, '\r' + cmd + '\r', char_timeout / slow, sleep, clock)
        if missed:
            rec.note(f'# "{cmd}": без ехо {missed} байт')
        allowed_bad = 0 if attempt == 1 else 1      # CR попереду міг закрити побитий рядок
        deadline = clock() + timeout / slow
        while True:
            lines = rec.since(mark)
            ok, bad = check_reply(lines, want, rec.is_quiet())
            if ok or (done is not None and done(lines)):
                return True
            if bad > allowed_bad:
                why = 'CLI st=BAD'
                break
            if clock() >= deadline:
                why = 'немає відповіді'
                break
            sleep(0.02)
        rec.note(f'# "{cmd}": {why}, спроба {attempt} з {tries}')
    rec.note(f'# "{cmd}" так і не виконалась')
    return False


# ---- кроки -------------------------------------------------------------------

def wait_mcu(rec, seconds, timeout_factor=TIMEOUT_FACTOR, sleep=time.sleep):
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


def stream(ser, rec, cmd, rate_hz, duration=None, count=None, sleep=time.sleep,
           clock=time.monotonic):
    """Потік однакових команд без очікування відповіді. rate_hz і duration - за часом МК
    (Recorder.mcu_now); rate_hz = 0 - без пауз, байт за байтом."""
    now, base = rec.mcu_now, 'час МК'
    if now() is None:
        now, base = (lambda: clock() * 1000.0), 'час хоста: міток МК ще не було'
    rec.note(f'# >> потік "{cmd}" rate_hz={rate_hz} '
             + (f'duration={duration}' if duration is not None else f'count={count}') + f' ({base})')
    line = (cmd + '\r').encode()
    t0 = now()
    end = None if duration is None else t0 + duration * 1000.0
    period = 1000.0 / rate_hz if rate_hz else 0.0
    span = duration if duration is not None else (count / rate_hz if rate_hz else 0.0)
    host_end = clock() + span * TIMEOUT_FACTOR + 5
    sent, stalled = 0, False
    while count is None or sent < count:
        while period and now() < t0 + sent * period and clock() < host_end:
            sleep(0.005)
        if clock() >= host_end:
            stalled = True
            break
        if end is not None and now() >= end:
            break
        ser.write(line)
        sent += 1
    if stalled:
        rec.note('# потік зупинено: час МК не йде (симуляція на паузі?)')
    rec.note(f'# >> потік завершено: надіслано {sent} команд, {sent * len(line)} байтів')
    return sent


def run(sc, ser, rec, input_fn=input, sleep=time.sleep, hash_wait=3.0, final_wait=1.0,
        timeout=REPLY_TIMEOUT, auto=False):
    def cmd(c):
        return command(ser, rec, c, timeout=timeout, sleep=sleep)

    rec.note(f'# сценарій {sc["name"]}: {sc.get("title", "")}')
    # BOOT міг пройти до підключення - тоді git-хеш візьмемо з PERF у відповіді на stats
    t_end = time.monotonic() + hash_wait
    while rec.git is None and time.monotonic() < t_end:
        sleep(0.1)
    if rec.git is None:
        cmd('stats')
    if sc.get('knobs_reset', True):
        for c in KNOBS_DEFAULT:
            cmd(c)
    for c in sc.get('setup', []):
        cmd(c)
    if auto:
        cmd(MARK_GO)
    for st in expand(sc['steps']):
        if 'send' in st:
            cmd(st['send'])
            if 'wait' in st:
                sleep(st['wait'])
        elif 'prompt' in st and auto:
            rec.note(f'# ! {st["prompt"]} (робить wokwi-cli)')
            secs = st.get('auto', st.get('wait', 0))
            if secs:
                wait_mcu(rec, secs, sleep=sleep)
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
    for c in sc.get('final', DEFAULT_FINAL):
        cmd(c)
    if auto:
        # wokwi-cli зупиняє симуляцію одразу на рядку SET name=end, EVT уже не буде
        command(ser, rec, MARK_END, timeout=timeout, sleep=sleep,
                done=lambda lines: any('SET name=end' in s for s in lines))
    # дочекатися рядків, що йдуть уже після останньої відповіді
    last, t_end = -1, time.monotonic() + final_wait
    while time.monotonic() < t_end and rec.lines != last:
        last = rec.lines
        sleep(0.5)


def connect(url, wait_s, sleep=time.sleep):
    """RFC2217-сервер Wokwi з'являється лише з запуском симуляції: чекаємо до wait_s с.
    Для сценарію старту (6.2.7) скрипт запускають раніше за симуляцію."""
    import serial
    t_end = time.monotonic() + wait_s
    told = False
    while True:
        try:
            return serial.serial_for_url(url, baudrate=115200, timeout=0.2)
        except (serial.SerialException, OSError) as e:
            if time.monotonic() >= t_end:
                sys.exit(f'{url}: немає з\'єднання ({e}). Симуляція запущена? '
                         'У wokwi.toml є rfc2217ServerPort = 4000?')
            if not told:
                print(f'чекаю на {url} (симуляцію у VS Code або wokwi-cli) ...', flush=True)
                told = True
            sleep(0.5)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('scenario')
    ap.add_argument('--port', help='rfc2217://localhost:4000 (з --wokwi - 4001)')
    ap.add_argument('--out', default='docs/metrics')
    ap.add_argument('--build', default='firmware/build', help='звідки build.cfg і git.cfg')
    ap.add_argument('--hash-wait', type=float, default=3.0)
    ap.add_argument('--wait-connect', type=float, default=60.0,
                    help='скільки секунд чекати на RFC2217-сервер Wokwi (60)')
    ap.add_argument('--reply-timeout', type=float, default=REPLY_TIMEOUT,
                    help=f'скільки секунд чекати на відповідь команди ({REPLY_TIMEOUT:g})')
    ap.add_argument('--wokwi', metavar='ДІЇ.yaml',
                    help='прогін без вікна: wokwi-cli з цим сценарієм дій (tools/wokwi/<S>.yaml)')
    ap.add_argument('--vcd', action='store_true',
                    help='з --wokwi: запис аналізатора в docs/captures/<назва логу>.vcd')
    ap.add_argument('--sim-limit', type=float, default=900,
                    help='з --wokwi: межа часу симуляції, с (900)')
    args = ap.parse_args(argv)

    sc = load_scenario(args.scenario)
    port = args.port or (CLI_PORT if args.wokwi else 'rfc2217://localhost:4000')
    now = datetime.datetime.now()
    path = log_path(args.out, sc['name'], now)
    vcd = None
    if args.vcd:
        vcd = os.path.join('docs', 'captures', os.path.basename(path)[:-4] + '.vcd')
    cli = start_cli(args.wokwi, args.sim_limit, vcd) if args.wokwi else None
    ser = connect(port, args.wait_connect)
    rec = Recorder(ser, path, {'scenario': sc['name'], 'build': read_cfg(args.build, 'build.cfg'),
                               'port': port, 'host_start': f'{now:%Y-%m-%dT%H:%M:%S}'})
    rec.start()
    try:
        run(sc, ser, rec, hash_wait=args.hash_wait, timeout=args.reply_timeout, auto=cli is not None)
    except KeyboardInterrupt:
        rec.note('# перервано вручну (Ctrl+C)')
    finally:
        rec.finish(read_cfg(args.build, 'git.cfg'))
        ser.close()
        if cli is not None:
            finish_cli(cli)
    print(f'лог: {path} ({rec.lines} рядків від МК)' + (f', VCD: {vcd}' if vcd else ''))
    return path


def start_cli(actions, sim_limit, vcd):
    """wokwi-cli у фоні: проєкт tools/wokwi (та сама прошивка і diagram.json)."""
    import subprocess
    with open(os.path.join(CLI_DIR, '..', '..', 'diagram.json'), encoding='utf-8') as f:
        diagram = json.load(f)
    for part in diagram['parts']:
        if part['type'] == 'wokwi-logic-analyzer':
            part.setdefault('attrs', {})['bufferSize'] = str(ANALYZER_BUFFER)
    with open(os.path.join(CLI_DIR, CLI_DIAGRAM), 'w', encoding='utf-8') as f:
        json.dump(diagram, f, indent=2)
    cmd = ['wokwi-cli', '--quiet', '--diagram-file', CLI_DIAGRAM,
           '--scenario', os.path.abspath(actions), '--timeout', str(int(sim_limit * 1000))]
    if vcd:
        os.makedirs(os.path.dirname(vcd), exist_ok=True)
        cmd += ['--vcd-file', os.path.abspath(vcd)]
    proc = subprocess.Popen(cmd + [CLI_DIR], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    proc.tail = collections.deque(maxlen=30)

    def drain():
        for line in proc.stdout:
            proc.tail.append(line.decode('utf-8', 'replace').rstrip())
    threading.Thread(target=drain, daemon=True).start()
    return proc


def finish_cli(proc, wait_s=300):
    """Дочекатися, поки wokwi-cli допише VCD і вийде."""
    try:
        code = proc.wait(timeout=wait_s)
    except Exception:
        proc.terminate()
        code = proc.wait()
    if code != 0:
        print(f'wokwi-cli завершився з кодом {code}:', *proc.tail, sep='\n', file=sys.stderr)


if __name__ == '__main__':
    main()
