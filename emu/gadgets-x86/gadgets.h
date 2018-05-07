# register assignments
#define xsp r8d
#define ip r9
#define tmp r10d
#define cpu r11
#define tlb r12
#define addr r13d
#define addrq r13

.macro .gadget name
    .global gadget_\()\name
    gadget_\()\name :
.endm
.macro gret pop=0
    addq $((\pop+1)*8), %ip
    jmp *-8(%ip)
.endm

# using a gas macro for this works fine on gcc but not on clang
#define each_reg irp reg, eax,ecx,edx,ebx,ebp,esp,esi,edi

.macro save_c
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
.endm
.macro restore_c
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
.endm

# vim: ft=gas
