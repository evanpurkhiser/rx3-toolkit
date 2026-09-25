#!/usr/bin/env python3
import bisect, re, struct, sys
from pathlib import Path
binary = Path(sys.argv[1])
symbols = Path(sys.argv[2])
targets = [int(x, 0) for x in sys.argv[3:]]
rows=[]
for line in symbols.read_text(errors='replace').splitlines():
    m=re.match(r'\s*\d+\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+\S+\s+FUNC\s+\S+\s+(\S+)', line)
    if m: rows.append((int(m.group(2),16), m.group(3)))
rows.sort(); addrs=[x[0] for x in rows]
data=binary.read_bytes()[:166373984]
slide=0x100000000-0x4000
for target in targets:
    print(f'target {target:#x}')
    found=0
    p = data.find(b'\xe8')
    while p >= 0 and p < len(data)-4:
        disp=struct.unpack_from('<i',data,p+1)[0]
        va=p+slide
        if va+5+disp != target:
            p = data.find(b'\xe8', p+1)
            continue
        i=bisect.bisect_right(addrs,va)-1
        print(f'  call {va:#x} file {p:#x} caller {rows[i][1] if i>=0 else "?"} +{va-rows[i][0]:#x}')
        found+=1
        p = data.find(b'\xe8', p+1)
    print(f'  {found} callers')
