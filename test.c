/* test.c -- DragonOS TCC demo program */

int fibonacci(int n) {
    if (n <= 1) return n;
    int a = 0;
    int b = 1;
    int i;
    for (i = 2; i <= n; i++) {
        int temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

int main() {
    printf("=== DragonOS TCC Compiler Demo ===\n");
    printf("Hello from C on DragonOS!\n\n");

    /* Sum computation */
    int sum = 0;
    int i;
    for (i = 1; i <= 100; i++) {
        sum = sum + i;
    }
    printf("Sum of 1..100 = %d\n", sum);

    /* Fibonacci sequence */
    printf("\nFibonacci sequence:\n");
    for (i = 0; i <= 10; i++) {
        int f = fibonacci(i);
        printf("  fib(%d) = %d\n", i, f);
    }

    /* Factorial */
    int fact = 1;
    for (i = 1; i <= 10; i++) {
        fact = fact * i;
    }
    printf("\n10! = %d\n", fact);

    printf("\nTCC compilation successful!\n");
    return 0;
}
