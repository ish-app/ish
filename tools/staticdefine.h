// credit goes to include/linux/kbuild.h
#define DEFINE(sym, val) \
    asm volatile("\n.ascii \"->" #sym " %0 " #val "\"" : : "i" (val))

#define BLANK() asm volatile("\n.ascii \"->\"" : : )

#define OFFSET(sym, str, mem) \
    DEFINE(sym, offsetof(str, mem))

#define COMMENT(x) \
    asm volatile("\n.ascii \"->#" x "\"")
