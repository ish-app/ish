//
//  CurrentRoot.h
//  iSH
//
//  Created by Theodore Dubois on 11/4/21.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

extern int fs_ish_version;
extern int fs_ish_apk_version;

void FsInitialize(void);
bool FsIsManaged(void);
bool FsNeedsRepositoryUpdate(void);
void FsUpdateOnlyRepositoriesFile(void);
void FsUpdateRepositories(void);

/// An integer representing the current major version of the apk repositories. An upgrade will be run if the number in /ish/apk-version is smaller. After a successful upgrade, the newer number is copied into /ish/apk-version.
/// To upgrade:
/// - update the default rootfs to the same version
/// - update gen_apk_repositories.py to generate the new version of /etc/apk/repositories
/// - set both of the following constants appropriately, making sure to use a larger number than the previous one
#define CURRENT_APK_VERSION 31900
#define CURRENT_APK_VERSION_STRING "Alpine v3.19"

extern NSString *const FsUpdatedNotification;

NS_ASSUME_NONNULL_END
