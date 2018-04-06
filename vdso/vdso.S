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
    ret

    .global __kernel_rt_sigreturn
    .type __kernel_rt_sigreturn,@function
__kernel_rt_sigreturn:
    popl %ebx
    mov $173, %eax // NR_rt_sigreturn
    int $0x80
    ret

    // this will probably need to be adjusted as the vdso grows, or ptrace
    // failures may result as the real linux vdso is exactly 8k and the fake
    // one needs to be the same number of pages
    .fill 4096, 1, 0x0b0f
