#!/usr/bin/env python3
"""Windows libraries for the cross build (macOS/Linux -> Windows), from MSYS2.

  win_deps.py <x86_64|aarch64> <sysroot dir> [package ...]

Downloads MSYS2's CLANG64 (x86_64) or CLANGARM64 (aarch64) packages and
everything they depend on, and unpacks them into <sysroot dir> (bin/,
include/, lib/, lib/pkgconfig/). On Windows itself none of this is needed:
MSYS2's pacman installs the same packages (tools/setup_windows.sh).
Needs Python 3.14+ (zstd in the standard library).
"""
import io, os, re, sys, tarfile, urllib.request

MIRROR = 'https://mirror.msys2.org/mingw'
REPOS = {'x86_64': ('clang64', 'mingw-w64-clang-x86_64-'),
         'aarch64': ('clangarm64', 'mingw-w64-clang-aarch64-')}
DEFAULT = ['SDL2', 'ffmpeg', 'pkgconf']
# Toolchain packages: llvm-mingw brings its own compiler, C runtime headers and
# winpthreads; their MSYS2 builds would only shadow them. libc++ and libunwind
# do come from MSYS2: its DLLs were built against those, and llvm-mingw's
# libunwind.dll lacked an entry point they import (_Unwind_Resume,
# 2026-10-23).
SKIP = {'crt-git', 'headers-git', 'winpthreads', 'winpthreads-git', 'libwinpthread',
        'libwinpthread-git', 'gcc-libs', 'compiler-rt', 'clang',
        'llvm', 'llvm-libs', 'lld', 'clang-libs', 'libmangle-git', 'tools-git'}


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def read_db(repo):
    """{name: {'file': ..., 'depends': [...], 'provides': [...]}}"""
    data = fetch(f'{MIRROR}/{repo}/{repo}.db')
    db = {}
    with tarfile.open(fileobj=io.BytesIO(data)) as t:     # gzip or zstd, detected
        for m in t.getmembers():
            if not m.name.endswith('/desc'):
                continue
            fields, key = {}, None
            for line in t.extractfile(m).read().decode().splitlines():
                if line.startswith('%') and line.endswith('%'):
                    key = line[1:-1]
                    fields[key] = []
                elif line and key:
                    fields[key].append(line)
            name = fields['NAME'][0]
            db[name] = {'file': fields['FILENAME'][0], 'depends': fields.get('DEPENDS', []),
                        'provides': fields.get('PROVIDES', [])}
    return db


def main():
    arch, root, wanted = sys.argv[1], sys.argv[2], sys.argv[3:] or DEFAULT
    repo, prefix = REPOS[arch]
    db = read_db(repo)
    provides = {}
    for n, p in db.items():
        for pr in p['provides']:
            provides.setdefault(re.split('[<>=]', pr)[0], n)

    todo, chosen = [prefix + w for w in wanted], []
    while todo:
        n = re.split('[<>=]', todo.pop())[0]
        n = n if n in db else provides.get(n, n)
        if n in chosen or n[len(prefix):] in SKIP:
            continue
        if n not in db:
            print(f'  (not in {repo}: {n})')
            continue
        chosen.append(n)
        todo.extend(db[n]['depends'])

    os.makedirs(root, exist_ok=True)
    stamp = os.path.join(root, '.packages')
    have = set(open(stamp).read().split()) if os.path.exists(stamp) else set()
    for n in sorted(chosen):
        f = db[n]['file']
        if f in have:
            continue
        print(f'  {f}')
        with tarfile.open(fileobj=io.BytesIO(fetch(f'{MIRROR}/{repo}/{f}'))) as t:
            top = repo + '/'
            for m in t.getmembers():
                if not m.name.startswith(top) or not (m.isfile() or m.issym()):
                    continue
                m.name = m.name[len(top):]
                t.extract(m, root, filter='tar')
        have.add(f)
    open(stamp, 'w').write('\n'.join(sorted(have)) + '\n')

    # .pc files name the prefix MSYS2 built with (/clang64); point them here.
    pcdir = os.path.join(root, 'lib', 'pkgconfig')
    for pc in os.listdir(pcdir) if os.path.isdir(pcdir) else []:
        p = os.path.join(pcdir, pc)
        s = open(p, encoding='utf-8', errors='replace').read()
        s2 = re.sub(r'^prefix=.*$', f'prefix={os.path.abspath(root)}', s, flags=re.M)
        s2 = re.sub(rf'/{repo}(?=/|\s|$)', os.path.abspath(root), s2, flags=re.M)   # paths written out in full
        if s2 != s:
            open(p, 'w', encoding='utf-8').write(s2)
    print(f'{len(chosen)} packages in {root}')


if __name__ == '__main__':
    main()
