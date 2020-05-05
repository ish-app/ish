//
//  iOSFS.h
//  iSH
//
//  Created by Noah Peeters on 26.10.19.
//

extern const struct fs_ops iosfs;
extern const struct fs_ops iosfs_unsafe;

void iosfs_init(void);
