#ifndef FS_DYN_DEV_H
#define FS_DYN_DEV_H

// dyn_devs are dynamically added devices (character only for now)
// with custom dev_ops assigned
// It's useful to add new device "drivers" in runtime (for example,
// devices only present on some platforms)

// (int)('D'+'Y') == 157
#define DYN_DEV_MAJOR 157

// dev_ops handing DYN_DEV_MAJOR major number
extern struct dev_ops dyn_dev;

// Registeres new device with major number DYN_DEV_MAJOR and returned minor number
// handled by provided ops, which should be valid for "kernel" lifetime (should
// not be freed, might be static)
//
// Return value:
//  - newly registered device minor number (>= 0) on success
//  - _ENOSPC if there are no free minor device numbers left
extern int dyn_dev_register(struct dev_ops *ops);

#endif
