#ifndef UTIL_DEBUG_H
#define UTIL_DEBUG_H
#include <stdio.h>
#include <stdlib.h>

void ish_printk(const char *msg, ...);
void ish_vprintk(const char *msg, va_list args);
#define printk ish_printk

// debug output utilities
// save me

#ifndef DEBUG_all
#define DEBUG_all 0
#endif
#ifndef DEBUG_verbose
#define DEBUG_verbose DEBUG_all
#endif
#ifndef DEBUG_instr
#define DEBUG_instr DEBUG_all
#endif
#ifndef DEBUG_debug
#define DEBUG_debug DEBUG_all
#endif
#ifndef DEBUG_strace
#define DEBUG_strace DEBUG_all
#endif
#ifndef DEBUG_memory
#define DEBUG_memory DEBUG_all
#endif

#if DEBUG_verbose
#define TRACE_verbose TRACE__
#else
#define TRACE_verbose TRACE__NOP
#endif
#if DEBUG_instr
#define TRACE_instr TRACE__
#else
#define TRACE_instr TRACE__NOP
#endif
#if DEBUG_debug
#define TRACE_debug TRACE__
#else
#define TRACE_debug TRACE__NOP
#endif
#if DEBUG_strace
#define TRACE_strace TRACE__
#else
#define TRACE_strace TRACE__NOP
#endif
#if DEBUG_memory
#define TRACE_memory TRACE__
#else
#define TRACE_memory TRACE__NOP
#endif

#ifdef LOG_OVERRIDE
extern int log_override;
#define TRACE__NOP(msg, ...) if (log_override) { TRACE__(msg, ##__VA_ARGS__); }
#else
#define TRACE__NOP(msg, ...) use(__VA_ARGS__)
#endif
#define TRACE__(msg, ...) printk(msg, ##__VA_ARGS__)

#define TRACE_(chan, msg, ...) glue(TRACE_, chan)(msg, ##__VA_ARGS__)
#define TRACE(msg, ...) TRACE_(DEFAULT_CHANNEL, msg, ##__VA_ARGS__)
#ifndef DEFAULT_CHANNEL
#define DEFAULT_CHANNEL verbose
#endif

#define TODO(msg, ...) die("TODO: " msg, ##__VA_ARGS__)
#define FIXME(msg, ...) printk("FIXME " msg "\n", ##__VA_ARGS__)
#define ERRNO_DIE(msg) { perror(msg); abort(); }
extern void (*die_handler)(const char *msg);
_Noreturn void die(const char *msg, ...);

#define STRACE(msg, ...) TRACE_(strace, msg, ##__VA_ARGS__)

#if defined(__i386__) || defined(__x86_64__)
#define debugger __asm__("int3")
#else
#include <signal.h>
#define debugger raise(SIGTRAP)
#endif

#endif
