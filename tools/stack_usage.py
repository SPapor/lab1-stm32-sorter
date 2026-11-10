#!/usr/bin/env python3
"""Найбільші кадри стека власних функцій з файлів .su (-fstack-usage).

    tools/stack_usage.py [build_dir] [-n N]

build_dir - каталог збірки з *.su (за замовчуванням firmware/build). Рядок .su від GCC:
<файл>:<рядок>:<стовпець>:<функція>\t<байт>\t<static|dynamic|bounded>
static - розмір відомий точно; dynamic - залежить від даних (VLA, alloca).
Це кадр однієї функції, а не глибина стека: повна глибина - сума кадрів
найглибшого ланцюжка викликів плюс кадри переривань (див. STACK у docs/perf.md).
"""
import argparse
import glob
import os
import re
import sys

MODULES = ('Core', 'Platform', 'Devices', 'App', 'Drivers')
LOC = re.compile(r'(.+?):(\d+):(\d+):(.+)$')


def module_of(path):
    parts = path.replace('\\', '/').split('/')
    for m in MODULES:
        if m in parts:
            return m
    return '?'


def read_su(build):
    rows = []
    for su in sorted(glob.glob(os.path.join(build, '*.su'))):
        with open(su, encoding='utf-8') as f:
            for line in f:
                fields = line.rstrip('\n').split('\t')
                if len(fields) != 3:
                    continue
                m = LOC.match(fields[0])
                if not m:
                    continue
                path, ln, _, func = m.groups()
                rows.append({'bytes': int(fields[1]), 'kind': fields[2],
                             'module': module_of(path), 'func': func,
                             'where': f'{path}:{ln}'})
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('build', nargs='?', default='firmware/build')
    ap.add_argument('-n', type=int, default=15, help='скільки функцій показати (15)')
    args = ap.parse_args()

    rows = read_su(args.build)
    if not rows:
        sys.exit(f'{args.build}: немає *.su - зберіть make -f app.mk (там -fstack-usage)')
    rows.sort(key=lambda r: (-r['bytes'], r['func']))

    print(f'Найбільші кадри стека ({len(rows)} функцій у {args.build}/*.su):')
    print(f'{"байт":>6}  {"тип":<8} {"модуль":<9} функція (файл:рядок)')
    for r in rows[:args.n]:
        print(f'{r["bytes"]:>6}  {r["kind"]:<8} {r["module"]:<9} {r["func"]} ({r["where"]})')

    print('\nНайбільший кадр за модулем:')
    best = {}
    for r in rows:
        if r['module'] not in best:
            best[r['module']] = r
    for mod in sorted(best):
        r = best[mod]
        print(f'  {mod:<9} {r["bytes"]:>5} B  {r["func"]}')

    other = [r for r in rows if r['kind'] != 'static']
    print(f'\nНе static: {len(other)}' + ''.join(
        f'\n  {r["kind"]:<8} {r["bytes"]:>5} B  {r["func"]} ({r["where"]})' for r in other))


if __name__ == '__main__':
    main()
