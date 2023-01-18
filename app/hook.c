//
//  hook.c
//  iSH
//
//  Created by Saagar Jha on 12/29/22.
//

#include "hook.h"
#include "mach_excServer.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach-o/dyld_images.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#if __arm64__

// No Foundation.h
extern void NSLog(CFStringRef, ...);

kern_return_t catch_mach_exception_raise(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt) {
    abort();
}

kern_return_t catch_mach_exception_raise_state_identity(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int *flavor,
    thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt) {
    abort();
}

static bool initialized;

struct hook {
    uintptr_t old;
    uintptr_t new;
};
static struct hook hooks[16];
static int active_hooks;
static int breakpoints;

mach_port_t server;

kern_return_t catch_mach_exception_raise_state(
    mach_port_t exception_port,
    exception_type_t exception,
    const mach_exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int *flavor,
    const thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt) {
    arm_thread_state64_t *old = (arm_thread_state64_t *)old_state;
    arm_thread_state64_t *new = (arm_thread_state64_t *)new_state;

    for (int i = 0; i < active_hooks; ++i) {
        if (hooks[i].old == arm_thread_state64_get_pc(*old)) {
            *new = *old;
            *new_stateCnt = old_stateCnt;
            arm_thread_state64_set_pc_fptr(*new, hooks[i].new);
            return KERN_SUCCESS;
        }
    }

    return KERN_FAILURE;
}

void *exception_handler(void *unused) {
    mach_msg_server(mach_exc_server, sizeof(union __RequestUnion__catch_mach_exc_subsystem), server, MACH_MSG_OPTION_NONE);
    abort();
}

static bool initialize_if_needed() {
    if (initialized) {
        return true;
    }

#define CHECK(x)          \
    do {                  \
        if (!(x)) {       \
            NSLog(CFSTR("hook failed: " #x)); \
            return false; \
        }                 \
    } while (0)

    size_t size = sizeof(breakpoints);
    CHECK(!sysctlbyname("hw.optional.breakpoint", &breakpoints, &size, NULL, 0));

    CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &server) == KERN_SUCCESS);
    CHECK(mach_port_insert_right(mach_task_self(), server, server, MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS);

    // This will break any connected debuggers. Unfortunately the workarounds
    // for this are not very good, so we're not going to bother with them.
    CHECK(task_set_exception_ports(mach_task_self(), EXC_MASK_BREAKPOINT, server, EXCEPTION_STATE | MACH_EXCEPTION_CODES, ARM_THREAD_STATE64) == KERN_SUCCESS);

    pthread_t thread;
    CHECK(!pthread_create(&thread, NULL, exception_handler, NULL));

#undef CHECK

    return initialized = true;
}

// This is marked as available on iPhone in libproc.h but pulling in the header
// on iOS is difficult, so we should just forward declare it.
static int (*proc_regionfilename)(int pid, uint64_t address, void *buffer, uint32_t buffersize);

// Pulled from https://github.com/apple-oss-distributions/dyld/blob/main/cache-builder/dyld_cache_format.h.
// The format hasn't changed yet but we should check the magic value if it does,
// since Apple's tools use it to detect if the layout will be updated.
struct dyld_cache_header {
    char magic[16];
    char padding[56];
    uint64_t localSymbolsOffset;
    uint64_t localSymbolsSize;
};

struct dyld_cache_local_symbols_info {
    uint32_t nlistOffset;
    uint32_t nlistCount;
    uint32_t stringsOffset;
};

void *find_symbol(void *base, char *symbol) {
    if (!proc_regionfilename) {
        proc_regionfilename = dlsym(dlopen(NULL, RTLD_LAZY), "proc_regionfilename");
    }

#define CHECK(x)         \
    do {                 \
        if (!(x)) {      \
            NSLog(CFSTR("hook failed: " #x)); \
            return NULL; \
        }                \
    } while (0)

    CHECK(proc_regionfilename);

    task_dyld_info_data_t dyld_info;
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
    CHECK(task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyld_info, &count) == KERN_SUCCESS);
    struct dyld_all_image_infos *all_image_infos = (struct dyld_all_image_infos *)dyld_info.all_image_info_addr;

    // proc_regionfilename doesn't seem to produce results unless the shared
    // region has been modified in some way. This "no-op" forces the mapping
    // to be backed by a vnode whose path we can query.
    vm_protect(mach_task_self(), (vm_address_t)base, 1, false, VM_PROT_READ | VM_PROT_COPY);
    vm_protect(mach_task_self(), (vm_address_t)base, 1, false, VM_PROT_READ | VM_PROT_EXECUTE);

    char path[MAXPATHLEN];
    int size = proc_regionfilename(getpid(), all_image_infos->sharedCacheBaseAddress, path, sizeof(path));
    CHECK(size > 0);

    CFURLRef region = CFURLCreateWithBytes(NULL, (UInt8 *)path, size, kCFStringEncodingUTF8, NULL);
    CFURLRef shared_cache = CFURLCreateCopyDeletingPathExtension(NULL, region);
    CFURLRef symbols_file = CFURLCreateCopyAppendingPathExtension(NULL, shared_cache, CFSTR("symbols"));
    CFStringGetCString(CFURLGetString(symbols_file), path, sizeof(path), kCFStringEncodingUTF8);
    CFRelease(region);
    CFRelease(shared_cache);
    CFRelease(symbols_file);

    int fd = open(path, O_RDONLY);
    CHECK(fd >= 0);

    struct stat buffer;
    CHECK(!stat(path, &buffer));

    void *file = mmap(NULL, buffer.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    CHECK(file);

#undef CHECK

    void *address = NULL;

    struct dyld_cache_header *header = (struct dyld_cache_header *)file;
    if (strcmp(header->magic, "dyld_v1   arm64") && strcmp(header->magic, "dyld_v1  arm64e")) {
        NSLog(CFSTR("hook failed: unknown shared cache magic %s"), header->magic); \
        goto done;
    }

    struct dyld_cache_local_symbols_info *symbols = (struct dyld_cache_local_symbols_info *)(file + header->localSymbolsOffset);

    struct nlist_64 *list = (struct nlist_64 *)(file + header->localSymbolsOffset + symbols->nlistOffset);
    char *strings = (char *)(file + header->localSymbolsOffset + symbols->stringsOffset);
    for (size_t i = 0; i < symbols->nlistCount; ++i) {
        if (!strcmp(strings + list[i].n_un.n_strx, symbol)) {
            uintptr_t _address = list[i].n_value + all_image_infos->sharedCacheSlide;
            Dl_info info;
            dladdr((void *)_address, &info);
            if (info.dli_fbase == base) {
                address = (void *)_address;
                break;
            }
        }
    }

done:
    munmap(file, buffer.st_size);
    return address;
}

bool hook(void *old, void *new) {
    initialize_if_needed();

#define CHECK(x)          \
    do {                  \
        if (!(x)) {       \
            NSLog(CFSTR("hook failed: " #x)); \
            return false; \
        }                 \
    } while (0)

    CHECK(active_hooks < breakpoints);

    arm_debug_state64_t state = {};
    state.__bvr[active_hooks] = (uintptr_t)old;
    // DBGBCR_EL1
    //  .BT = 0b0000 << 20 (unlinked address match)
    //  .BAS = 0xF << 5 (A64)
    //  .PMC = 0b10 << 1 (user)
    //  .E = 0b1 << 0 (enable)
    state.__bcr[active_hooks] = 0x1e5;

    CHECK(task_set_state(mach_task_self(), ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS);

#undef CHECK

    bool success = true;

    thread_act_array_t threads;
    mach_msg_type_number_t thread_count = ARM_DEBUG_STATE64_COUNT;
    task_threads(mach_task_self(), &threads, &thread_count);
    for (int i = 0; i < thread_count; ++i) {
        if (thread_set_state(threads[i], ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT) != KERN_SUCCESS) {
            NSLog(CFSTR("hook failed: could not set thread 0x%x debug state"), threads[i]);
            success = false;
            goto done;
        }
    }

    hooks[active_hooks++] = (struct hook){(uintptr_t)old, (uintptr_t) new};

done:
    for (int i = 0; i < thread_count; ++i) {
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)threads, thread_count * sizeof(*threads));

    return success;
}

#else

void *find_symbol(void *base, char *symbol) {
    return NULL;
}

bool hook(void *old, void *new) {
    return false;
}

#endif
