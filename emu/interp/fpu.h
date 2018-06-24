#include "emu/softfloat.h"

// yay hack
static inline floatx80 floatx80_to_float80(floatx80 f) { return f; }
static inline floatx80 float80_to_floatx80(floatx80 f) { return f; }

#define ty_real(x) ty_real_##x
#define ty_real_16 float16
#define ty_real_32 float32
#define ty_real_64 float64
#define ty_real_80 floatx80

#define mem_read_real(addr, size) mem_read_ts(addr, ty_real(size), size)
#define mem_write_real(addr, val, size) mem_write_ts(addr, val, ty_real(size), size)
#define get_mem_addr_real(size) mem_read_real(addr, size)
#define set_mem_addr_real(to, size) mem_write_real(addr, to, size)

#define floatx80_to_float(f, z) glue(floatx80_to_float, sz(z))(f)
#define float_to_floatx80(f_, z) glue3(float, sz(z), _to_floatx80)(f_)
#define floatx80_to_int(i, z) glue(floatx80_to_int, sz(z))(i)
#define int_to_floatx80(i_, z) glue3(int, sz(z), _to_floatx80)(i_)

#define ST(i) cpu->fp[cpu->top + i]
#define ST_i ST(modrm.rm_opcode)
#define FPUSH(val) \
    ftmp = val; cpu->top--; ST(0) = ftmp
#define FPOP \
    cpu->top++

#define FXCH() \
    floatx80 ftmp = ST(0); ST(0) = ST_i; ST_i = ftmp

#define st_0 ST(0)
#define st_i ST(modrm.rm_opcode)

#define FADD(src, dst) \
    dst = floatx80_add(dst, src)
#define FIADD(val,z) \
    ST(0) = floatx80_add(ST(0), int64_to_floatx80((sint(z)) get(val,z)))
#define FADDM(val,z) \
    ST(0) = floatx80_add(ST(0), float_to_floatx80(get(val,z),z))
#define FSUB(src, dst) \
    dst = floatx80_sub(dst, src)
#define FSUBM(val,z) \
    ST(0) = floatx80_sub(ST(0), float_to_floatx80(get(val,z),z))
#define FISUB(val,z) \
    ST(0) = floatx80_sub(ST(0), int64_to_floatx80((sint(z)) get(val,z)))
#define FMUL(src, dst) \
    dst = floatx80_mul(dst, src)
#define FIMUL(val,z) \
    ST(0) = floatx80_mul(ST(0), int64_to_floatx80((sint(z)) get(val,z)))
#define FMULM(val,z) \
    ST(0) = floatx80_mul(ST(0), float_to_floatx80(get(val,z),z))
#define FDIV(src, dst) \
    dst = floatx80_div(dst, src)
#define FIDIV(val,z) \
    ST(0) = floatx80_div(ST(0), int64_to_floatx80((sint(z)) get(val,z)))
#define FDIVM(val,z) \
    ST(0) = floatx80_div(ST(0), float_to_floatx80(get(val,z),z))

#define FCHS() \
    floatx80_neg(ST(0))
#define FABS() \
    floatx80_abs(ST(0))

// FIXME this is the IEEE ABSOLUTELY CORRECT AND AWESOME REMAINDER which is
// computed by fprem1, not fprem
// only known case of intel naming an instruction by taking another instruction
// that does the same thing but wrong and adding a 1
#define FPREM() \
    ST(0) = floatx80_rem(ST(0), ST(1))

#define FUCOMI() \
    cpu->zf = floatx80_eq(ST(0), ST_i); \
    cpu->cf = floatx80_lt(ST(0), ST_i); \
    cpu->pf = 0; cpu->pf_res = 0
// not worrying about nans and shit yet

#define FUCOM() \
    cpu->c0 = floatx80_lt(ST(0), ST_i); \
    cpu->c1 = 0; \
    cpu->c2 = 0; /* again, not worrying about nans */ \
    cpu->c3 = floatx80_eq(ST(0), ST_i)

#define FILD(val,z) \
    FPUSH(int64_to_floatx80((sint(z)) get(val,z)))
#define FLD() FPUSH(ST_i)
#define FLDM(val,z) \
    FPUSH(float_to_floatx80(get(val,z),z))

#define FLDC(what) FPUSH(fconst_##what)
#define fconst_one int64_to_floatx80(1)
#define fconst_zero int64_to_floatx80(0)

#define FSTM(dst,z) \
    set(dst, floatx80_to_float(ST(0),z),z)
#define FIST(dst,z) \
    set(dst, floatx80_to_int(ST(0), z),z)

#define FST() ST_i = ST(0)

#define FSTSW(dst) \
    set(dst, cpu->fsw,16)
#define FSTCW(dst) \
    set(dst, cpu->fcw,16)
#define FLDCW(dst) \
    cpu->fcw = get(dst,16)
