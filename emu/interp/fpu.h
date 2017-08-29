#include <softfloat.h>

#define ty_real(x) ty_real_##x
#define ty_real_16 float16_t
#define ty_real_32 float32_t
#define ty_real_64 float64_t
#define ty_real_80 extFloat80_t

#define mem_read_real(addr, size) mem_read_type(addr, ty_real(size))
#define mem_write_real(addr, val, size) mem_write_type(addr, val, ty_real(size))
#define get_mem_addr_real(size) mem_read_real(addr, size)
#define set_mem_addr_real(to, size) mem_write_real(addr, to, size)

#define extF80_to_f(f, z) CONCAT(extF80_to_f, sz(z))(f)
#define f_to_extF80(f_, z) CONCAT3(f, sz(z), _to_extF80)(f_)
#define extF80_to_i(i, round, exact, z) CONCAT(extF80_to_i, sz(z))(i, round, exact)
#define i_to_extF80(i_, round, exact, z) CONCAT3(i, sz(z), _to_extF80)(i_, round, exact)

#define ST(i) cpu->fp[cpu->top + i]
#define ST_i ST(modrm.rm_opcode)
#define FPUSH(val) \
    cpu->top--; ST(0) = val
#define FPOP \
    cpu->top++

#define FXCH() { \
    extFloat80_t tmp = ST(0); \
    ST(0) = ST_i; \
    ST_i = tmp; \
}

#define st_0 ST(0)
#define st_i ST(modrm.rm_opcode)

#define FADD(src, dst) \
    dst = extF80_add(dst, src)
#define FADDM(val,z) \
    ST(0) = extF80_add(ST(0), f_to_extF80(get(val,z),z))
#define FISUB(val,z) \
    ST(0) = extF80_sub(ST(0), i64_to_extF80((sint(z)) get(val,z)))
#define FMUL(val,z) \
    ST(0) = extF80_mul(ST(0), f_to_extF80(get(val,z),z))

#define FUCOMI() \
    cpu->zf = extF80_eq(ST(0), ST_i); cpu->zf_res = 0; \
    cpu->cf = extF80_lt(ST(0), ST_i); cpu->cf_ops = 0; \
    cpu->pf = 0; cpu->pf_res = 0
// not worrying about nans and shit yet

#define FUCOM() \
    cpu->c0 = extF80_lt(ST(0), ST_i); \
    cpu->c1 = 0; \
    cpu->c2 = 0; /* again, not worrying about nans */ \
    cpu->c3 = extF80_eq(ST(0), ST_i)

#define FILD(val,z) \
    FPUSH(i64_to_extF80((sint(z)) get(val,z)))
#define FLD(val,z) \
    FPUSH(f_to_extF80(get(val,z),z))

#define FLDC(what) FPUSH(fconst_##what)
#define fconst_zero i64_to_extF80(0)

#define FSTM(dst,z) \
    set(dst, extF80_to_f(ST(0),z),z)
#define FIST(dst,z) \
    set(dst, extF80_to_i(ST(0), softfloat_roundingMode, false, z),z)

#define FST() ST_i = ST(0)

#define FSTSW(dst) \
    set(dst, cpu->fsw,16)
#define FSTCW(dst) \
    set(dst, cpu->fcw,16)
#define FLDCW(dst) \
    cpu->fcw = get(dst,16)
