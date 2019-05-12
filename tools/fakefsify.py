#!/usr/bin/env python3
import os
import sys
from pathlib import Path
import struct
import urllib.request
import tarfile
import sqlite3

SCHEMA = """
create table meta (id integer unique default 0, db_inode integer);
insert into meta (db_inode) values (0);
create table stats (inode integer primary key, stat blob);
create table paths (path blob primary key, inode integer references stats(inode));
create index inode_to_path on paths (inode, path);
pragma user_version=3;
"""
# no index is needed on stats, because the rows are ordered by the primary key


def extract_member(archive, db, member):
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

    if path != data and not path.parent.exists():
        parent_member = tarfile.TarInfo(os.path.dirname(member.name))
        parent_member.type = tarfile.DIRTYPE
        parent_member.mode = 0o755
        extract_member(archive, db, parent_member)

    if member.isdir():
        path.mkdir()
    elif member.issym():
        path.write_text(member.linkname)
    elif member.isfile():
        archive.extract(member, data)
    else:
        path.touch()

    def meta_path(path):
        path = path.relative_to(data)
        return b'/' + bytes(path) if path.parts else b''

    cursor = db.cursor()
    if member.islnk():
        # a hard link shares its target's inode
        target_path = data/(member.linkname)
        cursor.execute('select inode from paths where path = ?', (meta_path(target_path),))
        inode, = cursor.fetchone()
    else:
        statblob = memoryview(struct.pack(
            '=iiii',
            mode,
            member.uid,
            member.gid,
            rdev,
        ))
        cursor.execute('insert into stats (stat) values (?)', (statblob,))
        inode = cursor.lastrowid
    cursor.execute('insert into paths values (?, ?)', (meta_path(path), inode))

def extract_archive(archive, db):
    for member in archive.getmembers():
        extract_member(archive, db, member)

try:
    _, archive_path, fs = sys.argv
except ValueError:
    print('wrong number of arguments')
    print("Usage: fakefsify.py <rootfs archive> <destination dir>")
    sys.exit(1)

fs = Path(fs)
fs.mkdir(parents=True, exist_ok=True)
data = fs/'data'

db_path = fs/'meta.db'
with open(archive_path, 'rb') as archive:
    with tarfile.open(fileobj=archive) as archive:
        db = sqlite3.connect(str(db_path))
        db.executescript(SCHEMA)
        extract_archive(archive, db)
        db.execute('update meta set db_inode = ?', (db_path.stat().st_ino,))
        db.commit()
