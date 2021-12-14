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

/// The smallest value for /ish/apk-version for which updating /etc/apk/repositories does not require running /sbin/apk upgrade, given that every update to /etc/apk/repositories is followed by copying /ish/version to /ish/apk-version.
#define COMPATIBLE_APK_VERSION 296
#define NEWEST_APK_VERSION "Alpine v3.14"

extern NSString *const FsUpdatedNotification;

NS_ASSUME_NONNULL_END
