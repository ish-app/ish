// register assignments
#define ip r8
#define tmp r9d
#define xsp r10d
#define cpu r11

.macro gadget name
.global gadget_\()\name
gadget_\()\name :
.endmacro
.macro endgadget
    lea 8(%ip), %ip
    jmp *-8(%ip)
.endmacro

.macro op reg
    mov (%ip), \reg
    lea 8(%ip), %ip
.endmacro
