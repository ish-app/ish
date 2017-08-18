#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// debug output utilities

#ifndef DEBUG_all
#define DEBUG_all 0
#endif
#ifndef DEBUG_default
#define DEBUG_default DEBUG_all
#endif
#ifndef DEBUG_instr
#define DEBUG_instr DEBUG_all
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

#define TRACE__NOP(msg, ...) do {} while(0)
#define TRACE__(msg, ...) printf(msg, ##__VA_ARGS__)
#define TRACE_(chan, msg, ...) CONCAT(TRACE_, chan)(msg, ##__VA_ARGS__)
#define TRACE(msg, ...) TRACE_(DEFAULT_CHANNEL, msg, ##__VA_ARGS__)
#define DEFAULT_CHANNEL default

#define TODO(msg, ...) { printf("TODO: " msg "\n", ##__VA_ARGS__); abort(); }
#define FIXME(msg, ...) printf("FIXME " msg "\n", ##__VA_ARGS__)
#define DIE(msg) { perror(msg); abort(); }

#if defined(__i386__) || defined(__x86_64__)
#define debugger __asm__("int3")
#elif defined(__arm__)
#define debugger __asm__("trap")
#endif

