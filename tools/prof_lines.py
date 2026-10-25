# tools/prof_lines.py <prof.bin> <threads.txt> <elf> <func> [tid]: hottest source lines of one
# function (by PC), and callers (by LR) -- addr2line on the build's ELF.
import bisect, struct, subprocess, sys
from collections import Counter
binf, thrf, elf, fn = sys.argv[1:5]
tid = int(sys.argv[5]) if len(sys.argv) > 5 else None
TC = 'work/toolchain/rootfs/opt/devkitpro/devkitA64/bin/aarch64-none-elf-'
syms = sorted((int(l.split()[0], 16), l.split()[2]) for l in subprocess.run([TC + 'nm', '-n', elf], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3 and l.split()[1] in 'tTwW')
A = [a for a, _ in syms]
ents = [int(l.split()[1], 16) & 0xFFFFFFFF for l in open(thrf) if int(l.split()[1], 16)]
start = Counter(a - e for a, n in syms if n.endswith('_thread') for e in set(ents)).most_common(1)[0][0]
def name(x):
    i = bisect.bisect_right(A, x) - 1
    return syms[i][1]
raw = open(binf, 'rb').read()
pcs, lrs = Counter(), Counter()
for k in range(len(raw) // 12):
    t, pc, lr = struct.unpack_from('<III', raw, k * 12)
    if tid is not None and t != tid:
        continue
    if name(start + pc) == fn:
        pcs[start + pc] += 1
    if name(start + lr) == fn and name(start + pc) != fn:
        lrs[start + lr] += 1
tot = sum(pcs.values())
print(f'{fn}: {tot} samples')
top = pcs.most_common(25)
out = subprocess.run([TC + 'addr2line', '-e', elf, '-i'] + [hex(a) for a, _ in top], capture_output=True, text=True).stdout.splitlines()
lines = Counter()
res = subprocess.run([TC + 'addr2line', '-e', elf] + [hex(a) for a in pcs], capture_output=True, text=True).stdout.splitlines()
for (a, n), l in zip(pcs.items(), res):
    lines[l.split('/')[-1]] += n
for l, n in lines.most_common(25):
    print(f'  {n * 100.0 / tot:5.1f}%  {l}')
