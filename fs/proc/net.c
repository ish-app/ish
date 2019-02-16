#include <sys/stat.h>
#include <inttypes.h>
#include "kernel/calls.h"
#include "fs/proc.h"
#include "platform/platform.h"

#import <ifaddrs.h>
#import <netinet/in.h>
#import <sys/socket.h>
#import <unistd.h>
#import <net/if_var.h>

#pragma mark - /proc/net

static ssize_t proc_show_dev(struct proc_entry * UNUSED(entry), char *buf)
{
    size_t n = 0;
    n += sprintf(buf + n, "Inter-|   Receive                            "
                 "                    |  Transmit\n"
                 " face |bytes    packets errs drop fifo frame "
                 "compressed multicast|bytes    packets errs "
                 "drop fifo colls carrier compressed\n");

    struct ifaddrs *addrs;
    bool success = (getifaddrs(&addrs) == 0);
    if (success) {
        const struct ifaddrs *cursor = addrs;
        while (cursor != NULL) {
            if (cursor->ifa_addr->sa_family == AF_LINK) {
                const struct sockaddr_in *dlAddr = (const struct sockaddr_in *)cursor->ifa_addr;

                const struct if_data *stats = (struct if_data *)cursor->ifa_data;
                if (stats != NULL) {
                    n += sprintf(buf + n, "%6s:%8lu %7lu %4lu %4lu %4lu %5lu %10lu %9lu "
                                 "%8lu %7lu %4lu %4lu %4lu %5lu %7lu %10lu\n",
                                 cursor->ifa_name,
                                 (unsigned long)stats->ifi_ibytes,   // stats->rx_bytes,
                                 (unsigned long)stats->ifi_ipackets,   // stats->rx_packets,
                                 (unsigned long)stats->ifi_ierrors,  // stats->rx_errors,
                                 (unsigned long)0,  // stats->rx_dropped + stats->rx_missed_errors,
                                 (unsigned long)0,  // stats->rx_fifo_errors,
                                 (unsigned long)0,  // stats->rx_length_errors + stats->rx_over_errors +
                                 (unsigned long)0,  // stats->rx_crc_errors + stats->rx_frame_errors,
                                 (unsigned long)0,  // stats->rx_compressed,
                                 (unsigned long)stats->ifi_imcasts,  // stats->multicast,
                                 (unsigned long)stats->ifi_obytes,  // stats->tx_bytes,
                                 (unsigned long)stats->ifi_opackets,  // stats->tx_packets,
                                 (unsigned long)stats->ifi_oerrors,  // stats->tx_errors,
                                 (unsigned long)0,  // stats->tx_dropped,
                                 (unsigned long)0,  // stats->tx_fifo_errors,
                                 (unsigned long)stats->ifi_collisions,  // stats->collisions,
                                 (unsigned long)0,  // stats->tx_carrier_errors + stats->tx_aborted_errors +
                                 (unsigned long)0,  // stats->tx_window_errors + stats->tx_heartbeat_errors,
                                 (unsigned long)0);  // stats->tx_compressed);
                } else {
                    n += sprintf(buf + n, "%6s: %7llu %7llu %4llu %4llu %4llu %5llu %10llu %9llu "
                                 "%8llu %7llu %4llu %4llu %4llu %5llu %7llu %10llu\n",
                                 cursor->ifa_name,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0,
                                 (unsigned long long)0);
                }
            }
            cursor = cursor->ifa_next;
        }
        freeifaddrs(addrs);
    }

//    n += sprintf(buf + n, "eth0: 2970170863 1695464088 12203 14490431    0     0          0         0 353276523 1536499657    0    0    0     0       0          0\n");

//    struct cpu_usage usage = get_cpu_usage();
//    size_t n = 0;
//    n += sprintf(buf + n, "cpu  %llu %llu %llu %llu\n", usage.user_ticks, usage.nice_ticks, usage.system_ticks, usage.idle_ticks);
    return n;
}

struct proc_dir_entry proc_net_entries[] = {
    { "dev", .show = proc_show_dev }
};
#define PROC_NET_LEN sizeof(proc_net_entries) / sizeofproc_net_entries

bool proc_net_readdir(struct proc_entry * UNUSED(entry), unsigned long *index, struct proc_entry *next_entry)
{
    if (*index < 1) {
        *next_entry = (struct proc_entry) {&proc_net_entries[*index], 0, 0 };
        (*index)++;
        return true;
    }

    return false;
}
