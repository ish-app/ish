#include <time.h>
#include "sys/calls.h"

dword_t sys_time(addr_t time_out) {
    dword_t now = time(NULL);
    if (time_out != 0)
        user_put_count(time_out, &now, sizeof(now));
    return now;
}
