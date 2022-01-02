#include <stddef.h>
#include <stdbool.h>

struct user_default_key {
    char *name;
    char *underlying_name;
};

struct all_user_default_keys {
    struct user_default_key *entries;
    size_t count;
};

extern struct all_user_default_keys user_default_keys;

extern void (*user_default_keys_requisition)(void);
extern void (*user_default_keys_relinquish)(void);

extern bool (*get_user_default)(char *name, char **buffer, size_t *size);
extern bool (*set_user_default)(char *name, char *buffer, size_t size);
extern bool (*remove_user_default)(char *name);
