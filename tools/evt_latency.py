#!/usr/bin/env python3
"""Затримки між подіями журналу EVT (методичка ЛР3, 5.2).

    tools/evt_latency.py run.log [start:end ...]

Рядок прошивки: "<мс> EVT t_us=<мкс> name=<подія>" (docs/perf.md). Час береться
з МК (t_us), а не час прибуття рядка на хості. Для кожної події start береться
найближча наступна end. Без пар у командному рядку рахуються стандартні:
estop_irq:estop_react, parcel_in:servo_set, cmd_rx:cmd_reply, i2c_fail:i2c_ok.
"""
import re
import statistics
import sys

EVT = re.compile(r'\bEVT t_us=(\d+) name=(\S+)')
PAIRS = ['estop_irq:estop_react', 'parcel_in:servo_set', 'cmd_rx:cmd_reply', 'i2c_fail:i2c_ok']


def read_events(path):
    events = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = EVT.search(line)
            if m:
                events.append((int(m.group(1)), m.group(2)))
    return events


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


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    events = read_events(sys.argv[1])
    print(f'{len(events)} подій EVT у {sys.argv[1]}')
    for pair in sys.argv[2:] or PAIRS:
        start, _, end = pair.partition(':')
        d = latencies(events, start, end)
        if not d:
            print(f'{start} -> {end}: немає пар')
            continue
        line = (f'{start} -> {end}: n={len(d)} min={min(d)} '
                f'median={statistics.median(d):.0f} max={max(d)} us')
        if len(d) >= 100:
            line += f' p99={statistics.quantiles(d, n=100)[98]:.0f} us'
        print(line)


if __name__ == '__main__':
    main()
