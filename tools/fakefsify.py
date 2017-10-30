#!/usr/bin/env python3
import sys
from pathlib import Path
import struct
import urllib
import tarfile
import dbm

def extract_archive(archive, db):
    for member in archive.getmembers():
        path = data/(member.name)
        major = member.devmajor
        minor = member.devminor
        rdev = ((minor & 0xfff00) << 12) | (major << 8) | (minor & 0xff) # copied from fs/dev.h
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
        else:
            raise ValueError('unrecognized tar entry type')

        metadata = struct.pack(
            '=iiii',
            mode,
            member.uid,
            member.gid,
            rdev,
        )
        db[b'meta:' + bytes(path)] = metadata

        if member.isdir():
            path.mkdir(parents=True, exist_ok=True)
        elif member.issym():
            path.symlink_to(member.linkname)
        elif member.isfile():
            archive.extract(member, data)
        else:
            path.touch()

_, archive_path, fs = sys.argv

fs = Path(fs)
fs.mkdir(parents=True, exist_ok=True)
data = fs/'data'
data.mkdir()
db = dbm.ndbm.open(str(fs/'meta'), 'c')

try:
    archive = open(archive_path, 'rb')
except FileNotFoundError:
    archive = urllib.request.urlopen(archive_path)

with archive:
    archive = tarfile.open(fileobj=archive)
    with archive:
        with db:
            extract_archive(archive, db)
