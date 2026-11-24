import re, sys, collections
FL = ('.text', '.rodata', '.isr_vector', '.ARM.extab', '.ARM.exidx', '.init_array', '.fini_array', '.preinit_array')
RA = ('.bss', '.noinit')
def parse(path):
    flash = collections.Counter(); ram = collections.Counter()
    out = None; pend = None
    lines = open(path, errors='replace').read().split('\n')
    try:
        i0 = next(i for i, l in enumerate(lines) if l.startswith('Linker script and memory map'))
    except StopIteration:
        i0 = 0
    for l in lines[i0:]:
        m = re.match(r'^(\.\S+)', l)
        if m:
            out = m.group(1)
        m = re.match(r'^ (\.\S+|COMMON)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)', l)
        if not m:
            m2 = re.match(r'^ (\.\S+|COMMON)\s*$', l)
            if m2:
                pend = m2.group(1); continue
            m3 = re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+\.o\)?|\S+\.a\(\S+\))\s*$', l)
            if m3 and pend:
                size = int(m3.group(2), 16); obj = m3.group(3)
                pend = None
            else:
                pend = None; continue
        else:
            size = int(m.group(3), 16); obj = m.group(4)
        if int(size) == 0 or out is None: continue
        name = re.sub(r'.*/', '', obj)
        if out.startswith(FL): flash[name] += size
        elif out == '.data': flash[name] += size; ram[name] += size
        elif out.startswith(RA): ram[name] += size
    return flash, ram
f, r = parse(sys.argv[1])
keys = sorted(set(f) | set(r), key=lambda k: -(f[k] + r[k]))
for k in keys: print(f'{k:40s} {f[k]:7d} {r[k]:7d}')
print(f'{"TOTAL":40s} {sum(f.values()):7d} {sum(r.values()):7d}')
