import re, sys
path, objs = sys.argv[1], sys.argv[2].split(',')
lines = open(path, errors='replace').read().split('\n')
i0 = next(i for i, l in enumerate(lines) if l.startswith('Linker script and memory map'))
pend = None
for l in lines[i0:]:
    m = re.match(r'^ (\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)', l)
    if m:
        sec, size, obj = m.group(1), int(m.group(3), 16), m.group(4)
    else:
        m2 = re.match(r'^ (\.\S+)\s*$', l)
        if m2: pend = m2.group(1); continue
        m3 = re.match(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)\s*$', l)
        if m3 and pend: sec, size, obj = pend, int(m3.group(2), 16), m3.group(3); pend = None
        else: pend = None; continue
    o = obj.split('/')[-1]
    if o in objs and size and not sec.startswith(('.debug', '.ARM.attr', '.comment')):
        print(f'{o:18s} {size:6d} {sec}')
