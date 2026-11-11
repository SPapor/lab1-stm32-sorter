#!/usr/bin/env python3
"""Затримки між подіями журналу EVT (методичка ЛР3, 5.2).

    tools/evt_latency.py run.log [run2.log ...] [--pair start:end ...] [--md]

Рядок прошивки: "<мс> EVT t_us=<мкс> name=<подія>" (docs/perf.md). Час береться
з МК (t_us), а не час прибуття рядка на хості. Для кожної події start береться
найближча наступна end. Без --pair рахуються стандартні пари (PAIRS). Кілька
логів - кожен окремо (t_us з різних прогонів не змішуються), потім разом.
p99 - за найближчим рангом: при n < 100 це фактично максимум.
"""
import argparse
import math
import re
import statistics
import sys

EVT = re.compile(r'\bEVT t_us=(\d+) name=(\S+)')
PAIRS = [
    ('estop_irq', 'estop_react'),
    ('parcel_in', 'servo_set'),
    ('cmd_rx', 'cmd_reply'),
    ('i2c_fail', 'i2c_ok'),
    ('boot', 'ready_gate'),
    ('boot', 'ready_class'),
    ('boot', 'ready_imu'),
    ('boot', 'ready_lcd'),
]


def parse_events(lines):
    events = []
    for line in lines:
        m = EVT.search(line)
        if m:
            events.append((int(m.group(1)), m.group(2)))
    return events


def read_events(path):
    with open(path, encoding='utf-8', errors='replace') as f:
        return parse_events(f)


def latencies(events, start, end):
    """Від кожної start до найближчої наступної end, мкс."""
    out, t0 = [], None
    for t, name in events:
        if name == start:
            t0 = t
        elif name == end and t0 is not None:
            out.append(t - t0)
            t0 = None
    return out


def percentile(values, q):
    """Найближчий ранг: найменше значення, не менше за яке q % вибірки."""
    s = sorted(values)
    return s[max(0, math.ceil(q / 100 * len(s)) - 1)]


def summary(values):
    if not values:
        return None
    return {'n': len(values), 'min': min(values), 'median': statistics.median(values),
            'max': max(values), 'p99': percentile(values, 99)}


def fmt_ms(us):
    """Мкс -> '2,65 мс' (як у таблицях звіту); None -> '-'."""
    if us is None:
        return '-'
    return f'{us / 1000:.2f}'.replace('.', ',') + ' мс'


def pair_stats(event_lists, pairs=PAIRS):
    """{(start, end): summary} по всіх логах разом; event_lists - події кожного логу окремо."""
    out = {}
    for start, end in pairs:
        vals = []
        for events in event_lists:
            vals += latencies(events, start, end)
        out[(start, end)] = summary(vals)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--pair', action='append', default=[], help='start:end, можна кілька')
    ap.add_argument('--md', action='store_true', help='таблиця markdown')
    args = ap.parse_args(argv)

    pairs = [tuple(p.split(':', 1)) for p in args.pair] or PAIRS
    if any(len(p) != 2 for p in pairs):
        sys.exit('--pair має бути start:end')
    event_lists = [read_events(p) for p in args.logs]
    total = sum(len(e) for e in event_lists)
    stats = pair_stats(event_lists, pairs)
    if args.md:
        print(f'Затримки за журналом EVT ({total} подій у {len(args.logs)} лог., мкс, сим.):\n')
        print('| Пара | n | min | медіана | max | p99 |')
        print('|---|---|---|---|---|---|')
    else:
        print(f'{total} подій EVT у {", ".join(args.logs)}')
    for (start, end), s in stats.items():
        if args.md:
            cells = ['-'] * 5 if not s else [s['n'], s['min'], f'{s["median"]:.0f}', s['max'], s['p99']]
            print(f'| {start} -> {end} | ' + ' | '.join(str(c) for c in cells) + ' |')
        elif not s:
            print(f'{start} -> {end}: немає пар')
        else:
            print(f'{start} -> {end}: n={s["n"]} min={s["min"]} median={s["median"]:.0f} '
                  f'max={s["max"]} p99={s["p99"]} us')


if __name__ == '__main__':
    main()
