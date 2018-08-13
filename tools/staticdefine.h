// credit goes to include/linux/kbuild.h
#define _DEFINE(sym, val) \
    asm volatile("\n.ascii \"->" sym " %0 " #val "\"" : : "i" (val))
#define DEFINE(sym, val) \
    _DEFINE(#sym, val)

#define BLANK() asm volatile("\n.ascii \"->\"" : : )

#define OFFSET(sym, str, mem) \
    DEFINE(sym##_##mem, offsetof(struct str, mem))

#define MACRO(macro) \
    _DEFINE(#macro, macro)

#define COMMENT(x) \
    asm volatile("\n.ascii \"->#" x "\"")
