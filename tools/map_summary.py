#!/usr/bin/env python3
"""Flash і RAM за модулями та найбільші символи з map-файлу GNU ld.

    tools/map_summary.py [firmware.map] [-n N]

За замовчуванням firmware/build/firmware.map (збірка з -Wl,-Map=...,--cref і
-ffunction-sections -fdata-sections, як у firmware/Makefile). Модуль об'єкта
визначається за тим, де лежить його джерело: Core, Drivers, Platform, Devices,
App; бібліотеки - libc, libgcc тощо. .data рахується і у flash (початкові
значення), і в RAM; ._user_heap_stack - резерв купи й стека зі скрипту лінкера.
Сума за модулями може бути трохи більшою за підсумок ld: у map розмір рядкових
секцій (.str1.1) указано до того, як лінкер злив однакові рядки.
"""
import argparse
import os
import re
import sys
from collections import defaultdict

FLASH_OUT = {'.isr_vector', '.text', '.rodata', '.ARM.extab', '.ARM',
             '.preinit_array', '.init_array', '.fini_array'}
BOTH_OUT = {'.data'}
RAM_OUT = {'.bss', '._user_heap_stack'}
MODULES = ('Core', 'Platform', 'Devices', 'App', 'Drivers')

IN_FULL = re.compile(r'^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s*(.*)$')
IN_NAME = re.compile(r'^ (\S+)\s*$')
ADDR_LINE = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s*(.*)$')
OUT_FULL = re.compile(r'^(\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)')
OUT_NAME = re.compile(r'^(\.\S+)\s*$')


def source_modules(fw):
    """basename без розширення -> модуль, за деревом firmware/."""
    mods = {}
    for root, _, files in os.walk(fw):
        rel = os.path.relpath(root, fw).replace('\\', '/').split('/')
        mod = next((m for m in MODULES if m in rel), None)
        for f in files:
            stem, ext = os.path.splitext(f)
            if ext in ('.c', '.s', '.S'):
                mods.setdefault(stem, mod or 'Core')   # startup_*.s лежить у корені
    return mods


def module_of(obj, mods):
    if not obj:
        return 'linker'
    if '.a(' in obj:                              # libc_nano.a(lib_a-printf.o) -> libc_nano
        return os.path.basename(obj.split('(')[0])[:-2]
    base = os.path.basename(obj)
    if base.startswith('crt'):
        return 'crt'
    return mods.get(os.path.splitext(base)[0], '?')


def symbol_of(section, obj):
    base = os.path.basename(obj) if obj else ''
    if re.search(r'\.str1\.\d+$', section):
        return f'рядкові константи {base}'
    for prefix in ('.text.startup.', '.text.', '.rodata.', '.data.', '.bss.'):
        if section.startswith(prefix):
            return section[len(prefix):]
    return f'{section} {base}'.strip()


def parse(path, mods):
    flash = defaultdict(int)
    ram = defaultdict(int)
    out_size = defaultdict(int)     # розміри вихідних секцій — підсумок ld
    symbols = []
    out = None
    pending = None          # ('out', name) або ('in', name): адреса на наступному рядку
    started = False
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if not started:
                started = line.startswith('Linker script and memory map')
                continue
            if pending:
                kind, name = pending
                pending = None
                m = ADDR_LINE.match(line)
                if m:
                    size = int(m.group(2), 16)
                    if kind == 'out':
                        out_size[name] += size
                        continue
                    account(out, name, size, m.group(3), mods, flash, ram, symbols)
                    continue
            m = OUT_FULL.match(line)
            if m:
                out = m.group(1)
                out_size[out] += int(m.group(3), 16)
                continue
            m = OUT_NAME.match(line)
            if m:
                out = m.group(1)
                pending = ('out', out)
                continue
            m = IN_FULL.match(line)
            if m:
                account(out, m.group(1), int(m.group(3), 16), m.group(4), mods, flash, ram, symbols)
                continue
            m = IN_NAME.match(line)
            if m and not m.group(1).startswith('*('):
                pending = ('in', m.group(1))
    if not started:
        sys.exit(f'{path}: немає розділу "Linker script and memory map"')
    ld_flash = sum(v for k, v in out_size.items() if k in FLASH_OUT or k in BOTH_OUT)
    ld_ram = sum(v for k, v in out_size.items() if k in RAM_OUT or k in BOTH_OUT)
    return flash, ram, symbols, (ld_flash, ld_ram)


def account(out, section, size, obj, mods, flash, ram, symbols):
    if size == 0 or out is None:
        return
    in_flash = out in FLASH_OUT or out in BOTH_OUT
    in_ram = out in RAM_OUT or out in BOTH_OUT
    if not (in_flash or in_ram):
        return
    obj = obj.strip()
    if section == '*fill*':
        # у ._user_heap_stack заповнення і є резервом купи (0x200) і стека (0x400)
        mod = 'heap+stack (ld)' if out == '._user_heap_stack' else 'fill'
    else:
        mod = module_of(obj, mods)
    if in_flash:
        flash[mod] += size
    if in_ram:
        ram[mod] += size
    if section not in ('*fill*',):
        region = 'FLASH+RAM' if out in BOTH_OUT else ('FLASH' if in_flash else 'RAM')
        symbols.append((size, region, mod, symbol_of(section, obj)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('map', nargs='?', default='firmware/build/firmware.map')
    ap.add_argument('-n', type=int, default=15, help='скільки символів показати (15)')
    args = ap.parse_args()

    fw = os.path.dirname(os.path.dirname(os.path.abspath(args.map)))
    flash, ram, symbols, (ld_flash, ld_ram) = parse(args.map, source_modules(fw))

    names = sorted(set(flash) | set(ram), key=lambda m: (-flash.get(m, 0), -ram.get(m, 0)))
    tf, tr = sum(flash.values()), sum(ram.values())
    print(f'{"модуль":<16} {"flash, B":>9} {"%":>6} {"RAM, B":>8} {"%":>6}')
    for m in names:
        f, r = flash.get(m, 0), ram.get(m, 0)
        print(f'{m:<16} {f:>9} {100 * f / tf:>5.1f}% {r:>8} {100 * r / tr if tr else 0:>5.1f}%')
    print(f'{"разом":<16} {tf:>9} {"":>6} {tr:>8}')
    print(f'{"підсумок ld":<16} {ld_flash:>9} {"":>6} {ld_ram:>8}'
          '   (порівняти з --print-memory-usage; RAM може бути на кілька байтів меншою'
          ' через вирівнювання між секціями)')

    symbols.sort(key=lambda s: -s[0])
    print(f'\nНайбільші символи (топ {args.n}):')
    print(f'{"байт":>6}  {"регіон":<9} {"модуль":<10} символ')
    for size, region, mod, sym in symbols[:args.n]:
        print(f'{size:>6}  {region:<9} {mod:<10} {sym}')


if __name__ == '__main__':
    main()
