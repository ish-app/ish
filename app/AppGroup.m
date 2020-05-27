//
//  AppGroup.m
//  iSH
//
//  Created by Theodore Dubois on 2/28/20.
//

#import <Foundation/Foundation.h>
#include <mach-o/loader.h>
#include <mach-o/getsect.h>
#include <dlfcn.h>

struct cs_blob_index {
    uint32_t type;
    uint32_t offset;
};

struct cs_superblob {
    uint32_t magic;
    uint32_t length;
    uint32_t count;
    struct cs_blob_index index[];
};

struct cs_entitlements {
    uint32_t magic;
    uint32_t length;
    char entitlements[];
};

static NSDictionary *AppEntitlements() {
    static NSDictionary *entitlements;
    if (entitlements != nil)
        return entitlements;
    
    // Inspired by codesign.c in Darwin sources for Security.framework
    
    // Find our mach-o header
    Dl_info dl_info;
    if (dladdr(AppEntitlements, &dl_info) == 0)
        return nil;
    if (dl_info.dli_fbase == NULL)
        return nil;
    char *base = dl_info.dli_fbase;
    struct mach_header_64 *header = dl_info.dli_fbase;
    if (header->magic != MH_MAGIC_64)
        return nil;
    
    // Simulator executables have fake entitlements in the code signature. The real entitlements can be found in an __entitlements section.
    size_t entitlements_size;
    char *entitlements_data = getsectiondata(header, "__TEXT", "__entitlements", &entitlements_size);
    if (entitlements_data != NULL) {
        NSData *data = [NSData dataWithBytesNoCopy:entitlements_data
                                            length:entitlements_size
                                      freeWhenDone:NO];
        return entitlements = [NSPropertyListSerialization propertyListWithData:data
                                                                        options:NSPropertyListImmutable
                                                                         format:nil
                                                                          error:nil];
    }
    
    // Find the LC_CODE_SIGNATURE
    struct load_command *lc = (void *) (base + sizeof(*header));
    struct linkedit_data_command *cs_lc = NULL;
    for (uint32_t i = 0; i < header->ncmds; i++) {
        if (lc->cmd == LC_CODE_SIGNATURE) {
            cs_lc = (void *) lc;
            break;
        }
        lc = (void *) ((char *) lc + lc->cmdsize);
    }
    if (cs_lc == NULL)
        return nil;

    // Read the code signature off disk, as it's apparently not loaded into memory
    NSFileHandle *fileHandle = [NSFileHandle fileHandleForReadingFromURL:NSBundle.mainBundle.executableURL error:nil];
    if (fileHandle == nil)
        return nil;
    [fileHandle seekToFileOffset:cs_lc->dataoff];
    NSData *csData = [fileHandle readDataOfLength:cs_lc->datasize];
    [fileHandle closeFile];
    const struct cs_superblob *cs = csData.bytes;
    if (ntohl(cs->magic) != 0xfade0cc0)
        return nil;
    
    // Find the entitlements in the code signature
    NSData *entitlementsData = nil;
    for (uint32_t i = 0; i < ntohl(cs->count); i++) {
        struct cs_entitlements *ents = (void *) ((char *) cs + ntohl(cs->index[i].offset));
        if (ntohl(ents->magic) == 0xfade7171) {
            entitlementsData = [NSData dataWithBytes:ents->entitlements
                                              length:ntohl(ents->length) - offsetof(struct cs_entitlements, entitlements)];
        }
    }
    if (entitlementsData == nil)
        return nil;
    
    return entitlements = [NSPropertyListSerialization propertyListWithData:entitlementsData
                                                                    options:NSPropertyListImmutable
                                                                     format:nil
                                                                      error:nil];
}

NSArray<NSString *> *CurrentAppGroups() {
    return AppEntitlements()[@"com.apple.security.application-groups"];
}

NSURL *ContainerURL() {
    NSString *appGroup = CurrentAppGroups()[0];
    return [NSFileManager.defaultManager containerURLForSecurityApplicationGroupIdentifier:appGroup];
}
