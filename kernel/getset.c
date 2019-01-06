#include "kernel/calls.h"

pid_t_ sys_getpid() {
    STRACE("getpid()");
    return current->tgid;
}
pid_t_ sys_gettid() {
    STRACE("gettid()");
    return current->pid;
}
pid_t_ sys_getppid() {
    STRACE("getppid()");
    pid_t_ ppid;
    lock(&pids_lock);
    if (current->parent != NULL)
        ppid = current->parent->pid;
    else
        ppid = 0;
    unlock(&pids_lock);
    return ppid;
}

dword_t sys_getuid32() {
    STRACE("getuid32()");
    return current->uid;
}
dword_t sys_getuid() {
    STRACE("getuid()");
    return current->uid & 0xffff;
}

dword_t sys_geteuid32() {
    STRACE("geteuid32()");
    return current->euid;
}
dword_t sys_geteuid() {
    STRACE("geteuid()");
    return current->euid & 0xffff;
}

int_t sys_setuid(uid_t_ uid) {
    STRACE("setuid(%d)", uid);
    if (superuser()) {
        current->uid = current->suid = uid;
    } else {
        if (uid != current->uid && uid != current->suid)
            return _EPERM;
    }
    current->euid = uid;
    return 0;
}

dword_t sys_setresuid(uid_t_ ruid, uid_t_ euid, uid_t_ suid) {
    STRACE("setresuid(%d, %d, %d)", ruid, euid, suid);
    if (!superuser()) {
        if (ruid != (uid_t) -1 && ruid != current->uid && ruid != current->euid && ruid != current->suid)
            return _EPERM;
        if (euid != (uid_t) -1 && euid != current->uid && euid != current->euid && euid != current->suid)
            return _EPERM;
        if (suid != (uid_t) -1 && suid != current->uid && suid != current->euid && suid != current->suid)
            return _EPERM;
    }

    if (ruid != (uid_t) -1)
        current->uid = ruid;
    if (euid != (uid_t) -1)
        current->euid = euid;
    if (suid != (uid_t) -1)
        current->suid = suid;
    return 0;
}

int_t sys_getresuid(addr_t ruid_addr, addr_t euid_addr, addr_t suid_addr) {
    STRACE("getresuid(%#x, %#x, %#x)", ruid_addr, euid_addr, suid_addr);
    if (user_put(ruid_addr, current->uid))
        return _EFAULT;
    if (user_put(euid_addr, current->euid))
        return _EFAULT;
    if (user_put(suid_addr, current->suid))
        return _EFAULT;
    return 0;
}

dword_t sys_getgid32() {
    STRACE("getgid32()");
    return current->gid;
}
dword_t sys_getgid() {
    STRACE("getgid()");
    return current->gid & 0xffff;
}

dword_t sys_getegid32() {
    STRACE("getegid32()");
    return current->egid;
}
dword_t sys_getegid() {
    STRACE("getegid()");
    return current->egid & 0xffff;
}

int_t sys_setgid(uid_t_ gid) {
    STRACE("setgid(%d)", gid);
    if (superuser()) {
        current->gid = current->sgid = gid;
    } else {
        if (gid != current->gid && gid != current->sgid)
            return _EPERM;
    }
    current->egid = gid;
    return 0;
}

dword_t sys_setresgid(uid_t_ rgid, uid_t_ egid, uid_t_ sgid) {
    STRACE("setresgid(%d, %d, %d)", rgid, egid, sgid);
    if (!superuser()) {
        if (rgid != (uid_t) -1 && rgid != current->gid && rgid != current->egid && rgid != current->sgid)
            return _EPERM;
        if (egid != (uid_t) -1 && egid != current->gid && egid != current->egid && egid != current->sgid)
            return _EPERM;
        if (sgid != (uid_t) -1 && sgid != current->gid && sgid != current->egid && sgid != current->sgid)
            return _EPERM;
    }

    if (rgid != (uid_t) -1)
        current->gid = rgid;
    if (egid != (uid_t) -1)
        current->egid = egid;
    if (sgid != (uid_t) -1)
        current->sgid = sgid;
    return 0;
}

int_t sys_getresgid(addr_t rgid_addr, addr_t egid_addr, addr_t sgid_addr) {
    STRACE("getresgid(%#x, %#x, %#x)", rgid_addr, egid_addr, sgid_addr);
    if (user_put(rgid_addr, current->gid))
        return _EFAULT;
    if (user_put(egid_addr, current->egid))
        return _EFAULT;
    if (user_put(sgid_addr, current->sgid))
        return _EFAULT;
    return 0;
}

int_t sys_getgroups(dword_t size, addr_t list) {
    if (size == 0)
        return current->ngroups;
    if (size < current->ngroups)
        return _EINVAL;
    if (user_write(list, current->groups, size * sizeof(uid_t_)))
        return _EFAULT;
    return 0;
}

int_t sys_setgroups(dword_t size, addr_t list) {
    if (size > MAX_GROUPS)
        return _EINVAL;
    if (user_read(list, current->groups, size * sizeof(uid_t_)))
        return _EFAULT;
    current->ngroups = size;
    return 0;
}

// this does not really work
int_t sys_capget(addr_t header_addr, addr_t data_addr) {
    STRACE("capget(%#x, %#x)", header_addr, data_addr);
    return 0;
}
int_t sys_capset(addr_t header_addr, addr_t data_addr) {
    STRACE("capset(%#x, %#x)", header_addr, data_addr);
    return 0;
}
