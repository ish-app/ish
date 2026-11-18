// MMX, SSE3, SSSE3, SSE4.1, SSE4.2: one checksum line per instruction, random inputs.
// Output must be identical natively and in ish.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <immintrin.h>

static uint64_t st[2] = {0x243f6a8885a308d3ull, 0x13198a2e03707344ull};
static uint64_t rnd(void) { uint64_t a = st[0], b = st[1]; st[0] = b; a ^= a << 23; st[1] = a ^ b ^ (a >> 17) ^ (b >> 26); return st[1] + b; }
static uint64_t h;
static void mix(uint64_t v) { h = (h ^ v) * 0x100000001b3ull; h ^= h >> 29; }
static void mixv(__m128i v) { uint64_t q[2]; memcpy(q, &v, 16); mix(q[0]); mix(q[1]); }
static __m128i rv(void) {
    uint64_t q[2] = {rnd(), rnd()};
    // sometimes small or special lanes, so saturation and sign cases are hit
    switch (rnd() % 6) {
        case 0: q[0] &= 0x00ff00ff00ff00ffull; break;
        case 1: q[1] = 0x8000800080008000ull; break;
        case 2: q[0] = 0x7fff7fff80008000ull; q[1] |= 0x8080808080808080ull; break;
        case 3: q[0] = ~0ull; break;
    }
    __m128i v; memcpy(&v, q, 16); return v;
}
static __m128d rd(void) {
    double d[2];
    for (int i = 0; i < 2; i++) { uint64_t r = rnd(); switch (r % 8) { case 0: d[i] = 0.5; break; case 1: d[i] = -2.5; break; case 2: memcpy(&d[i], &r, 8); break; default: d[i] = (double) (int64_t) r / 1e15; } }
    return _mm_loadu_pd(d);
}
static __m128 rf(void) {
    float f[4];
    for (int i = 0; i < 4; i++) { uint64_t r = rnd(); switch (r % 8) { case 0: f[i] = 1.5f; break; case 1: f[i] = -3.5f; break; case 2: { uint32_t u = (uint32_t) r; memcpy(&f[i], &u, 4); break; } default: f[i] = (float) (int32_t) r / 65536.0f; } }
    return _mm_loadu_ps(f);
}
#define N 3000
#define T2(name, expr) do { h = 0; for (int i = 0; i < N; i++) { __m128i a = rv(), b = rv(); (void) a; (void) b; mixv(expr); } printf("%-18s %016llx\n", name, (unsigned long long) h); } while (0)
#define TD(name, expr) do { h = 0; for (int i = 0; i < N; i++) { __m128d a = rd(), b = rd(); (void) a; (void) b; mixv(_mm_castpd_si128(expr)); } printf("%-18s %016llx\n", name, (unsigned long long) h); } while (0)
#define TF(name, expr) do { h = 0; for (int i = 0; i < N; i++) { __m128 a = rf(), b = rf(); (void) a; (void) b; mixv(_mm_castps_si128(expr)); } printf("%-18s %016llx\n", name, (unsigned long long) h); } while (0)

// MMX and other instructions by name, through inline assembly: mm0 = a, mm1 = b
#define MMX2(insn) do { h = 0; for (int i = 0; i < N; i++) { uint64_t a = rnd(), b = rnd(), r; \
    if (i % 5 == 0) b &= 0x3f; if (i % 7 == 0) a = 0x7fff80007fff8000ull; \
    __asm__ volatile("movq %1, %%mm0\n movq %2, %%mm1\n " insn " %%mm1, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a), "m"(b) : "mm0", "mm1"); mix(r); \
    __asm__ volatile("movq %1, %%mm0\n " insn " %2, %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a), "m"(b) : "mm0"); mix(r); } \
    printf("mmx %-14s %016llx\n", insn, (unsigned long long) h); } while (0)
#define MMXI(insn, imm) do { h = 0; for (int i = 0; i < N; i++) { uint64_t a = rnd(), r; \
    __asm__ volatile("movq %1, %%mm0\n " insn " $" #imm ", %%mm0\n movq %%mm0, %0\n emms" : "=m"(r) : "m"(a) : "mm0"); mix(r); } \
    printf("mmx %-10s %-3s %016llx\n", insn, #imm, (unsigned long long) h); } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    static const char *const mmx2[] = {0};
    (void) mmx2;
    MMX2("paddb"); MMX2("paddw"); MMX2("paddd"); MMX2("paddq"); MMX2("psubb"); MMX2("psubw"); MMX2("psubd"); MMX2("psubq");
    MMX2("paddsb"); MMX2("paddsw"); MMX2("paddusb"); MMX2("paddusw"); MMX2("psubsb"); MMX2("psubsw"); MMX2("psubusb"); MMX2("psubusw");
    MMX2("pmullw"); MMX2("pmulhw"); MMX2("pmulhuw"); MMX2("pmaddwd"); MMX2("pmuludq");
    MMX2("pand"); MMX2("pandn"); MMX2("por"); MMX2("pxor");
    MMX2("pcmpeqb"); MMX2("pcmpeqw"); MMX2("pcmpeqd"); MMX2("pcmpgtb"); MMX2("pcmpgtw"); MMX2("pcmpgtd");
    MMX2("packsswb"); MMX2("packssdw"); MMX2("packuswb");
    MMX2("punpcklbw"); MMX2("punpcklwd"); MMX2("punpckldq"); MMX2("punpckhbw"); MMX2("punpckhwd"); MMX2("punpckhdq");
    MMX2("psllw"); MMX2("pslld"); MMX2("psllq"); MMX2("psrlw"); MMX2("psrld"); MMX2("psrlq"); MMX2("psraw"); MMX2("psrad");
    MMX2("pavgb"); MMX2("pavgw"); MMX2("pminub"); MMX2("pmaxub"); MMX2("pminsw"); MMX2("pmaxsw"); MMX2("psadbw");
    MMX2("pshufb"); MMX2("phaddw"); MMX2("phaddd"); MMX2("phaddsw"); MMX2("phsubw"); MMX2("phsubd"); MMX2("phsubsw");
    MMX2("pmaddubsw"); MMX2("pmulhrsw"); MMX2("psignb"); MMX2("psignw"); MMX2("psignd"); MMX2("pabsb"); MMX2("pabsw"); MMX2("pabsd");
    MMXI("psllw", 3); MMXI("pslld", 13); MMXI("psllq", 33); MMXI("psrlw", 5); MMXI("psrld", 17); MMXI("psrlq", 60); MMXI("psraw", 4); MMXI("psrad", 31); MMXI("psrlw", 16); MMXI("psraw", 200);
    h = 0;
    for (int i = 0; i < N; i++) {
        uint64_t a = rnd(), b = rnd(), r; uint32_t g = (uint32_t) rnd(), go;
        __asm__ volatile("movq %1, %%mm3\n pshufw $0x9c, %%mm3, %%mm2\n movq %%mm2, %0\n emms" : "=m"(r) : "m"(a) : "mm2", "mm3"); mix(r);
        __asm__ volatile("movq %1, %%mm5\n palignr $3, %2, %%mm5\n movq %%mm5, %0\n emms" : "=m"(r) : "m"(a), "m"(b) : "mm5"); mix(r);
        __asm__ volatile("movd %1, %%mm4\n movq %%mm4, %0\n emms" : "=m"(r) : "r"(g) : "mm4"); mix(r);
        __asm__ volatile("movq %1, %%mm4\n movd %%mm4, %0\n emms" : "=r"(go) : "m"(a) : "mm4"); mix(go);
        __asm__ volatile("movq %1, %%mm6\n pmovmskb %%mm6, %0\n emms" : "=r"(go) : "m"(a) : "mm6"); mix(go);
        __asm__ volatile("movq %1, %%mm6\n pextrw $2, %%mm6, %0\n emms" : "=r"(go) : "m"(a) : "mm6"); mix(go);
        __asm__ volatile("movq %1, %%mm6\n pinsrw $3, %2, %%mm6\n movq %%mm6, %0\n emms" : "=m"(r) : "m"(a), "r"(g) : "mm6"); mix(r);
        __asm__ volatile("movq %1, %%mm7\n movq2dq %%mm7, %%xmm9\n movq %%xmm9, %0\n emms" : "=m"(r) : "m"(a) : "mm7", "xmm9"); mix(r);
        __asm__ volatile("movq %1, %%xmm9\n movdq2q %%xmm9, %%mm7\n movq %%mm7, %0\n emms" : "=m"(r) : "m"(a) : "mm7", "xmm9"); mix(r);
        __asm__ volatile("movq %1, %%mm7\n movntq %%mm7, %0\n emms" : "=m"(r) : "m"(a) : "mm7"); mix(r);
        __asm__ volatile("movq %1, %%mm0\n movq %%mm0, %%rax\n emms\n mov %%rax, %0" : "=m"(r) : "m"(a) : "mm0", "rax"); mix(r);
    }
    printf("mmx moves/misc     %016llx\n", (unsigned long long) h);

    // SSE3
    TD("addsubpd", _mm_addsub_pd(a, b)); TF("addsubps", _mm_addsub_ps(a, b));
    TD("haddpd", _mm_hadd_pd(a, b)); TF("haddps", _mm_hadd_ps(a, b)); TD("hsubpd", _mm_hsub_pd(a, b)); TF("hsubps", _mm_hsub_ps(a, b));
    TD("movddup", _mm_movedup_pd(a)); TF("movsldup", _mm_moveldup_ps(a)); TF("movshdup", _mm_movehdup_ps(b));
    T2("lddqu", _mm_lddqu_si128(&b));
    // SSSE3
    T2("pshufb", _mm_shuffle_epi8(a, b)); T2("phaddw", _mm_hadd_epi16(a, b)); T2("phaddd", _mm_hadd_epi32(a, b)); T2("phaddsw", _mm_hadds_epi16(a, b));
    T2("phsubw", _mm_hsub_epi16(a, b)); T2("phsubd", _mm_hsub_epi32(a, b)); T2("phsubsw", _mm_hsubs_epi16(a, b));
    T2("pmaddubsw", _mm_maddubs_epi16(a, b)); T2("pmulhrsw", _mm_mulhrs_epi16(a, b));
    T2("psignb", _mm_sign_epi8(a, b)); T2("psignw", _mm_sign_epi16(a, b)); T2("psignd", _mm_sign_epi32(a, b));
    T2("pabsb", _mm_abs_epi8(a)); T2("pabsw", _mm_abs_epi16(a)); T2("pabsd", _mm_abs_epi32(a));
    T2("palignr 5", _mm_alignr_epi8(a, b, 5)); T2("palignr 0", _mm_alignr_epi8(a, b, 0)); T2("palignr 19", _mm_alignr_epi8(a, b, 19)); T2("palignr 33", _mm_alignr_epi8(a, b, 33));
    // SSE4.1
    T2("pblendvb", _mm_blendv_epi8(a, b, rv())); TF("blendvps", _mm_blendv_ps(a, b, rf())); TD("blendvpd", _mm_blendv_pd(a, b, rd()));
    T2("pblendw", _mm_blend_epi16(a, b, 0xa6)); TF("blendps", _mm_blend_ps(a, b, 0x6)); TD("blendpd", _mm_blend_pd(a, b, 0x1));
    h = 0; for (int i = 0; i < N; i++) { __m128i a = rv(), b = rv(); if (i % 3 == 0) b = _mm_and_si128(a, b); if (i % 5 == 0) b = _mm_andnot_si128(a, b); mix(_mm_testz_si128(a, b)); mix(_mm_testc_si128(a, b)); mix(_mm_testnzc_si128(a, b)); }
    printf("%-18s %016llx\n", "ptest", (unsigned long long) h);
    T2("pmovsxbw", _mm_cvtepi8_epi16(a)); T2("pmovsxbd", _mm_cvtepi8_epi32(a)); T2("pmovsxbq", _mm_cvtepi8_epi64(a));
    T2("pmovsxwd", _mm_cvtepi16_epi32(a)); T2("pmovsxwq", _mm_cvtepi16_epi64(a)); T2("pmovsxdq", _mm_cvtepi32_epi64(a));
    T2("pmovzxbw", _mm_cvtepu8_epi16(a)); T2("pmovzxbd", _mm_cvtepu8_epi32(a)); T2("pmovzxbq", _mm_cvtepu8_epi64(a));
    T2("pmovzxwd", _mm_cvtepu16_epi32(a)); T2("pmovzxwq", _mm_cvtepu16_epi64(a)); T2("pmovzxdq", _mm_cvtepu32_epi64(a));
    T2("pmuldq", _mm_mul_epi32(a, b)); T2("pmulld", _mm_mullo_epi32(a, b)); T2("pcmpeqq", _mm_cmpeq_epi64(a, _mm_unpacklo_epi64(b, a))); T2("pcmpgtq", _mm_cmpgt_epi64(a, b));
    T2("packusdw", _mm_packus_epi32(a, b)); T2("movntdqa", _mm_stream_load_si128(&a));
    T2("pminsb", _mm_min_epi8(a, b)); T2("pminsd", _mm_min_epi32(a, b)); T2("pminuw", _mm_min_epu16(a, b)); T2("pminud", _mm_min_epu32(a, b));
    T2("pmaxsb", _mm_max_epi8(a, b)); T2("pmaxsd", _mm_max_epi32(a, b)); T2("pmaxuw", _mm_max_epu16(a, b)); T2("pmaxud", _mm_max_epu32(a, b));
    T2("phminposuw", _mm_minpos_epu16(a)); T2("mpsadbw 5", _mm_mpsadbw_epu8(a, b, 5)); T2("mpsadbw 2", _mm_mpsadbw_epu8(a, b, 2));
    TF("roundps 0", _mm_round_ps(a, 0)); TF("roundps 1", _mm_round_ps(a, 1)); TF("roundps 2", _mm_round_ps(a, 2)); TF("roundps 3", _mm_round_ps(a, 3)); TF("roundps cur", _mm_round_ps(a, 4));
    TD("roundpd 0", _mm_round_pd(a, 0)); TD("roundpd 1", _mm_round_pd(a, 1)); TD("roundpd 2", _mm_round_pd(a, 2)); TD("roundpd 3", _mm_round_pd(a, 3)); TD("roundpd half", _mm_round_pd(_mm_add_pd(_mm_round_pd(a, 3), _mm_set1_pd(0.5)), 8));
    TF("roundss", _mm_round_ss(a, b, 9)); TD("roundsd", _mm_round_sd(a, b, 10));
    TF("dpps", _mm_dp_ps(a, b, 0x7b)); TD("dppd", _mm_dp_pd(a, b, 0x31)); TF("insertps", _mm_insert_ps(a, b, 0x9a));
    h = 0; for (int i = 0; i < N; i++) { __m128i a = rv(); uint64_t g = rnd(); __m128 f = rf();
        mix(_mm_extract_epi8(a, 11)); mix(_mm_extract_epi16(a, 5)); mix(_mm_extract_epi32(a, 2)); mix(_mm_extract_epi64(a, 1)); mix(_mm_extract_ps(f, 3));
        mixv(_mm_insert_epi8(a, (int) g, 13)); mixv(_mm_insert_epi32(a, (int) g, 1)); mixv(_mm_insert_epi64(a, g, 1));
        uint8_t mb; uint16_t mw; uint32_t md; uint64_t mq;
        __asm__ volatile("pextrb $7, %1, %0" : "=m"(mb) : "x"(a)); __asm__ volatile("pextrw $6, %1, %0" : "=m"(mw) : "x"(a));
        __asm__ volatile("pextrd $3, %1, %0" : "=m"(md) : "x"(a)); __asm__ volatile("pextrq $0, %1, %0" : "=m"(mq) : "x"(a));
        mix(mb); mix(mw); mix(md); mix(mq); }
    printf("%-18s %016llx\n", "pextr/pinsr", (unsigned long long) h);
    // SSE4.2
    h = 0; for (int i = 0; i < N; i++) { uint64_t g = rnd(); uint32_t crc = (uint32_t) rnd();
        mix(_mm_crc32_u8(crc, (uint8_t) g)); mix(_mm_crc32_u16(crc, (uint16_t) g)); mix(_mm_crc32_u32(crc, (uint32_t) g)); mix(_mm_crc32_u64(crc, g));
        mix(_mm_popcnt_u64(g)); mix(_mm_popcnt_u32((uint32_t) g));
        uint64_t be; uint32_t be32; uint16_t be16;
        __asm__ volatile("movbe %1, %0" : "=r"(be) : "m"(g)); mix(be);
        __asm__ volatile("movbe %1, %0" : "=r"(be32) : "m"(crc)); mix(be32);
        __asm__ volatile("movbe %1, %0" : "=m"(be) : "r"(g)); mix(be);
        uint16_t w16 = (uint16_t) g; __asm__ volatile("movbe %1, %0" : "=r"(be16) : "m"(w16)); mix(be16); }
    printf("%-18s %016llx\n", "crc32/popcnt/movbe", (unsigned long long) h);
    // string compares: every control byte, short strings over a small alphabet
    for (int mode = 0; mode < 4; mode++) {
        h = 0;
        for (int i = 0; i < 6000; i++) {
            char sa[16], sb[16];
            int alpha = 2 + (int) (rnd() % 5);
            for (int k = 0; k < 16; k++) { sa[k] = (char) ('a' + rnd() % alpha); sb[k] = (char) ('a' + rnd() % alpha); }
            if (rnd() % 4) sa[rnd() % 16] = 0;
            if (rnd() % 4) sb[rnd() % 16] = 0;
            if (rnd() % 8 == 0) memcpy(sb, sa, 16);
            if (rnd() % 8 == 0) sa[rnd() % 16] = (char) 0x90; // a negative byte for the signed modes
            __m128i a = _mm_loadu_si128((void *) sa), b = _mm_loadu_si128((void *) sb);
            int la = (int) (rnd() % 20) - 2, lb = (int) (rnd() % 20) - 2;
            // the control byte must be a compile-time constant
#define STR(ctl) do { \
                if (mode == 0) { mix(_mm_cmpistri(a, b, ctl)); mix(_mm_cmpistrc(a, b, ctl)); mix(_mm_cmpistrz(a, b, ctl)); mix(_mm_cmpistrs(a, b, ctl)); mix(_mm_cmpistro(a, b, ctl)); mix(_mm_cmpistra(a, b, ctl)); } \
                else if (mode == 1) mixv(_mm_cmpistrm(a, b, ctl)); \
                else if (mode == 2) { mix(_mm_cmpestri(a, la, b, lb, ctl)); mix(_mm_cmpestrc(a, la, b, lb, ctl)); mix(_mm_cmpestrz(a, la, b, lb, ctl)); mix(_mm_cmpestrs(a, la, b, lb, ctl)); mix(_mm_cmpestro(a, la, b, lb, ctl)); } \
                else mixv(_mm_cmpestrm(a, la, b, lb, ctl)); } while (0)
            STR(0x00); STR(0x04); STR(0x08); STR(0x0c); STR(0x10); STR(0x14); STR(0x18); STR(0x1c);
            STR(0x30); STR(0x34); STR(0x38); STR(0x3c); STR(0x40); STR(0x44); STR(0x48); STR(0x4c);
            STR(0x01); STR(0x05); STR(0x09); STR(0x0d); STR(0x71); STR(0x75); STR(0x79); STR(0x7d);
            STR(0x02); STR(0x06); STR(0x0a); STR(0x0e); STR(0x12); STR(0x1a); STR(0x52); STR(0x5a);
            STR(0x03); STR(0x07); STR(0x0b); STR(0x0f); STR(0x23); STR(0x27); STR(0x6b); STR(0x6f);
        }
        printf("pcmp%s %016llx\n", mode == 0 ? "istri        " : mode == 1 ? "istrm        " : mode == 2 ? "estri        " : "estrm        ", (unsigned long long) h);
    }
    // cpuid says so
    unsigned eax = 1, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    printf("cpuid sse3 %d ssse3 %d sse4.1 %d sse4.2 %d popcnt %d cx16 %d\n", ecx & 1, (ecx >> 9) & 1, (ecx >> 19) & 1, (ecx >> 20) & 1, (ecx >> 23) & 1, (ecx >> 13) & 1);
    return 0;
}
