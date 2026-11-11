#!/usr/bin/env python3
"""Часові вимірювання з запису логічного аналізатора Wokwi (wokwi.vcd).

    tools/vcd_timing.py run.vcd [--label S3] [--debounce-ms 20] [--md]

Канали - як у diagram.json і docs/pinout.md:
D2 - ШІМ серво, D4 - LD4 (фронт = PARCEL_IN, спад = CCR записано),
D5 - кнопка E-STOP, D6 - працює будь-яка задача, D7 - працює lcd_task.

Що міряється:
  estop  - D5↓ (перший спад після ≥ 50 мс тиші, тобто початок натискання, брязкіт
           відкидається) -> перший D6↑ на тіку HAL, коли антибрязкіт (--debounce-ms,
           20 мс) уже минув: старт estop_task (вона в циклі перша після uart_rx).
           Антибрязкіт рахує цілі тіки від тіку натискання, тож estop_task стартує
           через 19-20 мс; межа пошуку - тік натискання + 20 мс - 0,5 мс (тіки в записі
           зсунуті від 0 на невідому частку мс). Включно з брязкотом, якого не бачить прошивка;
  fr03   - D4↑ -> початок першого імпульсу D2, ширина якого відрізняється від
           попереднього більш ніж на 10 мкс: повна затримка FR-03 від виявлення;
  d4     - ширина імпульсу D4: програмна частина (як parcel_latency);
  d6, d7 - ширини імпульсів (час задачі; час lcd_task);
  d6_gap - інтервали між фронтами D6↑: «дірки» в періодичному сигналі
           (найдовший інтервал - скільки цикл не стартував жодної нової задачі).
Результати в мкс; таблиця - у тому ж форматі, що tools/stats_table.py.
"""
import argparse
import statistics

from evt_latency import fmt_ms, percentile

UNITS = {'s': 0, 'ms': 3, 'us': 6, 'ns': 9, 'ps': 12, 'fs': 15}
PRESS_QUIET_US = 50_000
WIDTH_CHANGE_US = 10
FR03_WINDOW_US = 100_000


def load_vcd(path):
    """{назва каналу: [(t_us, 0|1), ...]} - лише справжні зміни рівня."""
    ids, chans, t, us_per_tick = {}, {}, 0, 1e-3
    with open(path, encoding='utf-8', errors='replace') as f:
        body = False
        for line in f:
            line = line.strip()
            if not body:
                if line.startswith('$timescale'):
                    ts = line.split()[1]
                    digits = ts.rstrip('afmnpsu')
                    us_per_tick = int(digits) * 10.0 ** (6 - UNITS[ts[len(digits):]])
                elif line.startswith('$var'):
                    parts = line.split()
                    ids[parts[3]] = parts[4]
                    chans[parts[4]] = []
                elif line.startswith('$enddefinitions'):
                    body = True
                continue
            if line.startswith('#'):
                t = int(line[1:]) * us_per_tick
            elif line and line[0] in '01' and line[1:] in ids:
                ch = chans[ids[line[1:]]]
                v = int(line[0])
                if not ch or ch[-1][1] != v:
                    ch.append((t, v))
    return chans


def rising(ch):
    return [t for t, v in ch[1:] if v == 1] if ch else []


def pulses(ch):
    """[(початок, ширина)] високих імпульсів."""
    out, start = [], None
    for t, v in ch[1:]:                  # початковий рівень - не фронт
        if v == 1:
            start = t
        elif start is not None:
            out.append((start, t - start))
            start = None
    return out


def press_starts(d5, quiet_us=PRESS_QUIET_US):
    """Початок кожного натискання: спад після ≥ quiet_us без жодного фронту."""
    out, last = [], None
    for t, v in d5:
        if v == 0 and (last is None or t - last >= quiet_us):
            out.append(t)
        last = t
    return out


def estop_latency(chans, debounce_us=20_000):
    d6_up = rising(chans.get('D6', []))
    out = []
    for t in press_starts(chans.get('D5', [])):
        t_min = (t // 1000) * 1000 + debounce_us - 500
        nxt = next((u for u in d6_up if u >= t_min), None)
        if nxt is not None:
            out.append(nxt - t)
    return out


def fr03_latency(chans):
    d2 = pulses(chans.get('D2', []))
    out = []
    for t4 in rising(chans.get('D4', [])):
        before = [p for p in d2 if p[0] < t4]
        prev_w = before[-1][1] if before else None
        for start, w in d2:
            if start < t4 or start > t4 + FR03_WINDOW_US:
                continue
            if prev_w is not None and abs(w - prev_w) > WIDTH_CHANGE_US:
                out.append(start - t4)
                break
            prev_w = w
    return out


def intervals(times):
    return [b - a for a, b in zip(times, times[1:])]


def summary(values):
    if not values:
        return None
    return {'n': len(values), 'min': min(values), 'avg': statistics.fmean(values),
            'max': max(values), 'p99': percentile(values, 99)}


def measure(path, debounce_ms=20):
    ch = load_vcd(path)
    return {
        'estop': summary(estop_latency(ch, debounce_ms * 1000)),
        'fr03': summary(fr03_latency(ch)),
        'd4': summary([w for _, w in pulses(ch.get('D4', []))]),
        'd6': summary([w for _, w in pulses(ch.get('D6', []))]),
        'd7': summary([w for _, w in pulses(ch.get('D7', []))]),
        'd6_gap': summary(intervals(rising(ch.get('D6', [])))),
    }


TITLES = {
    'estop': 'E-STOP: D5↓ -> D6↑ (старт estop_task)',
    'fr03': 'FR-03: D4↑ -> нова ширина ШІМ на D2',
    'd4': 'ширина D4 (PARCEL_IN -> CCR)',
    'd6': 'ширина D6 (час задачі)',
    'd7': 'ширина D7 (lcd_task)',
    'd6_gap': 'інтервал між стартами D6↑ (найдовший - «дірка»)',
}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('vcd')
    ap.add_argument('--label', default='')
    ap.add_argument('--debounce-ms', type=float, default=20)
    ap.add_argument('--md', action='store_true')
    args = ap.parse_args(argv)

    res = measure(args.vcd, args.debounce_ms)
    if args.md:
        print(f'Аналізатор {args.label} ({args.vcd}), час симуляційний:\n')
        print('| Метрика | n | min | середнє | max | p99 | Спосіб |')
        print('|---|---|---|---|---|---|---|')
    for key, title in TITLES.items():
        s = res[key]
        if args.md:
            cells = ['-'] * 5 if not s else [s['n']] + [fmt_ms(s[k]) for k in ('min', 'avg', 'max', 'p99')]
            print(f'| {title} | ' + ' | '.join(str(c) for c in cells) + ' | аналізатор |')
        elif not s:
            print(f'{title}: немає')
        else:
            print(f'{title}: n={s["n"]} min={fmt_ms(s["min"])} avg={fmt_ms(s["avg"])} '
                  f'max={fmt_ms(s["max"])} p99={fmt_ms(s["p99"])}')


if __name__ == '__main__':
    main()
