#include <stddef.h>
#include <stdbool.h>

struct user_default_key {
    char *name;
    char *underlying_name;
};

extern char **(*get_all_defaults_keys)(void);
extern char *(*get_friendly_name)(const char *name);
extern char *(*get_underlying_name)(const char *name);
extern bool (*get_user_default)(const char *name, char **buffer, size_t *size);
extern bool (*set_user_default)(const char *name, char *buffer, size_t size);
extern bool (*remove_user_default)(const char *name);
extern char *(*get_documents_directory)(void);
