#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// TODO turn this into function that outputs to a log buffer
#define printk(msg, ...) dprintf(666, msg, ##__VA_ARGS__)

// all line endings should use \r\n so it can work even with the terminal in raw mode
// this is subject to change, so use NEWLINE or println whenever you output a newline
#define NEWLINE "\r\n"
#define println(msg, ...) printk(msg NEWLINE, ##__VA_ARGS__)

// debug output utilities
// save me

#ifndef DEBUG_all
#define DEBUG_all 0
#endif
#ifndef DEBUG_default
#define DEBUG_default DEBUG_all
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

#if DEBUG_default
#define TRACE_default TRACE__
#else
#define TRACE_default TRACE__NOP
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
#define TRACE__NOP(msg, ...) do {} while(0)
#endif
#define TRACE__(msg, ...) printk(msg, ##__VA_ARGS__)

#define TRACE_(chan, msg, ...) glue(TRACE_, chan)(msg, ##__VA_ARGS__)
#define TRACE(msg, ...) TRACE_(DEFAULT_CHANNEL, msg, ##__VA_ARGS__)
#define TRACELN_(chan, msg, ...) TRACE_(chan, msg "\r\n", ##__VA_ARGS__)
#define TRACELN(msg, ...) TRACE(msg NEWLINE, ##__VA_ARGS__)
#ifndef DEFAULT_CHANNEL
#define DEFAULT_CHANNEL default
#endif

#define TODO(msg, ...) { println("TODO: " msg, ##__VA_ARGS__); abort(); }
#define FIXME(msg, ...) println("FIXME " msg, ##__VA_ARGS__)
#define DIE(msg) { perror(msg); abort(); }

#define STRACE(msg, ...) TRACE_(strace, msg, ##__VA_ARGS__)
#define STRACELN(msg, ...) TRACELN_(strace, msg, ##__VA_ARGS__)

#if defined(__i386__) || defined(__x86_64__)
#define debugger __asm__("int3")
#else
#include <signal.h>
#define debugger raise(SIGTRAP)
#endif
