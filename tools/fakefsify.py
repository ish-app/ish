#!/usr/bin/env python3
import sys
from pathlib import Path
import struct
import urllib.request
import tarfile
import dbm.gnu

def print_usage():
    print("Usage: fakefsify.py <rootfs archive> <destination dir>")

def extract_archive(archive, db):
    for member in archive.getmembers():
        # hack
        if member.name == './etc/securetty':
            continue

        path = data/(member.name)
        major = member.devmajor
        minor = member.devminor
        rdev = ((minor & 0xfff00) << 12) | (major << 8) | (minor & 0xff)
        mode = member.mode
        if member.isfile():
            mode |= 0o100000	
        elif member.isdir():
            mode |= 0o040000	
        elif member.issym():
            mode |= 0o120000
        elif member.ischr():
            mode |= 0o020000
        elif member.isblk():
            mode |= 0o060000
        elif member.isfifo():
            mode |= 0o010000
        elif member.islnk():
            pass
        else:
            raise ValueError('unrecognized tar entry type')

        if member.isdir():
            path.mkdir(parents=True, exist_ok=True)
        elif member.issym():
            path.write_text(member.linkname)
        elif member.isfile():
            archive.extract(member, data)
        else:
            path.touch()

        inode = bytes(str(path.stat().st_ino), 'ascii')
        if member.islnk():
            # a hard link shares its target's inode
            target_path = data/(member.linkname)
            inode = bytes(str(target_path.stat().st_ino), 'ascii')
        meta_path = path.relative_to(data)
        meta_path = b'/' + bytes(meta_path) if meta_path.parts else b''
        db[b'inode ' + meta_path] = inode
        if member.islnk():
            continue
        db[b'stat ' + inode] = struct.pack(
            '=iiii',
            mode,
            member.uid,
            member.gid,
            rdev,
        )

try:
    _, archive_path, fs = sys.argv
except ValueError:
    print('wrong number of arguments')
    print_usage()
    sys.exit(1)

fs = Path(fs)
fs.mkdir(parents=True, exist_ok=True)
data = fs/'data'

db_path = fs/'meta.db'
if archive_path.find("x86.tar.gz") == -1:
    print("Wrong rootfs version. Please get a i386 rootfs.")
    sys.exit(1)
with open(archive_path, 'rb') as archive:
    with tarfile.open(fileobj=archive) as archive:
        with dbm.gnu.open(str(db_path), 'c') as db:
            extract_archive(archive, db)
            db[b'db inode'] = str(db_path.stat().st_ino) + '\0'
