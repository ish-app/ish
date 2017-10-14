#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include "kernel/sock.h"
#include "kernel/fs.h"

struct sockaddr_un_ {
    uint16_t family;
    char path[108];
};

static struct sockaddr_un *sockaddr_unix_to_real(struct sockaddr_un_ *sockaddr) {
    // lol
    return ERR_PTR(_ENOENT);
    // struct sockaddr_un *sun = malloc(sizeof(struct sockaddr_un));
    // sun->sun_family = sockaddr->family;
    // char path[PATH_MAX];
    // int err = path_normalize(NULL, sockaddr->path, path, true);
    // if (err < 0)
        // return ERR_PTR(err);
    // if (strlen(path) >= sizeof(sun->sun_path))
        // return ERR_PTR(_ENAMETOOLONG);
    // strcpy(path, sun->sun_path);
    // return sun;
}

struct sockaddr_in_ {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
};

static struct sockaddr_in *sockaddr_inet_to_real(struct sockaddr_in_ *sockaddr) {
    struct sockaddr_in *sin = malloc(sizeof(struct sockaddr_in));
    sin->sin_family = sockaddr->family;
    sin->sin_port = sockaddr->port;
    sin->sin_addr.s_addr = sockaddr->addr;
    return sin;
}

struct sockaddr_in6_ {
    uint16_t family;
    uint16_t port;
    uint32_t flowinfo;
    uint8_t addr[16];
    uint32_t scope_id;
};

static struct sockaddr_in6 *sockaddr_inet6_to_real(struct sockaddr_in6_ *sockaddr) {
    struct sockaddr_in6 *sin6 = malloc(sizeof(struct sockaddr_in6));
    sin6->sin6_family = sockaddr->family;
    sin6->sin6_port = sockaddr->port;
    sin6->sin6_flowinfo = sockaddr->flowinfo;
    memcpy(&sin6->sin6_addr, &sockaddr->addr, 16);
    sin6->sin6_scope_id = sockaddr->scope_id;
    return sin6;
}

size_t sockaddr_size(void *sockaddr) {
    switch (((struct sockaddr_ *) sockaddr)->family) {
        case PF_LOCAL_:
            return sizeof(struct sockaddr_un);
        case PF_INET_:
            return sizeof(struct sockaddr_in);
        case PF_INET6_:
            return sizeof(struct sockaddr_in6);
    }
    return 0;
}

struct sockaddr *sockaddr_to_real(void *sockaddr) {
    switch (((struct sockaddr_ *) sockaddr)->family) {
        case PF_LOCAL_:
            return (void *) sockaddr_unix_to_real(sockaddr);
        case PF_INET_:
            return (void *) sockaddr_inet_to_real(sockaddr);
        case PF_INET6_:
            return (void *) sockaddr_inet6_to_real(sockaddr);
    }
    return ERR_PTR(_EINVAL);
}
