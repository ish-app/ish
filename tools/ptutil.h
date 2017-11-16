#include "../misc.h"

long trycall(long res, const char *msg);
int start_tracee(int at, const char *path, char *const argv[], char *const envp[]);
int open_mem(int pid);
dword_t pt_read(int pid, addr_t addr);
void pt_write8(int pid, addr_t addr, byte_t val);
void pt_write(int pid, addr_t addr, dword_t val);
void pt_readn(int pid, addr_t addr, void *buf, size_t count);
void pt_writen(int pid, addr_t addr, void *buf, size_t count);
