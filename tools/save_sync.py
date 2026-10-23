#!/usr/bin/env python3
"""Copy MC3 profiles between this computer and a Switch running an FTP server.

  save_sync.py down --host <switch ip> [options]   Switch -> computer
  save_sync.py up   --host <switch ip> [options]   computer -> Switch

  --game-id ID   title ID folder (default 54540079, MC3)
  --profile P    one save folder (12 hex, e.g. 0075195616BF); default all
  --udata DIR    the local UDATA folder (default $MC3_GAME_DIR/UDATA)
  --remote DIR   the Switch's UDATA folder, from the card's root
                 (default /switch/mcdub3-nx/game/UDATA)
  --port N       FTP port (default 5000: ftpd, sphaira)
  --user / --password
                 only for a server that asks for a login. Without them the
                 tool logs in anonymously, or not at all if the server
                 needs no login; a user without a password sends an empty one
  --list         only show what is on both sides

The receiving side is copied to work/saves-backup/<time>-<direction>/ first,
so nothing is lost by syncing the wrong way. Profiles move as they are: the
port signs them the same way on every platform (tools/save_import.py).
"""
import argparse, ftplib, io, os, posixpath, shutil, sys, time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ftp_is_dir(ftp, path):
    here = ftp.pwd()
    try:
        ftp.cwd(path)
        ftp.cwd(here)
        return True
    except ftplib.error_perm:
        return False


def ftp_names(ftp, path):
    try:
        names = ftp.nlst(path)
    except ftplib.error_perm:            # an empty folder answers 550 on some servers
        return []
    # Servers answer with bare names or with full paths.
    return sorted({posixpath.basename(n.rstrip('/')) for n in names} - {'.', '..', ''})


def ftp_walk(ftp, path):
    """{relative path: None} for the files under path (on the Switch)."""
    out = {}
    for n in ftp_names(ftp, path):
        full = posixpath.join(path, n)
        if ftp_is_dir(ftp, full):
            for k in ftp_walk(ftp, full):
                out[posixpath.join(n, k)] = None
        else:
            out[n] = None
    return out


def ftp_makedirs(ftp, path):
    parts, cur = [p for p in path.split('/') if p], ''
    for p in parts:
        cur += '/' + p
        if not ftp_is_dir(ftp, cur):
            try:
                ftp.mkd(cur)
            except ftplib.error_perm:
                pass


def login(ftp, user, password):
    """A login only as far as the server wants one: the given user (an empty
    password if none), else anonymous, else none at all."""
    try:
        if user:
            ftp.login(user, password)
        else:
            ftp.login()                          # anonymous
        return
    except ftplib.error_perm as e:
        if user:
            raise SystemExit(f'the FTP server refused the login for {user!r}: {e}')
        refused = e
    try:                                         # a server that wants no USER at all
        ftp.voidcmd('NOOP')
        ftp.pwd()
    except ftplib.Error:
        raise SystemExit(f'the FTP server wants a login ({refused}): '
                         f'give FTP_USER= and FTP_PASS= (make), or --user/--password')


def local_walk(root):
    out = {}
    for top, _, files in os.walk(root):
        for f in files:
            rel = os.path.relpath(os.path.join(top, f), root).replace(os.sep, '/')
            out[rel] = None
    return out


def describe(side, files):
    profiles = sorted({k.split('/')[0] for k in files if '/' in k})
    print(f'  {side}: {len(files)} files, {len(profiles)} profiles {" ".join(profiles)}'.rstrip())


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('direction', choices=['up', 'down'])
    ap.add_argument('--host', required=True)
    ap.add_argument('--port', type=int, default=5000)
    ap.add_argument('--user', default='')
    ap.add_argument('--password', default='')
    ap.add_argument('--game-id', default='54540079')
    ap.add_argument('--profile')
    ap.add_argument('--udata')
    ap.add_argument('--remote', default='/switch/mcdub3-nx/game/UDATA')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()

    udata = os.path.expanduser(a.udata) if a.udata else \
        os.path.join(os.environ.get('MC3_GAME_DIR') or os.path.join(REPO, 'game'), 'UDATA')
    local = os.path.join(udata, a.game_id)
    remote = posixpath.join(a.remote, a.game_id)
    if a.profile:
        local, remote = os.path.join(local, a.profile), posixpath.join(remote, a.profile)

    print(f'Switch {a.host}:{a.port}  {remote}\nhere          {local}')
    try:
        ftp = ftplib.FTP()
        ftp.connect(a.host, a.port, timeout=15)
    except (OSError, ftplib.Error) as e:
        raise SystemExit(f'cannot reach the FTP server on the Switch at {a.host}:{a.port} ({e}).\n'
                         f'Is it running (ftpd or sphaira on the Switch) and on the same network?')
    login(ftp, a.user, a.password)

    there = ftp_walk(ftp, remote) if ftp_is_dir(ftp, remote) else {}
    here = local_walk(local) if os.path.isdir(local) else {}
    describe('Switch', there)
    describe('here  ', here)
    if a.list:
        ftp.quit()
        return 0

    stamp = time.strftime('%Y%m%d_%H%M%S')
    backup = os.path.join(os.environ.get('MC3_WORK') or os.path.join(REPO, 'work'),
                          'saves-backup', f'{stamp}-before-sync-{a.direction}')
    if a.direction == 'down':
        if not there:
            raise SystemExit(f'no saves on the Switch in {remote}')
        if here:
            shutil.copytree(local, backup)
            print(f'  local saves copied to {backup}')
        for rel in sorted(there):
            dst = os.path.join(local, *rel.split('/'))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, 'wb') as f:
                ftp.retrbinary('RETR ' + posixpath.join(remote, rel), f.write)
            print(f'  <- {rel}')
    else:
        if not here:
            raise SystemExit(f'no saves here in {local}')
        if there:
            for rel in sorted(there):              # the Switch's saves, kept here first
                dst = os.path.join(backup, *rel.split('/'))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, 'wb') as f:
                    ftp.retrbinary('RETR ' + posixpath.join(remote, rel), f.write)
            print(f'  the Switch\'s saves copied to {backup}')
        for rel in sorted(here):
            dst = posixpath.join(remote, rel)
            ftp_makedirs(ftp, posixpath.dirname(dst))
            with open(os.path.join(local, *rel.split('/')), 'rb') as f:
                ftp.storbinary('STOR ' + dst, f)
            print(f'  -> {rel}')
    ftp.quit()
    print(f'done: {len(there) if a.direction == "down" else len(here)} files '
          f'{"from" if a.direction == "down" else "to"} the Switch')
    return 0


if __name__ == '__main__':
    sys.exit(main())
