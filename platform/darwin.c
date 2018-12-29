#include <mach/mach.h>
#include "platform/platform.h"

struct cpu_usage get_cpu_usage() {
    struct host_cpu_load_info load;
    mach_msg_type_number_t fuck = HOST_CPU_LOAD_INFO_COUNT;
    host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t) &load, &fuck);
    struct cpu_usage usage;
    usage.user_ticks = load.cpu_ticks[CPU_STATE_USER];
    usage.system_ticks = load.cpu_ticks[CPU_STATE_SYSTEM];
    usage.idle_ticks = load.cpu_ticks[CPU_STATE_IDLE];
    usage.nice_ticks = load.cpu_ticks[CPU_STATE_NICE];
    return usage;
}
