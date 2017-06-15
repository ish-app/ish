# hello world
executable('hello', ['hello.c'], c_args: ['-m32'], link_args: ['-m32', '-nostdlib'])
executable('hello-libc-static', ['hello-clib.c'], c_args: ['-m32'], link_args: ['-m32', '-static'])
executable('hello-libc', ['hello-clib.c'], c_args: ['-m32'], link_args: ['-m32'])

# simple benchmark
executable('looper', ['looper.c', 'nothing.c'], c_args: ['-m32'], link_args: ['-m32', '-static'])
executable('fibbonaci', ['fibbonaci.c'], c_args: ['-m32'], link_args: ['-m32', '-static'])

# filesystem motivation
executable('cat', ['cat.c'], c_args: ['-m32'], link_args: ['-m32', '-static'])
executable('stat', ['stat.c'], c_args: ['-m32', '-D_FILE_OFFSET_BITS=64'], link_args: ['-m32', '-static'])
