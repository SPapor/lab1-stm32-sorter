#!/usr/bin/env python3
"""Статична глибина стека за дизасемблером: найглибший ланцюжок викликів від main
і від обробників переривань, разом з HAL і newlib, для яких .su немає.

    arm-none-eabi-objdump -d firmware/build/firmware.elf > fw.dis
    tools/stack_graph.py fw.dis [--root main] [--isr SysTick_Handler --isr EXTI4_15_IRQHandler]

(make stack-graph робить те саме в Docker.)

Кадр функції - push {...} (4 байти на регістр) плюс sub sp, #N. Виклики - bl і b
на інший символ. Виклики через вказівник (blx) у дизасемблері не видно, тому
відомі з них дописані в INDIRECT вручну: шина I²C у драйверах, printf з newlib
через __sfputs_r і __swrite. Рекурсія обривається на першому повторі.

На вході в переривання Cortex-M0+ кладе в стек 8 слів (32 B). Переривання з
різними пріоритетами можуть вкладатися одне в одне, тож найгірше - сума всіх
обробників з --isr, кожен з цими 32 B (у прошивці EXTI має пріоритет 0, SysTick - 3).
"""
import argparse
import functools
import re
import sys

EXC_FRAME = 32

INDIRECT = {
    # драйвери кличуть bus->write / bus->read, у PERF=1 це meas_write / meas_read
    'write_port': ('meas_write',),
    'lcd1602_probe': ('meas_write', 'meas_read'),
    'lcd1602_clear': ('meas_write',),
    'lcd1602_init': ('meas_write',),
    'read_regs': ('meas_write', 'meas_read'),
    'mpu6050_init': ('meas_write', 'meas_read'),
    'meas_write': ('hal_write',),
    'meas_read': ('hal_read',),
    'xfer.part.0': ('MX_I2C1_Init',),     # ctx->reinit після помилки
    'load_line': ('ev_log',),
    # newlib-nano: форматування віддає байти через pfunc, flush - через fp->_write
    '_printf_i': ('__sfputs_r',),
    '_printf_common': ('__sfputs_r',),
    '__sflush_r': ('__swrite',),
    '__sfvwrite_r': ('__swrite',),
    '_fwalk': ('__sflush_r',),
    '_fwalk_reent': ('__sflush_r',),
}

HEAD = re.compile(r'^[0-9a-f]+ <([^>]+)>:')


def parse(lines):
    """{функція: {'frame': байт, 'calls': set()}}"""
    funcs, cur = {}, None
    for line in lines:
        m = HEAD.match(line)
        if m:
            cur = funcs.setdefault(m.group(1), {'frame': 0, 'calls': set()})
            continue
        if cur is None:
            continue
        cols = line.rstrip('\n').split('\t')
        if len(cols) < 3:
            continue
        op = cols[2].strip()
        args = cols[3].strip() if len(cols) > 3 else ''
        if op == 'push':
            cur['frame'] += 4 * len(args.strip('{}').split(','))
        elif op.startswith('sub') and re.match(r'sp, (sp, )?#', args):
            cur['frame'] += int(args.split('#')[1].split()[0], 0)
        elif op in ('bl', 'b', 'b.n', 'b.w') and '<' in args:
            target = args.split('<')[1].split('>')[0]
            if '+' not in target:
                cur['calls'].add(target)
    for name, extra in INDIRECT.items():
        if name in funcs:
            funcs[name]['calls'].update(extra)
    return funcs


def deepest(funcs, root):
    """(байт, [(функція, кадр), ...]) найглибшого ланцюжка від root."""
    @functools.lru_cache(maxsize=None)
    def walk(name, seen):
        if name not in funcs or name in seen:
            return 0, ()
        best = (0, ())
        for callee in sorted(funcs[name]['calls']):
            d = walk(callee, seen | frozenset([name]))
            if d[0] > best[0]:
                best = d
        frame = funcs[name]['frame']
        return frame + best[0], ((name, frame),) + best[1]

    total, chain = walk(root, frozenset())
    return total, list(chain)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('dis')
    ap.add_argument('--root', default='main')
    ap.add_argument('--isr', action='append', default=None)
    args = ap.parse_args(argv)
    isrs = args.isr or ['SysTick_Handler', 'EXTI4_15_IRQHandler']

    with open(args.dis, encoding='utf-8', errors='replace') as f:
        funcs = parse(f)
    if args.root not in funcs:
        sys.exit(f'{args.root}: немає в дизасемблері')

    total, chain = deepest(funcs, args.root)
    print(f'{args.root}: {total} B')
    print('  ' + ' -> '.join(f'{n}[{fr}]' for n, fr in chain))
    isr_total = 0
    for name in isrs:
        d, ch = deepest(funcs, name)
        isr_total += d + EXC_FRAME
        print(f'{name}: {d} B + {EXC_FRAME} B кадр винятку')
        print('  ' + ' -> '.join(f'{n}[{fr}]' for n, fr in ch))
    print(f'разом з вкладеними перериваннями: {total} + {isr_total} = {total + isr_total} B')


if __name__ == '__main__':
    main()
