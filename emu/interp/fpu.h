#include <softfloat.h>//0xffffb390

// a few extra things not included in the softfloat library
static inline extFloat80_t extF80_to_f80(extFloat80_t f) { return f; }
static inline extFloat80_t f80_to_extF80(extFloat80_t f) { return f; }

static inline extFloat80_t extF80_neg(extFloat80_t f) {
    f.signExp ^= 1 << 15; // flip the sign bit
    return f;
}
static inline extFloat80_t extF80_abs(extFloat80_t f) {
    f.signExp &= ~(1 << 15); // clear the sign bit
    return f;
}

#define ty_real(x) ty_real_##x
#define ty_real_16 float16_t
#define ty_real_32 float32_t
#define ty_real_64 float64_t
#define ty_real_80 extFloat80_t

#define mem_read_real(addr, size) mem_read_ts(addr, ty_real(size), size)
#define mem_write_real(addr, val, size) mem_write_ts(addr, val, ty_real(size), size)
#define get_mem_addr_real32() mem_read_real(addr, 32)
#define set_mem_addr_real32(to) mem_write_real(addr, to, 32)
#define get_mem_addr_real64() mem_read_real(addr, 64)
#define set_mem_addr_real64(to) mem_write_real(addr, to, 64)
#define get_mem_addr_real80() mem_read_real(addr, 80)
#define set_mem_addr_real80(to) mem_write_real(addr, to, 80)
#define sz_mem_addr_real32 32
#define sz_mem_addr_real64 64
#define sz_mem_addr_real80 80

#define extF80_to_f(f, z) glue(extF80_to_f, z)(f)
#define f_to_extF80(f_, z) glue3(f, z, _to_extF80)(f_)
#define extF80_to_i(i, round, exact, z) glue(extF80_to_i, z)(i, round, exact)
#define i_to_extF80(i_, round, exact, z) glue3(i, z, _to_extF80)(i_, round, exact)

#define ST(i) cpu->fp[cpu->top + i]
#define ST_i ST(modrm.rm_opcode)
#define FPUSH(val) \
    ftmp = val; cpu->top--; ST(0) = ftmp
#define FPOP \
    cpu->top++

#define FXCH() \
    extFloat80_t ftmp = ST(0); ST(0) = ST_i; ST_i = ftmp

#define st_0 ST(0)
#define st_i ST(modrm.rm_opcode)

#define FADD(src, dst) \
    dst = extF80_add(dst, src)
#define FIADD(val) \
    ST(0) = extF80_add(ST(0), i64_to_extF80((sint(sz(val))) get(val)))
#define FADDM(val) \
    ST(0) = extF80_add(ST(0), f_to_extF80(get(val), sz(val)))
#define FSUB(src, dst) \
    dst = extF80_sub(dst, src)
#define FSUBM(val) \
    ST(0) = extF80_sub(ST(0), f_to_extF80(get(val), sz(val)))
#define FISUB(val) \
    ST(0) = extF80_sub(ST(0), i64_to_extF80((sint(sz(val))) get(val)))
#define FMUL(src, dst) \
    dst = extF80_mul(dst, src)
#define FIMUL(val) \
    ST(0) = extF80_mul(ST(0), i64_to_extF80((sint(sz(val))) get(val)))
#define FMULM(val) \
    ST(0) = extF80_mul(ST(0), f_to_extF80(get(val), sz(val)))
#define FDIV(src, dst) \
    dst = extF80_div(dst, src)
#define FIDIV(val) \
    ST(0) = extF80_div(ST(0), i64_to_extF80((sint(sz(val))) get(val)))
#define FDIVM(val) \
    ST(0) = extF80_div(ST(0), f_to_extF80(get(val), sz(val)))

#define FCHS() \
    ST(0) = extF80_neg(ST(0))
#define FABS() \
    ST(0) = extF80_abs(ST(0))

// FIXME this is the IEEE ABSOLUTELY CORRECT AND AWESOME REMAINDER which is
// computed by fprem1, not fprem
// only known case of intel naming an instruction by taking another instruction
// that does the same thing but wrong and adding a 1
#define FPREM() \
    ST(0) = extF80_rem(ST(0), ST(1))

#define FUCOMI() \
    cpu->zf = extF80_eq(ST(0), ST_i); \
    cpu->cf = extF80_lt(ST(0), ST_i); \
    cpu->pf = 0; cpu->pf_res = 0
// not worrying about nans and shit yet

#define FUCOM() \
    cpu->c0 = extF80_lt(ST(0), ST_i); \
    cpu->c1 = 0; \
    cpu->c2 = 0; /* again, not worrying about nans */ \
    cpu->c3 = extF80_eq(ST(0), ST_i)

#define FILD(val) \
    FPUSH(i64_to_extF80((sint(sz(val))) get(val)))
#define FLD() FPUSH(ST_i)
#define FLDM(val) \
    FPUSH(f_to_extF80(get(val), sz(val)))

#define FLDC(what) FPUSH(fconst_##what)
#define fconst_one i64_to_extF80(1)
#define fconst_zero i64_to_extF80(0)

#define FSTM(dst) \
    set(dst, extF80_to_f(ST(0), sz(dst)))
#define FIST(dst) \
    set(dst, extF80_to_i(ST(0), softfloat_roundingMode, false, sz(dst)))

#define FST() ST_i = ST(0)

#define FSTSW(dst) \
    set(dst, cpu->fsw)
#define FSTCW(dst) \
    set(dst, cpu->fcw)
#define FLDCW(dst) \
    cpu->fcw = get(dst)
