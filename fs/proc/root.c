#include <sys/param.h> // for MIN and MAX
#include <sys/stat.h>
#include <inttypes.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/task.h"
#include "fs/proc.h"
#include "platform/platform.h"

static int proc_show_version(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    struct uname uts;
    do_uname(&uts);
    proc_printf(buf, "%s version %s %s\n", uts.system, uts.release, uts.version);
    return 0;
}

static int proc_show_stat(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    int ncpus = get_cpu_count();
    struct cpu_usage total_usage = get_total_cpu_usage();
    struct cpu_usage* per_cpu_usage = 0;
    struct uptime_info uptime_info = get_uptime();
    unsigned uptime = uptime_info.uptime_ticks;
    
    proc_printf(buf, "cpu  %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" 0 0 0 0\n", total_usage.user_ticks, total_usage.nice_ticks, total_usage.system_ticks, total_usage.idle_ticks);
    
    int err = get_per_cpu_usage(&per_cpu_usage);
    if (!err) {
        for (int i = 0; i < ncpus; i++) {
            proc_printf(buf, "cpu%d  %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" 0 0 0 0\n", i, per_cpu_usage[i].user_ticks, per_cpu_usage[i].nice_ticks, per_cpu_usage[i].system_ticks, per_cpu_usage[i].idle_ticks);
        }
        free(per_cpu_usage);
    }
    
    int blocked_task_count = get_count_of_blocked_tasks();
    int alive_task_count = get_count_of_alive_tasks();
    proc_printf(buf, "ctxt 0\n");
    proc_printf(buf, "btime %u\n", uptime);
    proc_printf(buf, "processes %d\n", alive_task_count);
    proc_printf(buf, "procs_running %d\n", alive_task_count - blocked_task_count);
    proc_printf(buf, "procs_blocked %d\n", blocked_task_count);
    
    return 0;
}

static void show_kb(struct proc_data *buf, const char *name, uint64_t value) {
    proc_printf(buf, "%s%8"PRIu64" kB\n", name, value / 1000);
}

char * parse_edx_flags(dword_t edx, char *edx_flags) { /* Translate edx bit flags into text */
    
    if(edx & (1<<0))
        strcat(edx_flags, "fpu ");
    if(edx & (1<<1))
        strcat(edx_flags, "vme ");
    if(edx & (1<<2))
        strcat(edx_flags, "de ");
    if(edx & (1<<3))
        strcat(edx_flags, "pse ");
    if(edx & (1<<4))
        strcat(edx_flags, "tsc ");
    if(edx & (1<<5))
        strcat(edx_flags, "msr ");
    if(edx & (1<<6))
        strcat(edx_flags, "pae ");
    if(edx & (1<<7))
        strcat(edx_flags, "mce ");
    if(edx & (1<<8))
        strcat(edx_flags, "cx8 ");
    if(edx & (1<<9))
        strcat(edx_flags, "apic ");
    if(edx & (1<<10))
        strcat(edx_flags, "Reserved ");
    if(edx & (1<<11))
        strcat(edx_flags, "sep ");
    if(edx & (1<<12))
        strcat(edx_flags, "mtrr ");
    if(edx & (1<<13))
        strcat(edx_flags, "pge ");
    if(edx & (1<<14))
        strcat(edx_flags, "mca ");
    if(edx & (1<<15))
        strcat(edx_flags, "cmov ");
    if(edx & (1<<17))
        strcat(edx_flags, "pse-36 ");
    if(edx & (1<<18))
        strcat(edx_flags, "psn ");
    if(edx & (1<<19))
        strcat(edx_flags, "clfsh ");
    if(edx & (1<<20))
        strcat(edx_flags, "Reserved ");
    if(edx & (1<<21))
        strcat(edx_flags, "ds ");
    if(edx & (1<<22))
        strcat(edx_flags, "acpi ");
    if(edx & (1<<23))
        strcat(edx_flags, "mmx ");
    if(edx & (1<<24))
        strcat(edx_flags, "fxsr ");
    if(edx & (1<<25))
        strcat(edx_flags, "sse ");
    if(edx & (1<<26))
        strcat(edx_flags, "sse2 ");
    if(edx & (1<<27))
        strcat(edx_flags, "ss ");
    if(edx & (1<<28))
        strcat(edx_flags, "htt ");
    if(edx & (1<<29))
        strcat(edx_flags, "tm ");
    if(edx & (1<<30))
        strcat(edx_flags, "Reserved ");
    if(edx & (1<<31))
        strcat(edx_flags, "pbe ");
    
    strcat(edx_flags, "\0");
    
    return(edx_flags);
}

char * translate_vendor_id(dword_t *ebx, dword_t *ecx, dword_t *edx) {
    char *byteArray = calloc(12 + 1, sizeof(char)); // vendor_id is fixed at 12 bytes
                                        
    // convert from an unsigned long int to a 4-byte array
    byteArray[0] = (int)((*ebx & 0XFF));
    byteArray[1] = (int)((*ebx >> 8) & 0XFF);
    byteArray[2] = (int)((*ebx >> 16) & 0xFF) ;
    byteArray[3] = (int)((*ebx >> 24) & 0xFF) ;
    byteArray[4] = (int)((*edx & 0XFF));
    byteArray[5] = (int)((*edx >> 8) & 0XFF);
    byteArray[6] = (int)((*edx >> 16) & 0xFF) ;
    byteArray[7] = (int)((*edx >> 24) & 0xFF) ;
    byteArray[8] = (int)((*ecx & 0XFF));
    byteArray[9] = (int)((*ecx >> 8) & 0XFF);
    byteArray[10] = (int)((*ecx >> 16) & 0xFF) ;
    byteArray[11] = (int)((*ecx >> 24) & 0xFF) ;
    byteArray[12] = 0;
    
    return(byteArray);
}

static int proc_show_cpuinfo(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    dword_t *eax = malloc(sizeof(dword_t));
    dword_t *ebx = malloc(sizeof(dword_t));
    dword_t *ecx = malloc(sizeof(dword_t));
    dword_t *edx = malloc(sizeof(dword_t));
    
    *eax = 0x00; // Get vendor_id.  It is returned as four bytes each in ebx, ecx & edx
    do_cpuid(eax, ebx, ecx, edx); // Get vendor_id
    
    char *vendor_id = calloc(12 + 1, sizeof(char)); // vendor_id is fixed at 12 bytes
    
    vendor_id = translate_vendor_id(ebx, ecx, edx);
    
    *eax = 1;
    do_cpuid(eax, ebx, ecx, edx);
    
    char *edx_flags=calloc(151, sizeof(char)); // Max size if all flags set
    parse_edx_flags(*edx, edx_flags);
    
    int cpu_count = get_cpu_count(); // One entry per device processor
    int i;
    
    for( i=0; i<cpu_count ; i++ ) {
        proc_printf(buf, "processor       : %d\n",i);
        proc_printf(buf, "vendor_id       : %s\n", vendor_id);
        proc_printf(buf, "cpu family      : %d\n",1);
        proc_printf(buf, "model           : %d\n",1);
        proc_printf(buf, "model name      : iSH Virtual i686-compatible CPU @ 1.066GHz\n");
        proc_printf(buf, "stepping        : %d\n",1);
        proc_printf(buf, "CPU MHz         : 1066.00\n");
        proc_printf(buf, "cache size      : %d kb\n",0);
        proc_printf(buf, "pysical id      : %d\n",0);
        proc_printf(buf, "siblings        : %d\n",0);
        proc_printf(buf, "core id         : %d\n",0);
        proc_printf(buf, "cpu cores       : %d\n",cpu_count);
        proc_printf(buf, "apicid          : %d\n",0);
        proc_printf(buf, "initial apicid  : %d\n",0);
        proc_printf(buf, "fpu             : yes\n");
        proc_printf(buf, "fpu_exception   : yes\n");
        proc_printf(buf, "cpuid level     : %d\n",13);
        proc_printf(buf, "wp              : yes\n");
        proc_printf(buf, "flags           : %s\n", edx_flags); // Pulled from do_cpuid
        proc_printf(buf, "bogomips        : 1066.00\n");
        proc_printf(buf, "clflush size    : %d\n", *ebx);
        proc_printf(buf, "cache_alignment : %d\n",64);
        proc_printf(buf, "address sizes   : 36 bits physical, 32 bits virtual\n");
        proc_printf(buf, "power management:\n");
        proc_printf(buf, "\n");
    }
    free(edx_flags);
    free(vendor_id);
    return 0;
}

static int proc_show_filesystems(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    char *filesystems = get_filesystems();
    proc_printf(buf, "%s", filesystems);
    return 0;
}

static int proc_show_meminfo(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    struct mem_usage usage = get_mem_usage();
    show_kb(buf, "MemTotal:       ", usage.total);
    show_kb(buf, "MemFree:        ", usage.free);
    show_kb(buf, "MemShared:      ", usage.free);
    // a bunch of crap busybox top needs to see or else it gets stack garbage
    show_kb(buf, "Shmem:          ", 0);
    show_kb(buf, "Buffers:        ", 0);
    show_kb(buf, "Cached:         ", 0);
    show_kb(buf, "SwapTotal:      ", 0);
    show_kb(buf, "SwapFree:       ", 0);
    show_kb(buf, "Dirty:          ", 0);
    show_kb(buf, "Writeback:      ", 0);
    show_kb(buf, "AnonPages:      ", 0);
    show_kb(buf, "Mapped:         ", 0);
    show_kb(buf, "Slab:           ", 0);
    return 0;
}

static int proc_show_uptime(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    struct uptime_info uptime_info = get_uptime();
    unsigned uptime = uptime_info.uptime_ticks;
    proc_printf(buf, "%u.%u %u.%u\n", uptime / 100, uptime % 100, uptime / 100, uptime % 100);
    return 0;
}

static int proc_show_loadavg(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    struct uptime_info uptime = get_uptime();
    struct pid *last_pid = pid_get_last_allocated();
    int last_pid_id = last_pid ? last_pid->id : 0;
    double load_1m = uptime.load_1m / 65536.0;
    double load_5m = uptime.load_5m / 65536.0;
    double load_15m = uptime.load_15m / 65536.0;
    int blocked_task_count = get_count_of_blocked_tasks();
    int alive_task_count = get_count_of_alive_tasks();
    // running_task_count is calculated approximetly, since we don't know the real number of currently running tasks.
    int running_task_count = MIN(get_cpu_count(), (int)(alive_task_count - blocked_task_count));
    proc_printf(buf, "%.2f %.2f %.2f %u/%u %u\n", load_1m, load_5m, load_15m, running_task_count, alive_task_count, last_pid_id);
    return 0;
}

static int proc_readlink_self(struct proc_entry *UNUSED(entry), char *buf) {
    sprintf(buf, "%d/", current->pid);
    return 0;
}

static void proc_print_escaped(struct proc_data *buf, const char *str) {
    // FIXME: this is hella slow
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (strchr(" \t\\", str[i]) != NULL) {
            proc_printf(buf, "\\%03o", str[i]);
        } else {
            proc_printf(buf, "%c", str[i]);
        }
    }
}

#define proc_printf_comma(buf, at_start, format, ...) do { \
    proc_printf((buf), "%s" format, *(at_start) ? "" : ",", ##__VA_ARGS__); \
    *(at_start) = false; \
} while (0)

static int proc_show_mounts(struct proc_entry *UNUSED(entry), struct proc_data *buf) {
    struct mount *mount;
    list_for_each_entry(&mounts, mount, mounts) {
        const char *point = mount->point;
        if (point[0] == '\0')
            point = "/";

        proc_print_escaped(buf, mount->source);
        proc_printf(buf, " ");
        proc_print_escaped(buf, point);
        proc_printf(buf, " %s ", mount->fs->name);
        bool at_start = true;
        proc_printf_comma(buf, &at_start, "%s", mount->flags & MS_READONLY_ ? "ro" : "rw");
        if (mount->flags & MS_NOSUID_)
            proc_printf_comma(buf, &at_start, "nosuid");
        if (mount->flags & MS_NODEV_)
            proc_printf_comma(buf, &at_start, "nodev");
        if (mount->flags & MS_NOEXEC_)
            proc_printf_comma(buf, &at_start, "noexec");
        if (strcmp(mount->info, "") != 0)
            proc_printf_comma(buf, &at_start, "%s", mount->info);
        proc_printf(buf, " 0 0\n");
    };
    return 0;
}

// in alphabetical order
struct proc_dir_entry proc_root_entries[] = {
    {"cpuinfo", .show = proc_show_cpuinfo},
    {"filesystems", .show = proc_show_filesystems},
    {"loadavg", .show = proc_show_loadavg},
    {"meminfo", .show = proc_show_meminfo},
    {"mounts", .show = proc_show_mounts},
    {"self", S_IFLNK, .readlink = proc_readlink_self},
    {"stat", .show = proc_show_stat},
    {"uptime", .show = proc_show_uptime},
    {"version", .show = proc_show_version},
};
#define PROC_ROOT_LEN sizeof(proc_root_entries)/sizeof(proc_root_entries[0])

static bool proc_root_readdir(struct proc_entry *UNUSED(entry), unsigned long *index, struct proc_entry *next_entry) {
    if (*index < PROC_ROOT_LEN) {
        *next_entry = (struct proc_entry) {&proc_root_entries[*index], 0, 0};
        (*index)++;
        return true;
    }

    pid_t_ pid = *index - PROC_ROOT_LEN;
    if (pid <= MAX_PID) {
        lock(&pids_lock);
        do {
            pid++;
        } while (pid <= MAX_PID && pid_get_task(pid) == NULL);
        unlock(&pids_lock);
        if (pid > MAX_PID)
            return false;
        *next_entry = (struct proc_entry) {&proc_pid, .pid = pid};
        *index = pid + PROC_ROOT_LEN;
        return true;
    }

    return false;
}

struct proc_dir_entry proc_root = {NULL, S_IFDIR, .readdir = proc_root_readdir};
