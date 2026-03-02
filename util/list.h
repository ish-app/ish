#ifndef LIST_H
#define LIST_H

#include <stdbool.h>
#include <stddef.h>

struct list {
    struct list *next, *prev;
};

#ifndef __KERNEL__

static inline void list_init(struct list *list) {
    list->next = list;
    list->prev = list;
}

#define LIST_INITIALIZER(x) {.prev = &x, .next = &x}

static inline bool list_null(struct list *list) {
    return list->next == NULL && list->prev == NULL;
}

static inline bool list_empty(struct list *list) {
    return list->next == list || list_null(list);
}

static inline void _list_add_between(struct list *prev, struct list *next, struct list *item) {
    prev->next = item;
    item->prev = prev;
    item->next = next;
    next->prev = item;
}

static inline void list_add_tail(struct list *list, struct list *item) {
    _list_add_between(list->prev, list, item);
}

static inline void list_add(struct list *list, struct list *item) {
    _list_add_between(list, list->next, item);
}

static inline void list_add_before(struct list *before, struct list *item) {
    list_add_tail(before, item);
}
static inline void list_add_after(struct list *after, struct list *item) {
    list_add(after, item);
}

static inline void list_init_add(struct list *list, struct list *item) {
    if (list_null(list))
        list_init(list);
    list_add(list, item);
}

static inline void list_remove(struct list *item) {
    item->prev->next = item->next;
    item->next->prev = item->prev;
    item->next = item->prev = NULL;
}

static inline void list_remove_safe(struct list *item) {
    if (!list_null(item))
        list_remove(item);
}

#define list_entry(item, type, member) \
    container_of(item, type, member)
#define list_first_entry(list, type, member) \
    list_entry((list)->next, type, member)
#define list_next_entry(item, member) \
    list_entry((item)->member.next, typeof(*(item)), member)

#define list_for_each(list, item) \
    for (item = (list)->next; item != (list); item = item->next)
#define list_for_each_safe(list, item, tmp) \
    for (item = (list)->next, tmp = item->next; item != (list); \
            item = tmp, tmp = item->next)

#define list_for_each_entry(list, item, member) \
    for (item = list_entry((list)->next, typeof(*item), member); \
            &item->member != (list); \
            item = list_entry(item->member.next, typeof(*item), member))
#define list_for_each_entry_safe(list, item, tmp, member) \
    for (item = list_first_entry(list, typeof(*(item)), member), \
            tmp = list_next_entry(item, member); \
            &item->member != (list); \
            item = tmp, tmp = list_next_entry(item, member))

static inline unsigned long list_size(struct list *list) {
    unsigned long count = 0;
    struct list *item;
    list_for_each(list, item) {
        count++;
    }
    return count;
}

#endif

#endif
