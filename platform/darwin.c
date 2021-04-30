#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include "kernel/errno.h"
#include "platform/platform.h"
#include "debug.h"

typedef double CFTimeInterval;

struct cpu_usage get_total_cpu_usage() {
    host_cpu_load_info_data_t load;
    mach_msg_type_number_t fuck = HOST_CPU_LOAD_INFO_COUNT;
    host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t) &load, &fuck);
    struct cpu_usage usage;
    usage.user_ticks = load.cpu_ticks[CPU_STATE_USER];
    usage.system_ticks = load.cpu_ticks[CPU_STATE_SYSTEM];
    usage.idle_ticks = load.cpu_ticks[CPU_STATE_IDLE];
    usage.nice_ticks = load.cpu_ticks[CPU_STATE_NICE];
    return usage;
} 

struct mem_usage get_mem_usage() {
    host_basic_info_data_t basic = {};
    mach_msg_type_number_t fuck = HOST_BASIC_INFO_COUNT;
    kern_return_t status = host_info(mach_host_self(), HOST_BASIC_INFO, (host_info_t) &basic, &fuck);
    vm_statistics64_data_t vm = {};
    fuck = HOST_VM_INFO64_COUNT;
    status = host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info_t) &vm, &fuck);

    struct mem_usage usage;
    usage.total = basic.max_mem;
    usage.free = vm.free_count * vm_page_size;
    usage.available = basic.memory_size;
    usage.cached = vm.speculative_count * vm_page_size;
    usage.active = vm.active_count * vm_page_size;
    usage.inactive = vm.inactive_count * vm_page_size;
    usage.wirecount = vm.wire_count * vm_page_size;
    usage.swapins = vm.swapins * vm_page_size;
    usage.swapouts = vm.swapouts * vm_page_size;
    
    return usage;
}

CFTimeInterval getSystemUptime(void)
{
    enum { NANOSECONDS_IN_SEC = 1000 * 1000 * 1000 };
    static double multiply = 0;
    if (multiply == 0)
    {
        mach_timebase_info_data_t s_timebase_info;
        kern_return_t result = mach_timebase_info(&s_timebase_info);
        assert(result == 0);
        // multiply to get value in the nano seconds
        multiply = (double)s_timebase_info.numer / (double)s_timebase_info.denom;
        // multiply to get value in the seconds
        multiply /= NANOSECONDS_IN_SEC;
    }
    return mach_continuous_time() * multiply;
}

struct uptime_info get_uptime() {
    uint64_t kern_boottime[2];
    size_t size = sizeof(kern_boottime);
    sysctlbyname("kern.boottime", &kern_boottime, &size, NULL, 0);

    struct {
        uint32_t ldavg[3];
        long scale;
    } vm_loadavg;
    size = sizeof(vm_loadavg);
    sysctlbyname("vm.loadavg", &vm_loadavg, &size, NULL, 0);

    // linux wants the scale to be 16 bits
    for (int i = 0; i < 3; i++) {
        if (FSHIFT < 16)
            vm_loadavg.ldavg[i] <<= 16 - FSHIFT;
        else
            vm_loadavg.ldavg[i] >>= FSHIFT - 16;
    }
    
    struct uptime_info uptime = {
        //.uptime_ticks = now.tv_sec - kern_boottime[0],
        .uptime_ticks = getSystemUptime() * 100, // This works but shouldn't.  -mke
        .load_1m = vm_loadavg.ldavg[0],
        .load_5m = vm_loadavg.ldavg[1],
        .load_15m = vm_loadavg.ldavg[2],
    };
    return uptime;
}

int get_cpu_count() {
    int ncpu;
    size_t size = sizeof(int);
    sysctlbyname("hw.ncpu", &ncpu, &size, NULL, 0);
    return ncpu;
}

int get_per_cpu_usage(struct cpu_usage** cpus_usage) {
    mach_msg_type_number_t info_size = sizeof(processor_cpu_load_info_t);
    processor_cpu_load_info_t sys_load_data = 0;
    natural_t ncpu;
    
    int err = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &ncpu, (processor_info_array_t*)&sys_load_data, &info_size);
    if (err) {
        STRACE("Unable to get per cpu usage");
        return err;
    }
    
    struct cpu_usage* cpus_load_data = (struct cpu_usage*)calloc(ncpu, sizeof(struct cpu_usage));
    if (!cpus_load_data) {
        return _ENOMEM;
    }
    
    for (natural_t i = 0; i < ncpu; i++) {
        cpus_load_data[i].user_ticks = sys_load_data[i].cpu_ticks[CPU_STATE_USER];
        cpus_load_data[i].system_ticks = sys_load_data[i].cpu_ticks[CPU_STATE_SYSTEM];
        cpus_load_data[i].idle_ticks = sys_load_data[i].cpu_ticks[CPU_STATE_IDLE];
        cpus_load_data[i].nice_ticks = sys_load_data[i].cpu_ticks[CPU_STATE_NICE];
    }
    *cpus_usage = cpus_load_data;
    
    // Freeing cpu load information
    if (sys_load_data) {
        munmap(sys_load_data, vm_page_size);
    }
    
    return 0;
}
