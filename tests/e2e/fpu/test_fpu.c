#include <stdio.h>

int main(void) {
    double a = 0.12;
    double b = 16.0;
    printf("%.2f\n", a + b);
    printf("%.2f\n", a - b);
    printf("%.2f\n", a * b);
    printf("%.2f\n", a / b);
    printf("%.2f\n", b / a);
    printf("%.2f\n", a + a / b + b);
    printf("%.2f\n", (a + a) / (b + b));
    return 0;
}

