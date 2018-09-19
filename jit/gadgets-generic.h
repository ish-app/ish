#include "cpu-offsets.h"

#define ifin(thing, ...) irp da_op, __VA_ARGS__; .ifc thing,\da_op
#define endifin endif; .endr

# sync with enum reg
#define REG_LIST reg_a,reg_c,reg_d,reg_b,reg_sp,reg_bp,reg_si,reg_di
# sync with enum arg
#define GADGET_LIST REG_LIST,imm,mem,addr,gs
# sync with enum size
#define SIZE_LIST 8,16,32

# darwin/linux compatibility
.macro .pushsection_rodata
#if __APPLE__
    .pushsection __TEXT,__const
#else
    .pushsection .data.rel.ro
#endif
.endm
.macro .pushsection_bullshit
#if __APPLE__
    .pushsection __TEXT,__text_bullshit,regular,pure_instructions
#else
    .pushsection .text.bullshit
#endif
.endm

#if __APPLE__
#define NAME(x) _##x
#else
#define NAME(x) x
#endif
.macro .global.name name
    .global NAME(\name)
    NAME(\name)\():
.endm

.macro .type_compat type:vararg
#if !__APPLE__
    .type \type
#endif
.endm

# an array of gadgets
.macro _gadget_array_start name
    .pushsection_rodata
    .type_compat \name\()_gadgets,@object
    .global.name \name\()_gadgets
.endm

.macro gadgets type, list:vararg
    .irp arg, \list
        .ifndef NAME(gadget_\type\()_\arg)
            .set NAME(gadget_\type\()_\arg), 0
        .endif
        .quad NAME(gadget_\type\()_\arg)
    .endr
.endm

.macro .gadget_list type, list:vararg
    _gadget_array_start \type
        gadgets \type, \list
    .popsection
.endm

.macro .gadget_list_size type, list:vararg
    _gadget_array_start \type
        # sync with enum size
        gadgets \type\()8, \list
        gadgets \type\()16, \list
        gadgets \type\()32, \list
        gadgets \type\()64, \list
        gadgets \type\()80, \list
    .popsection
.endm

.macro .gadget_array type
    .gadget_list_size \type, GADGET_LIST
.endm

# jfc
#define _C __LINE__

# vim: ft=gas
