#!/usr/bin/env python3
"""Copy the DLLs a Windows .exe needs, and theirs, next to it.

  copy_dlls.py <exe> <dll dir> [<dll dir> ...] <dest>

Reads the PE import tables (no Windows tools needed); a DLL found in none
of the folders is a system DLL (kernel32, user32, ...) and stays.
"""
import os, shutil, struct, sys


def imports(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    ddir = opt + (112 if magic == 0x20B else 96)
    rva = struct.unpack_from('<I', d, ddir + 8)[0]          # import directory
    secs = [struct.unpack_from('<IIII', d, opt + optsz + i * 40 + 8) for i in range(nsec)]

    def off(r):
        for vsize, va, rawsize, raw in secs:
            if va <= r < va + max(vsize, rawsize):
                return r - va + raw
        return None

    out, o = [], off(rva) if rva else None
    while o is not None:
        name_rva = struct.unpack_from('<I', d, o + 12)[0]
        if not name_rva:
            break
        n = off(name_rva)
        out.append(d[n:d.index(b'\0', n)].decode())
        o += 20
    return out


def main():
    exe, dirs, dest = sys.argv[1], sys.argv[2:-1], sys.argv[-1]
    lookup = {}
    for dr in dirs:
        for f in os.listdir(dr) if os.path.isdir(dr) else []:
            if f.lower().endswith('.dll'):
                lookup.setdefault(f.lower(), os.path.join(dr, f))
    todo, done = [exe], set()
    while todo:
        for dll in imports(todo.pop()):
            k = dll.lower()
            if k in done or k not in lookup:
                continue
            done.add(k)
            shutil.copy2(lookup[k], os.path.join(dest, os.path.basename(lookup[k])))
            todo.append(lookup[k])
    print(f'  {len(done)} DLLs copied to {dest}')


if __name__ == '__main__':
    main()
