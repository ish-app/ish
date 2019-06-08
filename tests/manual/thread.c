#include <stdio.h>
#include <pthread.h>

void *thread(void *data) {
    printf("thread\n");
    return data;
}

int main() {
    pthread_t t;
    printf("main before create\n");
    pthread_create(&t, NULL, thread, NULL);
    pthread_detach(t);
    printf("main after create\n");
}
