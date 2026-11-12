#!/usr/bin/env python3
"""Таблиця результатів S1/S2/S3 у форматі методички 6.3 з логів run_scenario.py.

    tools/stats_table.py --s1 S1.log --s2 S2.log --s3 S3.log [--vcd S3=run.vcd ...]
    tools/stats_table.py --blocks 6.2.5.log

Береться останній блок stats у кожному лозі (PERF git=... -> KNOBS -> STARTUP ->
STAT ... -> UART), рядки LOAD/STACK/HEAP за весь прогін і події EVT
(tools/evt_latency.py, з 99-м перцентилем). Вердикт - за максимумом у S3: «виконана»
лише тоді, коли максимум у S3 не перевищує вимоги PRD (методичка 6.3).
--vcd додає рядки з запису аналізатора (tools/vcd_timing.py) - так одну метрику видно
трьома способами: лічильник у прошивці, журнал і аналізатор.
--blocks: по рядку на кожен блок stats одного логу (сценарій 6.2.5, точка зламу).
Усі часи з Wokwi - симуляційні (методичка 2.2).
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import evt_latency  # noqa: E402
import vcd_timing   # noqa: E402
from evt_latency import fmt_ms  # noqa: E402
from run_scenario import RETRY_NOTE  # noqa: E402

KV = re.compile(r'(\w+)=(\S+)')
HEADER = re.compile(r'^# git=(\S+).*?scenario=(\S+) build="([^"]*)"')
STREAM_DONE = re.compile(r'^# >> потік завершено: надіслано \d+ команд, (\d+) байтів')
STACK_LIMIT = 1024           # резерв стека; used = резерв - це вже переповнення
GATE_PERIOD_MS = 5
PWM_PERIOD_MS = 20
FR03_MS = 50


def event_of(line):
    """('STAT', {...}) для рядка '<мс> STAT name=...' або (None, None)."""
    parts = line.split(' ', 2)
    if len(parts) < 2 or not parts[0].isdigit():
        return None, None
    return parts[1], dict(KV.findall(parts[2] if len(parts) > 2 else ''))


def _int(d, k, default=0):
    try:
        return int(d.get(k, default))
    except ValueError:
        return default


def parse_lines(lines):
    run = {'git': '?', 'scenario': '?', 'build': '?', 'blocks': [], 'load': [], 'stack': [],
           'heap': [], 'bad_cmds': 0, 'knobs': None, 'events': []}
    block, sent, in_stream, reset = None, 0, False, False
    lines = list(lines)
    for line in lines:
        line = line.rstrip('\n')
        if line.startswith('#'):
            m = HEADER.match(line)
            if m:
                run['git'], run['scenario'], run['build'] = m.groups()
            elif line.startswith('# > '):
                retry = line.endswith(' ' + RETRY_NOTE)
                cmd = line[4:-len(RETRY_NOTE) - 1] if retry else line[4:]
                if cmd == 'stats reset':
                    sent = 0                   # rx у прошивці теж обнуляється
                    reset = True
                else:
                    sent += len(cmd) + 1 + retry   # команда + '\r' (+ CR попереду при повторі)
            elif line.startswith('# >> потік "'):
                in_stream = True               # байти потоку відомі лише в кінці
            else:
                m = STREAM_DONE.match(line)
                if m:
                    sent += int(m.group(1))
                    in_stream = False
            continue
        ev, kv = event_of(line)
        if ev == 'BOOT':
            reset = True                       # МК стартував у цьому лозі: лічильники з нуля
        elif ev == 'PERF' and 'git' in kv:
            block = {'knobs': None, 'startup': None, 'stat': {}, 'uart': None,
                     'load': run['load'][-1] if run['load'] else None, 'reset': reset}
            run['blocks'].append(block)
        elif ev == 'KNOBS':
            run['knobs'] = kv
            if block is not None and block['knobs'] is None:
                block['knobs'] = kv
        elif ev == 'STARTUP' and block is not None:
            block['startup'] = {k: _int(kv, k) for k in kv}
        elif ev == 'STAT' and block is not None:
            block['stat'][kv.get('name', '?')] = {k: (v if k == 'name' else _int(kv, k))
                                                  for k, v in kv.items()}
        elif ev == 'UART' and block is not None:
            block['uart'] = {'rx': _int(kv, 'rx'), 'ore': _int(kv, 'ore'),
                             'sent': None if in_stream else sent}
            block = None
        elif ev == 'LOAD':
            run['load'].append({'pct': _int(kv, 'pct'), 'pm': _int(kv, 'pm')})
        elif ev == 'STACK':
            run['stack'].append({k: _int(kv, k) for k in kv})
        elif ev == 'HEAP':
            run['heap'].append({k: _int(kv, k) for k in kv})
        elif ev == 'CLI' and kv.get('st') == 'BAD':
            run['bad_cmds'] += 1
    run['events'] = evt_latency.parse_events(lines)
    run['blocks'] = [b for b in run['blocks'] if b['uart'] is not None]
    return run


def parse_log(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return parse_lines(f)


# ---- метрики -------------------------------------------------------------------

def last_block(run):
    return run['blocks'][-1] if run and run['blocks'] else None


def stat(run, name, field):
    b = last_block(run)
    s = b['stat'].get(name) if b else None
    return s.get(field) if s and s.get('n', 0) else None


def fr03_estimate_us(block, parcel_us=None):
    """Від появи посилки до зміни ШІМ: період gate + найгірший запізнілий старт gate +
    PARCEL_IN -> CCR + період ШІМ (docs/tasks.md, розділ 3)."""
    gate = block['stat'].get('gate') if block else None
    par = block['stat'].get('parcel_latency') if block else None
    if not gate or not gate.get('n'):
        return None
    p = par['max_us'] if par and par.get('n') else parcel_us
    if p is None:
        return None
    return GATE_PERIOD_MS * 1000 + max(gate.get('jit_max_us', 0), 0) + p + PWM_PERIOD_MS * 1000


def evt_max(run, start, end, field='p99'):
    if not run:
        return None
    s = evt_latency.summary(evt_latency.latencies(run['events'], start, end))
    return s[field] if s else None


def lost_bytes(run):
    b = last_block(run)
    if not b or b['uart'] is None or b['uart']['sent'] is None:
        return None
    return b['uart']['sent'] - b['uart']['rx']


def fmt_n(v):
    return '-' if v is None else str(v)


# (назва, функція run -> значення, формат, вимога PRD, межа для вердикту, спосіб)
ROWS = [
    ('FR-03: поява посилки -> зміна ШІМ (оцінка), max',
     lambda r: fr03_estimate_us(last_block(r)), fmt_ms, '≤ 50 мс (FR-03)', FR03_MS * 1000,
     'лічильник + розрахунок'),
    ('PARCEL_IN -> запис CCR, max', lambda r: stat(r, 'parcel_latency', 'max_us'), fmt_ms,
     'частина FR-03', None, 'лічильник'),
    ('PARCEL_IN -> запис CCR, p99', lambda r: evt_max(r, 'parcel_in', 'servo_set'),
     fmt_ms, 'частина FR-03', None, 'журнал EVT'),
    ('E-STOP: фронт PC13 -> реакція, max', lambda r: stat(r, 'estop_latency', 'max_us'), fmt_ms,
     'не задано (FR-07)', None, 'лічильник'),
    ('E-STOP: фронт PC13 -> реакція, p99', lambda r: evt_max(r, 'estop_irq', 'estop_react'),
     fmt_ms, 'не задано (FR-07)', None, 'журнал EVT'),
    ('gate: джитер старту, max', lambda r: stat(r, 'gate', 'jit_max_us'), fmt_ms,
     '≤ 22,4 мс (запас FR-03)', 22_400, 'лічильник'),
    ('gate: пропуски (інтервал > 2 періодів)', lambda r: stat(r, 'gate', 'miss'), fmt_n,
     '-', None, 'лічильник'),
    ('imu: джитер старту, max', lambda r: stat(r, 'imu', 'jit_max_us'), fmt_ms, '-', None,
     'лічильник'),
    ('imu: пропуски', lambda r: stat(r, 'imu', 'miss'), fmt_n, '-', None, 'лічильник'),
    ('lcd: час задачі, max', lambda r: stat(r, 'lcd', 'max_us'), fmt_ms, '-', None, 'лічильник'),
    ('період головного циклу (loop), max', lambda r: stat(r, 'loop', 'max_us'), fmt_ms, '-', None,
     'лічильник'),
    ('uart_rx (команда + відповідь), max', lambda r: stat(r, 'uart_rx', 'max_us'), fmt_ms, '-',
     None, 'лічильник'),
    ('команда -> відповідь, p99', lambda r: evt_max(r, 'cmd_rx', 'cmd_reply'), fmt_ms, '-', None,
     'журнал EVT'),
    ('i2c_xfer (транзакція з повторами), max', lambda r: stat(r, 'i2c_xfer', 'max_us'), fmt_ms,
     '-', None, 'лічильник'),
    ('завантаження CPU, max LOAD', lambda r: max((x['pct'] for x in r['load']), default=None)
     if r else None, lambda v: '-' if v is None else f'{v} %', '-', None, 'лічильник'),
    ('глибина стека (STACK used)', lambda r: max((x.get('used', 0) for x in r['stack']), default=None)
     if r else None, lambda v: '-' if v is None else f'{v} B', f'< {STACK_LIMIT} B (резерв)',
     STACK_LIMIT - 1, 'stack painting'),
    ('купа (HEAP arena)', lambda r: max((x.get('arena', 0) for x in r['heap']), default=None)
     if r else None, lambda v: '-' if v is None else f'{v} B', '-', None, 'mallinfo'),
    ('UART: переповнення приймача (ore)', lambda r: (last_block(r)['uart']['ore']
     if last_block(r) else None), fmt_n, '-', None, 'лічильник'),
    ('UART: загублено байтів (надіслано - rx)', lost_bytes, fmt_n, '-', None,
     'лічильник + скрипт'),
]


def verdict(s3_value, limit):
    if limit is None:
        return '-'
    if s3_value is None:
        return 'немає S3'
    return 'виконана' if s3_value <= limit else 'не виконана в S3'


def vcd_rows(label, path):
    res = vcd_timing.measure(path)
    out = []
    for key, title in (('estop', 'E-STOP: D5↓ -> D6↑'), ('fr03', 'FR-03: D4↑ -> нова ширина ШІМ'),
                       ('d6_gap', 'найдовша пауза між стартами задач (D6↑)'),
                       ('d7', 'lcd_task (ширина D7)')):
        s = res[key]
        out.append((f'{title} ({label}), max', s['max'] if s else None, s['p99'] if s else None))
    return out


def table(runs, vcds=()):
    names = ['S1', 'S2', 'S3']
    lines = ['| Метрика | S1 (сим.) | S2 (сим.) | S3 (сим.) | Вимога PRD | Спосіб | Вердикт |',
             '|---|---|---|---|---|---|---|']
    for title, fn, fmt, req, limit, how in ROWS:
        vals = [fn(runs.get(n)) if runs.get(n) else None for n in names]
        lines.append(f'| {title} | ' + ' | '.join(fmt(v) for v in vals)
                     + f' | {req} | {how} | {verdict(vals[2], limit)} |')
    for label, path in vcds:
        for title, mx, p99 in vcd_rows(label, path):
            cells = ['-', '-', '-']
            cells[names.index(label) if label in names else 2] = fmt_ms(mx)
            lines.append(f'| {title} | ' + ' | '.join(cells) + f' | - | аналізатор (p99 {fmt_ms(p99)}) | - |')
    return '\n'.join(lines)


def conditions(runs):
    out = ['', 'Умови прогонів:', '']
    for n in ('S1', 'S2', 'S3'):
        r = runs.get(n)
        if r:
            k = r['knobs'] or {}
            out.append(f'- {n}: git={r["git"]}, збірка {r["build"]}, ручки ' +
                       (' '.join(f'{a}={b}' for a, b in k.items()) or '?') + ', Wokwi (сим.)')
    return '\n'.join(out)


def blocks_table(run):
    lines = ['| Блок (ручки) | LOAD | gate jit max | gate miss | imu jit max | imu miss | lcd max '
             '| FR-03 (оцінка) |', '|---|---|---|---|---|---|---|---|']
    for b in run['blocks']:
        k = b['knobs'] or {}
        g, i, lc = (b['stat'].get(x, {}) for x in ('gate', 'imu', 'lcd'))
        load = b['load']['pct'] if b['load'] else None
        par = b['stat'].get('parcel_latency', {})
        p = par.get('max_us') if par.get('n') else 2600   # розрахунок, якщо посилок не було
        fr = fr03_estimate_us(b, p)
        mark = '' if par.get('n') else '*'
        label = ' '.join(f'{a}={c}' for a, c in k.items()) + ('' if b['reset'] else ' (без stats reset)')
        lines.append(f'| {label} | '
                     f'{"-" if load is None else str(load) + " %"} | {fmt_ms(g.get("jit_max_us"))} | '
                     f'{fmt_n(g.get("miss"))} | {fmt_ms(i.get("jit_max_us"))} | {fmt_n(i.get("miss"))} | '
                     f'{fmt_ms(lc.get("max_us"))} | {fmt_ms(fr)}{mark} |')
    lines.append('')
    lines.append('\\* без посилок у блоці: PARCEL_IN -> CCR взято з розрахунку, 2,6 мс (docs/tasks.md). '
                 'Час - симуляційний (Wokwi).')
    if any(not b['reset'] for b in run['blocks']):
        lines.append('')
        lines.append('«Без stats reset» - до блоку в цьому лозі не було ні `stats reset`, ні `BOOT`: '
                     'лічильники накопичені з попереднього скидання, разом з тим, що МК робив до прогону.')
    return '\n'.join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--s1')
    ap.add_argument('--s2')
    ap.add_argument('--s3')
    ap.add_argument('--vcd', action='append', default=[], help='S3=run.vcd (мітка=файл)')
    ap.add_argument('--blocks', help='лог з кількома блоками stats (6.2.5)')
    args = ap.parse_args(argv)

    if args.blocks:
        print(blocks_table(parse_log(args.blocks)))
        return
    runs = {n: parse_log(p) for n, p in (('S1', args.s1), ('S2', args.s2), ('S3', args.s3)) if p}
    if not runs:
        ap.error('потрібен хоча б один з --s1 / --s2 / --s3 або --blocks')
    vcds = [tuple(v.split('=', 1)) for v in args.vcd]
    print(table(runs, vcds))
    print(conditions(runs))


if __name__ == '__main__':
    main()
