#include <string.h>
#include "kernel/calls.h"

#define PRCTL_SET_KEEPCAPS_ 8
#define PRCTL_SET_NAME_ 15
#define PRCTL_GET_NAME_ 16
#define PRCTL_SET_PDEATHSIG_ 1
#define PRCTL_GET_DUMPABLE_ 3
#define PRCTL_SET_DUMPABLE_ 4
#define PRCTL_SET_NO_NEW_PRIVS_ 38

int_t sys_prctl(dword_t option, uword_t arg2, uword_t UNUSED(arg3), uword_t UNUSED(arg4), uword_t UNUSED(arg5)) {
    switch (option) {
        case PRCTL_SET_KEEPCAPS_:
            // stub
            return 0;
        case PRCTL_SET_NAME_: {
            char name[16];
            if (user_read_string(arg2, name, sizeof(name) - 1))
                return _EFAULT;
            name[sizeof(name) - 1] = '\0';
            STRACE("prctl(PRCTL_SET_NAME, \"%s\")", name);
            strcpy(current->comm, name);
            return 0;
        }
        case PRCTL_GET_NAME_: {
            char name[16] = {0};
            strncpy(name, current->comm, sizeof(name) - 1);
            STRACE("prctl(PRCTL_GET_NAME)");
            if (user_write(arg2, name, sizeof(name)))
                return _EFAULT;
            return 0;
        }
        case PRCTL_SET_PDEATHSIG_:
        case PRCTL_SET_DUMPABLE_:
        case PRCTL_SET_NO_NEW_PRIVS_:
            STRACE("prctl(%d, %#x) (ignored)", option, arg2);
            return 0;
        case PRCTL_GET_DUMPABLE_:
            return 1;
        default:
            STRACE("prctl(%#x)", option);
            return _EINVAL;
    }
}

int_t sys_arch_prctl(int_t code, addr_t addr) {
    STRACE("arch_prctl(%#x, %#x)", code, addr);
#if ISH_GUEST_64
    switch (code) {
        case 0x1002: // ARCH_SET_FS
            current->cpu.fs_base = addr;
            return 0;
        case 0x1001: // ARCH_SET_GS
            current->cpu.gs_base = addr;
            return 0;
        case 0x1003: // ARCH_GET_FS
            if (user_put(addr, current->cpu.fs_base))
                return _EFAULT;
            return 0;
        case 0x1004: // ARCH_GET_GS
            if (user_put(addr, current->cpu.gs_base))
                return _EFAULT;
            return 0;
    }
#endif
    return _EINVAL;
}

#define REBOOT_MAGIC1 0xfee1dead
#define REBOOT_MAGIC2 672274793
#define REBOOT_MAGIC2A 85072278
#define REBOOT_MAGIC2B 369367448
#define REBOOT_MAGIC2C 537993216

#define REBOOT_CMD_CAD_OFF 0
#define REBOOT_CMD_CAD_ON 0x89abcdef

int_t sys_reboot(int_t magic, int_t magic2, int_t cmd) {
    STRACE("reboot(%#x, %d, %d)", magic, magic2, cmd);
    if (!superuser())
        return _EPERM;
    if (magic != (int) REBOOT_MAGIC1 ||
            (magic2 != REBOOT_MAGIC2 &&
             magic2 != REBOOT_MAGIC2A &&
             magic2 != REBOOT_MAGIC2B &&
             magic2 != REBOOT_MAGIC2C))
        return _EINVAL;

    switch (cmd) {
        case REBOOT_CMD_CAD_ON:
        case REBOOT_CMD_CAD_OFF:
            return 0;
        default:
            return _EPERM;
    }
}

int_t sys_getcpu(addr_t cpu_addr, addr_t node_addr) {
    dword_t zero = 0;
    if (cpu_addr != 0 && user_put(cpu_addr, zero))
        return _EFAULT;
    if (node_addr != 0 && user_put(node_addr, zero))
        return _EFAULT;
    return 0;
}
