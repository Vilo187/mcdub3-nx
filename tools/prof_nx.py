# tools/prof_nx.py <prof.bin> <prof_threads.txt> <elf> [top]: the Switch sampling
# profiler's output (MC3_PROF=1, win32_compat.c nx_prof_thread) by function,
# per thread: self samples by PC, and the callers (LR) of the top ones.
import bisect, struct, subprocess, sys
from collections import Counter, defaultdict

binf, thrf, elf = sys.argv[1:4]
top = int(sys.argv[4]) if len(sys.argv) > 4 else 30
NM = 'work/toolchain/rootfs/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm'

syms = []
for l in subprocess.run([NM, '-n', elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3 and p[1] in 'tTwW':
        syms.append((int(p[0], 16), p[2]))
syms.sort()
addrs = [a for a, _ in syms]
# The offsets are against the running image's __start__, which the ELF's
# symbol does not always match: take the base that puts every thread entry
# exactly on a function start.
from collections import Counter as _C
ents = [int(l.split()[1], 16) & 0xFFFFFFFF for l in open(thrf) if int(l.split()[1], 16)]
votes = _C(a - e for a, n in syms if n.endswith('_thread') for e in set(ents))
start = votes.most_common(1)[0][0]

def name(off):
    i = bisect.bisect_right(addrs, start + off) - 1
    return syms[i][1] if i >= 0 else '?'

entries = {}
for l in open(thrf):
    i, e, _ = l.split()
    entries[int(i)] = name(int(e, 16) & 0xFFFFFFFF) if int(e, 16) else 'game main'

raw = open(binf, 'rb').read()
n = len(raw) // 12
import os
k0 = int(n * float(os.environ.get('FROM', '0')))   # FROM=0.7: from 70% on
k1 = int(n * float(os.environ.get('TO', '1')))     # TO=0.9: up to 90%
raw = raw[k0 * 12:k1 * 12]
n = k1 - k0
per = defaultdict(Counter)
callers = defaultdict(lambda: defaultdict(Counter))
for k in range(n):
    t, pc, lr = struct.unpack_from('<III', raw, k * 12)
    f = name(pc)
    per[t][f] += 1
    callers[t][f][name(lr)] += 1

for t, c in sorted(per.items(), key=lambda kv: -sum(kv[1].values())):
    tot = sum(c.values())
    print(f'== thread {t} ({entries.get(t, "?")}): {tot} samples')
    for f, k in c.most_common(top):
        cs = ', '.join(f'{g} {v * 100 // k}%' for g, v in callers[t][f].most_common(3))
        print(f'  {k * 100.0 / tot:5.1f}%  {f:40s}  <- {cs}')
