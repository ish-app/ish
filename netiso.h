#ifndef NETISO_H
#define NETISO_H

#include "misc.h"

#ifndef NETWORK_ISOLATION
#define NETWORK_ISOLATION 1
#endif

int_t netiso_sockaddr(addr_t sockaddr_addr, uint_t sockaddr_len);

#endif // NETISO_H