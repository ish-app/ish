// Intel standard interrupts
// Any interrupt not handled specially becomes a SIGSEGV
#define INT_NONE -1
#define INT_DIV 0
#define INT_DEBUG 1
#define INT_NMI 2
#define INT_BREAKPOINT 3
#define INT_OVERFLOW 4
#define INT_BOUND 5
#define INT_INVALID 6
#define INT_FPU 7 // apparently "device not available"
#define INT_DOUBLE 8 // interrupt during interrupt, i.e. interruptception
#define INT_SYSCALL 0x80
