#!/usr/bin/env python3
"""Import Midnight Club 3 profiles into the port: make import-save FILE=<file>.

  save_import.py <file> [--udata DIR] [--backup DIR] [--replace] [--list]

<file> is any of:
  .gsx / memory unit dump   a gzip'd or raw 8 MB Xbox memory unit (FATX),
                            with or without the 128-byte "GSXM" header
                            (the save sites' format)
  a folder                  an extracted save: a folder holding <name>.mc3
                            (+ SaveMeta.xbx), or a UDATA/54540079 folder
  <name>.mc3                a bare profile; SaveMeta.xbx is written for it
  .max                      an Action Replay MAX PS2 save (read and checked
                            only: see below)

Each profile is re-signed for the port and copied to
<udata>/54540079/<save folder>/ (default: $MC3_GAME_DIR/UDATA). The port's
existing saves are copied to --backup first.

Why re-sign: MC3 ends every profile with a 20-byte HMAC-SHA1 of the rest of
the file. A real Xbox keys it with its own secret; the port's kernel leaves
the Xbox keys at zero, so the port signs with an all-zero key and calls a
real Xbox's profile "damaged" (2026-10-22). Re-signed, they load. No
Microsoft key is involved; the reverse (a port save on a real Xbox) would
need one and is not done.

PS2 saves: MC3 profiles start with a format stamp. The Xbox game writes
0x02F59447; the PS2 Remix (SLUS-21355) writes 0x00512C4D and lays out
about half its fields differently (more cities, cars, events), so a Remix
profile is refused. A PS2 save with the Xbox stamp would be imported.
"""
import argparse, gzip, hashlib, hmac, os, shutil, struct, sys, time

TITLE_ID = '54540079'
XBOX_STAMP = 0x02F59447
KNOWN_STAMPS = {0x00512C4D: 'the PS2 Remix (SLUS-21355)'}
SIG_LEN = 20


def port_signature(body):
    return hmac.new(bytes(16), body, hashlib.sha1).digest()


# --- Xbox memory unit (FATX) ------------------------------------------------

def fatx_files(img):
    """{path: bytes} of every file in a FATX memory unit image."""
    _, spc, root = struct.unpack_from('<III', img, 4)
    # Memory units count in 4 KB sectors (16 KB clusters); try 512 too.
    for sector in (4096, 512):
        csize = spc * sector
        if not csize:
            continue
        nclus = len(img) // csize
        fat16 = nclus < 0xFFF0
        data0 = 0x1000 + ((nclus + 1) * (2 if fat16 else 4) + 4095) // 4096 * 4096
        e = img[data0 + (root - 1) * csize:]
        if len(e) > 2 and 0 < e[0] <= 42 and all(32 <= c < 127 for c in e[2:2 + e[0]]):
            break
    else:
        raise SystemExit('not a FATX memory unit this tool can read')

    def nxt(c):
        if fat16:
            v = struct.unpack_from('<H', img, 0x1000 + 2 * c)[0]
            return None if v >= 0xFFF0 else v
        v = struct.unpack_from('<I', img, 0x1000 + 4 * c)[0]
        return None if v >= 0xFFFFFFF0 else v

    def read(c, size=None):
        parts, seen = [], set()
        while c and c not in seen:
            seen.add(c)
            parts.append(img[data0 + (c - 1) * csize: data0 + c * csize])
            c = nxt(c)
        b = b''.join(parts)
        return b if size is None else b[:size]

    files = {}

    def walk(c, path):
        d = read(c)
        for i in range(0, len(d) - 63, 64):
            n = d[i]
            if n in (0x00, 0xFF):
                break
            if n == 0xE5 or n > 42:
                continue
            attr = d[i + 1]
            name = d[i + 2:i + 2 + n].decode('latin1')
            first, size = struct.unpack_from('<II', d, i + 44)
            p = f'{path}/{name}' if path else name
            if attr & 0x10:
                walk(first, p)
            else:
                files[p] = read(first, size)

    walk(root, '')
    return files


# --- Action Replay MAX (PS2): LZARI, after Okumura ---------------------------

def lzari_decode(src, outlen):
    N, F, THRESHOLD = 4096, 60, 2
    N_CHAR = 256 - THRESHOLD + F
    M = 15
    Q1, Q2, Q3, Q4 = 1 << M, 2 << M, 3 << M, 4 << M
    bitpos = 0

    def getbit():
        nonlocal bitpos
        i = bitpos >> 3
        b = (src[i] >> (7 - (bitpos & 7))) & 1 if i < len(src) else 0
        bitpos += 1
        return b

    char_to_sym = [0] * N_CHAR
    sym_to_char = [0] * (N_CHAR + 1)
    sym_freq = [0] * (N_CHAR + 1)
    sym_cum = [0] * (N_CHAR + 1)
    position_cum = [0] * (N + 1)
    for sym in range(N_CHAR, 0, -1):
        ch = sym - 1
        char_to_sym[ch] = sym
        sym_to_char[sym] = ch
        sym_freq[sym] = 1
        sym_cum[sym - 1] = sym_cum[sym] + sym_freq[sym]
    for i in range(N, 0, -1):
        position_cum[i - 1] = position_cum[i] + 10000 // (i + 200)

    def update(sym):
        if sym_cum[0] >= 0x7FFF:
            c = 0
            for i in range(N_CHAR, 0, -1):
                sym_cum[i] = c
                sym_freq[i] = (sym_freq[i] + 1) >> 1
                c += sym_freq[i]
            sym_cum[0] = c
        i = sym
        while sym_freq[i] == sym_freq[i - 1]:
            i -= 1
        if i < sym:
            ch_i, ch_sym = sym_to_char[i], sym_to_char[sym]
            sym_to_char[i], sym_to_char[sym] = ch_sym, ch_i
            char_to_sym[ch_i], char_to_sym[ch_sym] = sym, i
        sym_freq[i] += 1
        while i > 1:
            i -= 1
            sym_cum[i] += 1
        sym_cum[0] += 1

    low, high, value = 0, Q4, 0
    for _ in range(M + 2):
        value = 2 * value + getbit()

    def decode(table, n):
        nonlocal low, high, value
        rng = high - low
        x = ((value - low + 1) * table[0] - 1) // rng
        i, j = 1, n
        while i < j:
            k = (i + j) // 2
            if table[k] > x:
                i = k + 1
            else:
                j = k
        high = low + (rng * table[i - 1]) // table[0]
        low = low + (rng * table[i]) // table[0]
        while True:
            if low >= Q2:
                value -= Q2; low -= Q2; high -= Q2
            elif low >= Q1 and high <= Q3:
                value -= Q1; low -= Q1; high -= Q1
            elif high > Q2:
                break
            low += low; high += high
            value = 2 * value + getbit()
        return i

    text = bytearray(b' ' * N)
    r = N - F
    out = bytearray()
    while len(out) < outlen:
        sym = decode(sym_cum, N_CHAR)
        c = sym_to_char[sym]
        update(sym)
        if c < 256:
            out.append(c)
            text[r] = c
            r = (r + 1) & (N - 1)
        else:
            pos = decode(position_cum, N) - 1
            i = (r - pos - 1) & (N - 1)
            for k in range(c - 255 + THRESHOLD):
                ch = text[(i + k) & (N - 1)]
                out.append(ch)
                text[r] = ch
                r = (r + 1) & (N - 1)
    return bytes(out[:outlen])


def max_files(d):
    """{path: bytes} of an Action Replay MAX save, under its PS2 folder name."""
    dirname = d[16:48].split(b'\0')[0].decode('latin1')
    _, nfiles, usize = struct.unpack_from('<III', d, 0x50)
    raw = lzari_decode(d[0x5C:], usize)
    files, off = {}, 0
    for _ in range(nfiles):
        size = struct.unpack_from('<I', raw, off)[0]
        name = raw[off + 4:off + 36].split(b'\0')[0].decode('latin1')
        files[f'{dirname}/{name}'] = raw[off + 36:off + 36 + size]
        off = ((off + 36 + size + 23) & ~15) - 8     # headers sit 8 bytes before a 16-byte boundary
    return files


# --- Input ------------------------------------------------------------------

def load(path):
    """{path: bytes} of what the input holds."""
    if os.path.isdir(path):
        files, parent = {}, os.path.dirname(os.path.abspath(path))
        for top, _, names in os.walk(path):
            for n in names:
                p = os.path.join(top, n)
                files[os.path.relpath(p, parent)] = open(p, 'rb').read()   # keeps the folder's name
        return files
    d = open(path, 'rb').read()
    if d[:2] == b'\x1f\x8b':
        d = gzip.decompress(d)
    if d[:4] == b'GSXM':
        d = d[128:]
    if d[:4] == b'FATX':
        return fatx_files(d)
    if d[:12] == b'Ps2PowerSave':
        return max_files(d)
    if path.lower().endswith('.mc3'):
        parent = os.path.basename(os.path.dirname(os.path.abspath(path)))
        return {f'{parent}/{os.path.basename(path)}': d}   # its save folder's name, if it has one
    raise SystemExit(f'{path}: not a save this tool knows (memory unit dump, .gsx, .max, .mc3 or a folder)')


def profiles(files):
    """[(save folder name or None, profile name, .mc3 bytes, SaveMeta bytes or None)]"""
    out = []
    for p, data in sorted(files.items()):
        parts = p.replace('\\', '/').split('/')
        if not parts[-1].lower().endswith('.mc3') and not (len(parts) > 1 and parts[-1] == parts[-2]):
            continue                                  # PS2: the profile is named after its folder
        folder = parts[-2] if len(parts) > 1 and parts[-2] != TITLE_ID else None
        meta = files.get('/'.join(parts[:-1] + ['SaveMeta.xbx']))
        name = parts[-1][:-4] if parts[-1].lower().endswith('.mc3') else parts[-1]
        out.append((folder, name, data, meta))
    return out


def save_meta(name):
    return '\ufeffName={}\r\n'.format(name).encode('utf-16-le')


def folder_name(folder, name):
    """The Xbox names a save folder with 12 hex digits; keep the source's."""
    if folder and len(folder) == 12 and all(c in '0123456789ABCDEFabcdef' for c in folder):
        return folder.upper()
    return hashlib.sha1(name.encode()).hexdigest()[:12].upper()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('file')
    ap.add_argument('--udata', help='the port\'s UDATA folder (default: $MC3_GAME_DIR/UDATA)')
    ap.add_argument('--backup', help='where the existing saves are copied first (default: work/saves-backup/<time>)')
    ap.add_argument('--replace', action='store_true', help='overwrite a save folder that exists already')
    ap.add_argument('--list', action='store_true', help='only show what is in the file')
    a = ap.parse_args()
    a.file = os.path.expanduser(a.file)                # make passes ~ through as is
    if a.udata:
        a.udata = os.path.expanduser(a.udata)
    if not os.path.exists(a.file):
        raise SystemExit(f'{a.file}: no such file or folder')

    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    udata = a.udata or os.path.join(os.environ.get('MC3_GAME_DIR') or os.path.join(repo, 'game'), 'UDATA')
    title_dir = os.path.join(udata, TITLE_ID)

    files = load(a.file)
    found = profiles(files)
    if not found:
        raise SystemExit(f'{a.file}: no MC3 profile in it (files: {", ".join(sorted(files)) or "none"})')

    todo = []
    for folder, name, data, meta in found:
        stamp = struct.unpack_from('<I', data)[0] if len(data) >= 4 else None
        if stamp != XBOX_STAMP:
            what = KNOWN_STAMPS.get(stamp, 'an unknown version')
            print(f'  {name}: a profile from {what} (format 0x{stamp or 0:08X}), not the Xbox game\'s '
                  f'(0x{XBOX_STAMP:08X}): it cannot be converted, skipped')
            continue
        signed = port_signature(data[:-SIG_LEN]) == data[-SIG_LEN:]
        dest = folder_name(folder, name)
        print(f'  {name}: Xbox profile, {len(data)} bytes, '
              f'{"signed for the port" if signed else "signed by an Xbox: re-signed"} -> {TITLE_ID}/{dest}/')
        todo.append((dest, name, data[:-SIG_LEN] + port_signature(data[:-SIG_LEN]), meta or save_meta(name)))
    if a.list or not todo:
        return 0 if todo or a.list else 1

    if not os.path.isdir(udata):
        raise SystemExit(f'no UDATA folder at {udata}: start the game once first (or pass --udata / UDATA=)')
    write = []
    for dest, name, data, meta in todo:
        d = os.path.join(title_dir, dest)
        if os.path.exists(d) and not a.replace:
            print(f'  {name}: {d} exists already: skipped (--replace / REPLACE=1 to overwrite)')
        else:
            write.append((d, name, data, meta))
    if write and os.path.isdir(title_dir):
        backup = a.backup or os.path.join(os.environ.get('MC3_WORK') or os.path.join(repo, 'work'),
                                          'saves-backup', time.strftime('%Y%m%d_%H%M%S'))
        shutil.copytree(title_dir, os.path.join(backup, TITLE_ID))
        print(f'  existing saves copied to {backup}')

    done = 0
    for d, name, data, meta in write:
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, name + '.mc3'), 'wb').write(data)
        open(os.path.join(d, 'SaveMeta.xbx'), 'wb').write(meta)
        print(f'  {name}: installed in {d}')
        done += 1
    return 0 if done else 1


if __name__ == '__main__':
    sys.exit(main())
