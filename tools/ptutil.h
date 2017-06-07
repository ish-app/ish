#include "../misc.h"

long trycall(long res, const char *msg);
int start_tracee(const char *program, char *const argv[], char *const envp[]);
int open_mem(int pid);
dword_t pt_read(int pid, addr_t addr);
void pt_write8(int pid, addr_t addr, byte_t val);
void pt_write(int pid, addr_t addr, dword_t val);
dword_t aux_read(int pid, int type);
void aux_write(int pid, int type, dword_t value);
