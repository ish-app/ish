    .text
    .global __kernel_vsyscall
    .type __kernel_vsyscall,@function
__kernel_vsyscall:
    int $0x80
    ret

    .global __kernel_sigreturn
    .type __kernel_sigreturn,@function
__kernel_sigreturn:
    popl %ebx // I have no idea what this is for but apparently it needs unwind info?
    mov $119, %eax // NR_sigreturn
    int $0x80
    nop

    .global __kernel_rt_sigreturn
    .type __kernel_rt_sigreturn,@function
__kernel_rt_sigreturn:
    mov $173, %eax // NR_rt_sigreturn
    int $0x80
    nop
