# Changelog

## Build 34
### New
- Enough stubs and instructions to get numpy to import successfully.
### Fixed
- Massive VM leak, if you allocated 100M with mmap only the first 16k would ever get freed. This fixes MemoryError when installing stuff with pip.

## Build 33
### New
- Database changed from GDBM to SQLite, it's now much more reliable. You will need to reinstall the app, otherwise you'll get an error on launch.
### Fixed
- Segfault while doing large download with pip, due to returning NULL from successful mremap. ffmpeg also had this problem.
- File provider now shows files like it's supposed to.
