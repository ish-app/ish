#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <gdbm.h>
#include "kernel/fs.h"
#include "kernel/errno.h"
#include "util/list.h"

// ad hoc hashtable
struct entry {
    ino_t inode;
    char *path;
    struct list chain;
};

#define PREFIX_INO "inode "

int fakefs_rebuild(struct mount *mount, const char *db_path) {
    char new_db_path[MAX_PATH];
    strcpy(new_db_path, db_path);
    strcat(new_db_path, ".new");
    GDBM_FILE new_db = gdbm_open(new_db_path, 0, GDBM_NEWDB, 0666, NULL);
    if (new_db == NULL)
        return errno_map();

    struct list hashtable[2000];
#define HASH_SIZE (sizeof(hashtable)/sizeof(hashtable[0]))
    for (unsigned i = 0; i < HASH_SIZE; i++)
        list_init(&hashtable[i]);

    char keydata[strlen(PREFIX_INO) + MAX_PATH];
    datum dkey = {.dptr = keydata};
    char valuedata[30];
    datum dvalue = {.dptr = valuedata};

    for (datum key = gdbm_firstkey(mount->db), nextkey; key.dptr != NULL;
            nextkey = gdbm_nextkey(mount->db, key), free(key.dptr), key = nextkey) {
        if (strncmp(key.dptr, PREFIX_INO, strlen(PREFIX_INO)) != 0)
            continue;

        // fetch the inode from the value
        datum value_datum = gdbm_fetch(mount->db, key);
        char *value = strndup(value_datum.dptr, value_datum.dsize);
        free(value_datum.dptr);
        char *end;
        ino_t inode = strtol(value, &end, 10);
        if (*end != '\0') {
            free(value);
            continue;
        }

        // extract the path from the key
        char *path = strndup(key.dptr + strlen(PREFIX_INO), key.dsize - strlen(PREFIX_INO));
        struct stat stat;
        int err = fstatat(mount->root_fd, fix_path(path), &stat, 0);
        if (err < 0)
            goto next;
        ino_t real_inode = stat.st_ino;

        // restore hardlinks
        struct list *bucket = &hashtable[inode % HASH_SIZE];
        struct entry *entry;
        bool found = false;
        list_for_each_entry(bucket, entry, chain) {
            if (entry->inode == inode) {
                unlinkat(mount->root_fd, fix_path(path), 0);
                linkat(mount->root_fd, fix_path(entry->path), mount->root_fd, fix_path(path), 0);
                found = true;
                break;
            }
        }
        if (!found) {
            entry = malloc(sizeof(struct entry));
            entry->inode = inode;
            entry->path = strdup(path);
            list_add(bucket, &entry->chain);
        }

        // extract the stat data from the appropriate stat key
        dkey.dsize = sprintf(dkey.dptr, "stat %lu", inode);
        datum stat_data = gdbm_fetch(mount->db, dkey);
        if (stat_data.dptr == NULL)
            goto next;
        
        // store all the information in the new database
        dkey.dsize = sprintf(dkey.dptr, "inode %s", path);
        dvalue.dsize = sprintf(dvalue.dptr, "%lu", real_inode);
        gdbm_store(new_db, dkey, dvalue, GDBM_REPLACE);
        dkey.dsize = sprintf(dkey.dptr, "stat %lu", real_inode);
        gdbm_store(new_db, dkey, stat_data, GDBM_REPLACE);
        free(stat_data.dptr);

next:
        free(path);
    }

    for (unsigned i = 0; i < HASH_SIZE; i++) {
        struct entry *entry, *tmp;
        list_for_each_entry_safe(&hashtable[i], entry, tmp, chain) {
            list_remove(&entry->chain);
            free(entry->path);
            free(entry);
        }
    }

    if (rename(new_db_path, db_path) < 0)
        return errno_map();
    gdbm_close(mount->db);
    mount->db = new_db;
    return 0;
}
