#!/usr/bin/env python3
"""Вирізає часове вікно з VCD логічного аналізатора Wokwi.

    tools/vcd_window.py <in.vcd> <t0> <t1> <out.vcd>

t0, t1 — секунди від початку запису (можна дробові: 14.6004).
Вихід: заголовок без змін, стан усіх каналів на t0, зміни з [t0, t1].
Час зсунуто так, що t0 → 0; в кінці мітка t1, щоб вікно мало повну довжину.
"""
import sys
from decimal import Decimal

UNITS = {'s': 0, 'ms': 3, 'us': 6, 'ns': 9, 'ps': 12, 'fs': 15}


def main():
    if len(sys.argv) != 5:
        sys.exit('usage: tools/vcd_window.py <in.vcd> <t0> <t1> <out.vcd>')
    src, t0s, t1s, dst = sys.argv[1:]

    header, body = [], []
    with open(src) as f:
        for line in f:
            (body if header and header[-1].startswith('$enddefinitions') else header).append(line)

    # $timescale 1ns $end → тіків на секунду
    ts = next(l for l in header if l.startswith('$timescale')).split()[1]
    digits = ts.rstrip('afmnpsu')
    tick = Decimal(10) ** UNITS[ts[len(digits):]] / Decimal(digits)
    t0 = int(Decimal(t0s) * tick)
    t1 = int(Decimal(t1s) * tick)
    if t1 <= t0:
        sys.exit('t1 має бути більше за t0')

    ids = [l.split()[3] for l in header if l.startswith('$var')]
    state = dict.fromkeys(ids, 'x')
    changes = []
    t = 0
    for line in body:
        line = line.strip()
        if line.startswith('#'):
            t = int(line[1:])
            if t > t1:
                break
        elif line and line[1:] in state:
            if t <= t0:
                state[line[1:]] = line[0]
            else:
                changes.append((t - t0, line))

    with open(dst, 'w') as f:
        f.writelines(header)
        f.write('#0\n')
        f.writelines(f'{state[i]}{i}\n' for i in ids)
        last = 0
        for dt, line in changes:
            if dt != last:
                f.write(f'#{dt}\n')
                last = dt
            f.write(line + '\n')
        if t1 - t0 != last:
            f.write(f'#{t1 - t0}\n')


if __name__ == '__main__':
    main()
