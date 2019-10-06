#include "emu/float80.h"
#include "emu/fpu.h"

#define ty_real(x) ty_real_##x
#define ty_real_32 float
#define ty_real_64 double
#define ty_real_80 float80

#define mem_read_real(addr, size) mem_read_ts(addr, ty_real(size), size)
#define mem_write_real(addr, val, size) mem_write_ts(addr, val, ty_real(size), size)
#define get_mem_addr_real(size) mem_read_real(addr, size)
#define set_mem_addr_real(to, size) mem_write_real(addr, to, size)

// my god this is annoying
#define f80_from_float(x, z) f80_from_float##z(x)
#define f80_from_float32 f80_from_double
#define f80_from_float64 f80_from_double
#define f80_from_float80(x) x
#define f80_to_float(x, z) f80_to_float##z(x)
#define f80_to_float32 f80_to_double
#define f80_to_float64 f80_to_double
#define f80_to_float80(x) x

#define ST(i) cpu->fp[cpu->top + i]
#define ST_i ST(modrm.rm_opcode)
#define FPUSH(val) \
    ftmp = val; cpu->top--; ST(0) = ftmp
#define FPOP \
    cpu->top++

#define FXCH() \
    float80 ftmp = ST(0); ST(0) = ST_i; ST_i = ftmp

#define st_0 ST(0)
#define st_i ST(modrm.rm_opcode)

#define FADD(src, dst) \
    dst = f80_add(dst, src)
#define FIADD(val,z) \
    ST(0) = f80_add(ST(0), f80_from_int((sint(z)) get(val,z)))
#define FADDM(val,z) \
    ST(0) = f80_add(ST(0), f80_from_float(get(val,z),z))
#define FSUB(src, dst) \
    dst = f80_sub(dst, src)
#define FISUB(val,z) \
    ST(0) = f80_sub(ST(0), f80_from_int((sint(z)) get(val,z)))
#define FSUBM(val,z) \
    ST(0) = f80_sub(ST(0), f80_from_float(get(val,z),z))
#define FSUBR(src, dst) \
    dst = f80_sub(src, dst)
#define FISUBR(val,z) \
    ST(0) = f80_sub(f80_from_int((sint(z)) get(val,z)), ST(0))
#define FSUBRM(val,z) \
    ST(0) = f80_sub(f80_from_float(get(val,z),z), ST(0))
#define FMUL(src, dst) \
    dst = f80_mul(dst, src)
#define FIMUL(val,z) \
    ST(0) = f80_mul(ST(0), f80_from_int((sint(z)) get(val,z)))
#define FMULM(val,z) \
    ST(0) = f80_mul(ST(0), f80_from_float(get(val,z),z))
#define FDIV(src, dst) \
    dst = f80_div(dst, src)
#define FIDIV(val,z) \
    ST(0) = f80_div(ST(0), f80_from_int((sint(z)) get(val,z)))
#define FDIVM(val,z) \
    ST(0) = f80_div(ST(0), f80_from_float(get(val,z),z))
#define FDIVR(src, dst) \
    dst = f80_div(src, dst)
#define FIDIVR(val,z) \
    ST(0) = f80_div(f80_from_int((sint(z)) get(val,z)), ST(0))
#define FDIVRM(val,z) \
    ST(0) = f80_div(f80_from_float(get(val,z),z), ST(0))

#define FCHS() \
    ST(0) = f80_neg(ST(0))
#define FABS() \
    ST(0) = f80_abs(ST(0))

#define FPREM() \
    ST(0) = f80_mod(ST(0), ST(1))

#define FRNDINT() UNDEFINED
#define FSCALE() UNDEFINED
#define FYL2X() UNDEFINED
#define F2XM1() UNDEFINED
#define FSQRT() UNDEFINED

#define FUCOMI() \
    cpu->zf = f80_eq(ST(0), ST_i); \
    cpu->cf = f80_lt(ST(0), ST_i); \
    cpu->pf = 0; cpu->pf_res = 0
// not worrying about nans and shit yet

#define FCOMI FUCOMI
// FCOMI is supposed to be even more strict with NaNs. We still won't worry.

#define F_COMPARE(x) \
    cpu->c0 = f80_lt(ST(0), x); \
    cpu->c1 = 0; \
    cpu->c2 = 0; /* again, not worrying about nans */ \
    cpu->c3 = f80_eq(ST(0), x)
#define FCOM() F_COMPARE(ST_i)
#define FUCOM FCOM
#define FCOMM(val,z) F_COMPARE(f80_from_float(get(val,z),z))
#define FTST() F_COMPARE(fpu_consts[fconst_zero])
#define FICOM(val,z) UNDEFINED // nyehhh

#define FILD(val,z) \
    FPUSH(f80_from_int((sint(z)) get(val,z)))
#define FLD() FPUSH(ST_i)
#define FLDM(val,z) \
    FPUSH(f80_from_float(get(val,z),z))

#define FLDC(what) FPUSH(fpu_consts[fconst_##what])

#define FSTM(dst,z) \
    set(dst, f80_to_float(ST(0),z),z)
#define FIST(dst,z) \
    set(dst, f80_to_int(ST(0)),z)

#define FST() ST_i = ST(0)

#define FSTSW(dst) \
    set(dst, cpu->fsw,16)
#define FSTCW(dst) \
    set(dst, cpu->fcw,16)
#define FLDCW(dst) \
    cpu->fcw = get(dst,16)

// there's no native atan2 for 80-bit float yet.
#define FPATAN() \
    ST(1) = f80_from_double(atan2(f80_to_double(ST(1)), f80_to_double(ST(0)))); \
    FPOP

#define FXAM() UNDEFINED
