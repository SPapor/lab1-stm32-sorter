#!/usr/bin/env python3
"""Статична глибина стека за дизасемблером: найглибший ланцюжок викликів від main
і від обробників переривань, разом з HAL і newlib, для яких .su немає.

    arm-none-eabi-objdump -d firmware/build/firmware.elf > fw.dis
    tools/stack_graph.py fw.dis [--root main] [--isr SysTick_Handler --isr EXTI4_15_IRQHandler]

make stack-graph запускає objdump у Docker, а сам скрипт - на хості.

Кадр функції - push {...} (4 байти на регістр) плюс sub sp, #N. Виклики - bl і b
на інший символ. Виклики через вказівник (blx) у дизасемблері не видно, тому
куди вони ведуть, дописано в INDIRECT вручну. Функцію з blx, якої в INDIRECT
немає, скрипт не вгадує, а друкує в stderr: інакше її виклики мовчки дали б 0 B.

На вході в переривання Cortex-M0+ кладе в стек 8 слів (32 B). На ARMv6-M
кадр завжди вирівнюється на 8 байт (CCR.STKALIGN читається як 1), тож якщо SP
був вирівняний лише на 4, додається ще 4 B. Переривання з різними пріоритетами
можуть вкладатися одне в одне, тож найгірше - сума всіх обробників з --isr,
кожен з 32 + 4 B (у прошивці EXTI має пріоритет 0, SysTick - 3).

Цикл у графі (рекурсія) дає верхню оцінку: усі функції циклу рахуються по разу.
"""
import argparse
import re
import sys

EXC_FRAME = 32
EXC_ALIGN = 4

# Варіанти однієї цілі: береться перша, що є в дизасемблері. У PERF=1 драйвери
# кличуть вимірювальну обгортку meas_*, у PERF=0 і LAB2_CAP=1 - одразу hal_*.
BUS_WRITE = ('meas_write', 'hal_write')
BUS_READ = ('meas_read', 'hal_read')

# функція з blx (ім'я без суфікса GCC .part.N / .constprop.N / .isra.N) -> цілі
INDIRECT = {
    'write_port': (BUS_WRITE,),
    'lcd1602_probe': (BUS_READ,),
    'lcd1602_init': (('platform_delay_ms',),),            # d->delay_ms
    'lcd1602_clear': (('platform_delay_ms',),),
    'read_regs': (BUS_WRITE, BUS_READ),
    'mpu6050_init': (BUS_WRITE,),                         # write_reg вбудований
    'meas_write': (('hal_write',),),                      # raw->write
    'meas_read': (('hal_read',),),
    'xfer': (('HAL_I2C_Master_Transmit',), ('HAL_I2C_Master_Receive',),
             ('MX_I2C1_Init',)),                          # fn і ctx->reinit
    'load_line': (('ev_log',), ('cli_reply',)),
    # newlib-nano: форматування віддає байти через pfunc, FILE - через _write/_seek
    '_printf_i': (('__sfputs_r',),),
    '_printf_common': (('__sfputs_r',),),
    '__sflush_r': (('__swrite',), ('__sseek',)),
    '__sfvwrite_r': (('__swrite',),),
    '_fwalk': (('__fp_lock',), ('__fp_unlock',)),         # __fp_lock_all / __fp_unlock_all
    '_fwalk_reent': (('_fflush_r',),),                    # fflush(NULL), _cleanup_r
}


HEAD = re.compile(r'^[0-9a-f]+ <([^>]+)>:')


def base(name):
    return name.split('.')[0]


def parse(lines):
    """{функція: {'frame': байт, 'calls': set(), 'blx': bool}}"""
    funcs, cur, name = {}, None, None
    for line in lines:
        m = HEAD.match(line)
        if m:
            name = m.group(1)
            cur = funcs.setdefault(name, {'frame': 0, 'calls': set(), 'blx': False})
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
            if '+' in target or (op != 'bl' and target == name):
                continue                  # перехід усередині функції або цикл на її початок
            cur['calls'].add(target)
        elif op == 'blx':
            cur['blx'] = True
    return funcs


def resolve(funcs, name):
    if name in funcs:
        return name
    return next((n for n in sorted(funcs) if base(n) == name), None)


def add_indirect(funcs):
    """Дописує ребра з INDIRECT; повертає (функції з blx без запису, записи без цілі)."""
    unknown, missing = [], []
    for name, f in funcs.items():
        if not f['blx']:
            continue
        slots = INDIRECT.get(base(name))
        if slots is None:
            unknown.append(name)
            continue
        for alts in slots:
            target = next((t for t in (resolve(funcs, a) for a in alts) if t), None)
            if target is None:
                missing.append(f'{name} -> {"|".join(alts)}')
            else:
                f['calls'].add(target)
    return unknown, missing


def components(funcs):
    """Сильно зв'язні компоненти (Тарян, без рекурсії): {функція: id}, [члени]."""
    index, low, comp, members = {}, {}, {}, []
    stack, on = [], set()
    counter = 0
    for start in sorted(funcs):
        if start in index:
            continue
        work = [(start, iter(sorted(c for c in funcs[start]['calls'] if c in funcs)))]
        index[start] = low[start] = counter
        counter += 1
        stack.append(start)
        on.add(start)
        while work:
            v, it = work[-1]
            w = next(it, None)
            if w is not None:
                if w not in index:
                    index[w] = low[w] = counter
                    counter += 1
                    stack.append(w)
                    on.add(w)
                    work.append((w, iter(sorted(c for c in funcs[w]['calls'] if c in funcs))))
                elif w in on:
                    low[v] = min(low[v], index[w])
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                group = []
                while True:
                    w = stack.pop()
                    on.discard(w)
                    comp[w] = len(members)
                    group.append(w)
                    if w == v:
                        break
                members.append(sorted(group))
    return comp, members


def deepest(funcs, root):
    """(байт, [(функція, кадр), ...]) найглибшого ланцюжка від root."""
    if root not in funcs:
        return 0, []
    comp, members = components(funcs)
    memo = {}

    def walk(c):                      # компоненти обходяться в топологічному порядку
        if c in memo:
            return memo[c]
        group = members[c]
        frame = sum(funcs[n]['frame'] for n in group)
        best = (0, ())
        for callee in sorted({x for n in group for x in funcs[n]['calls'] if x in funcs}):
            k = comp[callee]
            if k == c:
                continue
            d = walk(k)
            if (d[0], len(d[1])) > (best[0], len(best[1])):
                best = d
        memo[c] = (frame + best[0], tuple((n, funcs[n]['frame']) for n in group) + best[1])
        return memo[c]

    old = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old, 4 * len(members) + 100))
    try:
        total, chain = walk(comp[root])
    finally:
        sys.setrecursionlimit(old)
    return total, list(chain)


def reachable(funcs, roots):
    seen, todo = set(), [r for r in roots if r in funcs]
    while todo:
        n = todo.pop()
        if n in seen:
            continue
        seen.add(n)
        todo.extend(c for c in funcs[n]['calls'] if c in funcs)
    return seen


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('dis')
    ap.add_argument('--root', default='main')
    ap.add_argument('--isr', action='append', default=None)
    args = ap.parse_args(argv)
    isrs = args.isr or ['SysTick_Handler', 'EXTI4_15_IRQHandler']

    with open(args.dis, encoding='utf-8', errors='replace') as f:
        funcs = parse(f)
    for name in [args.root] + isrs:
        if name not in funcs:
            sys.exit(f'{name}: немає в дизасемблері')
    unknown, missing = add_indirect(funcs)

    used = reachable(funcs, [args.root] + isrs)
    for name in sorted(n for n in unknown if n in used):
        print(f'увага: {name} кличе через вказівник (blx), а в INDIRECT її немає', file=sys.stderr)
    for m in missing:
        if m.split(' ->')[0] in used:
            print(f'увага: {m}: цілі немає в дизасемблері', file=sys.stderr)
    _, members = components(funcs)
    for group in members:
        if len(group) > 1 or group[0] in funcs[group[0]]['calls']:
            if used & set(group):
                print(f'увага: рекурсія {", ".join(group)} - оцінка верхня', file=sys.stderr)

    total, chain = deepest(funcs, args.root)
    print(f'{args.root}: {total} B')
    print('  ' + ' -> '.join(f'{n}[{fr}]' for n, fr in chain))
    isr_total = 0
    for name in isrs:
        d, ch = deepest(funcs, name)
        isr_total += d + EXC_FRAME + EXC_ALIGN
        print(f'{name}: {d} B + {EXC_FRAME} B кадр винятку + {EXC_ALIGN} B вирівнювання')
        print('  ' + ' -> '.join(f'{n}[{fr}]' for n, fr in ch))
    print(f'разом з вкладеними перериваннями: {total} + {isr_total} = {total + isr_total} B')


if __name__ == '__main__':
    main()
