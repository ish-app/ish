#include "sys/calls.h"
#include "emu/process.h"

dword_t user_get(addr_t addr) {
    return MEM_READ(curmem, addr, 32);
}

byte_t user_get8(addr_t addr) {
    return MEM_READ(curmem, addr, 8);
}

void user_put(addr_t addr, dword_t value) {
    MEM_WRITE(curmem, addr, 32) = value;
}

void user_put8(addr_t addr, byte_t value) {
    MEM_WRITE(curmem, addr, 8) = value;
}

int user_get_string(addr_t addr, char *buf, size_t max) {
    size_t i = 0;
    while (i < max) {
        buf[i] = user_get8(addr + i);
        if (buf[i] == '\0') break;
        i++;
    }
    return i;
}

void user_put_string(addr_t addr, const char *buf) {
    size_t i = 0;
    while (buf[i] != '\0') {
        user_put8(addr + i, buf[i]);
        i++;
    }
    user_put8(addr + i, '\0');
}

int user_get_count(addr_t addr, void *buf, size_t count) {
    char *cbuf = (char *) buf;
    size_t i = 0;
    while (i < count) {
        cbuf[i] = user_get8(addr + i);
        i++;
    }
    return i;
}

void user_put_count(addr_t addr, const void *buf, size_t count) {
    const char *cbuf = (const char *) buf;
    size_t i = 0;
    while (i < count) {
        user_put8(addr + i, cbuf[i]);
        i++;
    }
}
