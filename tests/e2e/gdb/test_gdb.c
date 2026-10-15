#include <stdio.h>

int square(int x) {
    return x * x;
}

int main() {
    int total = 0;
    for (int i = 0; i < 5; i++)
        total += square(i);
    printf("total=%d\n", total);
    return 0;
}
